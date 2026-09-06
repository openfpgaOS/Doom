/* Exercise real SFX code against a single reusable hardware voice. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_malloc, malloc_calls, frees;
static void *test_malloc(size_t size)
{ return ++malloc_calls == fail_malloc ? NULL : malloc(size); }
static void test_free(void *ptr) { if (ptr) ++frees; free(ptr); }
#define malloc test_malloc
#define free test_free
#ifndef DOOM_SFX_SOURCE
#define DOOM_SFX_SOURCE "../../src/doom/shim/i_sdlsound.c"
#endif
#include DOOM_SFX_SOURCE
#undef malloc
#undef free

static of_mixer_handle_t active, generation;
static int group, writes, last_left, last_right, stops, releases;
static byte lump[8 + 32 + 256];
unsigned int of_time_us(void) { static unsigned int t; return ++t; }
void of_audio_init(void) { }
void of_mixer_init(int voices, int rate) { (void)voices; (void)rate; }
void of_mixer_pump(void) { }
void of_mixer_set_master_volume(int v) { (void)v; }
void of_mixer_set_group_volume(int g, int v) { (void)g; (void)v; }
void of_mixer_stop_all(void) { active = 0; }
void of_mixer_stop_h(of_mixer_handle_t v) { assert(v == active); active = 0; ++stops; }
void of_mixer_set_vol_lr_h(of_mixer_handle_t v, int l, int r)
{ assert(v == active); ++writes; last_left = l; last_right = r; }
void of_mixer_set_rate_h(of_mixer_handle_t v, int rate) { assert(v == active && rate > 0); }
int of_mixer_handle_active(of_mixer_handle_t v) { return v && v == active; }
int of_mixer_handle_group(of_mixer_handle_t v) { assert(v == active); return group; }
of_mixer_handle_t of_mixer_alloc_for_group_h(int g, const uint8_t *p,
                                          int n, int r, int pri, int vol)
{ (void)p; (void)n; (void)r; (void)pri; (void)vol; group = g; return active = ++generation << 32 | 1; }
int W_CheckNumForName(const char *name) { (void)name; return 0; }
int W_GetNumForName(const char *name) { (void)name; return 0; }
int W_LumpLength(lumpindex_t n) { (void)n; return sizeof(lump); }
void *W_CacheLumpNum(lumpindex_t n, int tag) { (void)n; (void)tag; return lump; }
void W_ReleaseLumpNum(lumpindex_t n) { (void)n; ++releases; }
const char *DEH_String(const char *s) { return s; }
int M_snprintf(char *buf, size_t size, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, size, fmt, ap); va_end(ap); return n; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    sfxinfo_t sound = {0};
    strcpy(sound.name, "test");
    lump[0] = 3; lump[2] = 0x11; lump[3] = 0x2b;
    lump[4] = 32; lump[5] = 1;
    for (int i = 0; i < 256; ++i) lump[8 + 16 + i] = i;
    if (!strcmp(argv[1], "allocation")) {
        fail_malloc = 2;
        assert(!load_sfx(&sound));
        assert(sound.driver_data == NULL && frees == 1 && releases == 1);
    } else if (!strcmp(argv[1], "pcm")) {
        assert(load_sfx(&sound));
        sfx_slot_t *slot = sound.driver_data;
        assert(slot->sample_count == 256 && slot->sample_rate == 11025);
        for (int i = 0; i < 256; ++i) {
            int expected = (i - 128) * 256;
#ifdef OF_DOOM
            expected = expected * 15 / 10;
            if (expected > 32767) expected = 32767;
            if (expected < -32768) expected = -32768;
#endif
            assert(slot->pcm[i] == expected);
        }
        free_sfx_slot(&sound);
    } else if (!strcmp(argv[1], "params")) {
        I_SDL_InitSound(doom);
        assert(I_SDL_StartSound(&sound, 0, 100, 128, NORM_PITCH) == 0);
        for (int i = 0; i < 1000; ++i) I_SDL_UpdateSoundParams(0, 100, 128);
        assert(writes == 1);
        for (int vol = 0; vol <= 127; ++vol)
            for (int sep = 0; sep <= 254; ++sep) {
                I_SDL_UpdateSoundParams(0, vol, sep);
                int v = vol * 255 / 127;
                assert(last_left == (254 - sep) * v / 255 && last_right == sep * v / 255);
            }
        int previous = writes;
        I_SDL_StartSound(&sound, 0, 127, 254, NORM_PITCH);
        assert(writes == previous + 1); /* A new generation needs its initial gain. */
        of_mixer_handle_t recycled = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_MUSIC, NULL, 1, 11025, 1, 100);
        I_SDL_UpdateSoundParams(0, 50, 30);
        I_SDL_StopSound(0);
        assert(active == recycled && writes == previous + 1 && stops == 1);
        I_SDL_StartSound(&sound, 1, 127, 254, NORM_PITCH);
        assert(writes == previous + 2);
        I_SDL_UpdateSoundParams(-1, 1, 1);
        I_SDL_UpdateSoundParams(NUM_CHANNELS, 1, 1);
        free_sfx_slot(&sound);
    } else abort();
    printf("PASS: SFX %s\n", argv[1]);
}
