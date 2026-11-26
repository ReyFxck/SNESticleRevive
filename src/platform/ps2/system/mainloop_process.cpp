/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 * Implements mainloop process behavior for the PlayStation 2 application runtime.
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
#include "mainloop_target_profile.h"
#include "mainloop_telemetry.h"
#include "mainloop_late_diag.h"
#include "gskit_backend.h"

#include "types.h"
#include "console.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "mixbuffer.h"
#include "prof.h"
#include "sndbglog.h"
#include "audio_log.h"
#include "emusys.h"
#include "emumovie.h"
#include "netplay_input_merge.h"

extern "C" {
#include "gpprim.h"
#include "hw.h"
#include "gs.h"
};

extern "C" {
#include "netplay_ee.h"
};

extern "C" {
#include "audio.h"
};

static MainLoopFPSModeE s_FPSMode = MAINLOOP_FPS_OFF;
MainLoopFPSSnapshotT g_MainLoopFPS;
MainLoopDiagFrontT g_MainLoopDiagFront;
static int s_FPSColor = 0;
int MainLoopFPSGetColor() { return s_FPSColor; }
void MainLoopFPSSetColor(int color) { s_FPSColor = MainLoopFPSNormalizeColor(color); }
Bool MainLoopFPSIsEnabled() { return s_FPSMode != MAINLOOP_FPS_OFF; }
MainLoopFPSModeE MainLoopFPSGetMode() { return s_FPSMode; }
void MainLoopFPSSetMode(int mode)
{
    s_FPSMode = MainLoopFPSNormalizeMode(mode);
    SnesTargetProfileSetEnabled(s_FPSMode == MAINLOOP_FPS_PROFILER);
    g_MainLoopFPS.Ready = FALSE;
}

static void MainLoopFPSFinish(Bool gameplay, Uint32 start,
                             Uint32 sourceFrames, Uint32 drawnFrames)
{
    struct WindowT { Uint32 Start, Source, Draw, Present; };
    static WindowT window;
    if (!MainLoopFPSIsEnabled() || !gameplay)
    {
        memset(&window, 0, sizeof(window));
        g_MainLoopFPS.Ready = FALSE;
        return;
    }
    if (!window.Present) window.Start = start;
    window.Source += sourceFrames;
    window.Draw += drawnFrames;
    window.Present++;
    Uint32 elapsed = ProfCtrGetCycle() - window.Start;
    if (elapsed < PROFCTR_COUNT_HZ) return;
    g_MainLoopFPS.SourceRate10 = SnesTargetProfileRate10(window.Source, elapsed);
    g_MainLoopFPS.DrawRate10 = SnesTargetProfileRate10(window.Draw, elapsed);
    g_MainLoopFPS.PresentRate10 = SnesTargetProfileRate10(window.Present, elapsed);
    g_MainLoopFPS.Ready = TRUE;
    memset(&window, 0, sizeof(window));
}

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

#if SNES_TARGET_PROFILE
MainLoopTargetProfileSnapshotT g_MainLoopTargetProfile;

static void _MainLoopTargetProfileFinish(Bool bGameplay,
    Uint32 uLoopStart, Uint32 uNow)
{
    struct WindowT
    {
        Uint32 Start, Loops, SourceFrames, RenderedFrames;
        Uint64 Core, PPU, OBJ, SPC, Mix, Audio, GIFWait;
        Uint64 BGFetch, BGDraw, Output;
        Uint64 CPU, MDMA, HDMA, RenderBegin, RenderEnd;
        Uint64 Prep, Submit, Flip, Work;
    };
    static WindowT window;
    if (!bGameplay || !SnesTargetProfileIsEnabled())
    {
        if (window.Loops) memset(&window, 0, sizeof(window));
        g_MainLoopTargetProfile.Ready = FALSE;
        return;
    }
    if (!window.Loops) window.Start = uLoopStart;
    window.Loops++;
    window.SourceFrames += g_SnesTargetProfile.SourceFrames;
    window.RenderedFrames += g_SnesTargetProfile.RenderedFrames;
    window.Core += g_SnesTargetProfile.Core;
    window.PPU += g_SnesTargetProfile.PPU;
    window.OBJ += g_SnesTargetProfile.OBJ;
    window.SPC += g_SnesTargetProfile.SPC;
    window.Mix += g_SnesTargetProfile.Mix;
    window.Audio += g_SnesTargetProfile.Audio;
    window.GIFWait += g_SnesTargetProfile.GIFWait;
    window.BGFetch += g_SnesTargetProfile.BGFetch;
    window.BGDraw += g_SnesTargetProfile.BGDraw;
    window.Output += g_SnesTargetProfile.Output;
    window.CPU += g_SnesTargetProfile.CPU;
    window.MDMA += g_SnesTargetProfile.MDMA;
    window.HDMA += g_SnesTargetProfile.HDMA;
    window.RenderBegin += g_SnesTargetProfile.RenderBegin;
    window.RenderEnd += g_SnesTargetProfile.RenderEnd;
    window.Prep += _MainLoop_DiagPrepCount;
    window.Submit += _MainLoop_DiagSubmitCount;
    window.Flip += _MainLoop_DiagFlipCount;
    window.Work += uNow - uLoopStart - _MainLoop_DiagFlipCount;
    Uint32 uElapsed = uNow - window.Start;
    if (uElapsed < SNTARGET_COUNT_HZ) return;

    MainLoopTargetProfileSnapshotT *p = &g_MainLoopTargetProfile;
    p->SourceRate10 = SnesTargetProfileRate10(window.SourceFrames, uElapsed);
    p->PresentRate10 = SnesTargetProfileRate10(window.Loops, uElapsed);
    p->RenderRate10 = SnesTargetProfileRate10(window.RenderedFrames, uElapsed);
    p->WorkMs10 = SnesTargetProfileMillis10(window.Work, window.Loops);
    p->CoreMs10 = SnesTargetProfileMillis10(window.Core, window.Loops);
    p->PPUMs10 = SnesTargetProfileMillis10(window.PPU, window.Loops);
    p->OBJMs10 = SnesTargetProfileMillis10(window.OBJ, window.Loops);
    p->SPCMs10 = SnesTargetProfileMillis10(window.SPC, window.Loops);
    p->MixMs10 = SnesTargetProfileMillis10(window.Mix, window.Loops);
    p->AudioMs10 = SnesTargetProfileMillis10(window.Audio, window.Loops);
    p->GIFWaitMs10 = SnesTargetProfileMillis10(window.GIFWait, window.Loops);
    p->BGFetchMs10 = SnesTargetProfileMillis10(window.BGFetch, window.Loops);
    p->BGDrawMs10 = SnesTargetProfileMillis10(window.BGDraw, window.Loops);
    p->OutputMs10 = SnesTargetProfileMillis10(window.Output, window.Loops);
    p->CPUMs10 = SnesTargetProfileMillis10(window.CPU, window.Loops);
    p->MDMAMs10 = SnesTargetProfileMillis10(window.MDMA, window.Loops);
    p->HDMAMs10 = SnesTargetProfileMillis10(window.HDMA, window.Loops);
    p->RenderBeginMs10 = SnesTargetProfileMillis10(window.RenderBegin, window.Loops);
    p->RenderEndMs10 = SnesTargetProfileMillis10(window.RenderEnd, window.Loops);
    p->PrepMs10 = SnesTargetProfileMillis10(window.Prep, window.Loops);
    p->SubmitMs10 = SnesTargetProfileMillis10(window.Submit, window.Loops);
    p->FlipMs10 = SnesTargetProfileMillis10(window.Flip, window.Loops);
    p->Ready = TRUE;
    memset(&window, 0, sizeof(window));
}
#endif

static void _MainLoopResetSnesRegionCadence(void)
{
    MainLoopRegionCadenceReset(&s_SnesRegionCadence);
}

Bool MainLoopProcess()
{
#if SNDBG_LOG || SNES_TARGET_PROFILE
    Uint32 uLoopStart = (SNDBG_LOG || SnesTargetProfileIsEnabled()) ? ProfCtrGetCycle() : 0;
    Bool bSnesAtStart = _pSnes && _pSystem == _pSnes && !_bMenu;
#endif
#if SNDBG_LOG
    const Uint32 uStartVR = (Uint32)TestVRstart();
    g_DbgFrameReportCycles = 0;
#endif
#if SNES_TARGET_PROFILE
    if (SnesTargetProfileIsEnabled())
        memset(&g_SnesTargetProfile, 0, sizeof(g_SnesTargetProfile));
#endif
    Uint32 fpsStart = MainLoopFPSIsEnabled() ? ProfCtrGetCycle() : 0;
    Emu::System *fpsSystem = _pSystem;
    Uint32 fpsFrameBefore = (MainLoopFPSIsEnabled() && fpsSystem) ? fpsSystem->GetFrame() : 0;
    Uint32 fpsDrawnFrames = 0;
    Aud_Service();
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
                Uint32 sourceHz = _pNes->GetFrameRate();
                Uint32 hostHz = (Uint32)GSK_GetRefreshHz();
                _AudMix->SetFrameRate(sourceHz);
                Bool allowed = NetInput.eGameState == NETPLAY_GAMESTATE_IDLE &&
                    !s_pMovieClip->IsPlaying() && !s_pMovieClip->IsRecording();
                Uint32 hidden = MainLoopSafeFrameskipTake(allowed);
                Uint32 extra = 0;
                Bool hold = FALSE;
                if (hidden) _MainLoopResetSnesRegionCadence();
                else MainLoopRegionCadenceStep(&s_SnesRegionCadence, allowed,
                    sourceHz, hostHz, &extra, &hold);
                hidden += extra;

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
                for (Uint32 i = 0; i < hidden; ++i)
                    _pNes->ExecuteFrame(&Input, NULL, pMixBuffer, eMode);
                if (!hold)
                    _pNes->ExecuteFrame(&Input, pSurface, pMixBuffer, eMode);
                PROF_LEAVE("NesExecuteFrame");
                if (!hold)
                {
                    PROF_ENTER("NesTexUpload");
                    TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
                    PROF_LEAVE("NesTexUpload");
                }
                bProducedFrame = hold ? FALSE : TRUE;
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
            {
                _iframetex^=1;
                fpsDrawnFrames = 1;
            }
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
    if (MainLoopFPSIsEnabled())
        MainLoopFPSFinish(!_bMenu && fpsSystem && _pSystem == fpsSystem &&
            !_MainLoop_BlackScreen, fpsStart,
            _pSystem == fpsSystem && fpsSystem ? fpsSystem->GetFrame() - fpsFrameBefore : 0,
            fpsDrawnFrames);
    else
        MainLoopFPSFinish(FALSE, 0, 0, 0);

#if SNES_TARGET_PROFILE
    _MainLoopTargetProfileFinish(bSnesAtStart && _pSystem == _pSnes &&
        !_bMenu && !_MainLoop_BlackScreen, uLoopStart,
        SnesTargetProfileIsEnabled() ? ProfCtrGetCycle() : 0);
#endif

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
        g_MainLoopDiagFront.Ready = FALSE;
    }
    /* Start-to-start cadence includes post-accounting diagnostic output,
       capture pumping and the return to the next MainLoopProcess call.
       Keep it separate from the legacy ps2-frame schema. */
    struct CadenceWindowT
    {
        Uint32 Session, HostHz, PrevStart, PrevLoop, HavePrev;
        Uint32 PrevWork, PrevReport, PrevVR;
        MainLoopLateDiagT Late;
        Uint32 Count, Max, Over105, Over150;
        Uint64 Total, Tail;
    };
    static CadenceWindowT cadence;
    if (!bSnesDisplay || cadence.Session != g_DbgSessionId ||
        cadence.HostHz != g_DbgHostRefreshHz)
    {
        memset(&cadence, 0, sizeof(cadence));
        cadence.Session = g_DbgSessionId;
        cadence.HostHz = g_DbgHostRefreshHz;
    }
    // Observe the legacy interrupt counter, never wait on or reset it.
    // If a platform stops delivering this IRQ, zero deltas are diagnostic
    // evidence of an unavailable counter, not proof of a missed VBlank.
    struct VRWindowT { Uint32 Session, HostHz, PrevAfter, HavePrev, Count;
        Uint32 Before, During, Zero, Multiple, Invalid; };
    static VRWindowT vr;
    if (!bSnesDisplay || vr.Session != g_DbgSessionId || vr.HostHz != g_DbgHostRefreshHz)
    {
        memset(&vr, 0, sizeof(vr));
        vr.Session = g_DbgSessionId;
        vr.HostHz = g_DbgHostRefreshHz;
    }
    if (bSnesDisplay)
    {
        if (vr.HavePrev)
        {
            const Uint32 before = _MainLoop_DiagVRBefore - vr.PrevAfter;
            const Uint32 during = _MainLoop_DiagVRAfter - _MainLoop_DiagVRBefore;
            if (before > 16 || during > 16) ++vr.Invalid;
            else
            {
                vr.Before += before;
                vr.During += during;
                if (!(before + during)) ++vr.Zero;
                if (before + during > 1) ++vr.Multiple;
            }
            if (++vr.Count == SNDBG_FRAME_PERIOD)
            {
                DLog("[ps2-vblank-irq] session=%u intervals=%u before/during=%u/%u zero/multiple/invalid=%u/%u/%u",
                    (unsigned)vr.Session, (unsigned)vr.Count, (unsigned)vr.Before,
                    (unsigned)vr.During, (unsigned)vr.Zero, (unsigned)vr.Multiple,
                    (unsigned)vr.Invalid);
                vr.Count = vr.Before = vr.During = vr.Zero = vr.Multiple = vr.Invalid = 0;
            }
        }
        vr.PrevAfter = _MainLoop_DiagVRAfter;
        vr.HavePrev = 1;
        Uint32 uLoop = ProfCtrGetCycle() - uLoopStart;
        Uint32 uWork = uLoop - _MainLoop_DiagFlipCount;
        if (cadence.HavePrev && cadence.HostHz)
        {
            const Uint32 interval = uLoopStart - cadence.PrevStart;
            const Uint32 budget = PROFCTR_COUNT_HZ / cadence.HostHz;
            cadence.Total += interval;
            cadence.Late.Add(interval, budget, cadence.PrevWork,
                cadence.PrevReport, uStartVR - cadence.PrevVR);
            if (interval > cadence.PrevLoop)
                cadence.Tail += interval - cadence.PrevLoop;
            if (interval > cadence.Max) cadence.Max = interval;
            if (interval > budget + budget / 20) ++cadence.Over105;
            if (interval > budget + budget / 2) ++cadence.Over150;
            if (++cadence.Count == SNDBG_FRAME_PERIOD)
            {
                DLog("[ps2-cadence] session=%u host-hz=%u intervals=%u avg/max/tail=%u/%u/%u over105/150=%u/%u",
                    (unsigned)cadence.Session, (unsigned)cadence.HostHz,
                    (unsigned)cadence.Count,
                    (unsigned)(cadence.Total / cadence.Count), (unsigned)cadence.Max,
                    (unsigned)(cadence.Tail / cadence.Count),
                    (unsigned)cadence.Over105, (unsigned)cadence.Over150);
                DLog("[ps2-late-work] session=%u intervals=%u late=%u work-over/under=%u/%u report-cross=%u irq-multiple/invalid=%u/%u report-avg/max=%u/%u",
                    (unsigned)cadence.Session, (unsigned)cadence.Count,
                    (unsigned)cadence.Late.Late, (unsigned)cadence.Late.WorkOver,
                    (unsigned)cadence.Late.WorkUnder, (unsigned)cadence.Late.ReportCross,
                    (unsigned)cadence.Late.IRQMultiple, (unsigned)cadence.Late.IRQInvalid,
                    (unsigned)(cadence.Late.ReportTotal / cadence.Count),
                    (unsigned)cadence.Late.ReportMax);
                memset(&cadence.Late, 0, sizeof(cadence.Late));
                cadence.Count = cadence.Max = cadence.Over105 = cadence.Over150 = 0;
                cadence.Total = cadence.Tail = 0;
            }
        }
        cadence.PrevStart = uLoopStart;
        cadence.PrevLoop = uLoop;
        cadence.PrevWork = uWork;
        cadence.PrevReport = g_DbgFrameReportCycles;
        cadence.PrevVR = uStartVR;
        cadence.HavePrev = 1;
        if (!window.nFrames || uWork < window.uMinWork) window.uMinWork = uWork;
        if (uWork > window.uMaxWork) window.uMaxWork = uWork;
        window.uLoop += uLoop;
        window.uPrep += _MainLoop_DiagPrepCount;
        window.uSubmit += _MainLoop_DiagSubmitCount;
        window.uFlip += _MainLoop_DiagFlipCount;
        if (++window.nFrames == SNDBG_FRAME_PERIOD)
        {
            g_MainLoopDiagFront.Work10 = SnesTargetProfileMillis10(window.uLoop-window.uFlip,window.nFrames);
            g_MainLoopDiagFront.MaxWork10 = SnesTargetProfileMillis10(window.uMaxWork,1);
            g_MainLoopDiagFront.Flip10 = SnesTargetProfileMillis10(window.uFlip,window.nFrames);
            g_MainLoopDiagFront.Prep10 = SnesTargetProfileMillis10(window.uPrep,window.nFrames);
            g_MainLoopDiagFront.Submit10 = SnesTargetProfileMillis10(window.uSubmit,window.nFrames);
            g_MainLoopDiagFront.Ready = TRUE;
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
            AudOutputStatsT audio;
            if (Aud_GetOutputStats(&audio)) {
                DLog("[ps2-audio] session=%u ee/io=%u/%u underruns=%u silent=%u backpressure=%u missed-refills=%u",
                    (unsigned)window.uSession, audio.queued_ee_frames,
                    audio.queued_iop_frames, audio.underruns, audio.silent_frames,
                    audio.backpressure_waits, audio.missed_refills);
                DLog("[ps2-audio-rpc] session=%u submitted/accepted=%u/%u zero/partial=%u/%u stats/service=%u/%u flight/pending=%u/%u",
                    (unsigned)window.uSession, audio.submitted_frames, audio.accepted_frames,
                    audio.zero_accepts, audio.partial_accepts, audio.stats_launches,
                    audio.pending_services, audio.inflight_frames, audio.rpc_pending);
            } else
                DLog("[ps2-audio] session=%u stats=unavailable", (unsigned)window.uSession);
            memset(&window, 0, sizeof(window));
            window.uSession = g_DbgSessionId;
        }
    }
#endif

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

#if SNDBG_LOG
    /* File logging is intentionally pumped after frame/profiler accounting.
       DLog() itself only appends to RAM; this is the single gameplay-time
       storage write point, in blocks of at least 64 KiB. */
    DLogCapturePump();
#endif

    return MainLoopGetSystemAction() == MAINLOOP_SYSTEM_NONE ? TRUE : FALSE;
}
