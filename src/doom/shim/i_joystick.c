/* i_joystick.c — openfpgaOS has no generic joystick layer (gamepad
 * is handled in i_input.c directly).  Stub out the interface. */

#include "doomtype.h"
#include "i_joystick.h"
#include "m_config.h"

int  use_analog;
int  usejoystick;
char *joystick_guid = "";
int   joystick_index = -1;
int   joystick_x_axis;
int   joystick_x_invert;
int   joystick_y_axis;
int   joystick_y_invert;
int   joystick_strafe_axis;
int   joystick_strafe_invert;
int   joystick_look_axis;
int   joystick_look_invert;
int   joystick_physical_buttons[10];
int   joystick_turn_sensitivity = 10;
int   joystick_move_sensitivity = 10;
int   joystick_look_sensitivity = 10;

void I_InitJoystick(void)     { }
void I_ShutdownJoystick(void) { }
void I_UpdateJoystick(void)   { }

void I_BindJoystickVariables(void)
{
    M_BindIntVariable("use_joystick",           &usejoystick);
    M_BindStringVariable("joystick_guid",       &joystick_guid);
    M_BindIntVariable("joystick_index",         &joystick_index);
    M_BindIntVariable("joystick_x_axis",        &joystick_x_axis);
    M_BindIntVariable("joystick_x_invert",      &joystick_x_invert);
    M_BindIntVariable("joystick_y_axis",        &joystick_y_axis);
    M_BindIntVariable("joystick_y_invert",      &joystick_y_invert);
    M_BindIntVariable("joystick_strafe_axis",   &joystick_strafe_axis);
    M_BindIntVariable("joystick_strafe_invert", &joystick_strafe_invert);
    M_BindIntVariable("joystick_look_axis",     &joystick_look_axis);
    M_BindIntVariable("joystick_look_invert",   &joystick_look_invert);
    M_BindIntVariable("joystick_turn_sensitivity", &joystick_turn_sensitivity);
    M_BindIntVariable("joystick_move_sensitivity", &joystick_move_sensitivity);
    M_BindIntVariable("joystick_look_sensitivity", &joystick_look_sensitivity);
}
