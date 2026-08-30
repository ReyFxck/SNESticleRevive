/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop input interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"

Uint16 _MainLoopInput(Uint32 pad);
void _MainLoopInputProcess(Uint32 buttons);
void _MainLoopInputSuppressUntilRelease();
