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
#define AUDIO_PUMP_SKIP_US 1000u

typedef struct {
    int16_t *pcm;
    uint32_t sample_count;
    uint32_t sample_rate;
} sfx_slot_t;

/* Per-Doom-channel state (channel -> stable mixer handle). */
static of_mixer_handle_t channel_voice[NUM_CHANNELS];
static unsigned int mixer_last_pump_us;

void I_OpenFPGAMixerPump(void)
{
    unsigned int now_us = of_time_us();

    if (mixer_last_pump_us != 0 &&
        now_us - mixer_last_pump_us < AUDIO_PUMP_SKIP_US)
    {
        return;
    }

    of_mixer_pump();
    mixer_last_pump_us = of_time_us();
}

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

// Doom/Strife prefix sound lumps with "ds"; Heretic/Hexen name them directly
// (e.g. "CLKSIT"). I_SDL_InitSound sets this from the game's mission.
static boolean use_sfx_prefix = true;

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sounds (Heretic's SOUND_LINK aliases, e.g. "-impact" -> impsit)
    // resolve to the target sound's lump, not the alias name.
    if (sfx->link != NULL)
        sfx = sfx->link;

    if (use_sfx_prefix)
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    else
        M_snprintf(buf, buf_len, "%s", DEH_String(sfx->name));
}

static boolean load_sfx(sfxinfo_t *sfx)
{
    if (sfx->driver_data) return true;
    if (sfx->lumpnum < 0) {
        char namebuf[9];
        GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
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

    /* Current openfpgaOS can DMA mixer samples from ordinary SDRAM and
     * flushes the range before playback. Avoid the legacy mixer allocator:
     * it has a small compatibility slot table and Doom precaches more SFX
     * lumps than it can hold. */
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
    use_sfx_prefix = (mission == doom || mission == strife);
    of_audio_init();
    of_mixer_init(32, 48000);
    of_mixer_set_master_volume(255);
    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, 255);
    of_mixer_set_group_volume(OF_MIXER_GROUP_SFX, 255);

    for (int i = 0; i < NUM_CHANNELS; i++)
        channel_voice[i] = OF_MIXER_HANDLE_INVALID;
    return true;
}

static void I_SDL_ShutdownSound(void)
{
    of_mixer_stop_all();
}

static int I_SDL_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_SDL_UpdateSound(void)
{
    I_OpenFPGAMixerPump();
}

/* Doom volume: 0..127, sep: 0..254 (0=left, 128=center, 254=right).
 * The target mixer output is physically reversed for SFX, so hand the
 * mixer the opposite channel volumes while keeping Doom's sep semantics
 * intact above this layer. */
static void set_params(of_mixer_handle_t voice, int vol, int sep)
{
    if (voice == OF_MIXER_HANDLE_INVALID) return;
    /* Map 0..127 -> 0..255 */
    int v = (vol * 255) / 127;
    int left  = ((254 - sep) * v) / 255;
    int right = (sep        * v) / 255;
    if (left > 255)  left  = 255;
    if (right > 255) right = 255;
    of_mixer_set_vol_lr_h(voice, right, left);
}

static boolean sfx_voice_owned(of_mixer_handle_t voice)
{
    if (voice == OF_MIXER_HANDLE_INVALID || !of_mixer_handle_active(voice))
        return false;

    int group = of_mixer_handle_group(voice);
    return group < 0 || group == OF_MIXER_GROUP_SFX;
}

static void clear_voice_refs(of_mixer_handle_t voice)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        if (channel_voice[i] == voice)
            channel_voice[i] = OF_MIXER_HANDLE_INVALID;
    }
}

static void stop_channel_voice(int channel)
{
    of_mixer_handle_t voice = channel_voice[channel];

    if (sfx_voice_owned(voice))
        of_mixer_stop_h(voice);

    channel_voice[channel] = OF_MIXER_HANDLE_INVALID;
}

static int steal_one_sfx_voice(void)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        if (sfx_voice_owned(channel_voice[i]))
        {
            stop_channel_voice(i);
            return true;
        }
    }

    return false;
}

static void I_SDL_UpdateSoundParams(int channel, int vol, int sep)
{
    if ((unsigned)channel >= NUM_CHANNELS) return;
    if (!sfx_voice_owned(channel_voice[channel]))
    {
        channel_voice[channel] = OF_MIXER_HANDLE_INVALID;
        return;
    }
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
    of_mixer_handle_t voice;

    if ((unsigned)channel >= NUM_CHANNELS) return -1;

    if (!sfx->driver_data && !load_sfx(sfx)) return -1;
    slot = sfx->driver_data;
    if (!slot || !slot->pcm || !slot->sample_count) return -1;

    /* Stop any prior voice on this channel before reusing it. */
    if (channel_voice[channel] != OF_MIXER_HANDLE_INVALID)
        stop_channel_voice(channel);

    /* Linear pitch bend around NORM_PITCH (=128).  Vanilla uses a log
     * steptable; linear is within a few cents at the ±16 range Doom
     * actually emits and keeps the code arithmetic-only. */
    rate = slot->sample_rate;
    if (pitch != NORM_PITCH && pitch > 0)
        rate = (rate * (uint32_t)pitch) / NORM_PITCH;

    voice = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_SFX,
                                       (const uint8_t *)slot->pcm,
                                       slot->sample_count, rate,
                                       SFX_PRIORITY, 200);
    if (voice == OF_MIXER_HANDLE_INVALID && steal_one_sfx_voice())
    {
        voice = of_mixer_alloc_for_group_h(OF_MIXER_GROUP_SFX,
                                           (const uint8_t *)slot->pcm,
                                           slot->sample_count, rate,
                                           SFX_PRIORITY, 200);
    }
    if (voice == OF_MIXER_HANDLE_INVALID) return -1;

    /* Hardware mixer may ignore the sample_rate arg in of_mixer_play and
     * default to output rate (48 kHz), which plays an 11025 Hz DMX sample
     * ~4.4x too fast — whiny/chirpy.  Set rate explicitly like the MIDI
     * synth does after its own of_mixer_play.
     *
     * Do NOT call of_mixer_set_loop here: PC uses loop_end<0 as the "no
     * loop" sentinel, but passing -1 across the HW service ABI becomes
     * 0xFFFFFFFF and the mixer treats it as an infinite loop — the SFX
     * keeps playing forever.  Every voice starts as a one-shot from
     * the mixer allocator, so leaving it alone is correct. */
    of_mixer_set_rate_h(voice, (int)rate);

    clear_voice_refs(voice);
    channel_voice[channel] = voice;
    set_params(voice, vol, sep);
    return channel;
}

static void I_SDL_StopSound(int channel)
{
    if ((unsigned)channel >= NUM_CHANNELS) return;
    stop_channel_voice(channel);
}

static boolean I_SDL_SoundIsPlaying(int channel)
{
    of_mixer_handle_t v;
    if ((unsigned)channel >= NUM_CHANNELS) return false;
    v = channel_voice[channel];
    if (v == OF_MIXER_HANDLE_INVALID) return false;
    if (!sfx_voice_owned(v)) {
        /* Voice finished on its own — clear the slot so StartSound
         * doesn't try to stop a voice the mixer has already recycled. */
        channel_voice[channel] = OF_MIXER_HANDLE_INVALID;
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
