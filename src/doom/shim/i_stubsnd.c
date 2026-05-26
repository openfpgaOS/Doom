/* i_stubsnd.c — stub sound/music modules.
 *
 * chocolate-doom's i_sound.c has static tables that look up these
 * module objects by symbol name.  Provide inert modules so linkage
 * succeeds; music is silent on openfpgaOS for now.
 */

#include "config.h"
#include "i_sound.h"
#include "doomtype.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Config variables not provided by i_sound.c / gusconf.c. */
char *snd_dmxoption         = "-opl3";
char *music_pack_path       = "";
char *timidity_cfg_path     = "";
int   opl_io_port           = 0x388;
int   use_libsamplerate     = 0;
float libsamplerate_scale   = 0.65f;

#ifdef _WIN32
char *winmm_midi_device = "";
int   winmm_complevel   = 0;
int   winmm_reset_type  = 0;
int   winmm_reset_delay = 0;
#endif

/* ---- pcsound sound module ------------------------------------------- */

static const snddevice_t pc_devs[] = { SNDDEVICE_PCSPEAKER };

static boolean pc_Init(GameMission_t m) { (void)m; return true; }
static void    pc_Shutdown(void) { }
static int     pc_GetSfxLumpNum(sfxinfo_t *s) { (void)s; return -1; }
static void    pc_Update(void) { }
static void    pc_UpdateParams(int c, int v, int s) { (void)c; (void)v; (void)s; }
static int     pc_StartSound(sfxinfo_t *s, int c, int v, int p, int pi)
    { (void)s; (void)c; (void)v; (void)p; (void)pi; return -1; }
static void    pc_StopSound(int c) { (void)c; }
static boolean pc_SoundIsPlaying(int c) { (void)c; return false; }
static void    pc_CacheSounds(sfxinfo_t *s, int n) { (void)s; (void)n; }

const sound_module_t sound_pcsound_module = {
    pc_devs, 1,
    pc_Init, pc_Shutdown, pc_GetSfxLumpNum, pc_Update,
    pc_UpdateParams, pc_StartSound, pc_StopSound,
    pc_SoundIsPlaying, pc_CacheSounds,
};

/* ---- openfpgaOS MIDI music module (OPL slot) ------------------------ */

#include "of.h"
#include "of_midi.h"
#include "of_mixer.h"
#include "of_smp_bank.h"
#include "of_smp_voice.h"
#include "of_awe.h"
#include "memio.h"
#include "mus2mid.h"

typedef struct {
    void    *midi_data;
    size_t   midi_len;
} of_song_t;

static int opl_music_volume = 127;
static int opl_game_paused;
static int opl_zero_volume_suspended;

static void opl_suspend_zero_volume(void)
{
    opl_zero_volume_suspended = 1;

    if (!of_midi_playing())
    {
        return;
    }

    of_midi_pause();
    of_timer_set_callback(NULL, 0);
    smp_voice_all_off_global();
}

static void opl_resume_zero_volume(void)
{
    if (!opl_zero_volume_suspended)
    {
        return;
    }

    opl_zero_volume_suspended = 0;

    if (of_midi_playing() && !opl_game_paused)
    {
        of_timer_set_callback(of_midi_pump, 50);
        of_midi_resume();
    }
}

static boolean opl_Init(void)
{
    of_audio_init();
    of_mixer_init(32, 48000);
    of_mixer_set_master_volume(255);
    of_mixer_set_group_volume(OF_MIXER_GROUP_MUSIC, 255);
    of_mixer_set_group_volume(OF_MIXER_GROUP_SFX, 255);

    int rc = of_midi_init();

    /* SW voice engine — envelope/LFO/filter advance in C at 1 kHz,
     * mixer writes from CPU only.  Disables the AWE coprocessor path
     * that had the shared voice_state_ram, the multi-writer voice_tbl
     * mux, and the PITCH_COMPOSE stage.  Simpler, all debuggable in C. */
    smp_voice_enable_awe_backend(0);

    /* Reverb/chorus left at FPGA reset default (0 = bypass).  Duke3D's
     * midi_of.c warns explicitly that mididemo's (80,140) reverb +
     * (48,60,12) chorus settings sum past the mixer accumulator
     * headroom and clip — Duke3D and mididemo MODE_PLAY survive only
     * because their patch sets don't drive the dry mix to int16 max
     * for long.  Doom's id soundtrack is dense and sustained, so the
     * dry mix sits near saturation; adding ~30 % wet reverb +
     * ~18 % wet chorus on top pushed every loud passage into hard
     * clipping = "distorted mess".  Keeping wet at 0 leaves clean
     * dry mix; we can re-introduce a small wet send later once the
     * mixer adds compression instead of hard clamp. */

    return rc == OF_MIDI_OK;
}

static void opl_Shutdown(void)
{
    of_midi_stop();
    opl_game_paused = 0;
    opl_zero_volume_suspended = 0;
}

static void opl_SetVolume(int vol)
{
    if (vol < 0)
    {
        vol = 0;
    }
    else if (vol > 127)
    {
        vol = 127;
    }

    opl_music_volume = vol;
    of_midi_set_volume((vol * 255) / 127);

    if (vol == 0)
    {
        opl_suspend_zero_volume();
    }
    else
    {
        opl_resume_zero_volume();
    }
}

static void opl_Pause(void)
{
    opl_game_paused = 1;
    of_midi_pause();
}

static void opl_Resume(void)
{
    opl_game_paused = 0;

    if (opl_music_volume > 0 && of_midi_playing())
    {
        of_timer_set_callback(of_midi_pump, 50);
        of_midi_resume();
        opl_zero_volume_suspended = 0;
    }
}

static void *opl_RegisterSong(void *data, int len)
{
    of_song_t *song = malloc(sizeof(*song));
    if (!song) return NULL;

    if (len >= 4 && memcmp(data, "MThd", 4) == 0) {
        song->midi_data = malloc(len);
        if (!song->midi_data) { free(song); return NULL; }
        memcpy(song->midi_data, data, len);
        song->midi_len = len;
    } else {
        /* mus2mid() returns 0 on success, non-zero on failure (Unix
         * convention — not a boolean). */
        MEMFILE *in  = mem_fopen_read(data, len);
        MEMFILE *out = mem_fopen_write();
        if (!in || !out || mus2mid(in, out) != 0) {
            if (in)  mem_fclose(in);
            if (out) mem_fclose(out);
            free(song);
            return NULL;
        }
        mem_fclose(in);

        /* mem_get_buf hands back a pointer INTO the MEMFILE's internal
         * buffer — and mem_fclose() Z_Free's that buffer immediately
         * after.  of_midi_play stores the pointer for the ISR to read
         * asynchronously, so we must copy the MIDI to our own malloc'd
         * storage before closing the stream, otherwise the 500 Hz ISR
         * is reading freed memory and eventually traps. */
        void   *tmp_buf;
        size_t  tmp_len;
        mem_get_buf(out, &tmp_buf, &tmp_len);
        song->midi_data = malloc(tmp_len);
        if (!song->midi_data) { mem_fclose(out); free(song); return NULL; }
        memcpy(song->midi_data, tmp_buf, tmp_len);
        song->midi_len = tmp_len;
        mem_fclose(out);
    }

    return song;
}

static void opl_UnRegisterSong(void *handle)
{
    of_song_t *song = handle;
    if (!song) return;
    free(song->midi_data);
    free(song);
}

static void opl_PlaySong(void *handle, boolean looping)
{
    of_song_t *song = handle;
    if (!song) return;

    if (of_midi_play(song->midi_data, song->midi_len, looping) == OF_MIDI_OK)
    {
        if (opl_music_volume == 0)
        {
            opl_suspend_zero_volume();
        }
        else if (opl_game_paused)
        {
            of_midi_pause();
        }
    }
}

static void opl_StopSong(void)
{
    of_midi_stop();
    opl_zero_volume_suspended = opl_music_volume == 0;
}

static boolean opl_IsPlaying(void)   { return of_midi_playing(); }

/* MIDI envelope/LFO advance is still pumped by the 1 kHz timer ISR
 * (it's cheap).  But the heavy sample-mixing work has moved to the
 * main thread so the renderer can keep its cache warm — the ISR
 * used to trash it every ms and Doom rendered at 0.1 fps.  Poll is
 * called once per game tic (35 Hz); each call catches the CRAM1 DMA
 * ring back up to ~42 ms of buffered audio.  of_mixer_pump loops
 * swmixer_tick internally with a sane cap. */
static void    opl_Poll(void)
{
    if (opl_music_volume > 0)
    {
        of_mixer_pump();
    }
}

static const snddevice_t opl_devs[] = { SNDDEVICE_SB, SNDDEVICE_PAS,
                                         SNDDEVICE_ADLIB };

const music_module_t music_opl_module = {
    opl_devs, sizeof(opl_devs)/sizeof(*opl_devs),
    opl_Init, opl_Shutdown, opl_SetVolume,
    opl_Pause, opl_Resume,
    opl_RegisterSong, opl_UnRegisterSong,
    opl_PlaySong, opl_StopSong, opl_IsPlaying, opl_Poll
};

/* ---- Silent stub modules for the rest ------------------------------- */

static boolean mus_Init(void)                { return true; }
static void    mus_Shutdown(void)            { }
static void    mus_SetVolume(int v)          { (void)v; }
static void    mus_Pause(void)               { }
static void    mus_Resume(void)              { }
static void   *mus_Register(void *d, int l)  { (void)d; (void)l; return (void *)1; }
static void    mus_UnRegister(void *h)       { (void)h; }
static void    mus_Play(void *h, boolean l)  { (void)h; (void)l; }
static void    mus_Stop(void)                { }
static boolean mus_IsPlaying(void)           { return false; }
static void    mus_Poll(void)                { }

/* Pack-module stubs: *must* report "no substitution available" so that
 * I_RegisterSong falls back to music_module (our OPL module).  If the
 * pack module returns a non-NULL handle from RegisterSong, i_sound.c
 * sets active_music_module = &music_pack_module and every subsequent
 * PlaySong goes to the silent pack_Play stub — which is why music was
 * inaudible even though music_opl_module had initialised cleanly. */
static void *pack_Register(void *d, int l)   { (void)d; (void)l; return NULL; }

static const snddevice_t midi_devs[]   = { SNDDEVICE_GENMIDI,
                                           SNDDEVICE_WAVEBLASTER,
                                           SNDDEVICE_SOUNDCANVAS };
static const snddevice_t pack_devs[]   = { SNDDEVICE_GENMIDI,
                                           SNDDEVICE_WAVEBLASTER,
                                           SNDDEVICE_SOUNDCANVAS };
static const snddevice_t fsynth_devs[] = { SNDDEVICE_GENMIDI };

#define MUS_MOD(name, devs) \
    const music_module_t name = {                             \
        (devs), sizeof(devs)/sizeof(*(devs)),                 \
        mus_Init, mus_Shutdown, mus_SetVolume,                \
        mus_Pause, mus_Resume,                                \
        mus_Register, mus_UnRegister,                         \
        mus_Play, mus_Stop, mus_IsPlaying, mus_Poll           \
    }

MUS_MOD(music_sdl_module,  midi_devs);
MUS_MOD(music_fl_module,   fsynth_devs);
MUS_MOD(music_win_module,  midi_devs);

const music_module_t music_pack_module = {
    pack_devs, sizeof(pack_devs)/sizeof(*pack_devs),
    mus_Init, mus_Shutdown, mus_SetVolume,
    mus_Pause, mus_Resume,
    pack_Register, mus_UnRegister,
    mus_Play, mus_Stop, mus_IsPlaying, mus_Poll
};

void I_InitTimidityConfig(void) { }
void I_SetOPLDriverVer(opl_driver_ver_t v) { (void)v; }
void I_OPL_DevMessages(char *buf, size_t size) { if (size) buf[0] = 0; }
