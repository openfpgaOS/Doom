/* Exercise the real streamer with a failing file and a delayed DMA bridge. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include PCM_SOURCE

static wad_file_t file;
static lumpinfo_t lump;
lumpinfo_t *lumps[] = { &lump };
lumpinfo_t **lumpinfo = lumps;
GameMission_t gamemission = doom;
GameMode_t gamemode = registered;
gameaction_t gameaction;
boolean sendsave;
char **myargv;

static uint8_t source[32768];
static int read_calls, short_bytes, fail_call, opened, quiet_menu;
static int busy, issue_result, complete_on_poll, issue_calls, poll_calls;
static unsigned dma_offset, dma_bytes, last_read_offset;
static void *dma_dest;
static void (*dma_callback)(int, int);
static uint32_t clock_us, presented;
static int position, rate_changes, voices, pumps;

size_t W_Read(wad_file_t *f, unsigned offset, void *dest, size_t count)
{
    assert(f == &file && offset + count <= sizeof(source));
    last_read_offset = offset;
    ++read_calls;
    if (read_calls == fail_call) {
        if (count > (size_t)short_bytes) count = short_bytes;
    }
    memcpy(dest, source + offset, count);
    return count;
}
wad_file_t *W_AddFile(const char *name) { (void)name; return opened ? &file : NULL; }
void W_GenerateHashTable(void) {}
lumpindex_t W_CheckNumForName(const char *name) { return !strcmp(name, "PCMINFO") ? -1 : 0; }
int M_CheckParm(const char *name) { (void)name; return 0; }
int M_CheckParmWithArgs(const char *name, int n) { (void)name; (void)n; return 0; }
boolean M_SaveLoadMenuActive(void) { return quiet_menu; }
void I_OpenFPGAMixerPump(void) { ++pumps; }
void of_cache_flush_range(const void *p, uint32_t n) { (void)p; (void)n; }
void of_cache_inval_range(const void *p, uint32_t n) { (void)p; (void)n; }
void *of_uncached(void *p) { return p; }
uint32_t of_time_us(void) { return clock_us; }
uint32_t of_time_ms(void) { return clock_us / 1000; }
void of_video_get_timing(of_video_timing_t *v) { v->present_count = presented; }
int of_file_slot_find(const char *name, uint32_t *slot) { (void)name; *slot = 2; return 0; }
uint32_t of_file_async_max_read(void) { return 8192; }
int of_file_async_busy(void) { return busy; }
static void complete(int result)
{
    assert(busy && dma_callback);
    if (result == 0) memcpy(dma_dest, source + dma_offset, dma_bytes);
    busy = 0;
    dma_callback(7, result);
}
int of_file_async_poll(void)
{
    ++poll_calls;
    clock_us += 1000;
    if (busy && complete_on_poll) { complete(0); return 1; }
    return 0;
}
int of_file_read_async(int slot, uint32_t offset, void *dest, uint32_t n,
                       void (*callback)(int, int))
{
    assert(slot == 2 && !busy);
    ++issue_calls;
    if (issue_result < 0) return issue_result;
    assert(offset + n <= sizeof(source));
    dma_offset = offset; dma_bytes = n; dma_dest = dest; dma_callback = callback;
    busy = 1;
    return 7;
}
of_mixer_handle_t of_mixer_alloc_for_group_h(int group, const uint8_t *p,
                                              int length, int rate, int pri, int flags)
{
    (void)group; (void)p; (void)length; (void)rate; (void)pri; (void)flags;
    return ++voices;
}
void of_mixer_stop_h(of_mixer_handle_t h) { (void)h; }
void of_mixer_set_loop_h(of_mixer_handle_t h, int a, int b) { (void)h; (void)a; (void)b; }
void of_mixer_set_vol_lr_h(of_mixer_handle_t h, int a, int b) { (void)h; (void)a; (void)b; }
void of_mixer_set_group_volume(int g, int v) { (void)g; (void)v; }
void of_mixer_set_rate_h(of_mixer_handle_t h, int rate) { (void)h; (void)rate; ++rate_changes; }
int of_mixer_get_position_h(of_mixer_handle_t h) { (void)h; return position; }

static void reset(void)
{
    for (unsigned i = 0; i < sizeof(source); ++i) source[i] = i * 13u + (i >> 8);
    for (int i = 0; i <= RING_FRAMES_MAX; ++i) { ringL[i] = 1234; ringR[i] = -1234; }
    file.length = sizeof(source);
    lump.wad_file = &file; lump.position = 0; lump.size = sizeof(source);
    pcm_wad = &file; pcm_base = pcm_rd = 0; pcm_size = sizeof(source);
    pcm_looping = 1; pcm_ended = pcm_paused = pcm_playing = i_pcm_active = 0;
    pcm_checked = pcm_ready = 1;
    pcm_read_failed = 0; pcm_retry_ms = 0;
    ring_frames = 8192; ring_valid = 0; write_pos = last_pos = 0;
    cd_slot = 2; cd_stage = cd_stage_mem; cd_stage_frames = 2048; cd_stage_cram0 = 0;
    cd_pending = cd_done = cd_read_off = cd_backoff_ms = cd_retry_ms = 0;
    cd_issue_defers = cd_drain_gaveup = 0; cd_async_ok = 1;
    pcm_present_count = 0; pcm_present_change_us = 0;
    busy = issue_calls = poll_calls = issue_result = complete_on_poll = 0;
    read_calls = fail_call = short_bytes = quiet_menu = 0; opened = 1;
    clock_us = presented = position = rate_changes = voices = pumps = 0;
    gameaction = ga_nothing; sendsave = false;
    vL = vR = OF_MIXER_HANDLE_INVALID;
    I_SetMusicTrackName("D_E1M1");
}
static void unchanged(void)
{
    for (int i = 0; i <= ring_frames; ++i) {
        assert(ringL[i] == 1234 && ringR[i] == -1234);
    }
}
static void play(void) { pcm_playing = i_pcm_active = 1; vL = 1; vR = 2; }

int main(void)
{
    for (int partial = 0; partial < 8; ++partial) {
        reset(); fail_call = 1; short_bytes = partial;
        assert(!pcm_produce(16)); unchanged();
        assert(pcm_rd == 0 && write_pos == 0 && !pcm_ended);
        assert(!pcm_produce(16) && read_calls == 1);
        clock_us = 100000;
        assert(pcm_produce(16));
        assert(last_read_offset == 0 && pcm_rd == 64 && ring_valid == 16);
        assert(ringL[0] == (int16_t)(source[0] | source[1] << 8));
    }
    puts("PASS: zero/short/unaligned reads retain audio and retry the same chunk");

    reset(); pcm_rd = pcm_size - 16; fail_call = 2;
    assert(!pcm_produce(16)); unchanged();
    assert(pcm_rd == pcm_size - 16 && !pcm_ended);
    clock_us = 100000; assert(pcm_produce(16)); assert(pcm_rd == 48);
    puts("PASS: read failure across the track loop rolls back the whole chunk");

    reset(); pcm_looping = 0; pcm_rd = pcm_size - 8;
    assert(pcm_produce(16) && pcm_ended && ring_valid == 2);
    for (int i = 2; i < 16; ++i) assert(ringL[i] == 0 && ringR[i] == 0);
    puts("PASS: genuine non-looping EOF still pads with silence");

    reset(); assert(cd_issue() == 0); unsigned offset = dma_offset;
    complete(-5); cd_fold(); unchanged();
    assert(!cd_pending && !cd_async_ok && pcm_rd == 0);
    play(); presented = 1; position = 1024; I_PCM_Poll();
    assert(issue_calls == 1 && read_calls == 0);
    clock_us = 1000000; ++presented; I_PCM_Poll();
    assert(issue_calls == 2 && dma_offset == offset);
    complete(0); cd_fold(); assert(pcm_rd == dma_bytes && ring_valid == 2048);
    puts("PASS: DMA failure repeats buffered music, backs off, and retries without skipping");

    reset(); assert(cd_issue() == 0); I_PCM_DrainAsync();
    assert(busy && cd_pending && !cd_async_ok && cd_drain_gaveup);
    play(); position = 1024; presented = 1; I_PCM_Poll();
    assert(read_calls == 0 && issue_calls == 1); unchanged();
    complete_on_poll = 1; I_PCM_Poll();
    assert(!busy && !cd_pending && ring_valid == 2048 && pcm_rd == dma_bytes);
    puts("PASS: late DMA completion after drain timeout retires without reusing live staging");

    reset(); assert(cd_issue() == 0); play(); complete_on_poll = 1; quiet_menu = 1;
    I_PCM_Poll(); assert(poll_calls == 1 && !cd_pending && ring_valid == 2048);
    puts("PASS: polling recovers a pending read whose completion interrupt was lost");

    reset(); play(); ring_valid = 8192; clock_us = 300000;
    for (int i = 0; i < 12; ++i) {
        position = (position + 1024) % ring_frames;
        I_PCM_Poll(); clock_us += 100000;
    }
    unchanged(); assert(!issue_calls && !read_calls && !rate_changes && pumps == 12);
    ++presented; I_PCM_Poll(); assert(issue_calls == 1);
    puts("PASS: presentation stalls keep the last audio buffer looping and resume reads");

    reset(); play(); quiet_menu = 1; position = 1024;
    I_PCM_Poll(); unchanged(); assert(!issue_calls && !read_calls);
    quiet_menu = 0; I_PCM_Poll(); assert(issue_calls == 1);
    puts("PASS: save/load menus leave the file bridge free and replay the audio buffer");

    reset(); fail_call = 1; assert(!I_PCM_TryPlay(true));
    assert(!pcm_playing && voices == 0);
    reset(); pcm_checked = pcm_ready = 0; opened = 0;
    assert(!I_PCM_TryPlay(true) && voices == 0);
    puts("PASS: missing WAD or failed initial buffer falls back without starting invalid audio");

    reset(); assert(cd_issue() == 0); play(); I_PCM_Stop();
    assert(busy && !pcm_playing && !I_PCM_TryPlay(true) && !read_calls);
    complete(0); assert(I_PCM_TryPlay(true));
    puts("PASS: a new track waits for the old DMA buffer to be released");
    return 0;
}
