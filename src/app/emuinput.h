/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the emuinput interface for the emulator application layer.
 */

#ifndef _emuinput_h
#define _emuinput_h

#include <stdlib.h>

namespace Emu {

#define EMUSYS_DEVICE_NUM (5)
#define EMUSYS_DEVICE_DISCONNECTED 0xFFFF

/* Keep SysInputT binary-compatible with existing movies/save states.
   Standard SNES pads never use the low four bits, so uPad[4] values 1/2 are
   reserved as a special-peripheral tag. uPad[2]/uPad[3] carry the payload. */
#define EMUSYS_SNES_SPECIAL_MOUSE       0x0001
#define EMUSYS_SNES_SPECIAL_SUPERSCOPE  0x0002

#define EMUSYS_SNES_MOUSE_LEFT          0x0001
#define EMUSYS_SNES_MOUSE_RIGHT         0x0002

#define EMUSYS_SNES_SCOPE_FIRE          0x0001
#define EMUSYS_SNES_SCOPE_CURSOR        0x0002
#define EMUSYS_SNES_SCOPE_TURBO         0x0004
#define EMUSYS_SNES_SCOPE_PAUSE         0x0008

struct SysInputT
{
	Uint16	uPad[EMUSYS_DEVICE_NUM];
};

} // namespace
#endif // _emuinput_h
