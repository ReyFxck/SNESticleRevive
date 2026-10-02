#pragma once
#include "pch.h"

class Emulator;
class DebuggerRequest;
class DebugBreakHelper;

class EmulatorLock
{
private:
	Emulator* _emu = nullptr;
#ifndef PS2_PORT
	unique_ptr<DebuggerRequest> _debugger;
	unique_ptr<DebugBreakHelper> _breakHelper;
#endif

public:
	EmulatorLock(Emulator* emulator, bool allowDebuggerLock);
	~EmulatorLock();
};