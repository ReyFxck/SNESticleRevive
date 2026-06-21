#pragma once

/* Shared declarations for the PS2 main loop translation units.
 *
 * Every global that lives in mainloop.cpp (or any mainloop_*.cpp that
 * other mainloop_*.cpp files reach for) is declared here ONCE, so the
 * sibling files can just `#include "mainloop_shared.h"` instead of
 * each one carrying its own bag of inline `extern` redeclarations.
 *
 * Definitions stay where they are today (mostly mainloop.cpp); this
 * header only changes how those symbols are *visible* to the rest of
 * the main-loop tree. No runtime behaviour change.
 */

#include "types.h"
#include "snes.h"
#include "snstate.h"
#include "snrom.h"
#include "nessystem.h"
#include "nesrom.h"
#include "nesstate.h"
#include "emusys.h"
#include "emumovie.h"
#include "rendersurface.h"
#include "texture.h"
#include "sjpcmbuffer.h"
#include "wavfile.h"
#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"
#include "uiVideo.h"
#include "uiScreen.h"


/* ---- Project-wide build-time configuration ------------------------ *
 *
 * These macros used to be re-#defined at the top of every
 * mainloop_*.cpp that consumed them, all with identical values.
 * Centralising them here means a single edit point if any of the
 * release/debug toggles ever needs to change.
 *
 * Behaviour-preserving consolidation; the values are exactly what
 * mainloop_install.cpp / mainloop_iop.cpp / mainloop_globals.cpp /
 * mainloop_state.cpp / mainloop_menu_runtime.cpp / mainloop_init.cpp /
 * mainloop_net.cpp / mainloop_exec.cpp had locally. */

#ifndef MAINLOOP_MEMCARD
/* Save SRAM to the PS2 memory card (mc0:/SNESticle/<rom>.srm).
   Was gated on CODE_RELEASE, which is never defined anywhere in the
   codebase -- so MAINLOOP_MEMCARD silently evaluated to 0 and the
   build used iaddis's old host0:/cygdrive/d/emu/ dev path that only
   exists on a PS2 hooked up to the iaddis Cygwin host. Force it on
   here so retail-style builds (cdrom0:, real PS2, NetherSX2, etc.)
   actually save to the memory card. */
#define MAINLOOP_MEMCARD 1
#endif

#ifndef MAINLOOP_NETPORT
#define MAINLOOP_NETPORT (6113)
#endif

#ifndef MAINLOOP_STATEPATH
#if MAINLOOP_MEMCARD
#define MAINLOOP_STATEPATH "host0:"
#else
#define MAINLOOP_STATEPATH "host0:/cygdrive/d/emu/"
#endif
#endif

#ifndef MAINLOOP_SNESSTATEDEBUG
#define MAINLOOP_SNESSTATEDEBUG (CODE_DEBUG && 0)
#endif

#ifndef MAINLOOP_NESSTATEDEBUG
#define MAINLOOP_NESSTATEDEBUG (CODE_DEBUG && FALSE)
#endif

#ifndef MAINLOOP_HISTORY
#define MAINLOOP_HISTORY (CODE_DEBUG && 0)
#endif

#ifndef MAINLOOP_MAXSRAMSIZE
#define MAINLOOP_MAXSRAMSIZE (64 * 1024)
#endif


/* ---- Strings / paths ---------------------------------------------- */

extern Char _RomName[256];
extern Char _SramPath[256];
extern Char _MainLoop_SaveTitle[];
extern Char _MainLoop_ModalStr[256];
extern Char _MainLoop_StatusStr[256];

/* ---- Emulator core handles ---------------------------------------- */

extern Emu::System    *_pSystem;
extern Emu::MovieClip *s_pMovieClip;
extern SnesSystem     *_pSnes;
extern SnesRom        *_pSnesRom;
extern SnesStateT      _SnesState;

/* NES integration (Phase 2). Defined in mainloop_globals.cpp.
   FDS disk-swap state lives here too even though the runtime path
   that consumes it (mainloop_input.cpp's R1/L1 disk swap) is still
   gated until Phase 5 -- we only need the variables themselves to
   exist so the linker is happy. */
extern NesSystem      *_pNes;
extern NesRom         *_pNesRom;
extern NesFDSBios     *_pNesFDSBios;
extern NesDisk        *_pNesFDSDisk;
extern NesStateT       _NesState;
extern Int32           _MainLoop_iDisk;
extern Bool            _MainLoop_bDiskInserted;

/* ---- ROM / framebuffer / audio buffers ---------------------------- */

extern Uint8           _RomData[4 * 1024 * 1024 + 1024];
extern CRenderSurface *_fbTexture[2];
extern TextureT        _OutTex;
extern SJPCMMixBuffer *_SJPCMMix;

/* TBP (256-byte units) of the SNES output texture (_OutTex) and the
   blender's per-frame scratchpad slab. Both are populated by
   MainLoopInit() through gsKit's VRAM allocator so the addresses are
   coordinated with the framebuffer pages gsKit reserved for FB0/FB1.
   _MainLoop_uOutTexTBP is shared between _OutTex (sampled by
   MainLoopRender) and the SNES blender's render-to-texture target,
   so they must come from the same allocation. Initialised to 0 if
   gsKit is not the active backend (non-PS2 builds), in which case
   any consumer falls back to the legacy hardcoded layout. */
extern Uint32 _MainLoop_uOutTexTBP;
extern Uint32 _MainLoop_uBlenderTBP;
#ifdef DEBUG
extern CWavFile _WavFile;
#endif

/* ---- UI screens ---------------------------------------------------- */

extern CBrowserScreen *_MainLoop_pBrowserScreen;
extern CNetworkScreen *_MainLoop_pNetworkScreen;
extern CMenuScreen    *_MainLoop_pMenuScreen;
extern CLogScreen     *_MainLoop_pLogScreen;
extern CVideoScreen   *_MainLoop_pVideoScreen;
extern CScreen        *_MainLoop_pScreen;

/* ---- Flags / counters --------------------------------------------- */

extern Bool    _bMenu;
extern Bool    _bStateSaved;
extern Bool    _MainLoop_BlackScreen;
extern Int32   _MainLoop_ModalCount;
extern Int32   _MainLoop_StatusCount;
extern Uint32  _MainLoop_uDebugDisplay;
extern Uint32  _uInputFrame;
extern Uint32  _uInputChecksum[5];

/* ---- SRAM / save bookkeeping -------------------------------------- */

extern Uint32  _MainLoop_SRAMChecksum;
extern Uint32  _MainLoop_SaveCounter;
extern Uint32  _MainLoop_AutoSaveTime;
extern Bool    _MainLoop_SRAMUpdated;
extern Float32 _MainLoop_fOutputIntensity;

/* ---- Function entrypoints across mainloop_*.cpp ------------------- */

void MainLoopRender();
void _MenuEnable(Bool bEnable);
/* Drawn from MainLoopRender() (mainloop_render.cpp), defined in
   mainloop_menu_runtime.cpp. Was a file-static helper inside
   mainloop.cpp; promoted to extern when MainLoopRender() and the
   menu-runtime were split into separate translation units. */
void _MenuDraw();


enum
{
	MAINLOOP_ENTRYTYPE_GZ          ,
	MAINLOOP_ENTRYTYPE_ZIP         ,
	MAINLOOP_ENTRYTYPE_NESROM      ,
	MAINLOOP_ENTRYTYPE_NESFDSDISK  ,
	MAINLOOP_ENTRYTYPE_NESFDSBIOS  ,
	MAINLOOP_ENTRYTYPE_SNESROM     ,
	MAINLOOP_ENTRYTYPE_SNESPALETTE ,

	MAINLOOP_ENTRYTYPE_NUM
};
