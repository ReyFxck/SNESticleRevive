/* mainloop_render.cpp
 *
 * Hosts MainLoopRender() and the file-static state it needs
 * (_uVblankCycle, the screen-size #defines used to clear / blit the
 * SNES output texture, and the MAINLOOP_DEBUG_GS_TEST hook).
 *
 * Extracted from mainloop.cpp during the Batch 3 split. No logic,
 * literal, or attribute change.
 */

#include <stdio.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"

#include "types.h"
#include "console.h"
#include "snes.h"
#include "rendersurface.h"
#include "texture.h"
#include "font.h"
#include "poly.h"
#include "prof.h"
#include "snstate.h"
#include "snppublend_gs.h"
#include "common/debug/dbgterm.h"

#include "mainloop_iop.h"

extern "C" {
#include "hw.h"
#include "gs.h"
#include "gpfifo.h"
#include "gpprim.h"
#include "gskit_backend.h"
};

extern "C" {
#include "mcsave_ee.h"
};


/* Same MAINLOOP_SCREENWIDTH / HEIGHT pair as mainloop_init.cpp. The
   render path uses these to size the output blit; the init path uses
   them to size the GS framebuffer. Three other historical layouts are
   kept commented out in mainloop_init.cpp for reference. */
#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240


static Uint32 _uVblankCycle;


/* MAINLOOP_DEBUG_GS_TEST: when defined to 1 (-DMAINLOOP_DEBUG_GS_TEST=1
   in CFLAGS, e.g. `make MAINLOOP_DEBUG_GS_TEST=1`), MainLoopRender()
   first paints the entire framebuffer solid red before doing anything
   else. This is the lowest-level GS sanity check possible: if the TV
   shows red, GS_InitGraph + GS_SetEnv + the GIF/DMA pipeline are all
   working and any "black screen" symptom is in the menu/browser/font
   draw path on top. If the TV is still black, the GS itself is
   misconfigured for the current emulator/console (PMODE / DISPFB /
   DISPLAY1). */
#ifndef MAINLOOP_DEBUG_GS_TEST
#endif

void MainLoopRender()
{
	static Uint32 _iFrame=0;
        static int whichdrawbuf = 0;


    /* Re-anchor FRAME_1 to gsKit's current draw buffer before any
       primitive runs this frame. The legacy GS_SetDrawFB used to do
       this implicitly per frame; gsKit_sync_flip only swaps the
       display buffer, not the draw buffer. Without this, prims drew
       to a stale (or, after the SNES blender ran, completely wrong)
       buffer and the visible framebuffer flickered black on every
       other frame. See gskit_backend.h for the longer rationale. */
    GSK_ResetFrame();

    // render frame
    GPPrimDisableZBuf();

    /* Per-frame full-screen clear to black.
     *
     * MainLoopRender historically NEVER cleared the framebuffer: it
     * relied on the full-screen _OutTex blit below to repaint every
     * pixel.  But that blit is (a) skipped entirely when
     * _MainLoop_BlackScreen is set (boot log + menus) and (b) even when
     * drawn it starts at dy=8, so the top rows are never touched.  With
     * DoubleBuffering=ON each draw goes to the alternate buffer, so any
     * row we don't repaint shows stale content from two frames ago --
     * which appears as a fixed-position horizontal "faixa"/stripe
     * through the text (worst in the log and menus, where nothing
     * covers the background).  Clearing to black first costs a single
     * sprite and removes the band entirely.  Same fix used in
     * InfinityStation (ps2_video_clear_bands / per-frame draw_clear). */
    PolyTexture(NULL);
    PolyBlend(FALSE);
    PolyColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    PolyRect(0, 0, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);

#if MAINLOOP_DEBUG_GS_TEST
    PolyTexture(NULL);
    PolyBlend(FALSE);
    PolyColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    PolyRect(0, 0, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);
#endif

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
	if (_MainLoop_bMCSaveReady && MCSave_WriteSync(FALSE, NULL))
	{
		FontSelect(1);
		FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
		if (_iFrame & 4)
			FontPrintf(235,216, "#");
	}
	#endif



    PROF_ENTER("GPFlush");
    GPFifoFlush();
    PROF_LEAVE("GPFlush");

    /* gsKit_sync_flip waits for vsync, swaps the display buffer
       and resets gsKit's draw queue for the next frame. The
       legacy WaitForNextVRstart / GS_SetCrtFB / GS_SetDrawFB
       block is now subsumed by this single call. */
    PROF_ENTER("WaitVBlank");
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle();
    GSK_SyncFlip();
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle() - _uVblankCycle;
    PROF_LEAVE("WaitVBlank");

    /* whichdrawbuf is now decorative - gsKit owns the active
       framebuffer index via gsGlobal->ActiveBuffer. Keep it
       alive so the diff against the original is small. */
    whichdrawbuf ^= 1;
    (void)whichdrawbuf;

    _iFrame++;
}
