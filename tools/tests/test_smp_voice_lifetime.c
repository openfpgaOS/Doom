/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the production synth against a finite, generation-checked mixer
 * pool. Looping samples stay allocated even after their target volume is 0. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define OF_TIMER_H
#define OF_CACHE_H
#define OF_FASTRAM_H
#define OF_FASTDATA
#include VOICE_SOURCE

typedef struct {
    int active, group, priority, left, right;
    uint32_t generation;
} hw_voice_t;
static hw_voice_t hw[32];
static unsigned stops, allocation_failures;
static ofsf_header_t bank = {.sample_rate = 48000};
static const ofsf_zone_t zone = {
    .sample_length = 1024, .loop_start = 0, .loop_end = 1024,
    .loop_mode = OFSF_LOOP_FORWARD, .root_key = 60,
    .vol_attack_rate = 65536, .vol_sustain_level = 65536,
    .vol_release_ticks = 20, .initial_attn_scale = 255,
    .mod_attack_rate = 65536, .mod_sustain_level = 65536,
    .mod_release_ticks = 20,
};
static int16_t pcm[1024];

const ofsf_header_t *of_smp_bank_get(void) { return &bank; }
static int handle_voice(uint64_t h) {
    int i = h & 255;
    if (!h || i >= 32 || !hw[i].active || hw[i].generation != h >> 8)
        return -1;
    return i;
}
static int handle_group(uint64_t h) {
    int i = handle_voice(h);
    return i < 0 ? -1 : hw[i].group;
}
static int handle_active(uint64_t h) { return handle_voice(h) >= 0; }
static uint64_t allocate(int group, const uint8_t *p, uint32_t n,
                         uint32_t rate, int priority, int volume) {
    (void)p; (void)n; (void)rate; (void)volume;
    /* Same free-first, strictly-lower-priority policy as the HAL. Voice 31
     * is reserved for streaming and is never a synth allocation. */
    int selected = -1;
    for (int i = 0; i < 31; i++)
        if (!hw[i].active) { selected = i; break; }
    if (selected < 0)
        for (int i = 0; i < 31; i++)
            if (hw[i].priority < priority) { selected = i; break; }
    if (selected < 0) { allocation_failures++; return 0; }
    hw_voice_t *v = &hw[selected];
    v->active = 1;
    v->group = group;
    v->priority = priority;
    v->generation++;
    return ((uint64_t)v->generation << 8) | selected;
}
static void stop_index(int i) { hw[i].active = 0; stops++; }
static void stop_handle(uint64_t h) {
    int i = handle_voice(h);
    if (i >= 0) stop_index(i);
}
static int active_index(int i) { return hw[i].active; }
static int group_index(int i) { return hw[i].group; }
static void volume_index(int i, int l, int r) { hw[i].left = l; hw[i].right = r; }
static void volume_handle(uint64_t h, int l, int r) {
    int i = handle_voice(h);
    if (i >= 0) volume_index(i, l, r);
}
static void rate_index(int i, uint32_t r) { (void)i; (void)r; }
static void rate_handle(uint64_t h, uint32_t r) { (void)h; (void)r; }
static void loop_handle(uint64_t h, int a, int b) { (void)h; (void)a; (void)b; }
static void bidi_handle(uint64_t h, int b) { (void)h; (void)b; }
static void ramp_handle(uint64_t h, int r) { (void)h; (void)r; }
static struct of_services_table svc = {
    .count = 1000,
    .mixer_alloc_for_group_h = allocate,
    .mixer_handle_voice = handle_voice, .mixer_handle_group = handle_group,
    .mixer_handle_active = handle_active,
    .mixer_stop_h = stop_handle, .mixer_stop = stop_index,
    .mixer_voice_active = active_index, .mixer_voice_group = group_index,
    .mixer_set_vol_lr_h = volume_handle, .mixer_set_vol_lr = volume_index,
    .mixer_set_rate_raw_h = rate_handle, .mixer_set_rate_raw = rate_index,
    .mixer_set_loop_h = loop_handle, .mixer_set_bidi_h = bidi_handle,
    .mixer_set_vol_rate_h = ramp_handle,
};
const struct of_services_table *_of_svc_ptr = &svc;

static void reset(void) {
    memset(hw, 0, sizeof(hw));
    stops = allocation_failures = 0;
    smp_voice_init();
}
static void advance(int n) { while (n--) smp_voice_tick(); }
static int active_count(void) {
    int n = 0;
    for (int i = 0; i < 32; i++) n += hw[i].active;
    return n;
}
static int note(int n) {
    return smp_voice_note_on(&zone, 0, n, 127, pcm);
}
static void fill_synth(void) {
    for (int i = 0; i < SMP_MAX_VOICES; i++) assert(note(40 + i) >= 0);
    advance(2);
}
static void test_repeated_steals(void) {
    reset();
    fill_synth();
    /* More than a pool's worth of steals, separated by time for each fade.
     * No app-side smp_voice_reap_orphans call: DOOM never calls that API. */
    for (int i = 0; i < 100; i++) {
        int slot = note(72 + i % 24);
        if (slot < 0) {
            fprintf(stderr, "FAIL: note dropped after %d steals; %d hardware voices still active\n",
                    i, active_count());
            assert(slot >= 0);
        }
        advance(8);
    }
    assert(!allocation_failures);
    assert(active_count() == SMP_MAX_VOICES);
    smp_voice_all_off_global();
    assert(active_count() == 0);
}
static void test_fade_and_stop(void) {
    reset();
    fill_synth();
    assert(note(90) >= 0);
    assert(active_count() == SMP_MAX_VOICES + 1);
    advance(1);
    assert(stops == 0); // preserve the existing de-click fade
    advance(8);
    assert(stops == 1 && active_count() == SMP_MAX_VOICES);
    assert(note(91) >= 0);
    smp_voice_all_off_global(); // timer may stop now: reap pending fades too
    assert(active_count() == 0);
}
static void test_reassigned_fader(int group) {
    reset();
    fill_synth();
    assert(note(90) >= 0);
    int fading = -1;
    for (int i = 0; i < 31; i++)
        if (hw[i].active && !hw[i].left && !hw[i].right) { fading = i; break; }
    assert(fading >= 0);
    /* SFX or a new MUSIC allocation reuses the index before its old grace
     * deadline. Cleanup must validate the old generation, not just group. */
    hw[fading].generation++;
    hw[fading].group = group;
    hw[fading].left = hw[fading].right = 100;
    advance(8);
    assert(hw[fading].active && hw[fading].left == 100);
    smp_voice_all_off_global();
    assert(hw[fading].active && active_count() == 1);
}
static void test_tick_wrap(void) {
    reset();
    fill_synth();
    /* Seed near wrap instead of executing four billion timer interrupts. */
    tick_counter = UINT32_MAX - 2;
    assert(note(90) >= 0);
    advance(4);
    assert(stops == 0);
    advance(4);
    assert(stops == 1 && active_count() == SMP_MAX_VOICES);
    smp_voice_all_off_global();
    assert(active_count() == 0);
}
static void test_stale_retirements_without_index(void) {
    reset();
    fill_synth();
    /* Hardware can end/reassign faders before another software tick. Stale
     * handles must not fill the retirement table on older service tables. */
    for (int n = 0; n < 40; n++) {
        assert(note(90) >= 0);
        for (int i = 0; i < 31; i++)
            if (hw[i].active && !hw[i].left && !hw[i].right) hw[i].active = 0;
    }
    assert(note(91) >= 0);
    advance(8);
    assert(active_count() == SMP_MAX_VOICES);
    smp_voice_all_off_global();
    assert(active_count() == 0);
}
int main(void) {
    test_repeated_steals();
    test_fade_and_stop();
    test_reassigned_fader(OF_MIXER_GROUP_SFX);
    test_reassigned_fader(OF_MIXER_GROUP_MUSIC);
    test_tick_wrap();
    svc.mixer_handle_voice = NULL; // older service-table compatibility
    test_repeated_steals();
    test_stale_retirements_without_index();
    puts("PASS: synth voice reclamation, fade lifetime, stop and generation reuse");
    return 0;
}
