/* mainloop_globals.cpp
 *
 * Single home for the *definitions* of every cross-file symbol that
 * the rest of the mainloop_*.cpp tree reaches for through
 * mainloop_shared.h.
 *
 * No logic lives here -- only object definitions. Anything with
 * file-static linkage stays inside the .cpp that uses it (e.g.
 * mainloop_init.cpp, mainloop_process.cpp), and #if 0 dead code from
 * the original mainloop.cpp stays in mainloop.cpp.
 *
 * Extracted from mainloop.cpp during the Batch 3 split. No values,
 * initialisers, or attribute lists changed.
 */

#include "types.h"
#include "mainloop_shared.h"

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
#include "uiScreen.h"


/* MAINLOOP_MEMCARD lives in mainloop_shared.h (included above) and
   gates the memcard variants of _SramPath / _MainLoop_SaveTitle below. */


/* ---- UI screens --------------------------------------------------- */

CBrowserScreen *_MainLoop_pBrowserScreen;
CNetworkScreen *_MainLoop_pNetworkScreen;
CMenuScreen    *_MainLoop_pMenuScreen;
CLogScreen     *_MainLoop_pLogScreen;
CVideoScreen   *_MainLoop_pVideoScreen;
CScreen        *_MainLoop_pScreen = NULL;


/* ---- Emulator core handles ---------------------------------------- */

SnesSystem *_pSnes;
SnesRom    *_pSnesRom;

/* Phase 2 of the NES integration (feat/nes-infones).  The iaddis-era
   #if 0 around these declarations is now flipped.  They are no longer
   file-static because mainloop_init.cpp and mainloop_load.cpp need to
   reach them through the extern declarations in mainloop_shared.h.
   Disk-swap state stays un-#if'd here but is not yet driven from
   input -- that part of mainloop_input.cpp is still gated for
   Phase 5 (FDS support). */
NesSystem   *_pNes;
NesRom      *_pNesRom;
NesFDSBios  *_pNesFDSBios;
NesDisk     *_pNesFDSDisk;
Int32        _MainLoop_iDisk          = 0;
Bool         _MainLoop_bDiskInserted  = FALSE;

Char _RomName[256];

#if MAINLOOP_MEMCARD
Char _SramPath[256] = "mc0:/SNESticle";
Char _MainLoop_SaveTitle[] = "SNESticle Revive";
#else
Char _SramPath[256] = "host0:/cygdrive/d/emu/";
#endif

Emu::System *_pSystem;


/* ---- ROM / framebuffer / audio buffers ---------------------------- */

CRenderSurface *_fbTexture[2];

TextureT _OutTex;
Uint32 _MainLoop_uOutTexTBP  = 0;
Uint32 _MainLoop_uBlenderTBP = 0;
#ifdef DEBUG
CWavFile _WavFile;
#endif

/* 8MB+1KB: cobre LoROM/HiROM ate 4MB E ExLoROM (Jumbo) ate 8MB/64Mbit,
   usado por hacks grandes de SMW expandidas pelo Lunar Magic. Cabe folgado
   nos 32MB da PS2. */
Uint8 _RomData[8 * 1024 * 1024 + 1024] __attribute__((aligned(64))) __attribute__ ((section (".bss")));

SnesStateT		_SnesState;
NesStateT		_NesState;

Emu::MovieClip *s_pMovieClip;


/* ---- SRAM / save bookkeeping -------------------------------------- */

Uint32 _MainLoop_SRAMChecksum;
Uint32 _MainLoop_SaveCounter   = 0;
Uint32 _MainLoop_AutoSaveTime  = 8 * 60;
Bool   _MainLoop_SRAMUpdated   = FALSE;
Bool   _bStateSaved            = FALSE;
Float32 _MainLoop_fOutputIntensity = 0.8f;

SJPCMMixBuffer *_SJPCMMix;


/* ---- Flags / counters --------------------------------------------- */

Bool _bMenu = FALSE;

Char  _MainLoop_ModalStr[256];
Int32 _MainLoop_ModalCount = 0;

Char  _MainLoop_StatusStr[256];
Int32 _MainLoop_StatusCount = 0;

Bool   _MainLoop_BlackScreen   = FALSE;
Uint32 _MainLoop_uDebugDisplay = 0;

Uint32 _uInputFrame;
Uint32 _uInputChecksum[5];
