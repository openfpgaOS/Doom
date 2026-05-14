/* i_video.c — chocolate-doom video backend over openfpgaOS SDK.
 *
 * Doom renders into a 320x200 indexed buffer (I_VideoBuffer).  With GPU
 * flip enabled, the GPU layer retargets that pointer at the centered
 * 320x200 window inside the current rotating hardware framebuffer.  The
 * static buffer below remains the CPU fallback when GPU flip is disabled.
 */

#include "config.h"
#include "of.h"
#include "i_video.h"
#include "v_video.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "r_gpu.h"
#include "z_zone.h"
#include "m_controls.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Public globals expected by chocolate-doom core. */
char *video_driver    = "";
boolean screenvisible = true;

int     vanilla_keyboard_mapping = 1;
boolean screensaver_mode         = false;
int     usegamma                 = 0;
pixel_t *I_VideoBuffer           = NULL;

int     screen_width             = SCREENWIDTH;
int     screen_height            = SCREENHEIGHT;
int     fullscreen               = 1;
int     aspect_ratio_correct     = 0;
int     integer_scaling          = 1;
int     smooth_pixel_scaling     = 0;
int     vga_porch_flash          = 0;
int     force_software_renderer  = 0;
int     png_screenshots          = 0;
char   *window_position          = "center";
unsigned int joywait             = 0;
int     usemouse                 = 0;

/* ---- Gamma (chocolate-doom's 5-level table) -------------------------- */
static const byte gammatable[5][256] = {
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
     33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
     65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,
     97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
     121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
     145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,
     169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,
     193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,
     217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,
     241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,255}
};

/* Palette storage (doom hands us 256 × RGB 0-255 triplets). */
static uint32_t palette32[256];

/* Stable fallback backbuffer used when GPU flip is disabled/unavailable. */
static pixel_t video_buf[SCREENWIDTH * SCREENHEIGHT];

/* Grab-mouse callback (ignored on this backend). */
static grabmouse_callback_t grabmouse_cb;

#define LETTERBOX_Y  ((OF_SCREEN_H - SCREENHEIGHT) / 2)

/* ---- Init / shutdown ------------------------------------------------- */

void I_InitGraphics(void)
{
    of_video_init();
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    of_video_set_color_mode(OF_VIDEO_MODE_8BIT);

    /* Clear letterbox bars on all three back buffers once. */
    for (int buf = 0; buf < 3; buf++) {
        uint8_t *fb = of_video_surface();
        memset(fb, 0, OF_SCREEN_W * OF_SCREEN_H);
        of_video_flip();
        of_video_wait_flip();
    }

    I_VideoBuffer = video_buf;
    V_RestoreBuffer();
    R_GPU_Init();
    screenvisible = true;

    key_fire = KEY_ENTER;
    key_use  = ' ';
    key_speed = KEY_RSHIFT;
    key_strafeleft = ',';
    key_straferight = '.';
    key_prevweapon = '[';
    key_nextweapon = ']';
    key_message_refresh = 0;
}

void I_GraphicsCheckCommandLine(void) { }
void I_ShutdownGraphics(void)          { R_GPU_Shutdown(); I_VideoBuffer = NULL; }

void I_SetWindowTitle(const char *t)   { (void)t; }
void I_InitWindowTitle(void)           { }
void I_RegisterWindowIcon(const unsigned int *icon, int w, int h) { (void)icon; (void)w; (void)h; }
void I_InitWindowIcon(void)            { }
void I_CheckIsScreensaver(void)        { }
void I_SetGrabMouseCallback(grabmouse_callback_t func) { grabmouse_cb = func; (void)grabmouse_cb; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void)xoffs; (void)yoffs; }
void I_BeginRead(void)                 { }
void I_StartFrame(void)                { }
extern void I_PollInput(void);
void I_StartTic(void)                  { I_PollInput(); }

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void)w; (void)h;
    if (x) *x = 0;
    if (y) *y = 0;
}

void I_BindVideoVariables(void)
{
    M_BindIntVariable("use_mouse",               &usemouse);
    M_BindIntVariable("fullscreen",              &fullscreen);
    M_BindIntVariable("aspect_ratio_correct",    &aspect_ratio_correct);
    M_BindIntVariable("integer_scaling",         &integer_scaling);
    M_BindIntVariable("vga_porch_flash",         &vga_porch_flash);
    M_BindIntVariable("smooth_pixel_scaling",    &smooth_pixel_scaling);
    M_BindIntVariable("startup_delay",           (int[]){0});
    M_BindIntVariable("fullscreen_width",        &screen_width);
    M_BindIntVariable("fullscreen_height",       &screen_height);
    M_BindIntVariable("force_software_renderer", &force_software_renderer);
    M_BindIntVariable("max_scaling_buffer_pixels", (int[]){0});
    M_BindIntVariable("window_width",            &screen_width);
    M_BindIntVariable("window_height",           &screen_height);
    M_BindIntVariable("grabmouse",               (int[]){0});
    M_BindIntVariable("usegamma",                &usegamma);
    M_BindIntVariable("png_screenshots",         &png_screenshots);
    M_BindIntVariable("vanilla_keyboard_mapping",&vanilla_keyboard_mapping);
    M_BindStringVariable("video_driver",         &video_driver);
    M_BindStringVariable("window_position",      &window_position);
}

/* ---- Palette --------------------------------------------------------- */

void I_SetPalette(byte *doompal)
{
    const byte *gamma = gammatable[usegamma % 5];
    for (int i = 0; i < 256; i++) {
        uint32_t r = gamma[*doompal++];
        uint32_t g = gamma[*doompal++];
        uint32_t b = gamma[*doompal++];
        palette32[i] = (r << 16) | (g << 8) | b;
    }
    of_video_palette_bulk(palette32, 256);
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = 0x7FFFFFFF;
    for (int i = 0; i < 256; i++) {
        int dr = r - ((palette32[i] >> 16) & 0xFF);
        int dg = g - ((palette32[i] >>  8) & 0xFF);
        int db = b - ((palette32[i])       & 0xFF);
        int d  = dr*dr + dg*dg + db*db;
        if (d < best_diff) { best_diff = d; best = i; if (d == 0) break; }
    }
    return best;
}

/* ---- Frame update ---------------------------------------------------- */

void I_UpdateNoBlit(void) { }

void I_FinishUpdate(void)
{
    /* Pace the fallback blit path to ~60 Hz.
     *
     * Before uncapped interpolation landed, TryRunTics() blocked ~28 ms
     * per iteration waiting for the next 35 Hz tic — that was the only
     * thing keeping the loop from running at CPU speed, because
     * of_video_flip() is non-blocking.
     *
     * With the uncapped path, TryRunTics() returns immediately when no
     * new tic is ready. Without a wait here the main loop busy-spins at
     * CPU speed, which starves audio/IRQ handling enough to appear as a
     * freeze with a stuck buzzing sound effect when the menu sfx fires.
     *
     * Direct GPU flip already waits for the previous display swap in
     * R_GPU_PresentFrame(), so sleeping here as well creates duplicate
     * pacing and costs the frames we are trying to hit. */
    static unsigned int last_flip_us = 0;
    if (R_GPU_UsingDirectFramebuffer()) {
        last_flip_us = 0;
        if (R_GPU_PresentFrame())
            return;

        R_GPU_EndFrame();
    } else {
        R_GPU_EndFrame();

        const unsigned int target_us = 16667;  /* ~60 Hz */
        unsigned int now = of_time_us();
        unsigned int dt  = now - last_flip_us;
        if (last_flip_us && dt < target_us) {
            usleep(target_us - dt);
        }
        last_flip_us = of_time_us();
    }

    uint8_t *fb = of_video_surface();
    memcpy(fb + OF_SCREEN_W * LETTERBOX_Y, I_VideoBuffer,
           SCREENWIDTH * SCREENHEIGHT);
    of_video_flip();
}

void I_ReadScreen(pixel_t *scr)
{
    R_GPU_PrepareForCPUAccess();
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}
