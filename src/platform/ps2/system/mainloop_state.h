/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop state interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"

enum MainLoopStateDeviceE
{
	MAINLOOP_STATEDEVICE_AUTO,
	MAINLOOP_STATEDEVICE_USB,
	MAINLOOP_STATEDEVICE_MEMCARD,
	MAINLOOP_STATEDEVICE_MMCE,
	MAINLOOP_STATEDEVICE_HDD,

	MAINLOOP_STATEDEVICE_NUM
};

void PathTruncFileName(Char *pOut, Char *pStr, Int32 nMaxChars);
int PathGetMaxFileNameLength(const char *pPath);

Bool _MainLoopHasSRAM();
Bool _MainLoopSaveSRAM(Bool bSync);
void _MainLoopLoadSRAM();
Bool _MainLoopCheckSRAM();
Bool _MainLoopForceCheckSRAM();
Bool _MainLoopLoadState();
Bool _MainLoopSaveState();

void MainLoopStateSettingsLoad();
Bool MainLoopStateSettingsSave();
void MainLoopStateOnRomChanged();
Bool MainLoopStateHasDeviceChoice();
void MainLoopStateForgetDeviceChoice();
void MainLoopStateSetDevice(MainLoopStateDeviceE eDevice);
Bool MainLoopStateDeviceAvailable(MainLoopStateDeviceE eDevice);
void MainLoopStateCycleSlot();
void MainLoopStateCycleDevice();
Int32 MainLoopStateGetSlot();
MainLoopStateDeviceE MainLoopStateGetDevice();
const Char *MainLoopStateGetDeviceName();
const Char *MainLoopStateGetAvailability();
const Char *MainLoopStateGetLastMessage();
Int32 MainLoopStateGetUnformattedCard();

#if MAINLOOP_HISTORY
void _MainLoopResetHistory();
#endif
void _MainLoopResetInputChecksums();
#if MAINLOOP_HISTORY
void _MainLoopSaveHistory();
#endif
