/* i_main.c — openfpgaOS entry: boot SDK, then call D_DoomMain. */

#include "config.h"
#include "of.h"
#include "of_caps.h"
#ifndef OF_PC
#include "of_video.h"
#include "doom_loading_logo.h"
#endif
#include "doomtype.h"
#include "i_save.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#ifndef OF_PC
#include "p_saveg.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void D_DoomMain(void);

#ifndef OF_PC
static void ShowLoadingLogo(void)
{
    of_video_mode_t logo_mode = {
        OF_SCREEN_W,
        OF_SCREEN_H,
        OF_SCREEN_W,
        OF_VIDEO_MODE_8BIT,
        0
    };
    of_video_mode_t actual;
    unsigned int dst_w;
    unsigned int dst_h;
    unsigned int dst_stride;
    size_t surface_bytes;

    of_video_set_refresh_vtotal(OF_VIDEO_VTOTAL_60HZ);
    of_video_get_mode(&actual);
    if (actual.width != OF_SCREEN_W ||
        actual.height != OF_SCREEN_H ||
        actual.stride != OF_SCREEN_W ||
        actual.color_mode != OF_VIDEO_MODE_8BIT)
    {
        (void) of_video_set_mode(&logo_mode);
    }
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    of_video_get_mode(&actual);

    dst_w = actual.width != 0 ? actual.width : OF_SCREEN_W;
    dst_h = actual.height != 0 ? actual.height : OF_SCREEN_H;
    dst_stride = actual.stride != 0 ? actual.stride : dst_w;
    surface_bytes = (size_t) dst_stride * dst_h;

    printf("Doom logo video: mode %ux%u stride %u color %u\n",
           dst_w, dst_h, dst_stride, (unsigned int)actual.color_mode);
    of_video_palette_bulk(doom_loading_logo_palette, 256);

    for (int frame = 0; frame < 3; ++frame)
    {
        uint8_t *fb = of_video_surface();
        memset(fb, 0, surface_bytes);

        if (actual.color_mode == OF_VIDEO_MODE_8BIT
         && doom_loading_logo_w == (int) dst_w
         && doom_loading_logo_h == (int) dst_h
         && dst_stride == dst_w)
        {
            memcpy(fb, doom_loading_logo_pixels, sizeof(doom_loading_logo_pixels));
        }
        else if (actual.color_mode == OF_VIDEO_MODE_8BIT)
        {
            int copy_w = doom_loading_logo_w < (int) dst_w
                       ? doom_loading_logo_w : (int) dst_w;
            int copy_h = doom_loading_logo_h < (int) dst_h
                       ? doom_loading_logo_h : (int) dst_h;
            int src_x = (doom_loading_logo_w - copy_w) / 2;
            int src_y = (doom_loading_logo_h - copy_h) / 2;
            int dst_x = ((int) dst_w - copy_w) / 2;
            int dst_y = ((int) dst_h - copy_h) / 2;

            for (int y = 0; y < copy_h; ++y)
            {
                memcpy(fb + (dst_y + y) * dst_stride + dst_x,
                       doom_loading_logo_pixels
                       + (src_y + y) * doom_loading_logo_w + src_x,
                       (size_t) copy_w);
            }
        }

        of_video_flip();
        of_video_wait_flip();
    }

    {
        of_video_timing_t timing;

        of_video_get_timing(&timing);
        printf("Doom logo video: vblank=%u present=%u last=%u\n",
               timing.vblank_count,
               timing.present_count,
               timing.last_presented_idx);
    }
}

static const char *FindArgValue(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (!strcmp(argv[i], name))
        {
            return argv[i + 1];
        }
    }

    return NULL;
}

static void ConfigureDoomInstanceFromArgs(int argc, char **argv)
{
    const char *iwad = FindArgValue(argc, argv, "-iwad");
    const char *pwad = FindArgValue(argc, argv, "-merge");
    const char *save_prefix = FindArgValue(argc, argv, "-saveprefix");

    if (pwad == NULL)
    {
        pwad = FindArgValue(argc, argv, "-file");
    }

    P_SetOpenFPGASavePrefix(save_prefix);
    I_SetOpenFPGASaveIdentity(iwad, pwad);
    I_MigratePocketDoomSaves();
}

#endif

int main(int argc, char **argv)
{
#ifdef OF_PC
    /* Doom ticks at 35 Hz. On a 60 Hz desktop, hard vsync inside
     * SDL_RenderPresent aligns each frame to a 16.67 ms boundary,
     * which effectively quantises our 28.57 ms tic budget to every
     * *other* vsync (= 30 Hz) and makes the game feel choppy.
     * TryRunTics already paces the loop, so disable vsync and let
     * the tic cadence drive the display rate. */
    setenv("OF_NO_VSYNC", "1", 0);
#endif

    /* Boot the SDK. Audio/mixer init is deferred to I_SDL_InitSound
     * (i_sdlsound.c) so the sound module owns the channel count. */
    of_video_init();

#ifndef OF_PC
    ShowLoadingLogo();
    ConfigureDoomInstanceFromArgs(argc, argv);
#endif

    myargc = argc;
    myargv = malloc(myargc * sizeof(char *));
    assert(myargv != NULL);
    for (int i = 0; i < argc; i++)
        myargv[i] = M_StringDuplicate(argv[i]);

    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }

    M_FindResponseFile();
    M_SetExeDir();

    D_DoomMain();
    return 0;
}
