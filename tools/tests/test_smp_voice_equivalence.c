/* SPDX-License-Identifier: Apache-2.0 */
/* Compare real synth state and mixer writes across deterministic MIDI-like
 * workloads. Reuse the generation-checked pool from the lifetime regression. */
#define main lifetime_test_main
#include "test_smp_voice_lifetime.c"
#undef main

static uint32_t step_number;
static void emit(uint32_t kind, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t record[] = {step_number, kind, a, b, c};
    assert(fwrite(record, sizeof(record), 1, stdout) == 1);
}
static void trace_volume(int i, int l, int r)
{
    emit(1, i, l, r);
    volume_index(i, l, r);
}
static void trace_volume_h(uint64_t h, int l, int r)
{
    int i = handle_voice(h);
    if (i >= 0) trace_volume(i, l, r);
}
static void trace_rate(int i, uint32_t rate) { emit(2, i, rate, 0); }
static void trace_rate_h(uint64_t h, uint32_t rate)
{
    int i = handle_voice(h);
    if (i >= 0) trace_rate(i, rate);
}
static void trace_stop(int i) { emit(3, i, 0, 0); stop_index(i); }
static void trace_stop_h(uint64_t h)
{
    int i = handle_voice(h);
    if (i >= 0) trace_stop(i);
}
static void trace_ramp(uint64_t h, int rate)
{
    emit(4, h, h >> 32, rate);
}
static uint32_t random_state = 0x37ead914;
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void state_snapshot(void)
{
    emit(5, sizeof(smp_voice_t), tick_counter, steal_pending_count);
    for (int i = 0; i < SMP_MAX_VOICES; i++) {
        const smp_voice_t *v = &voices[i];
        emit(6, i, v->active, v->sample_ticks_remaining);
        if (!v->active) continue;
        emit(7, v->mixer_voice, v->mixer_voice >> 32, v->base_rate_fp16);
        emit(8, v->vol_env.stage, v->vol_env.level, v->vol_env.rate);
        emit(9, v->vol_env.timer, v->mod_env.stage, v->mod_env.level);
        emit(10, v->mod_env.rate, v->mod_env.timer, v->sustain_since);
        emit(11, v->mod_lfo.phase, v->mod_lfo.delay_ticks, v->vib_lfo.phase);
        emit(12, v->vib_lfo.delay_ticks, v->pan_mul_l, v->pan_mul_r);
        emit(13, v->sustain_held, prev_rate[i], prev_vol_l[i] | prev_vol_r[i] << 8);
    }
}
int main(void)
{
    svc.mixer_set_vol_lr = trace_volume;
    svc.mixer_set_vol_lr_h = trace_volume_h;
    svc.mixer_set_rate_raw = trace_rate;
    svc.mixer_set_rate_raw_h = trace_rate_h;
    svc.mixer_stop = trace_stop;
    svc.mixer_stop_h = trace_stop_h;
    svc.mixer_set_vol_rate_h = trace_ramp;
    ofsf_zone_t zones[8];
    for (int mode = 0; mode < 8; mode++) {
        zones[mode] = zone;
        ofsf_zone_t *z = &zones[mode];
        z->vib_lfo_to_pitch = mode & 1 ? -137 : 0;
        z->mod_lfo_to_pitch = mode & 2 ? 731 : 0;
        z->mod_env_to_pitch = mode & 4 ? -917 : 0;
        z->vol_delay_ticks = mode;
        z->vol_attack_rate = 19 + mode * 1500;
        z->vol_hold_ticks = 9 + mode;
        z->vol_decay_rate = 9 + mode;
        z->vol_sustain_level = mode * 8192;
        z->vol_release_ticks = 10 + mode * 50;
        z->mod_delay_ticks = 11 - mode;
        z->mod_attack_rate = 317;
        z->mod_hold_ticks = 50;
        z->mod_decay_rate = 51;
        z->mod_sustain_level = 4096 + mode * 4000;
        z->mod_release_ticks = 57;
        z->mod_lfo_delay_ticks = 71;
        z->vib_lfo_delay_ticks = 17;
        z->mod_lfo_rate = 383;
        z->vib_lfo_rate = 719;
        z->pan = -500 + mode * 143;
        z->initial_attn_scale = 100 + mode * 20;
        if (mode == 7) z->loop_mode = 0;
    }
    for (int scenario = 0; scenario < 4; scenario++) {
        reset();
        if (scenario == 1) tick_counter = UINT32_MAX - 1000;
        if (scenario == 2) svc.mixer_handle_voice = NULL;
        for (int tick = 0; tick < 10000; tick++, step_number++) {
            uint32_t r = next_random();
            int ch = (r >> 8) & 15;
            if (tick % 17 == 0) {
                int result = smp_voice_note_on(&zones[(r >> 12) & 7], ch,
                                              36 + r % 60, 1 + (r >> 16) % 127, pcm);
                emit(14, result, ch, r);
            }
            if (tick % 29 == 0) smp_voice_note_off(ch, 36 + (r >> 20) % 60);
            if (tick % 11 == 0) {
                switch ((r >> 28) & 7) {
                case 0: smp_voice_update_volume(ch, r % 150, (r >> 16) % 150); break;
                case 1: smp_voice_update_pan(ch, r % 150); break;
                case 2: smp_voice_update_bend(ch, (int)(r % 20000) - 10000); break;
                case 3: smp_voice_update_mod(ch, tick % 2 ? r % 128 : 0); break;
                case 4: smp_voice_update_sustain(ch, r & 1); break;
                case 5: smp_voice_set_master_volume(r % 280); break;
                case 6: smp_voice_all_off(ch); break;
                case 7: smp_voice_update_filter(ch, r % 128, (r >> 8) % 128); break;
                }
            }
            if (tick % 631 == 630) {
                /* A generation can be reused by SFX or music between ticks. */
                int hw_index = r % 31;
                hw[hw_index].generation++;
                hw[hw_index].group = r & 1 ? OF_MIXER_GROUP_SFX : OF_MIXER_GROUP_MUSIC;
            }
            if (tick % 991 == 990) smp_voice_all_off_global();
            smp_voice_tick();
            if (tick % 16 == 0) state_snapshot();
        }
        smp_voice_all_off_global();
        svc.mixer_handle_voice = handle_voice;
    }
    /* Sustained, unmodulated notes exercise cached values, late controllers,
     * repeated identical controllers, and the existing hung-note guard. */
    reset();
    assert(note(60) >= 0);
    for (int tick = 0; tick < 9500; tick++, step_number++) {
        if (tick == 200 || tick == 201) smp_voice_update_bend(0, 8191);
        if (tick == 500) smp_voice_update_bend(0, -8192);
        if (tick == 1000) smp_voice_update_bend(0, 0);
        if (tick == 1500 || tick == 1501) smp_voice_update_volume(0, 63, 100);
        if (tick == 2000) smp_voice_update_pan(0, 0);
        if (tick == 2500) smp_voice_update_pan(0, 127);
        if (tick == 3000) smp_voice_set_master_volume(0);
        if (tick == 3500) smp_voice_set_master_volume(255);
        smp_voice_tick();
        if (tick % 16 == 0) state_snapshot();
    }
    smp_voice_all_off_global();
    assert(!ferror(stdout));
    return 0;
}
