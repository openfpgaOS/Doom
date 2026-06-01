//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//      System-specific timer interface
//


#ifndef __I_TIMER__
#define __I_TIMER__

#include <stdint.h>

#define TICRATE 35

// Called by D_DoomLoop,
// returns current time in tics.
int I_GetTime (void);

// returns current time in ms
int I_GetTimeMS (void);

// returns current time in us
uint64_t I_GetTimeUS(void);

// returns the display-paced render sample time in us
uint64_t I_GetDisplayTimeUS(void);

// Override the display sample with a raw vblank timestamp captured by
// of_video_get_timing(). Pass 0 to return to measured/predicted timing.
void I_SetDisplayFrameSampleUS(uint64_t raw_us);

// Override the display sample with the current raw timer value.
void I_SetDisplayFrameSampleNow(void);

// Pause for a specified number of ms
void I_Sleep(int ms);

// Initialize timer
void I_InitTimer(void);

// Wait for vertical retrace or pause a bit.
void I_WaitVBL(int count);

// Optional display-locked tic clock helpers. Normal Doom gameplay uses the
// 35 Hz wall-clock timer; fixed LCD output only uses vblank as a render
// cadence and leaves these disabled.
void     I_SetPocketPacing(int enabled);
int      I_PocketPacingActive(void);
void     I_PocketTicAdvance(uint64_t now_us);
uint64_t I_PocketSmoothPeriodUS(void);

// Returns FRACUNIT-scaled fraction within the current tic when pocket
// pacing is on, or -1 when the caller should fall back to a wall-clock
// computation.
int  I_GetTimeFrac(void);

// Advance the optional display-locked counter by the number of display
// intervals that elapsed.
void I_PocketAdvanceFrameTics(unsigned int display_frames);
void I_PocketAdvanceFrameTic(void);

// Push the next-vblank period the hardware just latched (in µs) so
// I_GetDisplayTimeUS predicts using the new V_TOTAL immediately rather
// than waiting one frame for the measured average to catch up.
void I_SetPredictedVblankPeriodUS(uint64_t period_us);

#endif
