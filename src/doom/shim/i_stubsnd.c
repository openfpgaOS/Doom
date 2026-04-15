/* i_stubsnd.c — stub sound/music modules.
 *
 * chocolate-doom's i_sound.c has static tables that look up these
 * module objects by symbol name.  Provide inert modules so linkage
 * succeeds; music is silent on openfpgaOS for now.
 */

#include "config.h"
#include "i_sound.h"
#include "doomtype.h"

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

/* ---- Silent music modules ------------------------------------------- */

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

static const snddevice_t midi_devs[]   = { SNDDEVICE_GENMIDI,
                                           SNDDEVICE_WAVEBLASTER,
                                           SNDDEVICE_SOUNDCANVAS };
static const snddevice_t opl_devs[]    = { SNDDEVICE_SB, SNDDEVICE_PAS,
                                           SNDDEVICE_ADLIB };
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
MUS_MOD(music_opl_module,  opl_devs);
MUS_MOD(music_pack_module, pack_devs);
MUS_MOD(music_fl_module,   fsynth_devs);
#ifdef _WIN32
MUS_MOD(music_win_module,  midi_devs);
#else
MUS_MOD(music_win_module,  midi_devs);
#endif

void I_InitTimidityConfig(void) { }
void I_SetOPLDriverVer(opl_driver_ver_t v) { (void)v; }
void I_OPL_DevMessages(char *buf, size_t size) { if (size) buf[0] = 0; }
