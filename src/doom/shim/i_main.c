/* i_main.c — openfpgaOS entry: boot SDK, then call D_DoomMain. */

#include "config.h"
#include "of.h"
#include "of_caps.h"
#ifndef OF_PC
#include "of_file.h"
#include "of_video.h"
#include "doom_loading_logo.h"
#endif
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void D_DoomMain(void);

#ifndef OF_PC
static void ShowLoadingLogo(void)
{
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    of_video_set_color_mode(OF_VIDEO_MODE_8BIT);
    of_video_palette_bulk(doom_loading_logo_palette, 256);

    for (int frame = 0; frame < 3; ++frame)
    {
        uint8_t *fb = of_video_surface();
        memset(fb, 0, OF_SCREEN_W * OF_SCREEN_H);

        if (doom_loading_logo_w == OF_SCREEN_W
         && doom_loading_logo_h == OF_SCREEN_H)
        {
            memcpy(fb, doom_loading_logo_pixels, sizeof(doom_loading_logo_pixels));
        }
        else
        {
            int copy_w = doom_loading_logo_w < OF_SCREEN_W
                       ? doom_loading_logo_w : OF_SCREEN_W;
            int copy_h = doom_loading_logo_h < OF_SCREEN_H
                       ? doom_loading_logo_h : OF_SCREEN_H;
            int src_x = (doom_loading_logo_w - copy_w) / 2;
            int src_y = (doom_loading_logo_h - copy_h) / 2;
            int dst_x = (OF_SCREEN_W - copy_w) / 2;
            int dst_y = (OF_SCREEN_H - copy_h) / 2;

            for (int y = 0; y < copy_h; ++y)
            {
                memcpy(fb + (dst_y + y) * OF_SCREEN_W + dst_x,
                       doom_loading_logo_pixels
                       + (src_y + y) * doom_loading_logo_w + src_x,
                       (size_t) copy_w);
            }
        }

        of_video_flip();
        of_video_wait_flip();
    }
}

static const char *save_slot_names[] =
{
    "doom_0.sav",
    "doom_1.sav",
    "doom_2.sav",
    "doom_3.sav",
    "doom_4.sav",
    "doom_5.sav",
    "doom_6.sav",
    "doom_7.sav",
    "doom_8.sav",
    "doom_9.sav",
};

static void RegisterPersistentFiles(void)
{
    static boolean registered = false;

    if (registered)
    {
        return;
    }

    of_file_slot_register(9, "doom.cfg");

    for (int i = 0; i < 10; ++i)
    {
        of_file_slot_register(10 + (uint32_t) i, save_slot_names[i]);
    }

    registered = true;
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

    {
        const struct of_capabilities *c = of_get_caps();
        printf("heap: base=%08x size=%u (%u KB) sdram=%u MB\n",
               c->heap_base, c->heap_size, c->heap_size / 1024,
               c->sdram_size / (1024*1024));
    }

    RegisterPersistentFiles();
#endif

    /* openfpgaOS port: the Pocket kernel has no mechanism to pass argv
     * from instance.json, so we inject "-iwad DOOM.WAD" here. The
     * instance JSON binds DOOM.WAD to data slot 3; the kernel file
     * service resolves the filename via that binding. On the desktop
     * build (OF_PC) the user can still override via the real command
     * line -- anything they pass wins because M_CheckParmWithArgs()
     * returns the first match. */
#ifndef OF_PC
    int   injected = 3;
    char *injected_argv[] = { "-iwad", "DOOM.WAD", "-noautoload" };
#else
    int   injected = 0;
    char *injected_argv[] = { 0 };
#endif

    myargc = argc + injected;
    myargv = malloc(myargc * sizeof(char *));
    assert(myargv != NULL);
    for (int i = 0; i < argc; i++)
        myargv[i] = M_StringDuplicate(argv[i]);
    for (int i = 0; i < injected; i++)
        myargv[argc + i] = M_StringDuplicate(injected_argv[i]);

    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }

    M_FindResponseFile();
    M_SetExeDir();

    D_DoomMain();
    return 0;
}
