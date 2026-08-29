/* mainloop_render.cpp
 *
 * Hosts MainLoopRender() and the file-static state it needs
 * (_uVblankCycle and the screen-size #defines used to clear / blit the
 * SNES output texture).
 *
 * Extracted from mainloop.cpp during the Batch 3 split. No logic,
 * literal, or attribute change.
 */

#include <stdio.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop_bgm.h"
#include "mainloop_safe_frameskip.h"

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

static MainLoopSafeFrameskipScheduler _SafeFrameskip;
static Bool _bSafeFrameskipEnabled = FALSE;

Bool MainLoopSafeFrameskipIsEnabled()
{
#if SNESTICLE_SAFE_FRAMESKIP
	return _bSafeFrameskipEnabled;
#else
	return FALSE;
#endif
}

void MainLoopSafeFrameskipSetEnabled(Bool bEnabled)
{
#if SNESTICLE_SAFE_FRAMESKIP
	Bool bNewValue = bEnabled ? TRUE : FALSE;
	if (_bSafeFrameskipEnabled != bNewValue)
	{
		_bSafeFrameskipEnabled = bNewValue;
		_SafeFrameskip.Reset();
	}
#else
	(void)bEnabled;
	_bSafeFrameskipEnabled = FALSE;
#endif
}

Uint32 MainLoopSafeFrameskipTake(Bool bAllowed)
{
#if SNESTICLE_SAFE_FRAMESKIP
	if (_bSafeFrameskipEnabled && bAllowed)
		return _SafeFrameskip.TakeCatchupFrames();
#else
	(void)bAllowed;
#endif
	_SafeFrameskip.CancelRecovery();
	return 0;
}

void MainLoopSafeFrameskipAfterFlip()
{
#if SNESTICLE_SAFE_FRAMESKIP
	if (!_bSafeFrameskipEnabled)
		return;

	Bool bSnesGameplay = (!_bMenu && _pSystem == _pSnes &&
	                      !_MainLoop_BlackScreen) ? TRUE : FALSE;
	_SafeFrameskip.AfterFlip(ProfCtrGetCycle(), bSnesGameplay);
#endif
}

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
     * sprite and removes the band entirely.  GSK_ResetFrame now clears
     * the complete PHYSICAL framebuffer, including any overscan borders.
     * Keep the Poly state reset here, but do not queue a duplicate
     * logical-canvas clear. */
    PolyTexture(NULL);
    PolyBlend(FALSE);
    PolyColor4f(0.0f, 0.0f, 0.0f, 1.0f);

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
        PolyUV(0,0,256,240);
		PolyColor4f(fColor, fColor, fColor, 1.0f);


                if (g_GskVideoMode == GSK_VIDMODE_240P && _pSystem == _pNes)
        {
/*
 * InfoNES 240p overscan compensation.
 *
 * Keep the NES framebuffer at its native 256x240 size and
 * preserve a 1:1 pixel mapping. Only reposition the image
 * to compensate for CRT overscan.
 */
PolyRect(0.0f, 5.0f, 256.0f, 240.0f);
        }
        else
        {
PolyRect(0.0f, 7.0f, 256.0f, 240.0f);
        }

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

//			FontPrintf(195, 210, "%8d", _AudMix.GetLastOutput());
    }


	/* Keep menu audio alive even while a modal overlays the UI. Previously
	   BgmUpdate lived only in the non-modal branch below, so every fixed-time
	   message starved audsrv regardless of whether any I/O was happening. */
	if (_bMenu)
	{
		static Bool s_bgmArmed = FALSE;
		if ((void *)_MainLoop_pScreen == (void *)_MainLoop_pBrowserScreen)
			s_bgmArmed = TRUE;
		if (s_bgmArmed)
			BgmUpdate();
		/* Draw the live menu first, then place modal/status text on top. The
		   previous order painted _MenuDraw after the status and hid it. */
		_MenuDraw();
	}

	if (_MainLoop_ModalCount > 0)
	{
		FontSelect(0);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		FontPrintf(128 - FontGetStrWidth(_MainLoop_ModalStr) / 2,100, _MainLoop_ModalStr);

		_MainLoop_ModalCount--;
	}
	else
	{
		if (_MainLoop_StatusCount > 0)
		{
			FontSelect(0);
			FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
			FontPrintf(20, 200, _MainLoop_StatusStr);

			_MainLoop_StatusCount--;
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
	MainLoopSafeFrameskipAfterFlip();
    PROF_LEAVE("WaitVBlank");

    /* whichdrawbuf is now decorative - gsKit owns the active
       framebuffer index via gsGlobal->ActiveBuffer. Keep it
       alive so the diff against the original is small. */
    whichdrawbuf ^= 1;
    (void)whichdrawbuf;

    _iFrame++;
}
