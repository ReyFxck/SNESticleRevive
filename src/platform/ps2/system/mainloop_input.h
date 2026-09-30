/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop input interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"
#include "emuinput.h"

Uint16 _MainLoopInput(Uint32 pad);
void _MainLoopInputProcess(Uint32 buttons);
void _MainLoopInputSuppressUntilRelease();

enum MainLoopSnesPeripheralModeE
{
	MAINLOOP_SNES_INPUT_STANDARD = 0,
	MAINLOOP_SNES_INPUT_MOUSE,
	MAINLOOP_SNES_INPUT_SUPERSCOPE,
	MAINLOOP_SNES_INPUT_JUSTIFIER,
	MAINLOOP_SNES_INPUT_NUM
};

void _MainLoopSnesInputCycleMode();
const char *_MainLoopSnesInputModeName();
void _MainLoopSnesInputApply(Emu::SysInputT *pInput);
