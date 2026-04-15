/* SDL_gamecontroller.h shim — cdoom's i_joystick.h only uses one
 * symbol from this header (SDL_CONTROLLER_BUTTON_MAX), to base its
 * GAMEPAD_BUTTON_MAX enum on. The actual joystick code lives in
 * shim/i_joystick.c as stubs; openfpgaOS handles gamepad input via
 * of_input directly.
 *
 * The desktop app_pc build uses the real SDL2 headers; this file is
 * only picked up for the RISC-V build because -Ishim precedes -Icdoom.
 */

#ifndef SDL_gamecontroller_h_
#define SDL_gamecontroller_h_

#define SDL_CONTROLLER_BUTTON_MAX 21

#endif
