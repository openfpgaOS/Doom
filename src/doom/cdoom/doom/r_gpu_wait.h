/* A system menu may pause scanout with CMD_FLIP still in the GPU queue.
 * Do not acquire another buffer until that command's fence has retired.
 * Keep audio serviced while waiting; count the watchdog in active vblanks
 * so time spent in a platform menu cannot turn into a fatal GPU timeout.
 * Included only by the renderer, which owns the SDK's GPU command state. */
#ifndef DOOM_R_GPU_WAIT_H
#define DOOM_R_GPU_WAIT_H

static void gpu_wait_present_fence(uint32_t token)
{
    of_video_timing_t timing;
    uint32_t last_vblank, active_vblanks = 0;
    uint32_t last_poll, spins = 0;

    if (of_gpu_fence_reached(token))
        return;

    of_video_get_timing(&timing);
    last_vblank = timing.vblank_count;
    last_poll = of_time_us();
    while (!of_gpu_fence_reached(token))
    {
        if (++spins < 1024u)
            continue;
        spins = 0;
        uint32_t now = of_time_us();
        if ((uint32_t)(now - last_poll) < 1000u)
            continue;
        last_poll = now;
        I_PCM_Poll();
        of_video_get_timing(&timing);
        active_vblanks += timing.vblank_count - last_vblank;
        last_vblank = timing.vblank_count;
        if (active_vblanks >= 300u && !of_gpu_fence_reached(token))
            __builtin_trap();
    }
}

#endif
