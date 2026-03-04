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

#ifdef __cplusplus
}
#endif

#endif
