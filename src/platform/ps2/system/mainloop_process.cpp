/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop process behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <string.h>

#include "mainloop_debug.h"
#include "mainloop.h"
#include "mainloop_shared.h"
#include "mainloop_input.h"
#include "mainloop_state.h"
#include "mainloop_exec.h"
#include "mainloop_iop.h"
#include "mainloop_safe_frameskip.h"
#include "mainloop_region_cadence.h"
#include "gskit_backend.h"

#include "types.h"
#include "console.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "mixbuffer.h"
#include "prof.h"
#include "sndbglog.h"
#include "emusys.h"
#include "emumovie.h"
#include "netplay_input_merge.h"

extern "C" {
#include "gpprim.h"
#include "gs.h"
};

extern "C" {
#include "netplay_ee.h"
};

extern "C" {
#include "audio.h"
};

static Uint32 _iframetex=0;

/*
 * Source/host cadence converter for mixed-region use.
 *
 * The emulated SNES keeps the cartridge cadence (60 NTSC / 50 PAL) even
 * when the PS2 BIOS selected the other TV standard. One source frame is the
 * baseline for each host tick; the rate difference is accumulated:
 *   NTSC source -> PAL host: one extra hidden frame every 5 host VBlanks.
 *   PAL source  -> NTSC host: repeat the previous picture every 6 VBlanks.
 *
 * Netplay/movie paths keep their historical 1:1 semantics. Missed-VBlank
 * recovery takes precedence and resets this phase so both recovery systems
 * cannot charge the same elapsed interval.
 */
static MainLoopRegionCadenceT s_SnesRegionCadence = { 0, 0, 0 };

static void _MainLoopResetSnesRegionCadence(void)
{
    MainLoopRegionCadenceReset(&s_SnesRegionCadence);
}

Bool MainLoopProcess()
{
#if SNDBG_LOG
    Uint32 uLoopStart = ProfCtrGetCycle();
    Bool bSnesAtStart = _pSnes && _pSystem == _pSnes && !_bMenu;
#endif
    NetPlayRPCInputT NetInput;

    PROF_ENTER("Frame");

    PROF_ENTER("NetPlayRPCProcess");
    NetPlayRPCProcess();
    PROF_LEAVE("NetPlayRPCProcess");

    PROF_ENTER("InputProcess");
    InputPoll();

    PROF_LEAVE("InputProcess");

	{
	    /* OR the digital pad bits with d-pad bits synthesised from each
	       pad's left analog stick. The synthesised bits only travel
	       through _MainLoopInputProcess (menu / screen-cycle / debug
	       triggers), so SNES gameplay still uses the strictly digital
	       _Input_PadData via _MainLoopInput. Result: the analog stick
	       drives menu navigation just like InfinityStation, without
	       leaking into the running game. */
	    Uint32 buttons =
	          InputGetPadData(0) | InputGetPadData(1)
	        | InputGetPadData(2) | InputGetPadData(3)
	        | InputGetPadDpadFromAnalog(0) | InputGetPadDpadFromAnalog(1)
	        | InputGetPadDpadFromAnalog(2) | InputGetPadDpadFromAnalog(3);

	    _MainLoopInputProcess(buttons);
	}

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
            pMixBuffer = &_AudMix;
        }
        */
        pMixBuffer = _AudMix;

		// read inputs
		for (iPad=0; iPad < 5; iPad++)
		{
			if (InputIsPadConnected(iPad))
			{
				/* OR the digital pad bits with d-pad bits synthesised from
				   the left analog stick so the analog stick drives the SNES
				   d-pad in-game (LEFT/RIGHT/UP/DOWN). Digital and analog
				   inputs are merged: if both press the same direction the
				   result is identical to a single press, so users can use
				   whichever they prefer (or both). */
				Input.uPad[iPad] = _MainLoopInput(InputGetPadData(iPad)
				                               | InputGetPadDpadFromAnalog(iPad));
			} else
			{
				Input.uPad[iPad] = EMUSYS_DEVICE_DISCONNECTED;
			}
		}

		if (_pSystem == _pSnes)
			_MainLoopSnesInputApply(&Input);

		/* Send all five words, including the special-device tag, coordinates
		   and auxiliary buttons. The old 32-bit pair dropped the entire SNES
		   peripheral payload before it ever reached the transport. */
		for (Int32 i=0; i<EMUSYS_DEVICE_NUM; ++i)
			NetInput.InputSend.uPad[i] = Input.uPad[i];

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

			/* Rebuild the same five-word input on every peer. Normal
			   four-controller assignment stays identical to the old protocol;
			   SNES special controllers carry their whole report atomically. */
			NetPlayMergeInputs(&Input, NetInput.InputRecv,
				_pSystem == _pSnes ? TRUE : FALSE);

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

            GPPrimDisableZBuf();

            /* Phase 2 of the NES integration: dispatch ExecuteFrame
               through the polymorphic Emu::System* when the loaded
               system is the NES wrapper.  The SNES path stays on its
               bespoke _ExecuteSnes helper that does the PPU upload
               and CLUT bookkeeping.  NesSystem renders directly into
               the surface (Phase 2 = diagnostic test pattern) and we
               upload to the EE texture from here. */
            Bool bProducedFrame = TRUE;

            if (_pSystem == _pNes)
            {
                /* NES is a 60 Hz source in the current integration. */
                _AudMix->SetFrameRate(60);
                _MainLoopResetSnesRegionCadence();

                /* The shared VRAM block is 256 KiB either way:
                   NES = 256x256 RGBA32, SNES = 512x256 RGBA5551.
                   Re-describe it instead of reserving a second texture. */
                if (_OutTex.uWidth != 256 || _OutTex.eFormat != GS_PSMCT32)
                {
                    TextureNew(&_OutTex, 256, 256, GS_PSMCT32);
                    TextureSetAddr(&_OutTex, _MainLoop_uOutTexTBP);
                    TextureSetFilter(&_OutTex, g_GskTextureFilter);
                }
                PROF_ENTER("NesExecuteFrame");
                _pNes->ExecuteFrame(&Input, pSurface, pMixBuffer, eMode);
                PROF_LEAVE("NesExecuteFrame");
                PROF_ENTER("NesTexUpload");
                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
                PROF_LEAVE("NesTexUpload");
            }
            else
            {
                /* SNES native hires carrier. PSMCT16 keeps 512x256 at the
                   same 256 KiB VRAM cost as the old 256x256 PSMCT32 texture. */
                if (_OutTex.uWidth != 512 || _OutTex.eFormat != GS_PSMCT16)
                {
                    TextureNew(&_OutTex, 512, 256, GS_PSMCT16);
                    TextureSetAddr(&_OutTex, _MainLoop_uOutTexTBP);
                    TextureSetFilter(&_OutTex, g_GskTextureFilter);
                }
                Uint32 uSourceHz =
                    (_pSnesRom && _pSnesRom->m_eVideoType == SNROM_VIDEO_PAL)
                    ? 50u : 60u;
                Uint32 uHostHz = (Uint32)GSK_GetRefreshHz();
                _AudMix->SetFrameRate(uSourceHz);
#if SNDBG_LOG
				g_DbgHostRefreshHz = uHostHz;
#endif
				/* Recover after missed host VBlanks by running the missing SNES
				   frames without video before drawing the newest one.  Unlike merely
				   presenting the old texture, these hidden frames do not perform a
				   GS flip/wait, so CPU, SPC and input can regain real-time cadence. */
				Bool bFrameskipAllowed =
					(NetInput.eGameState == NETPLAY_GAMESTATE_IDLE &&
					 !s_pMovieClip->IsPlaying() &&
					 !s_pMovieClip->IsRecording()) ? TRUE : FALSE;
				Uint32 uCatchupFrames =
					MainLoopSafeFrameskipTake(bFrameskipAllowed);
                Uint32 uRegionExtra = 0;
                Bool bRegionHold = FALSE;

                if (uCatchupFrames == 0)
                {
                    MainLoopRegionCadenceStep(
                        &s_SnesRegionCadence,
                        bFrameskipAllowed, uSourceHz, uHostHz,
                        &uRegionExtra, &bRegionHold);
                }
                else
                {
                    _MainLoopResetSnesRegionCadence();
                }

                if (!bRegionHold)
                {
                    Uint32 uHiddenFrames = uCatchupFrames + uRegionExtra;
                    for (Uint32 uCatchup = 0; uCatchup < uHiddenFrames; ++uCatchup)
                    {
#if SNDBG_LOG
                        g_DbgVideoSkippedFrames++;
                        if (uCatchup < uCatchupFrames)
                            SnesDbgRequestCapture(SNDBG_CAPTURE_FRAMESKIP);
#endif
                        _ExecuteSnes(NULL, pMixBuffer, &Input, eMode);
                    }
#if SNDBG_LOG
                    g_DbgVideoRenderedFrames++;
#endif
                    _ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
                }
                else
                {
                    /* PAL source on a 60 Hz host: keep emulated time at 50 Hz
                       by presenting the already-uploaded texture once more. */
                    bProducedFrame = FALSE;
                }
            }
            if (bProducedFrame)
                _iframetex^=1;
        }

        Aud_BufferedAsyncStart();
    }
    else
    {
        _MainLoopResetSnesRegionCadence();
    }

    _MainLoopCheckSRAM();
	/* Deferred menu work runs only after _MenuEnable has returned. In the
	   L2+R2 path this leaves two complete frames for the menu/status to become
	   visible before a synchronous SRAM write begins. */
	_MenuRuntimeUpdate();

	MainLoopRender();

#if SNDBG_LOG
    /* Measure the whole application iteration, including audio enqueue and
       presentation. The old core counter ends before frontend finish/flip. */
    struct FrameWindowT
    {
        Uint32 uSession, nFrames, uMinWork, uMaxWork;
        Uint64 uLoop, uPrep, uSubmit, uFlip;
    };
    static FrameWindowT window;
    Bool bSnesDisplay = bSnesAtStart && _pSystem == _pSnes &&
        !_bMenu && !_MainLoop_BlackScreen;
    if (!bSnesDisplay || window.uSession != g_DbgSessionId)
    {
        memset(&window, 0, sizeof(window));
        window.uSession = g_DbgSessionId;
    }
    if (bSnesDisplay)
    {
        Uint32 uLoop = ProfCtrGetCycle() - uLoopStart;
        Uint32 uWork = uLoop - _MainLoop_DiagFlipCount;
        if (!window.nFrames || uWork < window.uMinWork) window.uMinWork = uWork;
        if (uWork > window.uMaxWork) window.uMaxWork = uWork;
        window.uLoop += uLoop;
        window.uPrep += _MainLoop_DiagPrepCount;
        window.uSubmit += _MainLoop_DiagSubmitCount;
        window.uFlip += _MainLoop_DiagFlipCount;
        if (++window.nFrames == SNDBG_FRAME_PERIOD)
        {
            DLog("[ps2-frame] session=%u input=%u host-hz=%u display-frames=%u count avg loop/prep/submit/flip=%u/%u/%u/%u work min/avg/max=%u/%u/%u",
                (unsigned)window.uSession, (unsigned)_uInputFrame,
                (unsigned)g_DbgHostRefreshHz, (unsigned)window.nFrames,
                (unsigned)(window.uLoop / window.nFrames),
                (unsigned)(window.uPrep / window.nFrames),
                (unsigned)(window.uSubmit / window.nFrames),
                (unsigned)(window.uFlip / window.nFrames),
                (unsigned)window.uMinWork,
                (unsigned)((window.uLoop - window.uFlip) / window.nFrames),
                (unsigned)window.uMaxWork);
            memset(&window, 0, sizeof(window));
            window.uSession = g_DbgSessionId;
        }
    }
#endif

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

    return MainLoopGetSystemAction() == MAINLOOP_SYSTEM_NONE ? TRUE : FALSE;
}
