/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop interface for the PlayStation 2 application runtime.
 */

#ifndef _MAINLOOP_H
#define _MAINLOOP_H

#include "types.h"

enum MainLoopSystemActionE
{
    MAINLOOP_SYSTEM_NONE = 0,
    MAINLOOP_SYSTEM_BROWSER,
    MAINLOOP_SYSTEM_BOOT_ELF,
    MAINLOOP_SYSTEM_POWEROFF
};

Bool MainLoopInit();
Bool MainLoopProcess();
void MainLoopShutdown();

Bool MainLoopRequestSystemAction(MainLoopSystemActionE eAction);
MainLoopSystemActionE MainLoopGetSystemAction();
const Char *MainLoopGetSystemBootPath();
void MainLoopPerformSystemAction();

#endif
