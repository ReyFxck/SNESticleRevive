#pragma once

#include "types.h"
#include "snes.h"
#include "snstate.h"
#include "emusys.h"

extern Char _RomName[256];
extern Char _SramPath[256];

extern Emu::System *_pSystem;
extern SnesSystem *_pSnes;
extern SnesStateT _SnesState;

extern Uint32 _MainLoop_SRAMChecksum;
extern Uint32 _MainLoop_SaveCounter;
extern Uint32 _MainLoop_AutoSaveTime;
extern Bool _MainLoop_SRAMUpdated;
extern Bool _bStateSaved;

enum
{					   
	MAINLOOP_ENTRYTYPE_GZ	   ,
	MAINLOOP_ENTRYTYPE_ZIP	   ,
	MAINLOOP_ENTRYTYPE_NESROM  ,
	MAINLOOP_ENTRYTYPE_NESFDSDISK  ,
	MAINLOOP_ENTRYTYPE_NESFDSBIOS,
	MAINLOOP_ENTRYTYPE_SNESROM ,
	MAINLOOP_ENTRYTYPE_SNESPALETTE ,

	MAINLOOP_ENTRYTYPE_NUM 
};
