/* doom_loading_logo.h — openfpgaOS Heretic boot splash (not upstream).
 *
 * The shared FPGA entry (shim/i_main.c, non-PC path) blits a loading logo
 * before the game starts, using the Doom module's baked-in image. Heretic
 * has no such asset here, so provide a blank (black) splash of the same
 * dimensions. Zero-initialised: palette entry 0 is black and every pixel
 * indexes it, so the boot shows a brief black frame instead of the Doom
 * logo. Symbol names match the Doom header because the shim is game-agnostic
 * and references them directly. */
#ifndef OF_HERETIC_LOADING_LOGO_H
#define OF_HERETIC_LOADING_LOGO_H

#include <stdint.h>

static const int doom_loading_logo_w = 320;
static const int doom_loading_logo_h = 190;
static const uint32_t doom_loading_logo_palette[256] = {0};
static const uint8_t doom_loading_logo_pixels[320 * 190] = {0};

#endif /* OF_HERETIC_LOADING_LOGO_H */
