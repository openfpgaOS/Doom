/* i_main.c — openfpgaOS entry: boot SDK, then call D_DoomMain. */

#include "config.h"
#include "of.h"
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void D_DoomMain(void);

int main(int argc, char **argv)
{
    /* Boot the SDK (opens the 320x240 window on PC, or inits FB on target). */
    of_video_init();
    of_audio_init();
    of_mixer_init(32, OF_MIXER_OUTPUT_RATE);

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
