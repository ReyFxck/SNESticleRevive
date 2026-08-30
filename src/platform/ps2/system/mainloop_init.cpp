/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop init behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"
#include "mainloop_ui.h"
#include "mainloop_menu.h"
#include "mainloop_state.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
#include "mainloop.h"

#include "types.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "prof.h"
#include "font.h"
#include "poly.h"
#include "embedded_irx.h"   /* Mx4sioLoadIfEnabled (carga adiada do mx4sio) */
#include "texture.h"
#include "audmixbuffer.h"
#include "pathext.h"
#include "snppucolor.h"
#include "emumovie.h"

#include <sifrpc.h>
#include <loadfile.h>

extern "C" {
#include "ps2ip.h"
#include "netplay_ee.h"
#include "mcsave_ee.h"
};

extern "C" {
#include "hw.h"
#include "gs.h"
#include "gpfifo.h"
#include "gpprim.h"
#include "gskit_backend.h"
};

extern "C" {
#include "audio.h"
};

#include "uiBrowser.h"
#include "uiCover.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"

/* BOOTLOG: route boot-phase diagnostics through DLog (defined in
   modules/sjpcm/sjpcm_rpc.c).  Plain printf on the EE never reaches
   PCSX2/NetherSX2's emulator log in this build, so DLog (which writes
   to the EE SIO TX FIFO) is the only practical way to see the boot
   progression from outside the running app. */
extern "C" void DLog(const char *fmt, ...);
#define BOOTLOG(...) DLog(__VA_ARGS__)
#define MENU_STARTDIR _MainLoop_MenuStartDir

/* MAINLOOP_NETPORT lives in mainloop_shared.h (included above). */

/* The chosen GS layout (PAL/NTSC width, FB and texture addresses).
   Three alternative blocks were left commented out in the original
   mainloop.cpp; they are preserved verbatim for reference. */
/*
#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 65
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0400
#define Z0      	0x0800
#define TEXADDR 	0x0B00
#define FONT_TEX  	0x2000
*/
/*
#define MAINLOOP_SCREENWIDTH  640
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 160
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0C00
#define Z0      	0x1800
#define TEXADDR 	0x2400
#define FONT_TEX 	0x3000
*/
/*
#define MAINLOOP_SCREENWIDTH  512
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 160
#define MAINLOOP_DISPY 17
*/

#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 65
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0C00
#define Z0      	0x1800

/* GS VRAM resources allocated after the mode-specific framebuffers.

   The old fixed layout put _OutTex at TBP 0x2400 (byte 0x240000).
   That was safe for the old 640x448/480i layout, whose two RGBA32 buffers
   ended at 0x230000, but NOT for any 640x480 mode: the second buffer ends
   at 0x258000. _OutTex therefore overwrote the last 96 KiB of every other
   640x480 framebuffer, producing repeating bands and alternate-frame
   flicker. Both supported interlaced modes use 480 source rows and are
   protected by this dynamic allocation.

   Keep every allocation 8 KiB aligned because _OutTex and the blender
   temporary surface are also used as FRAME targets (FRAME.FBP units).
   gsKit has already reserved the active mode's FB0/FB1 when this helper
   runs, so the same layout remains valid for 480i and 1080i. */
#define MAINLOOP_VRAM_FRAME_ALIGN   8192U
#define MAINLOOP_OUT_TEX_BYTES      (256U * 256U * 4U)
/* Highest blender address is base + 0x200 TBP (temporary 256px RGBA
   surface). Reserve through +0x280 TBP so the complete GS page span is
   private to the blender. */
#define MAINLOOP_BLEND_VRAM_BYTES   (0x280U * 256U)

static Uint32 s_FontTexTBP  = 0;
static Uint32 s_CoverTexTBP = 0;

static Uint32 _MainLoopAlignVramBytes(Uint32 bytes)
{
	return (bytes + MAINLOOP_VRAM_FRAME_ALIGN - 1U) &
	       ~(MAINLOOP_VRAM_FRAME_ALIGN - 1U);
}

static Bool _MainLoopAllocVideoVram(void)
{
	Uint32 outTBP;
	Uint32 fontTBP;
	Uint32 coverTBP;
	Uint32 blenderTBP;

	outTBP = GSK_VramAllocTBP(
		_MainLoopAlignVramBytes(MAINLOOP_OUT_TEX_BYTES));
	fontTBP = GSK_VramAllocTBP(
		_MainLoopAlignVramBytes(FontGetVramSize()));
	coverTBP = GSK_VramAllocTBP(
		_MainLoopAlignVramBytes(CoverGetVramSize()));
	blenderTBP = GSK_VramAllocTBP(
		_MainLoopAlignVramBytes(MAINLOOP_BLEND_VRAM_BYTES));

	/* Address zero is reserved for the first framebuffer, so every user
	   allocation must be non-zero. A zero here means gsKit rejected the
	   request because the 4 MiB GS VRAM budget was exhausted. */
	if (!outTBP || !fontTBP || !coverTBP || !blenderTBP)
	{
		_MainLoop_uOutTexTBP  = 0;
		_MainLoop_uBlenderTBP = 0;
		s_FontTexTBP = s_CoverTexTBP = 0;
		printf("[video] GS VRAM allocation failed\n");
		return FALSE;
	}

	_MainLoop_uOutTexTBP  = outTBP;
	_MainLoop_uBlenderTBP = blenderTBP;
	s_FontTexTBP          = fontTBP;
	s_CoverTexTBP         = coverTBP;

	printf("[video] VRAM TBP out=%04X font=%04X cover=%04X blend=%04X\n",
	       (unsigned)outTBP, (unsigned)fontTBP,
	       (unsigned)coverTBP, (unsigned)blenderTBP);
	return TRUE;
}

/* Browser starting directory. On real PS2 you typically want "mass:/"
   (USB stick) or a memcard path. On PCSX2/AetherSX2/NetherSX2 the
   "host:" device is not mapped, so an empty string here makes the
   browser screen list nothing - which looks like a "dead" menu even
   though everything is rendering correctly. "mass:/" is the safe
   default that works on real PS2 and on emulators with a USB image
   attached. Override at runtime if needed. */
static Char _MainLoop_MenuStartDir[] = "";

static int dispx, dispy;

static Uint8 _MainLoop_GfxPipe[0x40000] _ALIGN(128) __attribute__ ((section (".bss")));

static SNPPUColorCalibT _ColorCalib =
{
	0.9f,
	20.0f,
	0.2f
};

//static Char * _pSnesWavFileName = "host0:d:/snesps2.wav";

static Char *_pRomFile =
//"host:c:/emu/snesrom/mario.smc";
NULL
//"cdfs:\\USA\\SUPER~_U.SMC";
//"cdfs:\\ROMS\\Super Mario World.smc";

// "host:c:/emu/Zombies Ate My Neighbors (U) [!].smc";

// "host:c:/emu/Contra 3.smc";
//"host:c:/emu/Castlevania 4.smc";
//"host:c:/emu/Super Bomberman (U).smc";
//"host:c:/emu/Legend of Zelda, The (U).smc";
//"host:c:/emu/Final Fight (U).smc";

//"cdfs:\\ROMS\\mario.smc";
;

/* MainGetBootDir() / MainGetBootPath() are exported from the app
   entrypoint; forward-declared here exactly as in the original
   mainloop.cpp. */
char *MainGetBootDir();
char *MainGetBootPath();

Bool MainLoopInit()
{
    #if PROF_ENABLED
    /* Bound the log to 32K entries. The old 128K allocation used about
       2.5 MiB and could fail in packed builds on the 32 MiB PS2. */
    ProfInit(32 * 1024);
    #endif
	GS_InitGraph(GS_NTSC,GS_INTERLACE);
	dispx = MAINLOOP_DISPX;
	dispy = MAINLOOP_DISPY;
	GS_SetDispMode(dispx,dispy, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);
	GS_SetEnv(MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT,
	          FB0, FB1, GS_PSMCT32, Z0, GS_PSMZ16S);

	if (!_MainLoopAllocVideoVram())
		return FALSE;

	GPFifoInit((Uint128 *)_MainLoop_GfxPipe, sizeof(_MainLoop_GfxPipe));
	PolyInit();
	FontInit(s_FontTexTBP);
	CoverInit(s_CoverTexTBP);

	// setup log screen
	_MainLoop_pLogScreen = new CLogScreen();
	_MainLoop_pLogScreen->SetMsgFunc(_MainLoopLogEvent);
	_MainLoopSetScreen(_MainLoop_pLogScreen);
	_bMenu = TRUE;

	/* Boot banner: original SNESticlePS2 title + iaddis copyright
	   (replaces the #if 0 block above which depended on VersionGetInfo,
	   itself wrapped in #if 0 inside version.cpp), followed by the
	   ReyFxck fork credit. */
	/* BUILD_DATE/BUILD_TIME vem do Makefile (TZ Brasilia, p/ nao ficar 3h
	   adiantado como o __TIME__ em UTC).  APP_VERSION e' opt-in: so' e'
	   definido se o build passar APP_VERSION=...; sem ele, o banner nao
	   mostra numero de versao. */
#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif
#ifndef BUILD_TIME
#define BUILD_TIME __TIME__
#endif
#ifdef APP_VERSION
	ScrPrintf("SNESticle Revive v%s   %s  %s", APP_VERSION, BUILD_DATE, BUILD_TIME);
#else
	ScrPrintf("SNESticle Revive   %s  %s", BUILD_DATE, BUILD_TIME);
#endif
	ScrPrintf("Copyright (c) 1997-2004 Icer Addis");
	ScrPrintf("Forked By ReyFxck - Thomas R. (2026)");

	ScrPrintf("BootPath: %s", MainGetBootPath());
	ScrPrintf("BootDir: %s", MainGetBootDir());

	// set boot dir
	strcpy(_MainLoop_BootDir, MainGetBootDir());

	/* Keep the last mandatory embedded-module stack out of main()'s invisible
	   OPL hand-off. Pad initialization below depends on this sio2man, so load
	   sio2man/mcman/mcserv here after GS + the log screen are alive and before
	   _MainLoopLoadModules(). If a VMC/BIOS-specific module start stalls, the
	   real console now stops on this explicit marker instead of an inherited
	   black/white loader background. */
	BootMark("[IOP] memory card...");
	{
		int mcret = MemCardLoadEmbeddedIrx();
		BOOTLOG("[boot] MemCardLoadEmbeddedIrx: done (ret=%d)", mcret);
		(void)mcret;
	}
    _MainLoopLoadModules(_MainLoop_IOPModulePaths);

    /* Video settings live on the memory card, whose stack came up immediately
       before _MainLoopLoadModules. Load them now and apply: the display offset
       is live (no realloc); a non-default video mode needs a one-shot GS
       re-init + font re-upload, since the first GSK_Init already ran at
       480i before the card was available.  Default (480i) users take the
       else branch and the GS is left exactly as it was. */
    VideoSettingsLoad();
    /* MX4SIO (SD via SIO2) carrega AQUI, depois da config -- nunca no boot.
       So' se o suporte a Mass estiver ligado (padrao on).  Best-effort:
       em quem nao tem o adaptador, so' nao acha hardware. */
    Mx4sioLoadIfEnabled();
    /* state.cfg can live on USB/MX4SIO/MMCE as well as a memory card. Load it
       only after the configured removable-storage backend is available. */
    MainLoopStateSettingsLoad();
    if (g_GskVideoMode != GSK_GetActiveVideoMode())
    {
        GSK_ReinitVideo();
        if (!_MainLoopAllocVideoVram())
            return FALSE;
        FontInit(s_FontTexTBP);
        CoverInit(s_CoverTexTBP);
    }
    else
    {
        GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
    }
	/* The legacy VramInit() bumped a software VRAM watermark used by
	   the now-deleted VramAlloc helper. gsKit owns the GS-side VRAM
	   allocator (gskit_backend.c::GSK_VramAllocTBP) so there is
	   nothing left to initialise here. */
_AudMix = new AudMixBuffer(32000, TRUE);
	#if CODE_DEBUG
    printf("MainLoopInit\n");
	#endif

	/* Let the GS/PCRTC settle WITHOUT ever hanging.
	 *
	 * The original `while (loop--) WaitForNextVRstart(1);` spins on
	 * VRcount, which is ONLY advanced by the VBlank INTC handler
	 * installed in app/main.cpp (hw.s).  If that handler stops
	 * advancing the counter the loop never returns -- an infinite hang
	 * on the very first frame.
	 *
	 * That can happen under an external OPL GSM override: GSM takes over
	 * the vsync path to
	 * re-program the PCRTC every field, the app's VRcount stops
	 * incrementing, and the boot freezes on a black screen full of
	 * leftover-VRAM colour bars -- the symptom Adriano photographed.
	 * Without GSM the VBlank fires normally and the old loop completed,
	 * which is why the bug only showed up when an external mode was forced.
	 *
	 * Fix: use the non-blocking TestVRstart() and cap the wait with a
	 * usleep-based wall-clock ceiling, so a stalled / hijacked VBlank
	 * can never freeze the boot.  Same approach already used for the
	 * pad waits in input.cpp (which were moved off WaitForNextVRstart
	 * for this very reason).  The ScrPrintf doubles as a diagnostic: if
	 * "settle done" shows timed_out=1 on a real console, the VBlank was
	 * indeed stalled (confirms the GSM hang); timed_out=0 means it fired
	 * normally and any remaining GSM glitch is geometry, not a hang. */
	BootMark("[VID] settle...");   /* area de risco (hang GSM); marcador na hora */
	{
		int base  = TestVRstart();
		int guard = 1500;            /* 1500 * 1ms = 1.5s hard ceiling */
		while ((TestVRstart() - base) < 60 && guard-- > 0)
			usleep(1000);
	}
// create textures in main ram
    _fbTexture[0] = new CRenderSurface;
    _fbTexture[1] = new CRenderSurface;

    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[1]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[0]->Clear();
    _fbTexture[1]->Clear();
    TextureNew(&_OutTex, 256, 256, GS_PSMCT32);
    TextureSetAddr(&_OutTex, _MainLoop_uOutTexTBP);
TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
	PathExtAdd(MAINLOOP_ENTRYTYPE_GZ, (char *)"gz");
	PathExtAdd(MAINLOOP_ENTRYTYPE_ZIP, (char *)"zip");

	SNPPUColorCalibrate(&_ColorCalib);

	// create nes machine
	_pSnes = new SnesSystem();
	_pSnes->Reset();

	_pSnesRom = new SnesRom();
	for (Uint32 iExt=0; iExt < _pSnesRom->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_SNESROM, _pSnesRom->GetExtName(iExt));
	}

	PathExtAdd(MAINLOOP_ENTRYTYPE_SNESPALETTE, (char *)"snpal");

	/* Phase 2 of the NES integration: instantiate the NES core +
	   register .nes / .fds / disksys.rom with the existing browser.
	   PathExtAdd feeds the same CBrowserScreen that already lists
	   .smc / .sfc, so the user gets one unified ROM picker.
	   The NesSystem::ExecuteFrame() body is still a STUB - selecting
	   a .nes paints a diagnostic test pattern. Real InfoNES wiring
	   comes in Phase 3. */
	_pNes = new NesSystem();
	_pNes->Reset();

	_pNesRom = new NesRom();
	for (Uint32 iExt=0; iExt < _pNesRom->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESROM, _pNesRom->GetExtName(iExt));
	}

	_pNesFDSDisk = new NesDisk();
	for (Uint32 iExt=0; iExt < _pNesFDSDisk->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESFDSDISK, _pNesFDSDisk->GetExtName(iExt));
	}

	_pNesFDSBios = new NesFDSBios();
	for (Uint32 iExt=0; iExt < _pNesFDSBios->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESFDSBIOS, _pNesFDSBios->GetExtName(iExt));
	}

	s_pMovieClip = new Emu::MovieClip(_pSnes->GetStateSize(), 60 * 60 * 60);

	// init menu
	_MainLoop_pBrowserScreen = new CBrowserScreen(6000);
	_MainLoop_pBrowserScreen->SetMsgFunc(_MainLoopBrowserEvent);
	_MainLoop_pBrowserScreen->SetDir(MENU_STARTDIR);

	/* Separate, smaller browser for state-file maintenance.  Keeping it
	   independent means opening State Manager never destroys the user's
	   current ROM-browser directory or selection. */
	_MainLoop_pStateBrowserScreen = new CBrowserScreen(1024);
	_MainLoop_pStateBrowserScreen->SetMsgFunc(_MainLoopStateBrowserEvent);
	_MainLoop_pStateBrowserScreen->SetStateManager(TRUE);

	_MainLoop_pNetworkScreen = new CNetworkScreen();

	_MainLoop_pVideoScreen = new CVideoScreen();

	_MainLoop_pStateScreen = new CMenuScreen();
	_MainLoop_pStateScreen->SetMsgFunc(_MainLoopStateMenuEvent);
	_MainLoop_pStateScreen->SetTitle("Save States");
	_MainLoop_pStateScreen->SetTop(20);
	_MainLoop_pStateScreen->SetEntries(_MainLoopStateMenuEntries);
	_MainLoopStateMenuRefresh();

	/* Transient one-time destination chooser.  It is deliberately not
	   part of the L1/R1 screen ring; L2+X opens it only when no quick-save
	   target has been chosen yet. */
	_MainLoop_pStateDeviceScreen = new CMenuScreen();
	_MainLoop_pStateDeviceScreen->SetMsgFunc(_MainLoopStateDeviceMenuEvent);
	_MainLoop_pStateDeviceScreen->SetTitle("Save State Location");
	_MainLoop_pStateDeviceScreen->SetTop(20);

	_MainLoop_pMemCardFormatScreen = new CMenuScreen();
	_MainLoop_pMemCardFormatScreen->SetMsgFunc(
		_MainLoopMemCardFormatMenuEvent
	);
	_MainLoop_pMemCardFormatScreen->SetTitle("Memory Card");
	_MainLoop_pMemCardFormatScreen->SetTop(20);

	_MainLoop_pMenuScreen = new CMenuScreen();
	_MainLoop_pMenuScreen->SetMsgFunc(_MainLoopMenuEvent);
	_MainLoop_pMenuScreen->SetTitle("Install Menu");
	_MainLoop_pMenuScreen->SetEntries((char **)_MainLoopMenuEntries );

	_MainLoopSetScreen(_MainLoop_pBrowserScreen);
        // espera ~2s (ajuste se quiser)
	_bMenu = FALSE;

	// load snes palette
        _MainLoopLoadSnesPalette("mc0:/SNESticle/default.snpal");
	// load rom
        _MainLoopExecuteFile(_pRomFile, TRUE);
        _bMenu = _pSystem ? FALSE : TRUE;
        if (_MainLoop_bAudioReady)
        {
            /* Aud_Init already starts with a clean queue.  Stopping audsrv
               here used to leave it asleep because the old Aud_Play() was a
               no-op; whether menu/SNES sound returned then depended on which
               producer happened to enqueue first (commonly the NES core). */
            Aud_Play();
        }
	// BOOTLOG("[boot] MainLoopInit: leave (bMenu=%d, sjpcm=%d, mcsave=%d)\n",
	// 	(int)_bMenu, (int)_MainLoop_bAudioReady, (int)_MainLoop_bMCSaveReady);

/*
    if (!_WavFile.Open(_pSnesWavFileName, 32000, 16, 2))
    {
         printf("WavOut Open\n");
    }
  */

	InputPoll();

    return TRUE;
}
