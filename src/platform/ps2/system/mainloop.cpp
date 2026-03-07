
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MENU_STARTDIR _MainLoop_MenuStartDir
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <iopheap.h>
#include <libpad.h>
#include "libxpad.h"
#include "libxmtap.h"
#include <libmc.h>
#include <kernel.h>
#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"
#include "mainloop_ui.h"
#include "mainloop_install.h"
#include "mainloop_menu.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
#include "mainloop_input.h"
#include "mainloop_exec.h"
#include "types.h"
#include "vram.h"
#include "mainloop.h"
#include "console.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "file.h"
#include "dataio.h"
#include "prof.h"
#include "bmpfile.h"
#if 0
#include "font.h"
#else
#include "../../../Gep/Include/ps2/font.h"
#endif
#include "poly.h"
#include "texture.h"
#include "mixbuffer.h"
#include "wavfile.h"
#include "snstate.h"
#include "sjpcmbuffer.h"
#include "memcard.h"
#include "pathext.h"
#include "snppucolor.h"
#if 0
#include "version.h"
#endif
#include "emumovie.h"
extern "C" {
#include "cd.h"
#include "ps2dma.h"
#include "sncpu_c.h"
#include "snspc_c.h"
};

//#include "nespal.h"
#include "snes.h"
//#include "nesstate.h"

#include <sifrpc.h>
#include <loadfile.h>

extern "C" {
#include "ps2ip.h"
#include "netplay_ee.h"
#include "mcsave_ee.h"
};

#include "zlib.h"
extern "C" {
#include "hw.h"
#include "gs.h"
#include "gpfifo.h"
#include "gpprim.h"
};

extern "C" {
#include "titleman.h"
};

extern "C" {
#include "sjpcm.h"
#include "cdvd_rpc.h"
};

extern "C" Int32 SNCPUExecute_ASM(SNCpuT *pCpu);


#define MAINLOOP_MEMCARD (CODE_RELEASE || 0)

#define MAINLOOP_NETPORT (6113)


#if CODE_RELEASE
#else
#endif


#if CODE_RELEASE
#define MAINLOOP_STATEPATH "host0:"
#else
#define MAINLOOP_STATEPATH "host0:/cygdrive/d/emu/"
#endif

#define MAINLOOP_SNESSTATEDEBUG (CODE_DEBUG && 0)
#define MAINLOOP_NESSTATEDEBUG (CODE_DEBUG && FALSE)
#define MAINLOOP_HISTORY (CODE_DEBUG && 0)
#define MAINLOOP_MAXSRAMSIZE (64 * 1024)

#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"
#include "emurom.h"

CBrowserScreen *_MainLoop_pBrowserScreen;
CNetworkScreen *_MainLoop_pNetworkScreen;
CMenuScreen *_MainLoop_pMenuScreen;
CLogScreen *_MainLoop_pLogScreen;
CScreen *_MainLoop_pScreen = NULL;

SnesSystem *_pSnes;
SnesRom *_pSnesRom;
#if 0
static NesSystem  *_pNes;
static NesRom	  *_pNesRom;
static NesFDSBios  *_pNesFDSBios;
static NesDisk	  *_pNesFDSDisk;
static Int32 _MainLoop_iDisk=0;
static Bool _MainLoop_bDiskInserted=FALSE;
#endif
Char _RomName[256];
static Char _MainLoop_MenuStartDir[] = "";

#if MAINLOOP_MEMCARD
Char _SramPath[256] = "mc0:/SNESticle";
Char _MainLoop_SaveTitle[] = "SNESticle\nSNESticle";
#else
Char _SramPath[256] = "host0:/cygdrive/d/emu/";
#endif

Emu::System  *_pSystem;

CRenderSurface *_fbTexture[2];
static Uint32 _iframetex=0;

TextureT _OutTex;
#ifdef DEBUG
CWavFile _WavFile;
#endif

Uint8 _RomData[4 * 1024 * 1024 + 1024] __attribute__((aligned(64))) __attribute__ ((section (".bss")));

SnesStateT		_SnesState;
#if 0
static NesStateT		_NesState;
#endif

Emu::MovieClip *s_pMovieClip;


Uint32 _MainLoop_SRAMChecksum;
Uint32 _MainLoop_SaveCounter = 0;
Uint32 _MainLoop_AutoSaveTime = 8 * 60;
Bool _MainLoop_SRAMUpdated = FALSE;
Bool _bStateSaved = FALSE;
Float32 _MainLoop_fOutputIntensity = 0.8f;


static Uint8 _MainLoop_GfxPipe[0x40000] _ALIGN(128) __attribute__ ((section (".bss")));

//static SJPCMMixBuffer _SJPCMMix(32000, TRUE) _ALIGN(16);
SJPCMMixBuffer *_SJPCMMix;

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


void _MenuEnable(Bool bEnable);
void *_MainLoopNetCallback(NetPlayCallbackE eCallback, char *data, int size);
static void _MenuDraw();
void MainLoopRender();

#if MAINLOOP_HISTORY
#endif
Bool _bMenu = FALSE;

Char _MainLoop_ModalStr[256];
Int32 _MainLoop_ModalCount=0;

Char _MainLoop_StatusStr[256];
Int32 _MainLoop_StatusCount=0;

Bool _MainLoop_BlackScreen = FALSE;
Uint32 _MainLoop_uDebugDisplay = 0;

extern "C" {
#include "unzip.h"
};

#if 0
static void _MainLoopSetPalette(NesPalE eNesPal)
{
	Color32T BasePal[64];
	Int32 iPal;

	memcpy(BasePal, NesPalGetStockPalette(eNesPal), sizeof(BasePal));
//		NesPalGenerate(BasePal, 334.0f, 0.4f);

	for (iPal=0; iPal < NESPAL_NUMPALETTES; iPal++)
	{
		Color32T Palette[64];

		// get palette
		NesPalComposePalette(iPal, Palette, BasePal, 64);

		// set palette for surface
		_fbTexture[0]->SetPaletteEntries(iPal, Palette, 64);
		_fbTexture[1]->SetPaletteEntries(iPal, Palette, 64);
	}
}
#endif

/* UI_L1R1_CYCLE */
/* UI_CYCLE_L1R1 */
//"mc0:/BADATA-SYSTEM"
/*
void InstallSNESticle()
{
	InstallFiles("mc0:/BADATA-SYSTEM", "host0:", _MainLoop_pInstallFiles, NULL);
}

void InstallLoader()
{
	InstallFiles("mc0:/BADATA-SYSTEM", "host0:", _MainLoop_pLoaderFiles, NULL);
}
*/


char *MainGetBootDir();
char *MainGetBootPath();


int dispx, dispy;

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
#define TEXADDR 	0x2400
#define FONT_TEX 	0x3000

#if 0
static SNPPUColorCalibT _ColorCalib =
{
	0.9f,
	15.0f,
	0.2f
};
#else
static SNPPUColorCalibT _ColorCalib =
{
	0.9f,
	20.0f,
	0.2f
};
#endif

Bool MainLoopInit()
{
//    assert(0);
    #if PROF_ENABLED
    ProfInit(128 * 1024);
    #endif

	// initialize GS
	GS_InitGraph(GS_NTSC,GS_NONINTERLACE);
	dispx = MAINLOOP_DISPX;
	dispy = MAINLOOP_DISPY;
	GS_SetDispMode(dispx,dispy, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);
	GS_SetEnv(MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT, FB0, FB1, GS_PSMCT32, Z0, GS_PSMZ16S);


	GPFifoInit((Uint128 *)_MainLoop_GfxPipe, sizeof(_MainLoop_GfxPipe));
    PolyInit();
    FontInit(FONT_TEX);

	// setup log screen
	_MainLoop_pLogScreen = new CLogScreen();
	_MainLoop_pLogScreen->SetMsgFunc(_MainLoopLogEvent);
	_MainLoopSetScreen(_MainLoop_pLogScreen);
	_bMenu = TRUE;
#if 0
	const VersionInfoT *pVersionInfo = VersionGetInfo();

	ScrPrintf("%s v%d.%d.%d %s %s %s", 
		pVersionInfo->ApplicationName, 
		pVersionInfo->Version[0],
		pVersionInfo->Version[1],
		pVersionInfo->Version[2],
		pVersionInfo->BuildType,
		pVersionInfo->BuildDate, 
		pVersionInfo->BuildTime);
	ScrPrintf("%s",  pVersionInfo->CopyRight);
#endif
	ScrPrintf("BootPath: %s", MainGetBootPath());
	ScrPrintf("BootDir: %s", MainGetBootDir());

	// set boot dir
	strcpy(_MainLoop_BootDir, MainGetBootDir());

    _MainLoopLoadModules(_MainLoop_IOPModulePaths);

	VramInit();

	_SJPCMMix = new SJPCMMixBuffer(32000, TRUE);

	#if CODE_DEBUG
    printf("MainLoopInit\n");
	#endif

	int loop=60 * 2;
	while (loop--)
		WaitForNextVRstart(1);

	// allocate textures
/*	TextureNew(&_frametex[0], 256, 256, TEX_FORMAT_RGB565);
	TextureNew(&_frametex[1], 256, 256, TEX_FORMAT_RGB565);
	_fbTexture[0]->Set((Uint8 *)TextureGetData(&_frametex[0]), _frametex[0].uWidth, _frametex[0].uHeight, _frametex[0].uPitch, PixelFormatGetByEnum(PIXELFORMAT_BGR565));
	_fbTexture[1].Set((Uint8 *)TextureGetData(&_frametex[1]), _frametex[1].uWidth, _frametex[1].uHeight, _frametex[1].uPitch, PixelFormatGetByEnum(PIXELFORMAT_BGR565));
  */
//    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGB555));
//    _fbTexture[1].Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGB555));

    // create textures in main ram
    _fbTexture[0] = new CRenderSurface;
    _fbTexture[1] = new CRenderSurface;

    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[1]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[0]->Clear();
    _fbTexture[1]->Clear();
//    printf("%08X\n", (Uint32)_fbTexture[0]->GetLinePtr(0));
//    printf("%08X\n", _fbTexture[1].GetLinePtr(0));

//    TextureNew(&_OutTex, 256, 256, GS_PSMCT16);
    // create texture in vram
    TextureNew(&_OutTex, 256, 256, GS_PSMCT32);
    TextureSetAddr(&_OutTex, TEXADDR );

    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
#if 0
	_MainLoopSetPalette(NESPAL_FCEU);
#endif
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
#if 0
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
#endif

	s_pMovieClip = new Emu::MovieClip(_pSnes->GetStateSize(), 60 * 60 * 60);

	// init menu
	_MainLoop_pBrowserScreen = new CBrowserScreen(6000);
	_MainLoop_pBrowserScreen->SetMsgFunc(_MainLoopBrowserEvent);
	_MainLoop_pBrowserScreen->SetDir(MENU_STARTDIR);

	_MainLoop_pNetworkScreen = new CNetworkScreen();
	_MainLoop_pNetworkScreen->SetMsgFunc(_MainLoopNetworkEvent);
	_MainLoop_pNetworkScreen->SetPort(MAINLOOP_NETPORT);

	_MainLoop_pMenuScreen = new CMenuScreen();
	_MainLoop_pMenuScreen->SetMsgFunc(_MainLoopMenuEvent);
	_MainLoop_pMenuScreen->SetTitle("Install Menu");
	_MainLoop_pMenuScreen->SetEntries((char **)_MainLoopMenuEntries );


	_MainLoopSetScreen(_MainLoop_pBrowserScreen);
        // espera ~2s (ajuste se quiser)
	_bMenu = FALSE;

//	while (1);

	// load snes palette
        _MainLoopLoadSnesPalette("mc0:/SNESticle/default.snpal");
	// load rom
	_MainLoopExecuteFile(_pRomFile, TRUE);
        _bMenu = _pSystem ? FALSE : TRUE;
        SjPCM_Clearbuff();
        SjPCM_Play();

/*
    if (!_WavFile.Open(_pSnesWavFileName, 32000, 16, 2))
    {
         printf("WavOut Open\n");
    }
  */

	InputPoll();

#if 0
	while (1)
	{
		MainLoopRender();
		_MainLoop_pBrowserScreen->SetDir("cdfs:/ROMS/SNES");
		MainLoopRender();
		_MainLoop_pBrowserScreen->SetDir("cdfs:/ROMS/SNES/USA");
	}
#endif

    return TRUE;
}


#if 0
static Uint16 _MainLoopNesInput(Uint32 cond)
{
	Uint8 pad = 0;

	if ((cond & PAD_LEFT) ) pad|= (1<<NESIO_BIT_LEFT);
	if ((cond & PAD_RIGHT)) pad|= (1<<NESIO_BIT_RIGHT);
	if ((cond & PAD_UP)   ) pad|= (1<<NESIO_BIT_UP);
	if ((cond & PAD_DOWN) ) pad|= (1<<NESIO_BIT_DOWN);
	if ((cond & PAD_SQUARE)    ) pad|= (1<<NESIO_BIT_B);
	if ((cond & PAD_CROSS)    ) pad|= (1<<NESIO_BIT_A);

	if ((cond & PAD_TRIANGLE)    ) pad|= (1<<NESIO_BIT_B);
	if ((cond & PAD_CIRCLE)    ) pad|= (1<<NESIO_BIT_A);

	if ((cond & PAD_SELECT)    ) pad|= (1<<NESIO_BIT_SELECT);
	if ((cond & PAD_START)) pad|= (1<<NESIO_BIT_START);
	return pad;
}
#endif
#if MAINLOOP_NESSTATEDEBUG
static NesStateT _NesTestState[3];
#endif

//extern Bool bStateDebug;

#if MAINLOOP_HISTORY
#endif

static Uint32 _uVblankCycle;
Uint32 _uInputFrame;
Uint32 _uInputChecksum[5];

#if 1
extern "C" {
//#include "ncpu_c.h"
};
#if 0
static Bool _ExecuteNes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, EmuSysInputT *pInput, EmuSysModeE eMode)
{

#if !MAINLOOP_NESSTATEDEBUG
    N6502SetExecuteFunc(NCPUExecute_C);

	PROF_ENTER("NesExecuteFrame");
  	_pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer, eMode);
	PROF_LEAVE("NesExecuteFrame");
    #else

	_pNes->SaveState(&_NesTestState[0]);
	_pNes->ExecuteFrame(pInput, pSurface, NULL);
	_pNes->SaveState(&_NesTestState[1]);

	_pNes->RestoreState(&_NesTestState[0]);
	_pNes->ExecuteFrame(pInput, pSurface, NULL);
	_pNes->SaveState(&_NesTestState[2]);

	if (memcmp(&_NesTestState[1], &_NesTestState[2],sizeof(NesStateT)))
	{
		printf("State fault\n");
	}

    #endif
    return TRUE;
}
#endif
#endif



#include "snppublend_gs.h"



#include "common/debug/dbgterm.h"

void MainLoopRender()
{
    static int __dbg_render_calls = 0;
    __dbg_render_calls++;
    if (__dbg_render_calls <= 20)
    {
        DbgLog("MainLoopRender #%d bMenu=%d black=%d screen=0x%08x system=0x%08x",
               __dbg_render_calls,
               (int)_bMenu,
               (int)_MainLoop_BlackScreen,
               (unsigned int)_MainLoop_pScreen,
               (unsigned int)_pSystem);
    }

	static Uint32 _iFrame=0;

    // render frame
    GPPrimDisableZBuf();

	if (!_MainLoop_BlackScreen)
	{
//		Float32 fDestColor = (_bMenu || _MainLoop_ModalCount) ? 0.10f : 0.80f;
		Float32 fDestColor = 0.10f;
		
		if  (!_bMenu && !_MainLoop_ModalCount)
		{
			fDestColor = _MainLoop_fOutputIntensity;
		}

		static Float32 fColor=0.0f;
		Float32 dx = 0.0f;
		Float32 dy = 8.0f;

		if (fColor < fDestColor) 
		{
			fColor+=0.06f;
			if (fColor > fDestColor) 
			{
				fColor = fDestColor;
			}
		} 

		if (fColor > fDestColor) 
		{
			fColor-=0.06f;
			if (fColor < fDestColor) 
			{
				fColor = fDestColor;
			}
		}


        PolyBlend(FALSE);
        PolyTexture(&_OutTex);
//        PolyUV(0,0,256,240);
        PolyUV(0,0,256,240);
		PolyColor4f(fColor, fColor, fColor, 1.0f);
//		PolyColor4f(0.50f, 0.50f, 0.50f, 1.0f);


        PolyRect(dx,dy,MAINLOOP_SCREENWIDTH,MAINLOOP_SCREENHEIGHT);

        PolyBlend(TRUE);
        //PolyTexture(NULL);
        //PolyRect(dx,dy,128,120);
    }


    if (!_bMenu)
    {	
	
		if (s_pMovieClip->IsPlaying())
		{
	        FontSelect(2);
	        FontColor4f(0.5, 0.5f, 0.5f, 1.0f);
	        FontPrintf(240,220, ">");
		}

		if (s_pMovieClip->IsRecording())
		{
	        FontSelect(2);
	        FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
	        FontPrintf(240,220, "O");
		}


		switch (_MainLoop_uDebugDisplay)
        {
		case 0:
/*	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", InputGetPadData(0));
  */

//		        FontSelect(2);
//		        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
//		        FontPrintf(40,190, "%3d", xpadGetFrameCount(0,0));
			break;
		case 1:
		/*
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,190, "%3d %3d", NetInput.InputSize[0], NetInput.OutputSize[0]);
	        FontPrintf(40,200, "%3d %3d", NetInput.InputSize[1], NetInput.OutputSize[1]);
	        FontPrintf(40,210, "%3d %3d", NetInput.InputSize[2], NetInput.OutputSize[2]);
	        FontPrintf(40,220, "%3d %3d", NetInput.InputSize[3], NetInput.OutputSize[3]);
			*/
			break;
		case 2:
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", _uInputFrame);
	        FontPrintf(40,180, "%08X", _uInputChecksum[0]);
	        FontPrintf(40,190, "%08X", _uInputChecksum[1]);
	        FontPrintf(40,200, "%08X", _uInputChecksum[2]);
	        FontPrintf(40,210, "%08X", _uInputChecksum[3]);
	        FontPrintf(40,220, "%08X", _uInputChecksum[4]);
			break;
		case 3:
			FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
			FontPrintf(195, 210, "%8d", _uVblankCycle / 1024);
			break;
        }

        FontSelect(2);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		{

/*
		FontPrintf(15, 180, "%08X %08X Y", (Int32)(_ColorCalib.y_mul * 0x10000), (Int32)(_ColorCalib.y_add * 0x10000));
		FontPrintf(15, 190, "%08X %08X I", (Int32)(_ColorCalib.i_mul * 0x10000), (Int32)(_ColorCalib.i_add * 0x10000));
		FontPrintf(15, 200, "%08X %08X Q", (Int32)(_ColorCalib.q_mul * 0x10000), (Int32)(_ColorCalib.q_add * 0x10000));
  */

		/*
		FontPrintf(195, 180, "%6.3f %6.3f", _ColorCalib.y_mul, _ColorCalib.y_add);
		FontPrintf(195, 190, "%6.3f %6.3f", _ColorCalib.i_mul, _ColorCalib.i_add);
		FontPrintf(195, 200, "%6.3f %6.3f", _ColorCalib.q_mul, _ColorCalib.q_add);
		*/
		}

//			FontPrintf(195, 210, "%8d", _SJPCMMix.GetLastOutput());
    }


	if (_MainLoop_ModalCount > 0)
	{
		FontSelect(0);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		FontPrintf(128 - FontGetStrWidth(_MainLoop_ModalStr) / 2,100, _MainLoop_ModalStr);

		_MainLoop_ModalCount--;
	} else
	{

		if (_MainLoop_StatusCount > 0)
		{
			FontSelect(0);
			FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
			FontPrintf(20, 200, _MainLoop_StatusStr);

			_MainLoop_StatusCount--;
		} 


		if (_bMenu)
		{
			_MenuDraw();
		} 
	}


	#if CODE_DEBUG
	if (MCSave_WriteSync(FALSE, NULL))
	{
		FontSelect(1);
		FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
		if (_iFrame & 4)
			FontPrintf(235,216, "#");
	}
	#endif



//	FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
//	FontPuts(100, 100, _VersionStr);

//    PolyTexture(NULL);
//	PolyColor4f(0.0f, 1.0f, 0.0f, 1.0f);
//	PolyRect(0, 0, 100, 50);

    PROF_ENTER("GPFlush");
    GPFifoFlush();
    PROF_LEAVE("GPFlush");

    PROF_ENTER("WaitVBlank");

    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle();
	WaitForNextVRstart(1);
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle() - _uVblankCycle;

    PROF_LEAVE("WaitVBlank");

    PROF_ENTER("GSSetCrt");
    GS_SetCrtFB(0);
    GS_SetDrawFB(0);
    PROF_LEAVE("GSSetCrt");

    _iFrame++;
}


Bool MainLoopProcess()
{
    NetPlayRPCInputT NetInput;

    PROF_ENTER("Frame");

    PROF_ENTER("NetPlayRPCProcess");
    NetPlayRPCProcess();
    PROF_LEAVE("NetPlayRPCProcess");

    PROF_ENTER("InputProcess");
    InputPoll();

    PROF_LEAVE("InputProcess");


	_MainLoopInputProcess(InputGetPadData(0) | InputGetPadData(1) | InputGetPadData(2) | InputGetPadData(3));

//	_MainLoopInputProcess(InputGetPadData(0));
//	_MainLoopInputProcess(InputGetPadData(1));

    if (!_bMenu && _pSystem && !_MainLoop_BlackScreen)
    {
        CRenderSurface *pSurface;
        CMixBuffer *pMixBuffer = NULL;
        pSurface = _fbTexture[_iframetex];
	
		Emu::SysInputT Input;
	
		Int32 iPad;

        /*
        if (_WavFile.IsOpen())
        {
            pMixBuffer = &_WavFile;
        } else
        {
            pMixBuffer = &_SJPCMMix;
        } 
        */                        
        pMixBuffer = _SJPCMMix;
//                pMixBuffer = NULL;

		// read inputs
		for (iPad=0; iPad < 5; iPad++)
		{
			if (InputIsPadConnected(iPad))
			{
				Input.uPad[iPad] = _MainLoopInput(InputGetPadData(iPad));
			} else
			{
				Input.uPad[iPad] = EMUSYS_DEVICE_DISCONNECTED;
			}
		}

		// send controller 1 + 2 inputs combined to 32-bits
		NetInput.InputSend = ((Uint32)Input.uPad[0]) | (((Uint32)Input.uPad[1])<<16);

        PROF_ENTER("NetPlayClientInput");
        NetPlayClientInput(&NetInput);
        PROF_LEAVE("NetPlayClientInput");

        if (NetInput.eGameState == NETPLAY_GAMESTATE_PLAY)
        {
            if ((_pSystem->GetFrame()+1) != NetInput.uFrame)
            {
				#if CODE_DEBUG
                printf("Not executing frame %d %d\n", NetInput.uFrame, _pSystem->GetFrame());
				#endif
                NetInput.eGameState = NETPLAY_GAMESTATE_PAUSE;
            }

			// we are connected, retrieve input data
	        Input.uPad[0] = (Uint16)NetInput.InputRecv[0];
	        Input.uPad[1] = (Uint16)NetInput.InputRecv[1];
	        Input.uPad[2] = (Uint16)NetInput.InputRecv[2];
	        Input.uPad[3] = (Uint16)NetInput.InputRecv[3];
			Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;

			if (Input.uPad[2] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 3 is disconnected, use controller 2 of first peer
				Input.uPad[2] = (Uint16)(NetInput.InputRecv[0]>>16);
			}

			if (Input.uPad[3] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 4 is disconnected, use controller 2 of second peer
				Input.uPad[3] = (Uint16)(NetInput.InputRecv[1]>>16);
			}
		
        }  
		else
		{
		
            if (s_pMovieClip->IsPlaying())
            {
                if (!s_pMovieClip->PlayFrame(Input))
                {
                    s_pMovieClip->PlayEnd();
                    ConPrint("Movie: Play End\n");
                }
            }
	
		}

        if (NetInput.eGameState != NETPLAY_GAMESTATE_PAUSE)
        {
			Emu::System::ModeE eMode;

            #if MAINLOOP_HISTORY
            if (_nHistory < 16384 * 2)
            {
                _History[_nHistory++] = Input.uPad[0];
                _History[_nHistory++] = Input.uPad[1];
                _History[_nHistory++] = Input.uPad[2];
                _History[_nHistory++] = Input.uPad[3];
            }
            #endif

			_uInputFrame    = NetInput.uFrame;
			_uInputChecksum[0] += Input.uPad[0];
			_uInputChecksum[1] += Input.uPad[1];
			_uInputChecksum[2] += Input.uPad[2];
			_uInputChecksum[3] += Input.uPad[3];
			_uInputChecksum[4] += Input.uPad[4];

			eMode = (NetInput.eGameState == NETPLAY_GAMESTATE_IDLE) ? Emu::System::MODE_ACCURATENONDETERMINISTIC : Emu::System::MODE_INACCURATEDETERMINISTIC;

            if (s_pMovieClip->IsRecording())
            {
                if (!s_pMovieClip->RecordFrame(Input))
                {
                    s_pMovieClip->RecordEnd();
                    ConPrint("Movie: Reached end of record buffer!\n");
                }
            }

#if 0
            if (_pSystem==_pSnes)
            {
//                _ExecuteSnes(NULL, NULL, &Input, eMode);
 			    GPPrimDisableZBuf();
                _ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
//                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
            } else
            if (_pSystem==_pNes)
            {
                _ExecuteNes(pSurface, pMixBuffer, &Input, eMode);

                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
            } 
#else
 			    GPPrimDisableZBuf();
                _ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
#endif
		    _iframetex^=1;
        }

        SjPCM_BufferedAsyncStart();
    }

    _MainLoopCheckSRAM();

	MainLoopRender();

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

    return TRUE;
}

//
//
//



/*
	Menu stuff

*/

void _MenuEnable(Bool bEnable)
{
	if (bEnable!=_bMenu)
	{
		// if menu is enabled, then attempt to save sram immediately
		if (bEnable)
		{
            #if 1 
			if (_MainLoopHasSRAM() && _MainLoop_SRAMUpdated)
			{
			   	MainLoopModalPrintf(10, "Saving SRAM...");

			    if (_MainLoopHasSRAM())
			    {
					#if MAINLOOP_MEMCARD
					MCSave_WriteSync(1, NULL);

					if (MemCardCheckNewCard())
					{
						printf("New memcard detected\n");
						if (MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, FALSE))
						{
							MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, FALSE);
						}
					}
					#endif

				    if (_MainLoopSaveSRAM(TRUE))
				    {
					    MainLoopModalPrintf(60, "SRAM saved.\n");
				    } else
				    {
					    MainLoopModalPrintf(60 * 1 + 30, "Error Saving SRAM!\n");
				    }
			    }
			}
            #endif
		}

		_bMenu = bEnable;

//    	SjPCM_Clearbuff();
	}
}

#if MAINLOOP_HISTORY
    if (trigger & PAD_L3)
    {
         _MainLoopSaveHistory();
   }
#endif






static void _MenuDraw()
{
	FontSelect(0);

	PolyTexture(NULL);
    PolyBlend(TRUE);


    t_ip_info config;
    memset(&config, 0, sizeof(config));

	// draw current screen
	if (_MainLoop_pScreen)
	{
		_MainLoop_pScreen->Draw();
	}

	int vy = 215;

	FontSelect(2);
//	FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
//	FontColor4f(1.0, 0.5f, 0.5f, 1.0f);
	FontColor4f(0.2, 0.6f, 0.2f, 1.0f);

#if 0
	const VersionInfoT *pVersionInfo = VersionGetInfo();

	char VersionStr[256];
	
	sprintf(VersionStr, "%s v%d.%d.%d %s", 
		pVersionInfo->ApplicationName, 
		pVersionInfo->Version[0],
		pVersionInfo->Version[1],
		pVersionInfo->Version[2],
		pVersionInfo->BuildType
		);

	FontPuts(256 - 16 - FontGetStrWidth(VersionStr), vy, VersionStr);

//	FontPrintf(8, vy-16, "%d", CDVD_DiskReady(1));





	FontPrintf(8, vy, "%s%d.%d", 
		pVersionInfo->Compiler, 
		pVersionInfo->CompilerVersion[0],  
		pVersionInfo->CompilerVersion[1]
		);
#endif	
    FontPrintf(48,vy,"IP: %d.%d.%d.%d", 
            (config.ipaddr.s_addr >> 0) & 0xFF,
            (config.ipaddr.s_addr >> 8) & 0xFF,
            (config.ipaddr.s_addr >>16) & 0xFF,
            (config.ipaddr.s_addr >>24) & 0xFF
                    );



	FontSelect(0);
}





#if 1
void *operator new(unsigned x)
{
	void *ptr = malloc(x);
	#if CODE_DEBUG
	printf("new %d %08X\n", x, (unsigned)ptr);
	#endif
	return ptr;
}

void operator delete(void *ptr)
{
	#if CODE_DEBUG
	printf("delete %08X\n", (unsigned)ptr);
	#endif
	free(ptr);
}



void *operator new[](unsigned x)
{
//	printf("new %d\n", x);
	return malloc(x);
}

void operator delete[](void *ptr)
{
//	printf("delete %08X\n", (unsigned)ptr);
	free(ptr);
}

#endif







