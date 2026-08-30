/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop net interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"

extern "C" {
#include "netplay_ee.h"
}

extern char *_MainLoop_NetConfigPaths[];

Bool _MainLoopConfigureNetwork(char **ppSearchPaths, char *pConfigFileName);
Bool _MainLoopInitNetwork(Char **ppSearchPaths);
Bool _MainLoopWaitForNetwork(Int32 timeoutMs);
void *_MainLoopNetCallback(NetPlayCallbackE eCallback, char *data, int size);
int _MainLoopNetworkEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
