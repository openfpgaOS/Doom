/* i_pcmmusic.c — stream PCM music from a WAD lump over the HW mixer, else MIDI (not upstream).
 *
 * Doom core only.  S_ChangeMusic's lump D_E1M1 maps to a 'P'-prefixed lump
 * (PE1M1) of raw stereo s16le PCM (sample rate from the WAD's PCMINFO lump,
 * else 48 kHz) in a merged music WAD; present ->
 * stream it, absent -> the OPL/MIDI module keeps playing.
 *
 * Playback model (mirrors Quake's cd_of.c, which never underruns): two mono HW
 * mixer voices (L hard-left, R hard-right, MUSIC group) loop a deinterleaved
 * SDRAM ring forever — the FPGA mixer DMAs+loops it with no CPU audio deadline,
 * unlike of_audio_stream (a CPU-fed FIFO that goes silent the moment Poll is
 * late, e.g. a level load).  Each Poll reads the play cursor and tops the ring
 * up *behind* it; of_cache_flush_range after every write (the mixer fetch is an
 * external AXI master).
 *
 * Refill is async DMA: of_file_read_async hands the SD transfer to the
 * data-slot engine (a blocking fread of the 192 KB/s stream costs ~13% CPU),
 * the bytes land in CRAM0 in the background, and a completion IRQ flags them;
 * the next Poll deinterleaves the finished chunk into the ring.  The single
 * data-slot bridge serves one transfer at a time, so any engine blocking read
 * (level load, save) must first retire an in-flight CD read — I_PCM_DrainAsync()
 * is called from W_StdC_Read and the save path.  Falls back to synchronous
 * W_Read when async is unavailable; prefill is always synchronous.
 */

#include "config.h"

#ifdef OF_DOOM

#include "doomtype.h"
#include "doomdef.h"      /* gameaction_t / ga_savegame / ga_loadgame */
#include "doomstat.h"     /* gamemode / gamemission — per-IWAD music-WAD default */
#include "i_sound.h"
#include "m_argv.h"       /* -pcmwad override */
#include "w_wad.h"
#include "w_file.h"

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "of_mixer.h"
#include "of_cache.h"
#include "of_error.h"     /* OF_ERR_TIMEOUT: dispatch-guard drop on async issue */
#include "of_file.h"
#include "of_timer.h"
#include "of_video.h"

#define PCM_WAD_DEFAULT  "DOOMMUS.WAD"   /* Doom 1 music (D_E1M1.. -> PE1M1..) */
#define PCM_WAD_DEFAULT2 "DOOM2MUS.WAD"  /* Doom II / Final Doom music (D_RUNNIN.. -> PRUNNIN..) */
#define MUS_RATE_MAX     48000         /* output rate; ring arrays are sized for this */
#define MUS_RATE_DEFAULT 48000         /* assumed when the WAD has no PCMINFO lump (old DOOMMUS.WAD) */
#define MUS_CHANNELS    2
#define MUS_BYTES_FRAME (MUS_CHANNELS * (int)sizeof(int16_t))   /* 4 */
#define RING_SECONDS    5              /* refill slack: must outlast on-demand WAD reads
                                        * starving the music DMA during early gameplay (4 s
                                        * underran -> ring laps -> music jumps to the start) */
#define RING_FRAMES_MAX (MUS_RATE_MAX * RING_SECONDS)
#define STAGING_FRAMES  4096           /* one sync read+deinterleave pass */
#define CHUNK_FRAMES    1024           /* MIN async read (4 KB): floor so steady state at high
                                        * fps keeps the small, jitter-free fold. */
#define CD_STAGE_FRAMES 4096           /* MAX async read (16 KB).  The read size adapts to the
                                        * free ring space, so refill throughput tracks 48 kHz
                                        * consumption down to ~12 polls/s -- the old fixed 4 KB
                                        * chunk needed 47 polls/s and sawtoothed below that
                                        * (Poll runs once per FRAME).  Capped so the per-chunk
                                        * fold in Poll stays ~1 ms. */
#define CD_STAGE_FALLBACK_FRAMES 2048  /* async_max_read unsupported (old OS): old HI size */
#define MUS_PRIORITY    200
#define PRESENT_STALE_US 250000u       /* no scanout for this long == OSD/stall */

/* Output gain on the music samples, hard-clamped so peaks limit (not wrap).
 * Unity (1.0): the PCM is pre-mastered (loudnorm) with -1 dBTP headroom, so any
 * boost here would only clip the already-hot transients (e.g. drums). Master
 * loudness offline, not at runtime. */
#define MASTER_GAIN_NUM 10
#define MASTER_GAIN_DEN 10

static inline int16_t gain_clamp(int s)
{
    s = s * MASTER_GAIN_NUM / MASTER_GAIN_DEN;
    if (s > 32767)  s = 32767;
    else if (s < -32768) s = -32768;
    return (int16_t)s;
}

void I_OpenFPGAMixerPump(void);        /* shared SFX mixer maintenance (i_sdlsound.c) */

/* Exported (read by the bridge-drain hooks in w_file_stdc.c / i_save_migrate.c):
 * nonzero only while CD music is the active source, so those hooks stay inert
 * for MIDI / no-WAD play. */
int                i_pcm_active;

static char        pcm_lump[9];        /* "PE1M1" etc. — empty = no track */
static int         pcm_checked;        /* WAD merge + async setup attempted */
static int         pcm_ready;          /* WAD present (rings usable) */
static int         pcm_playing;
static int         pcm_looping;
static int         pcm_paused;
static int         pcm_volume = 127;   /* 0..127 */
static int         pcm_ended;          /* non-looping track read to EOF */

/* source: a byte range within the merged WAD file */
static wad_file_t *pcm_wad;
static unsigned    pcm_base;           /* lump file offset */
static unsigned    pcm_size;           /* lump byte length (whole frames) */
static unsigned    pcm_rd;             /* sync read cursor within the lump */

/* deinterleaved SDRAM rings (+1 guard frame mirrors [0] for the loop seam) */
static int16_t     ringL[RING_FRAMES_MAX + 1];
static int16_t     ringR[RING_FRAMES_MAX + 1];
static int16_t     staging[STAGING_FRAMES * MUS_CHANNELS] __attribute__((aligned(8)));
static int         write_pos;          /* next ring frame to fill */
static int         last_pos;           /* play cursor at previous refill */
static int         last_vol;           /* last group volume pushed (0..255) */
static int         ring_valid;         /* real (non-silence) frames ahead of cursor */

static int         mus_rate    = MUS_RATE_DEFAULT;  /* voice rate: PCMINFO lump or default */
static int         ring_frames = RING_FRAMES_MAX;   /* active ring length = mus_rate * RING_SECONDS */

static of_mixer_handle_t vL = OF_MIXER_HANDLE_INVALID;
static of_mixer_handle_t vR = OF_MIXER_HANDLE_INVALID;

/* ---- async (data-slot DMA) refill state --------------------------- */
static int           cd_slot = -1;     /* data slot of the music WAD */
/* DMA staging is SDRAM via the uncached alias.  The OS CRAM0 app pool is the
 * zero-copy path but is UNUSABLE from the app -- see pcm_ensure_init.  Kept
 * below for the record:
 * DMA staging PREFERS the OS CRAM0 app pool (of_file_dma_stage_alloc): the
 * bridge writes it zero-copy and the completion IRQ stays microseconds.  Any
 * other destination makes the OS bounce the whole chunk uncached->uncached
 * INSIDE the completion IRQ (the RTL bridge->SDRAM write master is removed),
 * a multi-ms interrupt blackout per chunk == the music frame stutter.  The
 * fold then reads CRAM0 uncached, one 32-bit frame per access (ring_write's
 * src32 walk), bounded by CD_STAGE_FRAMES and in Poll context where it
 * belongs.  SDRAM staging remains only as the old-OS fallback. */
static uint8_t       cd_stage_mem[CD_STAGE_FRAMES * MUS_BYTES_FRAME] __attribute__((aligned(64)));
static uint8_t      *cd_stage;          /* DMA dest: CRAM0 pool, else uncached-SDRAM alias */
static int           cd_stage_cram0;    /* staging is CRAM0 (zero-copy, no inval needed) */
static int           cd_stage_frames;   /* per-read cap: CD_STAGE_FRAMES, OS max, or fallback */
static uint32_t      cd_retry_ms;       /* next async re-attempt after a drop (of_time_ms) */
static uint32_t      cd_backoff_ms;     /* retry backoff, doubles per consecutive drop */
static int           cd_issue_defers;   /* consecutive dispatch-guard drops (OF_ERR_TIMEOUT) */
static int           cd_drain_gaveup;   /* a drain timed out; skip the wait until a read lands */
static int           cd_async_ok;       /* async usable for the current track */
static int           cd_pending;        /* a DMA read is in flight */
static int           cd_frames;         /* frames the in-flight read delivers */
static unsigned      cd_read_off;       /* async read cursor within the lump */
static volatile int  cd_done;           /* set by the completion IRQ callback */
static volatile int  cd_result;         /* IRQ result: 0 ok, <0 error */
static int           cd_menu_prev;      /* `quiet` (menu/save/load) last Poll, edge detect */

/* Async DMA refill is ENABLED, but suspended while frame presentation is
 * stalled.  When the Analogue Pocket *system* menu opens, the Pocket owns the
 * display (scanout stops) and the shared single data-slot bridge; an in-flight
 * of_file_read_async issued then gets stuck on the bridge and the stalled
 * transfer starves the GPU until its watchdog traps (==TRAP== mcause=3 in
 * of_gpu_wait — reproduced only with CD music).  We detect the stall via the
 * OS present_count freezing (of_video_get_timing) and, like Quake, freeze the
 * voices (the ring repeats the last sample) and issue no DMA until scanout
 * resumes. */
static const int     pcm_async_enabled = 1;
static int           pcm_frozen;            /* voices frozen during a scanout stall */
static uint32_t      pcm_present_count;      /* last of_video present_count seen */
static uint32_t      pcm_present_change_us;  /* of_time_us when present_count last moved */

void I_SetMusicTrackName(const char *name)
{
    size_t i = 0;

    pcm_lump[0] = '\0';
    if (name == NULL || name[0] == '\0')
        return;
    if ((name[0] == 'd' || name[0] == 'D') && name[1] == '_')
        name += 2;

    pcm_lump[i++] = 'P';
    for (; i < 8 && *name; name++)
    {
        char c = *name;
        pcm_lump[i++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    pcm_lump[i] = '\0';
}

int I_PCM_Active(void)
{
    return pcm_playing;
}

static int pcm_group_volume(void)
{
    return (pcm_volume * 255) / 127;     /* 0..127 -> 0..255 */
}

/* Write `frames` interleaved frames from src into the ring at the write cursor,
 * wrapping, flushing each span and keeping the loop-seam guard in sync. */
static void ring_write(const int16_t *src, int frames)
{
    /* One stereo frame == one 32-bit word (L in the low half, R in the high).
     * Reading the word does a single uncached CRAM0 access per frame instead of
     * two int16 reads -- halves the CDC-crossing latency that drives the fold. */
    const uint32_t *src32 = (const uint32_t *)src;
    int done = 0;

    while (done < frames)
    {
        int w = write_pos;
        int contig = ring_frames - w;
        int batch = frames - done;
        int i;

        if (batch > contig)
            batch = contig;

        for (i = 0; i < batch; i++)
        {
            uint32_t lr = src32[done + i];
            ringL[w + i] = gain_clamp((int16_t)(lr & 0xffffu));
            ringR[w + i] = gain_clamp((int16_t)(lr >> 16));
        }
        of_cache_flush_range(&ringL[w], (uint32_t)batch * sizeof(int16_t));
        of_cache_flush_range(&ringR[w], (uint32_t)batch * sizeof(int16_t));

        if (w == 0)
        {
            ringL[ring_frames] = ringL[0];
            ringR[ring_frames] = ringR[0];
            of_cache_flush_range(&ringL[ring_frames], sizeof(int16_t));
            of_cache_flush_range(&ringR[ring_frames], sizeof(int16_t));
        }

        write_pos = (w + batch) % ring_frames;
        done += batch;
    }
}

/* Synchronous read of `count` interleaved frames into staging, looping within
 * the lump.  Returns the count of *real* (non-EOF-pad) frames. */
static int pcm_read_frames(int count)
{
    int got = 0, real = 0;

    while (got < count)
    {
        unsigned remain = pcm_size - pcm_rd;
        unsigned want;
        size_t   n;

        if (remain == 0)
        {
            if (pcm_looping) { pcm_rd = 0; remain = pcm_size; }
            else
            {
                pcm_ended = 1;
                memset(&staging[got * MUS_CHANNELS], 0,
                       (size_t)(count - got) * MUS_BYTES_FRAME);
                return real;
            }
        }

        want = (unsigned)(count - got) * MUS_BYTES_FRAME;
        if (want > remain) want = remain;

        n = W_Read(pcm_wad, pcm_base + pcm_rd, &staging[got * MUS_CHANNELS], want);
        if (n == 0)
        {
            memset(&staging[got * MUS_CHANNELS], 0,
                   (size_t)(count - got) * MUS_BYTES_FRAME);
            return real;
        }
        pcm_rd += (unsigned)n;
        got    += (int)n / MUS_BYTES_FRAME;
        real   += (int)n / MUS_BYTES_FRAME;
    }
    return real;
}

/* Synchronous producer: read+deinterleave `nframes` into the ring. */
static void pcm_produce(int nframes)
{
    while (nframes > 0)
    {
        int batch = nframes;
        int real;
        if (batch > STAGING_FRAMES)
            batch = STAGING_FRAMES;
        real = pcm_read_frames(batch);
        ring_write(staging, batch);
        ring_valid += real;
        nframes -= batch;
    }
}

/* ---- diagnostics (PCM_DIAG): state ----------------------------------------
 * Self-contained, bounded logging to a file the host can read off the vhd.
 * Distinguishes the two "music loops every few seconds" failure modes:
 *   - track-mismatch : W_CheckNumForName(<lump>) < 0 -> PCM never starts, the
 *                      track falls back to MIDI (midi_fallthrough++).
 *   - refill-failure : PCM starts (pcm_started++) but async S2 reads fail
 *                      (cd_fail++ / issue_err++ / drain_timeout++) so the 5 s
 *                      ring laps and repeats.
 * Declared here (above the refill helpers) so cd_issue/cd_fold/DrainAsync can
 * bump the counters; the fopen/snprintf helpers live below pcm_wad_name().
 * Enable with `make PCMLOG=1` (adds -DPCM_DIAG).  Off by default. */
#ifdef PCM_DIAG
#define PCMLOG_PATH      "/pcmlog.txt"
#define PCMLOG_EVTCAP    3072u
#define PCMLOG_PERIOD_MS 2000u

static char     dg_evt[PCMLOG_EVTCAP];
static unsigned dg_evt_len;
static int      dg_evt_full;
static uint32_t dg_last_flush_ms;

/* cumulative counters */
static unsigned dg_tryplay, dg_pcm_started, dg_midi_fallthrough;
static unsigned dg_lump_missing, dg_empty_lump, dg_no_voices;
static unsigned dg_cd_issue, dg_cd_ok, dg_cd_fail, dg_cd_issue_err;
static unsigned dg_sync_topup, dg_drain_timeout, dg_async_dropped;

#define DG(x) do { x; } while (0)
#else
#define DG(x) do { } while (0)
#endif

/* Drop to the sync path with a scheduled retry: a wedged bridge (Pocket OSD,
 * save collision) is transient, so a permanent downgrade left the rest of the
 * track doing blocking main-thread SD reads (= frame stutter). */
static void cd_async_drop(void)
{
    cd_async_ok = 0;
    if (cd_backoff_ms < 1000u)
        cd_backoff_ms = 1000u;
    else if (cd_backoff_ms < 8000u)
        cd_backoff_ms <<= 1;
    cd_retry_ms = of_time_ms() + cd_backoff_ms;
}

/* ---- async refill ------------------------------------------------- */
#ifndef OF_PC
static void cd_cb(int token, int result)
{
    (void)token;
    cd_result = result;
    cd_done = 1;
}

/* Kick a DMA read at cd_read_off, looping/EOF.
 * Non-blocking: sets cd_pending and pre-advances the read cursor. */
static int cd_issue(void)
{
    /* Adaptive size: fill all free ring space in one read (capped by the
     * staging buffer / OS request limit), so refill keeps pace with the
     * cursor at any poll rate and catch-up after a stall is immediate. */
    int want = ring_frames - ring_valid;
    if (want > cd_stage_frames)
        want = cd_stage_frames;
    if (want < CHUNK_FRAMES)
        want = CHUNK_FRAMES;
    unsigned remain = pcm_size - cd_read_off;
    int frames, tok, space;

    if (remain == 0)
    {
        if (!pcm_looping) { pcm_ended = 1; return -1; }
        cd_read_off = 0;
        remain = pcm_size;
    }
    frames = want;
    if ((unsigned)frames * MUS_BYTES_FRAME > remain)
        frames = (int)(remain / MUS_BYTES_FRAME);
    space = ring_frames - ring_valid;
    if (frames > space)             /* never write past the play cursor */
        frames = space;
    if (frames <= 0)
        return -1;

    cd_done = 0;
    cd_result = -1;
    tok = of_file_read_async(cd_slot, pcm_base + cd_read_off, cd_stage,
                             (uint32_t)(frames * MUS_BYTES_FRAME), cd_cb);
    if (tok < 0)
    {
        /* TIMEOUT = the dispatch guard dropped the command (old OS: ack-CDC
         * settling after recent bridge traffic; falling back to sync reads
         * would put traffic in front of every retry = livelock).  BUSY = the
         * bridge is momentarily owned (fixed OS reports it honestly after
         * its quiet window).  Both are transient: keep async ON and retry
         * next poll; the ring absorbs the gap.  Persistent failure of either
         * kind, or any other error, is a real drop. */
        if ((tok == OF_ERR_TIMEOUT || tok == OF_ERR_BUSY) && cd_issue_defers < 8)
        {
            cd_issue_defers++;
            return tok;
        }
        cd_async_drop();
        DG(dg_cd_issue_err++; dg_async_dropped++);
        return tok;
    }
    cd_issue_defers = 0;
    cd_drain_gaveup = 0;        /* bridge accepted a command — trust it again */

    cd_pending = 1;
    cd_frames  = frames;
    cd_read_off += (unsigned)frames * MUS_BYTES_FRAME;
    DG(dg_cd_issue++);
    return 0;
}
#else
static int cd_issue(void) { cd_async_ok = 0; return -1; }
#endif

/* Retire an in-flight DMA read so the single-slot bridge is free for the
 * engine's own blocking file I/O.  Called from W_StdC_Read and the save path.
 * Bounded wait; on timeout, drop to the sync refill path. */
void I_PCM_DrainAsync(void)
{
#ifndef OF_PC
    unsigned start;

    /* Gate on the BRIDGE, not on cd_pending: cd_pending tracks only the read
     * whose completion WE saw, and the OS can still own a transfer whose IRQ
     * was lost (across a Pocket OSD visit).  The caller is about to take the
     * CRAM0 mux for blocking slot I/O and must own the bridge alone --
     * M_ReadSaveStrings flips it ten times back to back. */
    if (cd_drain_gaveup || !of_file_async_busy())
        return;

    start = of_time_ms();
    while (of_file_async_busy())
    {
        of_file_async_poll();
        if (!of_file_async_busy())
            break;
        if ((unsigned)(of_time_ms() - start) >= 200u)
        {
            cd_drain_gaveup = 1;    /* don't re-stall every following read */
            cd_async_drop();        /* wedged — sync for now, retry later */
            DG(dg_drain_timeout++; dg_async_dropped++);
            break;
        }
    }
#endif
}

static void pcm_stop_voices(void)
{
    if (vL != OF_MIXER_HANDLE_INVALID) { of_mixer_stop_h(vL); vL = OF_MIXER_HANDLE_INVALID; }
    if (vR != OF_MIXER_HANDLE_INVALID) { of_mixer_stop_h(vR); vR = OF_MIXER_HANDLE_INVALID; }
}

void I_PCM_Stop(void)
{
    if (!pcm_playing)
        return;
    I_PCM_DrainAsync();             /* free the bridge before the next track */
    cd_pending = 0;
    cd_async_ok = 0;
    pcm_stop_voices();
    pcm_playing = 0;
    i_pcm_active = 0;
    pcm_paused = 0;
    pcm_ended = 0;
}

/* Music WAD filename: -pcmwad <file> overrides everything (so each instance can
 * ship its own without colliding in common/).  With no override the default is
 * chosen PER IWAD: Doom II / Final Doom request Doom2 track names (D_RUNNIN ->
 * PRUNNIN), which live ONLY in DOOM2MUS.WAD; Doom 1 requests D_E1M1 -> PE1M1,
 * which live in DOOMMUS.WAD.  Defaulting Doom II to the Doom 1 wad (the old
 * behaviour) meant W_CheckNumForName never resolved the track -> the PCM path
 * was dead and every track fell back to MIDI.  gamemission is set by
 * D_IdentifyVersion() long before the first track plays, so it is valid here. */
static int pcm_iwad_is_doom2(void)
{
    /* pack_chex/pack_hacx alias through logical_gamemission; commercial is the
     * belt-and-braces check for a Doom II-style IWAD. */
    GameMission_t m = logical_gamemission;
    return m == doom2 || m == pack_tnt || m == pack_plut
        || gamemode == commercial;
}

static const char *pcm_wad_name(void)
{
    int p = M_CheckParmWithArgs("-pcmwad", 1);
    if (p > 0)
        return myargv[p + 1];
    return pcm_iwad_is_doom2() ? PCM_WAD_DEFAULT2 : PCM_WAD_DEFAULT;
}

/* ---- diagnostics (PCM_DIAG): logging helpers -------------------------------
 * (Counters + the DG() increment macro are declared higher up so the refill
 *  helpers that sit above pcm_wad_name() can bump them.) */
#ifdef PCM_DIAG

static void dg_add(const char *s)
{
    unsigned n = (unsigned)strlen(s);
    if (dg_evt_len + n + 1u >= PCMLOG_EVTCAP)
    {
        if (!dg_evt_full)
        {
            const char *t = "...[event log full]\n";
            unsigned tn = (unsigned)strlen(t);
            if (dg_evt_len + tn < PCMLOG_EVTCAP)
            {
                memcpy(dg_evt + dg_evt_len, t, tn);
                dg_evt_len += tn;
            }
            dg_evt_full = 1;
        }
        return;
    }
    memcpy(dg_evt + dg_evt_len, s, n);
    dg_evt_len += n;
}

/* Rewrite the whole log file: bounded event history, then a freshly-formatted
 * cumulative SUMMARY block that is ALWAYS present even once the event buffer is
 * full.  wb (truncate) each time -> the file is always a complete valid snapshot,
 * no reliance on append semantics.  Drains the async CD read first so the small
 * fatfs write never collides with an in-flight refill on the single DS bridge. */
static void dg_flush(void)
{
#ifndef OF_PC
    FILE *fp;
    char  sum[512];
    int   n;

    I_PCM_DrainAsync();
    fp = fopen(PCMLOG_PATH, "wb");
    if (fp == NULL)
        return;
    if (dg_evt_len)
        fwrite(dg_evt, 1, dg_evt_len, fp);
    n = snprintf(sum, sizeof(sum),
        "SUMMARY wad=%s ready=%d cd_slot=%d async_ok=%d frozen=%d rate=%d ring=%d\n"
        "  gamemission=%d gamemode=%d\n"
        "  tryplay=%u pcm_started=%u midi_fallthrough=%u lump_missing=%u empty_lump=%u no_voices=%u\n"
        "  cd_issue=%u cd_ok=%u cd_fail=%u issue_err=%u sync_topup=%u drain_timeout=%u async_dropped=%u\n",
        pcm_wad_name(), pcm_ready, cd_slot, cd_async_ok, pcm_frozen, mus_rate, ring_frames,
        (int)gamemission, (int)gamemode,
        dg_tryplay, dg_pcm_started, dg_midi_fallthrough, dg_lump_missing, dg_empty_lump, dg_no_voices,
        dg_cd_issue, dg_cd_ok, dg_cd_fail, dg_cd_issue_err, dg_sync_topup, dg_drain_timeout, dg_async_dropped);
    if (n > 0)
        fwrite(sum, 1, (size_t)n, fp);
    fclose(fp);
    dg_last_flush_ms = of_time_ms();
#endif
}

static void dg_logf(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    dg_add(b);
}

/* Rate-limited flush from the Poll loop so the file survives even if a refill
 * failure escalates to a hang. */
static void dg_tick(void)
{
    uint32_t now = of_time_ms();
    if ((uint32_t)(now - dg_last_flush_ms) >= PCMLOG_PERIOD_MS)
        dg_flush();
}

#define DG_LOG(...)  dg_logf(__VA_ARGS__)
#define DG_FLUSH()   dg_flush()
#define DG_TICK()    dg_tick()

#else  /* !PCM_DIAG */

#define DG_LOG(...)  do { } while (0)
#define DG_FLUSH()   do { } while (0)
#define DG_TICK()    do { } while (0)

#endif /* PCM_DIAG */

/* Merge the optional music WAD + set up async refill on first use (graceful if
 * the WAD or async support is absent). */
static void pcm_ensure_init(void)
{
    const char *wad;

    if (pcm_checked)
        return;
    pcm_checked = 1;

    /* -nomusic silences the PCM stream too — I_PlaySong probes this path
     * before (and regardless of) the music module the flag disables. */
    if (M_CheckParm("-nomusic") > 0)
        return;

    wad = pcm_wad_name();
    if (W_AddFile(wad) == NULL)
    {
        /* printf("CD music: %s absent, MIDI only\n", wad); */  /* UART silenced for jitter test */
        DG_LOG("init wad=%s W_AddFile=FAIL -> MIDI only (mission=%d mode=%d)\n",
               wad, (int)gamemission, (int)gamemode);
        DG_FLUSH();
        return;
    }
    W_GenerateHashTable();
    pcm_ready = 1;

    /* Optional PCMINFO lump: little-endian <u32 sample_rate>[<u32 channels>].
     * Absent -> default rate, so an old headerless 48 kHz DOOMMUS.WAD stays valid. */
    {
        lumpindex_t ri = W_CheckNumForName("PCMINFO");
        if (ri >= 0)
        {
            lumpinfo_t *rl = lumpinfo[ri];
            uint32_t    rate = 0;
            if (rl != NULL && rl->size >= 4
                && W_Read(rl->wad_file, (unsigned)rl->position, &rate, 4) == 4
                && rate >= 8000 && rate <= (uint32_t)MUS_RATE_MAX)
                mus_rate = (int)rate;
        }
    }
    ring_frames = mus_rate * RING_SECONDS;

#ifndef OF_PC
    if (pcm_async_enabled)
    {
        uint32_t slot;
        if (of_file_slot_find(wad, &slot) == 0)
        {
            uint32_t maxrd = of_file_async_max_read();

            cd_slot  = (int)slot;
            cd_stage_frames = CD_STAGE_FRAMES;
            if (maxrd == 0)
                cd_stage_frames = CD_STAGE_FALLBACK_FRAMES;
            else if (cd_stage_frames > (int)(maxrd / MUS_BYTES_FRAME))
                cd_stage_frames = (int)(maxrd / MUS_BYTES_FRAME);
            if (cd_stage_frames < CHUNK_FRAMES)
                cd_stage_frames = CHUNK_FRAMES;

            /* SDRAM staging via the uncached alias.  The OS bounces the chunk
             * in the completion IRQ, so it pairs with the smaller fallback
             * size; flush the buffer once so dirty BSS-zero lines can't evict
             * over a DMA.
             *
             * The CRAM0 pool (of_file_dma_stage_alloc) is the zero-copy path,
             * but the app CANNOT use it: cd_fold reads the staging buffer with
             * the CPU, and CRAM0 is behind a CPU/bridge ownership mux the app
             * has no API to hold.  Any NV-slot fclose (a save, a load, the
             * Load menu, a config write) hands the mux back to the bridge, and
             * a CPU read taken then is never answered -- cram0_cdc has no
             * timeout, which back-pressures every per_axi read in the machine
             * including the UART poll.  That is a hard freeze with no output.
             * Re-enable only once the OS exports a mux bracket. */
            cd_stage = NULL;
            cd_stage_cram0 = 0;
            if (!cd_stage_cram0)
            {
                if (cd_stage_frames > CD_STAGE_FALLBACK_FRAMES)
                    cd_stage_frames = CD_STAGE_FALLBACK_FRAMES;
                cd_stage = (uint8_t *)of_uncached(cd_stage_mem);
                of_cache_flush_range(cd_stage_mem, sizeof(cd_stage_mem));
            }
        }
    }
#endif
    /* printf("CD music: %s loaded (%s refill)\n", wad,
              cd_stage ? "async DMA" : "sync"); */  /* UART silenced for jitter test */
    DG_LOG("init wad=%s W_AddFile=OK ready=1 rate=%d cd_slot=%d async=%d (mission=%d mode=%d)\n",
           wad, mus_rate, cd_slot, (cd_stage != NULL), (int)gamemission, (int)gamemode);
    DG_FLUSH();
}

int I_PCM_TryPlay(boolean looping)
{
    lumpindex_t n;
    lumpinfo_t *l;

    pcm_ensure_init();
    DG(dg_tryplay++);
    if (!pcm_ready || pcm_lump[0] == '\0')
    {
        /* No music WAD (MIDI-only) or no track name — not a mismatch. */
        DG(dg_midi_fallthrough++);
        DG_LOG("try lump='%s' ready=%d -> MIDI (no wad / empty name)\n",
               pcm_lump, pcm_ready);
        DG_FLUSH();
        return 0;
    }

    n = W_CheckNumForName(pcm_lump);
    if (n < 0)
    {
        /* TRACK MISMATCH: the requested lump is not in this music WAD (e.g. a
         * Doom II track name against a Doom 1 wad) -> clean MIDI fallback. */
        DG(dg_lump_missing++; dg_midi_fallthrough++);
        DG_LOG("try lump='%s' wad=%s checknum=MISSING -> MIDI\n",
               pcm_lump, pcm_wad_name());
        DG_FLUSH();
        return 0;
    }
    l = lumpinfo[n];
    if (l == NULL || l->size < MUS_BYTES_FRAME)
    {
        DG(dg_empty_lump++; dg_midi_fallthrough++);
        DG_LOG("try lump='%s' checknum=%d size=%ld too-small -> MIDI\n",
               pcm_lump, (int)n, l ? (long)l->size : -1L);
        DG_FLUSH();
        return 0;
    }

    if (pcm_playing)
        I_PCM_Stop();

    pcm_looping = looping ? 1 : 0;
    pcm_paused  = 0;
    pcm_ended   = 0;
    pcm_wad     = l->wad_file;
    pcm_base    = (unsigned)l->position;
    pcm_size    = (unsigned)l->size & ~3u;
    pcm_rd      = 0;

    /* Prefill the whole ring synchronously (one-time per track, ~100 ms,
     * hidden inside the level transition).  It must cover the level-load
     * poll gap: tracks start DURING loads, the voices consume immediately,
     * and Poll doesn't run again until the load finishes -- a short prefill
     * runs dry there and the track goes silent for seconds. */
    write_pos  = 0;
    ring_valid = 0;
    pcm_produce(ring_frames);
    write_pos  = 0;
    last_pos   = 0;

    /* Steady-state goes async if enabled and the data-slot DMA is available. */
    cd_async_ok = (pcm_async_enabled && cd_stage != NULL && cd_slot >= 0);
    cd_pending  = 0;
    cd_read_off = pcm_rd;          /* async resumes where the prefill left off */
    pcm_frozen  = 0;
    pcm_present_count = 0;
    pcm_present_change_us = of_time_us();

    vL = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_MUSIC, (const uint8_t *)ringL,
                                    ring_frames, mus_rate, MUS_PRIORITY, 0);
    vR = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_MUSIC, (const uint8_t *)ringR,
                                    ring_frames, mus_rate, MUS_PRIORITY, 0);
    if (vL == OF_MIXER_HANDLE_INVALID || vR == OF_MIXER_HANDLE_INVALID)
    {
        /* printf("CD music: no free music voices, using MIDI\n"); */  /* UART silenced for jitter test */
        pcm_stop_voices();
        DG(dg_no_voices++; dg_midi_fallthrough++);
        DG_LOG("try lump='%s' no free music voices -> MIDI\n", pcm_lump);
        DG_FLUSH();
        return 0;
    }

    of_mixer_set_loop_h(vL, 0, ring_frames);
    of_mixer_set_loop_h(vR, 0, ring_frames);
    of_mixer_set_vol_lr_h(vL, 255, 0);
    of_mixer_set_vol_lr_h(vR, 0, 255);

    last_vol = pcm_group_volume();
    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, last_vol);

    pcm_playing = 1;
    i_pcm_active = 1;
    /* printf("CD music: streaming lump %s (%u bytes, %s)\n", pcm_lump, pcm_size,
              cd_async_ok ? "async" : "sync"); */  /* UART silenced for jitter test */
    DG(dg_pcm_started++);
    DG_LOG("try lump='%s' wad=%s PCM START size=%u async=%d cd_slot=%d\n",
           pcm_lump, pcm_wad_name(), pcm_size, cd_async_ok, cd_slot);
    DG_FLUSH();
    return 1;
}

/* Fold a finished DMA read into the ring. */
static void cd_fold(void)
{
    if (cd_async_ok && cd_pending && cd_done)
    {
        if (cd_result >= 0)
        {
            if (cd_stage_cram0)
            {
                /* Zero-copy CRAM0 staging: uncached, so no maintenance --
                 * fold straight from it (one 32-bit read per frame). */
                ring_write((const int16_t *)cd_stage, cd_frames);
            }
            else
            {
                /* SDRAM fallback: the OS bounced into cd_stage_mem.
                 * Invalidate, then fold from the CACHED alias. */
                of_cache_inval_range(cd_stage_mem,
                                     (uint32_t)cd_frames * MUS_BYTES_FRAME);
                ring_write((const int16_t *)cd_stage_mem, cd_frames);
            }
            ring_valid += cd_frames;
            cd_backoff_ms = 0;      /* healthy again: reset the retry backoff */
            DG(dg_cd_ok++);
        }
        else
        {
            /* Refill DMA failed: the frames the cursor already freed stay
             * silent-or-stale and, if this keeps up, the ring laps = the
             * "loops every few seconds" symptom.  Count it. */
            DG(dg_cd_fail++);
        }
        cd_pending = 0;
    }
}

/* No async CD reads across the save/load sequence: sendsave covers
 * menu-confirm -> button, gameaction covers the tic the blocking slot I/O
 * executes, and the Load/Save menus cover the browsing in between -- the
 * confirm closes the menu a few tics BEFORE G_DoSaveGame/G_DoLoadGame runs
 * the blocking slot I/O, and a read still in flight there collides with it
 * on the single data-slot bridge (GPU watchdog hang).  I_PCM_DrainAsync
 * alone does not close that window: its wait is bounded at 200 ms and then
 * hands the bridge over anyway.  Every OTHER menu stays async -- a menu-wide
 * quiet window put every refill on the blocking sync path (one ~25 ms bridge
 * command per poll, the APF host's per-command latency). */
static int pcm_quiet(void)
{
    extern gameaction_t gameaction;
    extern boolean sendsave;
    extern boolean M_SaveLoadMenuActive(void);
    return M_SaveLoadMenuActive()
        || sendsave
        || gameaction == ga_savegame
        || gameaction == ga_loadgame;
}

void I_PCM_Poll(void)
{
    if (pcm_playing)
    {
        int quiet = pcm_quiet();
        int pos, consumed, present_live;
        of_video_timing_t vt;
        uint32_t now_us = of_time_us();

        cd_fold();

        /* Track scanout: present_count advances each time the Pocket presents a
         * core frame.  It freezes while the Pocket *system* menu owns the
         * display — and at that point the Pocket also owns the shared data-slot
         * bridge, so an async CD read issued then wedges it and starves the GPU
         * (watchdog trap).  Detect the stall and, like Quake, freeze the voices
         * (the ring repeats the last sample) + issue no DMA until scanout
         * resumes.  Uses our own of_time_us stamp of the last change, so it
         * makes no cross-clock assumption about present timestamps. */
        of_video_get_timing(&vt);
        if (vt.present_count != pcm_present_count)
        {
            pcm_present_count = vt.present_count;
            pcm_present_change_us = now_us;
        }
        present_live = (uint32_t)(now_us - pcm_present_change_us) < PRESENT_STALE_US;

        if (!present_live)
        {
            if (!pcm_frozen)
            {
                I_PCM_DrainAsync();             /* retire any in-flight read */
                cd_fold();
                of_mixer_set_rate_h(vL, 0);     /* freeze cursors -> ring repeats */
                of_mixer_set_rate_h(vR, 0);
                pcm_frozen = 1;
            }
            I_OpenFPGAMixerPump();
            return;                             /* no DMA while the OSD owns the bridge */
        }
        if (pcm_frozen)
        {
            of_mixer_set_rate_h(vL, mus_rate);  /* scanout resumed — unfreeze */
            of_mixer_set_rate_h(vR, mus_rate);
            pcm_frozen = 0;
        }

        /* Orphaned OS-side transfer (completion IRQ lost, e.g. across a
         * Pocket OSD visit): the OS poll fallback observes latched DONE and
         * retires it, unwedging of_file_read_async — which otherwise returns
         * BUSY forever and pins us to the blocking sync path. */
#ifndef OF_PC
        if (!cd_pending && of_file_async_busy())
            of_file_async_poll();
#endif

        /* Async dropped earlier (bridge error / drain timeout): those causes
         * are transient, so re-arm after the backoff instead of spending the
         * rest of the track on blocking main-thread reads. */
        if (!cd_async_ok && pcm_async_enabled && cd_stage != NULL
            && cd_slot >= 0 && !quiet && !cd_pending
            && (int32_t)(of_time_ms() - cd_retry_ms) >= 0)
        {
            cd_async_ok = 1;
            cd_read_off = pcm_rd;   /* async resumes where sync left off */
        }

        /* Entering a quiet window (see pcm_quiet): retire the in-flight read
         * and refill synchronously (plain file reads, no DMA bridge), so the
         * slot I/O that follows owns the bridge alone; hand back to async on
         * the way out. */
        if (cd_async_ok)
        {
            if (quiet && !cd_menu_prev)
            {
                I_PCM_DrainAsync();
                cd_fold();
                pcm_rd = cd_read_off;   /* sync resumes where async left off */
            }
            else if (!quiet && cd_menu_prev)
            {
                cd_read_off = pcm_rd;   /* async resumes where sync left off */
            }
        }
        cd_menu_prev = quiet;

        pos = of_mixer_get_position_h(vL);
        if (pos >= 0)
        {
            consumed = (pos - last_pos + ring_frames) % ring_frames;
            if (consumed >= ring_frames)
                consumed = ring_frames - 1;
            last_pos = pos;

            if (!pcm_paused)
            {
                int vol = pcm_group_volume();
                if (vol != last_vol)
                {
                    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, vol);
                    last_vol = vol;
                }
            }

            ring_valid -= consumed;
            if (ring_valid < 0)
                ring_valid = 0;

            if (cd_async_ok && !quiet)
            {
                /* Issue the next read once the cursor freed a min chunk. */
                if (!cd_pending && !pcm_ended &&
                    ring_frames - ring_valid >= CHUNK_FRAMES)
                    cd_issue();

                if (!cd_async_ok)            /* async dropped mid-stream */
                    pcm_rd = cd_read_off;    /* resync the sync cursor */
            }
            else if (consumed > 0)
            {
                pcm_produce(consumed);       /* sync top-up (save/load, or no async) */
                DG(dg_sync_topup++);
            }

            if (pcm_ended && ring_valid <= 0)
                I_PCM_Stop();
        }

        DG_TICK();     /* rate-limited log snapshot while a track streams */
    }

    I_OpenFPGAMixerPump();
}

void I_PCM_SetVolume(int volume)
{
    if (volume < 0) volume = 0; else if (volume > 127) volume = 127;
    pcm_volume = volume;
}

void I_PCM_Pause(void)
{
    if (pcm_playing && !pcm_paused)
    {
        of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, 0);
        pcm_paused = 1;
    }
}

void I_PCM_Resume(void)
{
    if (pcm_playing && pcm_paused)
    {
        last_vol = pcm_group_volume();
        of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, last_vol);
        pcm_paused = 0;
    }
}

#else

typedef int i_pcmmusic_translation_unit_not_empty;

#endif /* OF_DOOM */
