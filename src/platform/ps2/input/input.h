#ifndef _INPUT_H
#define _INPUT_H

#include "types.h"

#define INPUT_MAXPADS (5)

typedef enum InputDeviceE
{
    INPUT_DEVICE_NULL,
    INPUT_DEVICE_MOUSE,
    INPUT_DEVICE_KEYBOARD0,
    INPUT_DEVICE_KEYBOARD1,
    INPUT_DEVICE_KEYBOARD2,
    INPUT_DEVICE_KEYBOARD3,
    INPUT_DEVICE_JOYSTICK0,
    INPUT_DEVICE_JOYSTICK1,
    INPUT_DEVICE_JOYSTICK2,
    INPUT_DEVICE_JOYSTICK3,
    INPUT_DEVICE_NUM
} InputDeviceE;

#ifdef __cplusplus
#include "inputdevice.h"
extern "C" {
#endif

void   InputInit(Bool bXLib);
void   InputShutdown(void);
void   InputPoll(void);
Uint32 InputGetPadData(Uint32 uPad);
Bool   InputIsPadConnected(Uint32 uPad);

/* Returns digital d-pad bits (PAD_LEFT/RIGHT/UP/DOWN) synthesised from the
   pad's left analog stick deflection. Returns 0 when the stick is inside
   the dead zone, when the pad is disconnected, or when the controller is
   not running in dualshock mode. Callers OR these bits into the digital
   pad data so the analog stick can drive both menu navigation and the
   in-game SNES d-pad. */
Uint32 InputGetPadDpadFromAnalog(Uint32 uPad);

#ifdef __cplusplus
}
#endif

#endif
