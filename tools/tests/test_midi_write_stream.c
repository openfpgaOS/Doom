/* SPDX-License-Identifier: Apache-2.0 */
/* Drive the SDK MIDI parser + sample voice engine on the host with real
 * songs and a real bank, against a mixer model ported from the openfpgaOS
 * HAL (handle generations, groups, priority stealing, VOL_TARGET dedupe,
 * end-of-sample IRQ + lazy reap).  Every mixer register store is logged as
 * (time_us, register, voice, value), plus the service calls that change OS
 * state.  The 1 kHz timer callback fires at exact 1000 us steps, with
 * scripted gaps, pauses, volume changes, song changes and SFX traffic that
 * steals voices.  MT_WRITE_COST_US > 0 advances the clock on every register
 * store, which exercises the pump's 2 ms budget paths.
 *
 *   test_midi_write_stream <bank.ofsf> <trace.out|-> <seconds> <song.mid>...
 * stderr: per-song statistics (voices per tick, stages, writes). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include VOICE_SOURCE
#include "include/of_midi.h"
#include "include/of_smp_bank.h"
#include "include/of_mixer.h"

/* ------------------------------------------------------------ clock */
static uint32_t clock_us;
static uint64_t now_tick;
static uint32_t t_get_us(void) { return clock_us; }
static uint32_t t_get_ms(void) { return clock_us / 1000u; }
static void (*timer_cb)(void);
static void t_set_cb(void (*cb)(void), uint32_t hz) { (void)hz; timer_cb = cb; }

/* ------------------------------------------------------------ trace */
static FILE *trace;
static uint64_t n_records;
enum { R_CTRL = 1, R_ADDR, R_LEN, R_RATE, R_POSW, R_LS, R_LE, R_VOLLR, R_VOLRATE, R_VOLTGT,
       R_IRQCLR, R_GRPLO, R_GRPHI, C_ALLOC = 32, C_ALLOCFAIL, C_STOPH_MISS, C_HANDLE_GROUP, C_HANDLE_VOICE,
       C_HANDLE_ACTIVE, C_VOICE_ACTIVE, C_VOICE_GROUP };
static uint64_t n_queries[8];
/* Pure service queries (no OS state or register change) are counted, not traced. */
static void rec(uint32_t kind, uint32_t voice, uint32_t value) {
    if (kind >= C_HANDLE_GROUP) { n_queries[kind - C_HANDLE_GROUP]++; return; }
    uint32_t r[4] = {clock_us, kind, voice, value};
    n_records++;
    if (trace) fwrite(r, sizeof(r), 1, trace);
}

/* ------------------------------------------------------------ HW model */
typedef struct { int active, loop; uint64_t pos; uint32_t len, rate, ls, le; } hwv_t;
static hwv_t hwv[32];
static uint32_t irq_pending;
/* Optional deterministic CPU-time model: every register store advances the
 * clock (MT_WRITE_COST_US), so the pump's 2 ms budget paths are exercised at
 * points that do not depend on how many queries or clock reads a version does. */
static uint32_t write_cost_us;
static void mmio_w(int reg, int v, uint32_t val) {
    rec(reg, v, val);
    clock_us += write_cost_us;
    hwv_t *h = &hwv[v & 31];
    switch (reg) {
    case R_CTRL: h->active = val & 1; h->loop = (val >> 2) & 1; break;
    case R_LEN: h->len = val; break;
    case R_RATE: h->rate = val; break;
    case R_POSW: h->pos = (uint64_t)val << 16; break;
    case R_LS: h->ls = val; break;
    case R_LE: h->le = val; break;
    case R_IRQCLR: irq_pending &= ~val; break;
    }
}
static void hw_advance_1ms(void) {
    for (int i = 0; i < 32; i++) {
        hwv_t *h = &hwv[i];
        if (!h->active) continue;
        h->pos += (uint64_t)h->rate * 48u;
        if (h->loop && h->le > h->ls) {
            while ((h->pos >> 16) >= h->le) h->pos -= (uint64_t)(h->le - h->ls) << 16;
        } else if ((h->pos >> 16) >= h->len) {
            h->active = 0; irq_pending |= 1u << i;
        }
    }
}

/* ------------------------------------------------------------ OS mixer model */
#define NV 32
#define SCRATCH 31
static uint32_t active_shadow, gen[NV], vt_shadow[NV];
static uint8_t ctrl_s[NV], vol_s[NV], pan_s[NV], grp[NV];
static int8_t prio[NV];
static void write_ctrl(int v, uint8_t c) {
    ctrl_s[v] = c;
    if (c & 1) active_shadow |= 1u << v; else active_shadow &= ~(1u << v);
    mmio_w(R_CTRL, v, c);
}
static void reap(void) {
    uint32_t e = irq_pending;
    if (!e) return;
    uint32_t ae = e & active_shadow;
    for (int i = 0; i < NV; i++) if (ae & (1u << i)) ctrl_s[i] = 0;
    active_shadow &= ~e;
    mmio_w(R_IRQCLR, 0, e);
}
static int hidx(uint64_t h) { int v = h & 0xff; if (!h || !(h >> 8) || v >= NV) return -1; return v; }
static int validate(uint64_t h) { int v = hidx(h); if (v < 0 || !(active_shadow & (1u << v)) || gen[v] != (uint32_t)(h >> 8)) return -1; return v; }
static void write_vt(int v, uint32_t p) { if (vt_shadow[v] != p) { mmio_w(R_VOLTGT, v, p); vt_shadow[v] = p; } }
static void write_groups(void) {
    uint32_t lo = 0, hi = 0;
    for (int i = 0; i < 16; i++) lo |= (uint32_t)(grp[i] & 3) << (i * 2);
    for (int i = 16; i < 32; i++) hi |= (uint32_t)(grp[i] & 3) << ((i - 16) * 2);
    mmio_w(R_GRPLO, 0, lo); mmio_w(R_GRPHI, 0, hi);
}
static int alloc_grouped(int pri, int g) {
    if (g == OF_MIXER_GROUP_MUSIC) { for (int i = 0; i < SCRATCH; i++) if (!(active_shadow & (1u << i))) return i; }
    else { for (int i = SCRATCH - 1; i >= 0; i--) if (!(active_shadow & (1u << i))) return i; }
    int victim = -1, lowest = pri;
    for (int i = 0; i < SCRATCH; i++) { if (grp[i] != (uint8_t)g) continue; if (prio[i] < lowest) { lowest = prio[i]; victim = i; } }
    if (victim >= 0) return victim;
    lowest = pri;
    for (int i = 0; i < SCRATCH; i++) if (prio[i] < lowest) { lowest = prio[i]; victim = i; }
    return victim;
}
static const uint8_t *bank_bytes;
static int16_t sfx_pcm[11025];
/* Sample address as an ASLR-independent id: offset into the bank, or SFX buffer. */
static uint32_t addr_id(const uint8_t *p) {
    if (p >= (const uint8_t *)sfx_pcm && p < (const uint8_t *)(sfx_pcm + 11025)) return 0x80000000u | (uint32_t)(p - (const uint8_t *)sfx_pcm);
    return (uint32_t)(p - bank_bytes);
}
static uint64_t m_alloc_for_group_h(int g, const uint8_t *pcm, uint32_t n, uint32_t rate_hz, int pri, int vol) {
    if (!pcm || !n) return 0;
    uint32_t rate = (uint32_t)(((uint64_t)rate_hz << 16) / 48000u);
    if (g < 0 || g > 3) g = 0;
    reap();
    int v = alloc_grouped(pri, g);
    if (v < 0) { rec(C_ALLOCFAIL, g, n); return 0; }
    grp[v] = (uint8_t)g; write_groups();
    uint32_t ng = gen[v] + 1; if (!ng) ng = 1; gen[v] = ng;
    uint64_t h = ((uint64_t)ng << 8) | (uint8_t)v;
    write_ctrl(v, 0);
    mmio_w(R_IRQCLR, v, 1u << v);
    mmio_w(R_ADDR, v, addr_id(pcm)); mmio_w(R_LEN, v, n); mmio_w(R_RATE, v, rate); mmio_w(R_POSW, v, 0);
    mmio_w(R_LS, v, 0); mmio_w(R_LE, v, n); mmio_w(R_VOLLR, v, 0); mmio_w(R_VOLRATE, v, 8);
    vol_s[v] = vol & 0xff; pan_s[v] = 128; prio[v] = (int8_t)pri; vt_shadow[v] = 0xFFFFFFFFu;
    {   /* apply_vol_pan at pan 128: pan_cos[128] = 180, pan_sin[128] = 181 */
        int vl = (vol_s[v] * 180) >> 8, vr = (vol_s[v] * 181) >> 8;
        write_vt(v, (uint32_t)(((vr & 0xff) << 8) | (vl & 0xff)));
    }
    write_ctrl(v, 1);
    rec(C_ALLOC, v, (uint32_t)(h >> 8));
    return h;
}
static void m_stop(int v) { if (v < 0 || v >= NV) return; write_ctrl(v, 0); }
static void m_stop_h(uint64_t h) { int v = validate(h); if (v >= 0) write_ctrl(v, 0); else rec(C_STOPH_MISS, h & 0xff, (uint32_t)(h >> 8)); }
static int m_handle_active(uint64_t h) { reap(); int r = validate(h) >= 0; rec(C_HANDLE_ACTIVE, h & 0xff, r); return r; }
static int m_handle_group(uint64_t h) { int v = validate(h); int g = v >= 0 ? grp[v] : -1; rec(C_HANDLE_GROUP, h & 0xff, (uint32_t)g); return g; }
static int m_handle_voice(uint64_t h) { int v = hidx(h); if (v >= 0 && gen[v] != (h >> 8)) v = -1; rec(C_HANDLE_VOICE, h & 0xff, (uint32_t)v); return v; }
static int m_voice_active(int v) { int r = (v >= 0 && v < NV) ? (int)((active_shadow >> v) & 1) : 0; rec(C_VOICE_ACTIVE, v, r); return r; }
static int m_voice_group(int v) { int r = (v >= 0 && v < NV) ? grp[v] : -1; rec(C_VOICE_GROUP, v, r); return r; }
static void m_set_vol_lr(int v, int l, int r) { if (v < 0 || v >= NV) return; write_vt(v, (uint32_t)(((r & 0xff) << 8) | (l & 0xff))); }
static void m_set_vol_lr_h(uint64_t h, int l, int r) { int v = validate(h); if (v >= 0) write_vt(v, (uint32_t)(((r & 0xff) << 8) | (l & 0xff))); }
static void m_set_rate_raw(int v, uint32_t r) { if (v < 0 || v >= NV) return; mmio_w(R_RATE, v, r); }
static void m_set_rate_raw_h(uint64_t h, uint32_t r) { int v = validate(h); if (v >= 0) mmio_w(R_RATE, v, r); }
static void m_set_loop_h(uint64_t h, int a, int b) {
    reap(); int v = validate(h); if (v < 0) return;
    uint8_t cur = ctrl_s[v];
    if (a < 0) write_ctrl(v, cur & ~4u);
    else { mmio_w(R_LS, v, (uint32_t)a); if (b > 0) mmio_w(R_LE, v, (uint32_t)b); write_ctrl(v, cur | 4u); }
}
static void m_set_bidi_h(uint64_t h, int e) { (void)h; (void)e; }
static void m_set_vol_rate_h(uint64_t h, int r) { int v = validate(h); if (v >= 0) mmio_w(R_VOLRATE, v, (uint32_t)(r & 0xff)); }

static struct of_services_table svc;
const struct of_services_table *_of_svc_ptr = &svc;

/* ------------------------------------------------------------ driver */
static void *load(const char *p, uint32_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) exit(2); fclose(f); *len = (uint32_t)n; return b;
}
static uint32_t rnd = 0x9e3779b9u;
static uint32_t next_rnd(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

/* statistics (read from the engine's voice table through the public tick stats) */
static uint64_t st_ticks, st_active_sum, st_hist[SMP_MAX_VOICES + 2];
static uint64_t st_pumps, st_stage[8], st_lfo, st_oneshot, st_pending;

static void step_ms(void) {
    clock_us += 1000u; now_tick++;
    hw_advance_1ms();
}
static void fire(void) { if (timer_cb) { st_pumps++; timer_cb(); } }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: midi_trace bank.ofsf trace|- seconds song.mid...\n"); return 2; }
    uint32_t bank_len; void *bank = load(argv[1], &bank_len); bank_bytes = bank;
    trace = strcmp(argv[2], "-") ? fopen(argv[2], "wb") : NULL;
    int seconds = atoi(argv[3]);
    if (getenv("MT_WRITE_COST_US")) write_cost_us = (uint32_t)atoi(getenv("MT_WRITE_COST_US"));
    svc.magic = OF_SVC_MAGIC; svc.version = OF_SVC_VERSION; svc.count = 100000;
    svc.timer_get_us = t_get_us; svc.timer_get_ms = t_get_ms; svc.timer_set_callback = t_set_cb;
    svc.smp_bank_preload_base = bank; svc.smp_bank_preload_size = bank_len;
    svc.mixer_alloc_for_group_h = m_alloc_for_group_h; svc.mixer_stop = m_stop; svc.mixer_stop_h = m_stop_h;
    svc.mixer_handle_active = m_handle_active; svc.mixer_handle_group = m_handle_group; svc.mixer_handle_voice = m_handle_voice;
    svc.mixer_voice_active = m_voice_active; svc.mixer_voice_group = m_voice_group;
    svc.mixer_set_vol_lr = m_set_vol_lr; svc.mixer_set_vol_lr_h = m_set_vol_lr_h;
    svc.mixer_set_rate_raw = m_set_rate_raw; svc.mixer_set_rate_raw_h = m_set_rate_raw_h;
    svc.mixer_set_loop_h = m_set_loop_h; svc.mixer_set_bidi_h = m_set_bidi_h; svc.mixer_set_vol_rate_h = m_set_vol_rate_h;
    for (int i = 0; i < NV; i++) { gen[i] = 1; vt_shadow[i] = 0xFFFFFFFFu; }
    clock_us = 0xFFFFFFFFu - 30000000u;          /* wrap the 32-bit us clock inside the first song */
    assert(of_midi_init() == OF_MIDI_OK);
    uint64_t sfx_h[8] = {0};
    for (int s = 4; s < argc; s++) {
        uint32_t len; uint8_t *song = load(argv[s], &len);
        smp_voice_tick_reset_stats();
        uint64_t t0 = st_ticks, a0 = st_active_sum, r0 = n_records;
        int rc = of_midi_play(song, len, 1);
        if (rc != OF_MIDI_OK) { fprintf(stderr, "%s: play rc %d\n", argv[s], rc); continue; }
        uint32_t gap = 0; int paused = 0;
        for (int ms = 0; ms < seconds * 1000; ms++) {
            step_ms();
            uint32_t r = next_rnd();
            /* scripted timer gaps (interrupts held off), multi-tick catch-up */
            if (gap) { gap--; if (gap) continue; }
            else if ((r & 1023) == 7) { gap = 2 + (r >> 10) % 9; continue; }
            if (ms % 20011 == 15000) { of_midi_pause(); paused = 1; }
            if (paused && ms % 20011 == 15700) { of_midi_resume(); paused = 0; }
            if (ms % 9973 == 5000) of_midi_set_volume((int)(r >> 8) % 256);
            if (ms % 9973 == 5500) of_midi_set_volume(128);
            /* SFX traffic: high-priority allocations that can steal MUSIC voices */
            if ((r & 63) == 3) {
                int k = (r >> 6) & 7;
                if (sfx_h[k] && of_mixer_handle_active(sfx_h[k])) of_mixer_stop_h(sfx_h[k]);
                sfx_h[k] = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_SFX, (const uint8_t *)sfx_pcm,
                                                      1000 + (r >> 9) % 10000, 11025, 100, 200);
            }
            fire();
            /* sample the voice table after each timer callback */
            int na = 0;
            for (int i = 0; i < SMP_MAX_VOICES; i++) {
                if (voices[i].active == 1) {
                    na++;
                    st_stage[voices[i].vol_env.stage & 7]++;
                    if (voices[i].update_flags & 7) st_lfo++;
                    if (voices[i].sample_ticks_remaining > 0) st_oneshot++;
                } else if (voices[i].active) {
                    st_pending++;
                }
            }
            st_ticks++; st_active_sum += na; st_hist[na]++;
        }
        smp_tick_stats_t ts; smp_voice_tick_get_stats(&ts);
        fprintf(stderr, "%-24s ticks=%u pumps=%llu vol_writes=%u rate_writes=%u records=%llu active_peak=%u bursts=%u budget=%u\n",
                strrchr(argv[s], '/') ? strrchr(argv[s], '/') + 1 : argv[s], ts.tick_count,
                (unsigned long long)st_pumps, ts.vol_writes, ts.rate_writes,
                (unsigned long long)(n_records - r0), ts.active_peak, ts.pump_burst_count, ts.pump_budget_exceeded);
        fprintf(stderr, "    mean active voices %.2f\n", (double)(st_active_sum - a0) / (double)(st_ticks - t0 ? st_ticks - t0 : 1));
        (void)t0;
        if (s % 3 == 0) of_midi_stop();              /* alternate stop+play and play-over-play */
        else { of_midi_stop(); }
        free(song);
    }
    smp_voice_all_off_global();
    if (trace) fclose(trace);
    fprintf(stderr, "total records %llu; samples %llu, mean active voices %.2f; stage share:", (unsigned long long)n_records,
            (unsigned long long)st_ticks, (double)st_active_sum / (double)st_ticks);
    const char *sn[8] = {"off", "delay", "attack", "hold", "decay", "sustain", "release", "done"};
    for (int i = 0; i < 8; i++) fprintf(stderr, " %s=%.1f%%", sn[i], 100.0 * (double)st_stage[i] / (double)st_active_sum);
    fprintf(stderr, "; pitch-mod routed %.1f%%, one-shot tracked %.1f%%, steal-pending/tick %.2f\n",
            100.0 * (double)st_lfo / (double)st_active_sum, 100.0 * (double)st_oneshot / (double)st_active_sum, (double)st_pending / (double)st_ticks);
    fprintf(stderr, "service queries: handle_group=%llu handle_voice=%llu handle_active=%llu voice_active=%llu voice_group=%llu\n",
            (unsigned long long)n_queries[0], (unsigned long long)n_queries[1], (unsigned long long)n_queries[2],
            (unsigned long long)n_queries[3], (unsigned long long)n_queries[4]);
    fprintf(stderr, "active-voice histogram:");
    for (int i = 0; i <= SMP_MAX_VOICES; i++) fprintf(stderr, " %d:%.1f%%", i, 100.0 * (double)st_hist[i] / (double)st_ticks);
    fprintf(stderr, "\n");
    return 0;
}
