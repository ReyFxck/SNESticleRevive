#pragma once

#include "types.h"

int _MainLoopMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
int _MainLoopLogEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
extern const char *_MainLoopMenuEntries[];
extern char *_MainLoop_pInstallFiles[];
