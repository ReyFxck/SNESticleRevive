/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop menu interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"

enum MainLoopMemCardFormatActionE
{
	MAINLOOP_MEMCARDFORMAT_STATE_SAVE,
	MAINLOOP_MEMCARDFORMAT_SRAM_SAVE,
	MAINLOOP_MEMCARDFORMAT_BROWSE
};

int _MainLoopMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
int _MainLoopStateMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
int _MainLoopStateBrowserEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
int _MainLoopStateDeviceMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
int _MainLoopMemCardFormatMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
void _MainLoopStateMenuRefresh();
void _MainLoopStateDevicePromptOpen();
void _MainLoopStateDevicePromptCancel();
void _MainLoopMemCardFormatPromptOpen(
	Int32 iPort,
	MainLoopMemCardFormatActionE eAction
);
void _MainLoopMemCardFormatPromptCancel();
int _MainLoopLogEvent(Uint32 Type, Uint32 Parm1, void *Parm2);
extern const char *_MainLoopMenuEntries[];
extern char *_MainLoopStateMenuEntries[];
extern char *_MainLoop_pInstallFiles[];
