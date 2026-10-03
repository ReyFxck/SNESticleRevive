#include "pch.h"
#include "Shared/EmulatorLock.h"
#include "Shared/Emulator.h"
#ifndef PS2_PORT
#include "Shared/DebuggerRequest.h"
#include "Debugger/DebugBreakHelper.h"
#endif

#ifdef PS2_PORT
EmulatorLock::EmulatorLock(Emulator *emu, bool)
{
	_emu = emu;
	_emu->Lock();
}

EmulatorLock::~EmulatorLock()
{
	_emu->Unlock();
}
#else
EmulatorLock::EmulatorLock(Emulator *emu, bool allowDebuggerLock)
{
	_emu = emu;

	if(_emu->_runLock.IsLockedByCurrentThread()) {
		_emu->Lock();
	} else {
		if(allowDebuggerLock) {
			_debugger.reset(new DebuggerRequest(emu->GetDebugger(false)));
			if(_debugger->GetDebugger()) {
				_breakHelper.reset(new DebugBreakHelper(_debugger->GetDebugger(), true));
			} else {
				_debugger.reset();
				_emu->Lock();
			}
		} else {
			_emu->Lock();
		}
	}
}

EmulatorLock::~EmulatorLock()
{
	if(_debugger) {
		_breakHelper.reset();
	} else {
		_emu->Unlock();
	}
}

#endif
