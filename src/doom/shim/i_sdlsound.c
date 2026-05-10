/* i_sdlsound.c — Doom sound effects over the openfpgaOS mixer.
 *
 * Doom SFX are DMX lumps: 2-byte version, 2-byte rate, 4-byte length,
 * 16 bytes padding, then raw unsigned 8-bit PCM.  We expand to 16-bit
 * signed at load time and drive of_mixer_* directly (one mixer voice
 * per Doom channel).
 */

#include "config.h"
#include "of.h"
#include "i_sound.h"
#include "i_system.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"
#include "doomtype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_CHANNELS 16

typedef struct {
    int16_t *pcm;
    uint32_t sample_count;
    uint32_t sample_rate;
} sfx_slot_t;

/* Per-Doom-channel state (channel -> mixer voice). */
static int channel_voice[NUM_CHANNELS];

/* Sound devices we claim we can drive. */
static const snddevice_t sdl_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GENMIDI,
    SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32
};

/* ---- DMX SFX loader -------------------------------------------------- */

static void free_sfx_slot(sfxinfo_t *sfx)
{
    sfx_slot_t *s = sfx->driver_data;
    if (!s) return;
    /* s->pcm was allocated from CRAM1 via of_mixer_alloc_samples, which
     * is a bump allocator with no per-slot free.  Leave it; the whole
     * pool is reclaimed by of_mixer_free_samples() at shutdown. */
    free(s);
    sfx->driver_data = NULL;
}

static boolean load_sfx(sfxinfo_t *sfx)
{
    if (sfx->driver_data) return true;
    if (sfx->lumpnum < 0) {
        char namebuf[9];
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
        sfx->lumpnum = W_CheckNumForName(namebuf);
        if (sfx->lumpnum < 0) return false;
    }

    int len = W_LumpLength(sfx->lumpnum);
    if (len < 8) return false;
    byte *data = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);

    /* DMX magic: 03 00 00 00.  Reject non-DMX lumps so we don't expand
     * arbitrary bytes into samples. */
    if (data[0] != 0x03 || data[1] != 0x00) {
        W_ReleaseLumpNum(sfx->lumpnum);
        return false;
    }

    uint32_t rate = data[2] | (data[3] << 8);
    uint32_t samples = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    if (samples > (uint32_t)(len - 8)) samples = len - 8;
    if (samples < 32) { W_ReleaseLumpNum(sfx->lumpnum); return false; }

    /* DMX format skips 16 bytes of padding after the header, then PCM. */
    byte *pcm8 = data + 8;
    if (samples > 32) { pcm8 += 16; samples -= 32; }

    /* First-few-loads diagnostic so we can sanity-check rate/length/CRAM1
     * contents when SFX sound wrong (e.g. whiny = rate misinterpretation,
     * silent-then-garbage = CRAM1 write not visible to mixer DMA). */
    static int sfx_load_diag = 0;
    if (sfx_load_diag < 6) {
        printf("SFX load[%d]: name=%-8s lump=%d len=%d rate=%u samples=%u\n",
               sfx_load_diag, sfx->name, sfx->lumpnum, len,
               (unsigned)rate, (unsigned)samples);
        printf("  first 8 raw DMX bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               pcm8[0], pcm8[1], pcm8[2], pcm8[3],
               pcm8[4], pcm8[5], pcm8[6], pcm8[7]);
        sfx_load_diag++;
    }

    /* The hardware mixer reads samples from CRAM1 — ordinary malloc'd
     * heap memory is not accessible to it, so we MUST allocate via
     * of_mixer_alloc_samples and expand 8→16-bit into that buffer. */
    int16_t *pcm16 = of_mixer_alloc_samples(samples * sizeof(int16_t));
    if (!pcm16) { W_ReleaseLumpNum(sfx->lumpnum); return false; }
    for (uint32_t i = 0; i < samples; i++)
        pcm16[i] = (int16_t)((pcm8[i] - 128) << 8);

    W_ReleaseLumpNum(sfx->lumpnum);

    if (sfx_load_diag <= 6 && sfx_load_diag > 0) {
        printf("  cram1=%p first 4 int16: %d %d %d %d\n",
               (void *)pcm16, pcm16[0], pcm16[1], pcm16[2], pcm16[3]);
    }

    sfx_slot_t *slot = malloc(sizeof(*slot));
    slot->pcm          = pcm16;
    slot->sample_count = samples;
    slot->sample_rate  = rate ? rate : 11025;
    sfx->driver_data   = slot;
    return true;
}

/* ---- Module functions ----------------------------------------------- */

static boolean I_SDL_InitSound(GameMission_t mission)
{
    (void)mission;
    /* Audio + mixer are initialised by music_opl_module's Init.  Two inits
     * race over master/group volumes and were the only divergence from
     * mididemo's clean path. */
    for (int i = 0; i < NUM_CHANNELS; i++) channel_voice[i] = -1;
    printf("SFX: init ok (stub — music_opl_module owns mixer)\n");
    return true;
}

static void I_SDL_ShutdownSound(void)
{
    of_mixer_stop_all();
}

static int I_SDL_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    return W_GetNumForName(namebuf);
}

static void I_SDL_UpdateSound(void)
{
    of_mixer_pump();
}

/* Doom volume: 0..127, sep: 0..255 (0=left, 127=center, 255=right). */
static void set_params(int voice, int vol, int sep)
{
    if (voice < 0) return;
    /* Map 0..127 -> 0..255 */
    int v = (vol * 255) / 127;
    int left  = ((254 - sep) * v) / 255;
    int right = (sep        * v) / 255;
    if (left > 255)  left  = 255;
    if (right > 255) right = 255;
    of_mixer_set_vol_lr(voice, left, right);
}

static void I_SDL_UpdateSoundParams(int channel, int vol, int sep)
{
    if ((unsigned)channel >= NUM_CHANNELS) return;
    set_params(channel_voice[channel], vol, sep);
}

/* SFX priority is set above MIDI note priority (which of_smp_voice uses
 * with priority=0) so a busy music track can't starve gameplay sounds
 * when the 32-voice mixer is saturated.  NORM_PITCH (=127) comes from
 * i_sound.h. */
#define SFX_PRIORITY 100

static int I_SDL_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep, int pitch)
{
    sfx_slot_t *slot;
    uint32_t    rate;
    int         voice;

    if ((unsigned)channel >= NUM_CHANNELS) return -1;

    if (!sfx->driver_data && !load_sfx(sfx)) return -1;
    slot = sfx->driver_data;
    if (!slot || !slot->pcm || !slot->sample_count) return -1;

    /* Stop any prior voice on this channel before reusing it. */
    if (channel_voice[channel] >= 0)
        of_mixer_stop(channel_voice[channel]);

    /* Linear pitch bend around NORM_PITCH (=128).  Vanilla uses a log
     * steptable; linear is within a few cents at the ±16 range Doom
     * actually emits and keeps the code arithmetic-only. */
    rate = slot->sample_rate;
    if (pitch != NORM_PITCH && pitch > 0)
        rate = (rate * (uint32_t)pitch) / NORM_PITCH;

    voice = of_mixer_play((const uint8_t *)slot->pcm, slot->sample_count,
                          rate, SFX_PRIORITY, 200);
    if (voice < 0) return -1;

    /* Hardware mixer may ignore the sample_rate arg in of_mixer_play and
     * default to output rate (48 kHz), which plays an 11025 Hz DMX sample
     * ~4.4x too fast — whiny/chirpy.  Set rate explicitly like the MIDI
     * synth does after its own of_mixer_play.
     *
     * Do NOT call of_mixer_set_loop here: PC uses loop_end<0 as the "no
     * loop" sentinel, but passing -1 across the HW service ABI becomes
     * 0xFFFFFFFF and the mixer treats it as an infinite loop — the SFX
     * keeps playing forever.  Every voice starts as a one-shot from
     * of_mixer_play, so leaving it alone is correct. */
    of_mixer_set_rate(voice, (int)rate);

    of_mixer_set_group(voice, OF_MIXER_GROUP_SFX);
    channel_voice[channel] = voice;
    set_params(voice, vol, sep);
    return channel;
}

static void I_SDL_StopSound(int channel)
{
    if ((unsigned)channel >= NUM_CHANNELS) return;
    if (channel_voice[channel] >= 0) {
        of_mixer_stop(channel_voice[channel]);
        channel_voice[channel] = -1;
    }
}

static boolean I_SDL_SoundIsPlaying(int channel)
{
    int v;
    if ((unsigned)channel >= NUM_CHANNELS) return false;
    v = channel_voice[channel];
    if (v < 0) return false;
    if (!of_mixer_voice_active(v)) {
        /* Voice finished on its own — clear the slot so StartSound
         * doesn't try to stop a voice the mixer has already recycled. */
        channel_voice[channel] = -1;
        return false;
    }
    return true;
}

static void I_SDL_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    for (int i = 0; i < num_sounds; i++) load_sfx(&sounds[i]);
    (void)free_sfx_slot;
}

const sound_module_t sound_sdl_module = {
    sdl_devices, sizeof(sdl_devices) / sizeof(*sdl_devices),
    I_SDL_InitSound,
    I_SDL_ShutdownSound,
    I_SDL_GetSfxLumpNum,
    I_SDL_UpdateSound,
    I_SDL_UpdateSoundParams,
    I_SDL_StartSound,
    I_SDL_StopSound,
    I_SDL_SoundIsPlaying,
    I_SDL_PrecacheSounds,
};
