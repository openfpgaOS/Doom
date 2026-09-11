/* Run the production fence wait with scanout stopped for a long OSD visit. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct { uint32_t vblank_count; } of_video_timing_t;
static uint64_t now;
static uint32_t last_vblank;
static unsigned pumps;
static int mode;
static int of_gpu_fence_reached(uint32_t token)
{
    assert(token == 9);
    return mode == 0 || (mode == 1 && now >= 60000000u)
        || (mode == 3 && now >= 20000u);
}
static uint32_t of_time_us(void) { now += 100; return (uint32_t)now; }
static void I_PCM_Poll(void) { ++pumps; }
static void of_video_get_timing(of_video_timing_t *t)
{
    uint64_t active = mode == 1 ? (now >= 60000000u ? now - 60000000u : 0) : now;
    t->vblank_count = last_vblank + (uint32_t)(active / 16667u);
}
#include "r_gpu_wait.h"
int main(void)
{
    gpu_wait_present_fence(9);
    assert(now == 0 && pumps == 0);
    puts("PASS: a completed fence adds no timer or audio work");
    mode = 1;
    gpu_wait_present_fence(9);
    assert(now >= 60000000u && pumps >= 59000);
    puts("PASS: a minute in the system OSD keeps audio serviced and resumes without a trap");
    mode = 3; now = 0; pumps = 0; last_vblank = UINT32_MAX;
    gpu_wait_present_fence(9);
    assert(now >= 20000 && pumps > 0);
    puts("PASS: fence completion and vblank counter wrap are handled");
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
        mode = 2; now = 0; pumps = 0; last_vblank = 0;
        gpu_wait_present_fence(9);
        _exit(0);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    puts("PASS: a GPU hang with active scanout still triggers the watchdog");
}
