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
    free(s->pcm);
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

    uint32_t rate = data[2] | (data[3] << 8);
    uint32_t samples = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    if (samples > (uint32_t)(len - 8)) samples = len - 8;
    if (samples < 32) { W_ReleaseLumpNum(sfx->lumpnum); return false; }

    /* DMX format skips 16 bytes of padding after the header, then PCM. */
    byte *pcm8 = data + 8;
    if (samples > 32) { pcm8 += 16; samples -= 32; }

    int16_t *pcm16 = malloc(samples * sizeof(int16_t));
    if (!pcm16) { W_ReleaseLumpNum(sfx->lumpnum); return false; }
    for (uint32_t i = 0; i < samples; i++)
        pcm16[i] = (int16_t)((pcm8[i] - 128) << 8);

    W_ReleaseLumpNum(sfx->lumpnum);

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
    of_audio_init();
    of_mixer_init(NUM_CHANNELS, OF_MIXER_OUTPUT_RATE);
    for (int i = 0; i < NUM_CHANNELS; i++) channel_voice[i] = -1;
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

static int I_SDL_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep, int pitch)
{
    (void)pitch;
    if ((unsigned)channel >= NUM_CHANNELS) return -1;
    if (!load_sfx(sfx)) return -1;
    sfx_slot_t *s = sfx->driver_data;

    /* Kill any existing voice on this channel. */
    if (channel_voice[channel] >= 0)
        of_mixer_stop(channel_voice[channel]);

    int voice = of_mixer_play((const uint8_t *)s->pcm, s->sample_count,
                              s->sample_rate, sfx->priority, 255);
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
    if ((unsigned)channel >= NUM_CHANNELS) return false;
    int v = channel_voice[channel];
    return v >= 0 ? (boolean)of_mixer_voice_active(v) : false;
}

static void I_SDL_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    for (int i = 0; i < num_sounds; i++) load_sfx(&sounds[i]);
    /* free_sfx_slot is called on shutdown implicitly via Z_Free if used;
     * we just hang onto allocations. (PC has plenty of memory.) */
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
