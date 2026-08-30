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

struct SysInputT
{
	Uint16	uPad[EMUSYS_DEVICE_NUM];
};

} // namespace
#endif // _emuinput_h
