/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop process behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>

#include "mainloop_debug.h"
#include "mainloop.h"
#include "mainloop_shared.h"
#include "mainloop_input.h"
#include "mainloop_state.h"
#include "mainloop_exec.h"
#include "mainloop_iop.h"
#include "mainloop_safe_frameskip.h"
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

extern "C" {
#include "gpprim.h"
};

extern "C" {
#include "netplay_ee.h"
};

extern "C" {
#include "audio.h"
};

static Uint32 _iframetex=0;

/* Convert physical GS refresh ticks into emulated frames. PAL ROMs must
   remain 50 Hz even when the PS2 is outputting NTSC/60 Hz; likewise an
   NTSC ROM on a 50 Hz GS mode occasionally needs two emulated frames on
   one host tick. The phase accumulator keeps the long-term ratio exact. */
static Emu::System *_CadenceSystem = NULL;
static Uint32 _CadenceHostHz = 0;
static Uint32 _CadenceEmuHz = 0;
static Uint32 _CadencePhase = 0;

static void _MainLoopCadenceReset()
{
	_CadenceSystem = NULL;
	_CadenceHostHz = 0;
	_CadenceEmuHz = 0;
	_CadencePhase = 0;
}

static Uint32 _MainLoopCadenceFrames(Emu::System *pSystem,
	Uint32 uHostHz, Uint32 uHostTicks)
{
	if (!pSystem || !uHostTicks)
		return 0;

	Uint32 uEmuHz = pSystem->GetFrameRate();
	if (!uHostHz) uHostHz = 60;
	if (!uEmuHz) uEmuHz = uHostHz;

	if (_CadenceSystem != pSystem ||
	    _CadenceHostHz != uHostHz ||
	    _CadenceEmuHz != uEmuHz)
	{
		_CadenceSystem = pSystem;
		_CadenceHostHz = uHostHz;
		_CadenceEmuHz = uEmuHz;
		/* Make the first host tick render immediately when emulation is
		   slower than the display (50-on-60), then settle into 5/6 cadence. */
		_CadencePhase = (uHostHz > uEmuHz) ? (uHostHz - uEmuHz) : 0;
	}

	Uint64 uAccum = (Uint64)_CadencePhase +
		(Uint64)uEmuHz * (Uint64)uHostTicks;
	Uint32 uFrames = (Uint32)(uAccum / uHostHz);
	_CadencePhase = (Uint32)(uAccum % uHostHz);
	return uFrames;
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

    if (_bMenu || !_pSystem || _MainLoop_BlackScreen)
		_MainLoopCadenceReset();

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
		if (_AudMix)
			_AudMix->SetFrameRate(_pSystem->GetFrameRate());

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

            GPPrimDisableZBuf();

            /* Phase 2 of the NES integration: dispatch ExecuteFrame
               through the polymorphic Emu::System* when the loaded
               system is the NES wrapper.  The SNES path stays on its
               bespoke _ExecuteSnes helper that does the PPU upload
               and CLUT bookkeeping.  NesSystem renders directly into
               the surface (Phase 2 = diagnostic test pattern) and we
               upload to the EE texture from here. */
            Bool bRenderedEmuFrame = TRUE;
            if (_pSystem == _pNes)
            {
                PROF_ENTER("NesExecuteFrame");
                _pNes->ExecuteFrame(&Input, pSurface, pMixBuffer, eMode);
                PROF_LEAVE("NesExecuteFrame");
                PROF_ENTER("NesTexUpload");
                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
                PROF_LEAVE("NesTexUpload");
            }
            else
            {
				const Uint32 uHostHz = (Uint32)GSK_GetRefreshHz();
#if SNDBG_LOG
				g_DbgHostRefreshHz = uHostHz;
#endif
				/* Local gameplay is paced in emulated time, not GS time. Safe
				   frameskip reports missed host ticks; convert the current tick plus
				   those missed ticks through the same rational cadence so recovery
				   remains correct for both 50-on-60 and 60-on-50. Movies/netplay
				   retain their one-logical-frame contract. */
				Bool bCadenceAllowed =
					(NetInput.eGameState == NETPLAY_GAMESTATE_IDLE &&
					 !s_pMovieClip->IsPlaying() &&
					 !s_pMovieClip->IsRecording()) ? TRUE : FALSE;
				Uint32 uCatchupTicks =
					MainLoopSafeFrameskipTake(bCadenceAllowed);
				Uint32 uFramesDue;
				if (bCadenceAllowed)
				{
					uFramesDue = _MainLoopCadenceFrames(
						_pSystem, uHostHz, 1 + uCatchupTicks);
				}
				else
				{
					_MainLoopCadenceReset();
					uFramesDue = 1;
				}

				if (uFramesDue == 0)
				{
					/* 50 Hz PAL on a 60 Hz display: repeat the previously uploaded
					   texture for this host tick and consume no emulated time/audio. */
					bRenderedEmuFrame = FALSE;
				}
				else
				{
					for (Uint32 uHidden = 1; uHidden < uFramesDue; ++uHidden)
					{
#if SNDBG_LOG
						g_DbgVideoSkippedFrames++;
						SnesDbgRequestCapture(SNDBG_CAPTURE_FRAMESKIP);
#endif
						_ExecuteSnes(NULL, pMixBuffer, &Input, eMode);
					}
#if SNDBG_LOG
					g_DbgVideoRenderedFrames++;
#endif
					_ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
				}
            }
		    if (bRenderedEmuFrame)
				_iframetex^=1;
        }

        Aud_BufferedAsyncStart();
    }

    _MainLoopCheckSRAM();
	/* Deferred menu work runs only after _MenuEnable has returned. In the
	   L2+R2 path this leaves two complete frames for the menu/status to become
	   visible before a synchronous SRAM write begins. */
	_MenuRuntimeUpdate();

	MainLoopRender();

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

    return MainLoopGetSystemAction() == MAINLOOP_SYSTEM_NONE ? TRUE : FALSE;
}
