/* Link wrappers observe real startup without changing gameplay flags. */
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "doomstat.h"
#include "g_game.h"
#include "m_argv.h"

static int graphics_ready, level_ready, recording_ready;
void __real_I_InitGraphics(void);
void __real_G_InitNew(skill_t skill, int episode, int map);
void __real_G_BeginRecording(void);

void __wrap_I_InitGraphics(void)
{
    __real_I_InitGraphics();
    graphics_ready = 1;
}

void __wrap_G_InitNew(skill_t skill, int episode, int map)
{
    assert(graphics_ready);
    assert(skill == sk_hard && episode == 1 && map == 1);
    assert(nomonsters && fastparm && respawnparm);
    __real_G_InitNew(skill, episode, map);
    assert(gameskill == skill && gameepisode == episode && gamemap == map);
    assert(nomonsters && fastparm && respawnparm);
    level_ready = 1;
}

void __wrap_G_BeginRecording(void)
{
    assert(graphics_ready && level_ready);
    __real_G_BeginRecording();
    recording_ready = 1;
}

void __wrap_TryRunTics(void)
{
    assert(graphics_ready && level_ready);
    assert(recording_ready == !!M_CheckParm("-record"));
    puts("PASS startup: graphics, level textures, recording header, first tic; gameplay flags preserved");
    fflush(stdout);
    _exit(0);
}
