/* i_net_stub.c — replaces net_sdl.c. No multiplayer. */

#include "config.h"
#include "doomtype.h"
#include "net_defs.h"
#include "net_sdl.h"
#include "m_misc.h"
#include <string.h>

net_module_t net_sdl_module = {
    NULL, /* InitClient */
    NULL, /* InitServer */
    NULL, /* SendPacket */
    NULL, /* RecvPacket */
    NULL, /* AddrToString */
    NULL, /* FreeAddress */
    NULL, /* ResolveAddress */
};

void NET_SDL_Init(void) { }

void NET_DedicatedServer(void) { }

/* i_cdmus.c -------------------------------------------------------- */
int I_CDMusInit(void)        { return -1; }
int I_CDMusPlay(int track, int looping) { (void)track; (void)looping; return -1; }
int I_CDMusStop(void)        { return -1; }
int I_CDMusResume(void)      { return -1; }
int I_CDMusSetVolume(int v)  { (void)v; return -1; }
int I_CDMusGetVolume(void)   { return -1; }
int I_CDMusPause(void)       { return -1; }
void I_CDMusPrintError(void) { }

/* i_endoom.c ------------------------------------------------------- */
void I_Endoom(byte *endoom_data) { (void)endoom_data; }

/* net_gui.c -------------------------------------------------------- */
#include "net_client.h"
boolean NET_WaitForLaunch(void) { return true; }

/* i_videohr.c ------------------------------------------------------ */
boolean I_SetVideoModeHR(void) { return false; }
void    I_SetWindowTitleHR(const char *t) { (void)t; }
void    I_UnsetVideoModeHR(void) { }
void    I_ClearScreenHR(void) { }
void    I_InitPaletteHR(void) { }
void    I_SetPaletteHR(const byte *p) { (void)p; }
void    I_SlamBlockHR(int x, int y, int w, int h, const byte *src) {
    (void)x; (void)y; (void)w; (void)h; (void)src;
}
void    I_SlamHR(const byte *buf) { (void)buf; }
void    I_CopyScreenHR(byte *dest) { (void)dest; }
void    I_InputHR(void) { }
/* i_videohr.h declares this boolean; Hexen's ST_Progress quits when it reads
 * true, so a void stub returns a garbage register and aborts startup. */
boolean I_CheckAbortHR(void) { return false; }
