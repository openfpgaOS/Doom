/* Doom-side refresh constants for the current openfpgaOS video API.
 *
 * Some bundled SDK copies still define the old half-scale V_TOTAL values
 * while current openfpgaOS expects 514..750. Keep this compatibility in Doom
 * so the app can build against either header set without editing src/sdk.
 */

#ifndef DOOM_VIDEO_REFRESH_H
#define DOOM_VIDEO_REFRESH_H

#include "of_video.h"

#if defined(OF_VIDEO_VTOTAL_MAX) && OF_VIDEO_VTOTAL_MAX < 500u
#define DOOM_VIDEO_VTOTAL_MIN      514u
#define DOOM_VIDEO_VTOTAL_MAX      750u
#define DOOM_VIDEO_VTOTAL_61_25HZ  514u
#define DOOM_VIDEO_VTOTAL_60HZ     525u
#define DOOM_VIDEO_VTOTAL_55HZ     573u
#define DOOM_VIDEO_VTOTAL_50HZ     630u
#define DOOM_VIDEO_VTOTAL_45HZ     700u
#define DOOM_VIDEO_VTOTAL_42HZ     750u
#else
#define DOOM_VIDEO_VTOTAL_MIN      OF_VIDEO_VTOTAL_MIN
#define DOOM_VIDEO_VTOTAL_MAX      OF_VIDEO_VTOTAL_MAX
#define DOOM_VIDEO_VTOTAL_61_25HZ  OF_VIDEO_VTOTAL_61_25HZ
#define DOOM_VIDEO_VTOTAL_60HZ     OF_VIDEO_VTOTAL_60HZ
#define DOOM_VIDEO_VTOTAL_55HZ     OF_VIDEO_VTOTAL_55HZ
#define DOOM_VIDEO_VTOTAL_50HZ     OF_VIDEO_VTOTAL_50HZ
#define DOOM_VIDEO_VTOTAL_45HZ     OF_VIDEO_VTOTAL_45HZ
#define DOOM_VIDEO_VTOTAL_42HZ     OF_VIDEO_VTOTAL_42HZ
#endif

#endif
