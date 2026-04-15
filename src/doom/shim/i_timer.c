/* i_timer.c — openfpgaOS timer: 1/35s tics, ms clock. */

#include "of.h"
#include "i_timer.h"
#include "doomtype.h"

static unsigned int basetime = 0;

int I_GetTime(void)
{
    unsigned int ticks = of_time_ms();
    if (basetime == 0) basetime = ticks;
    ticks -= basetime;
    return (int)((ticks * TICRATE) / 1000);
}

int I_GetTimeMS(void)
{
    unsigned int ticks = of_time_ms();
    if (basetime == 0) basetime = ticks;
    return (int)(ticks - basetime);
}

void I_Sleep(int ms)
{
    /* Busy-wait: kernel usleep is fine too but of_time_ms is the canonical clock. */
    unsigned int start = of_time_ms();
    while ((int)(of_time_ms() - start) < ms) { /* spin */ }
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    basetime = 0;
}
