/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop input behavior for the PlayStation 2 application runtime.
 */

#include "types.h"
#include <stdio.h>
#include <string.h>
#include <libpad.h>
#include <kernel.h>
#include "mainloop_input.h"
#include "mainloop_iop.h"
#include "mainloop_menu.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "mainloop_shared.h"
#include "mainloop.h"
#include "input.h"
#include "memcard.h"
#include "prof.h"
#include "sndbglog.h"

extern "C" {
#include "audio.h"
}

#define MENU_REPEAT (16)

//#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE)
#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE|PAD_CROSS|PAD_TRIANGLE|PAD_LEFT|PAD_RIGHT)

static Bool _MainLoop_bSuppressGameInputUntilRelease = FALSE;

void _MainLoopInputSuppressUntilRelease()
{
	_MainLoop_bSuppressGameInputUntilRelease = TRUE;
}

static Uint16 _MainLoopSnesInput(Uint32 cond)
{
	Uint32 pad = 0;

	if (cond & PAD_LEFT)    pad|= (SNESIO_JOY_LEFT);
	if (cond & PAD_RIGHT)   pad|= (SNESIO_JOY_RIGHT);
	if (cond & PAD_UP)      pad|= (SNESIO_JOY_UP);
	if (cond & PAD_DOWN)    pad|= (SNESIO_JOY_DOWN);

	if (cond & PAD_SQUARE)   pad|= (SNESIO_JOY_Y);
	if (cond & PAD_TRIANGLE) pad|= (SNESIO_JOY_X);
	if (cond & PAD_CROSS)    pad|= (SNESIO_JOY_B);
	if (cond & PAD_CIRCLE)   pad|= (SNESIO_JOY_A);

	if (cond & PAD_L1)   pad|= (SNESIO_JOY_L);
	if (cond & PAD_R1)   pad|= (SNESIO_JOY_R);

	if (cond & PAD_SELECT)  pad|= (SNESIO_JOY_SELECT);
	if (cond & PAD_START)   pad|= (SNESIO_JOY_START);
	return pad;
}

Uint16 _MainLoopInput(Uint32 pad)
{
	if (_MainLoop_bSuppressGameInputUntilRelease)
	{
		return 0;
	}

	if (pad & (PAD_R2|PAD_L2))
    {
        return 0;
    }
	return _MainLoopSnesInput(pad);

}

static void _MainLoopQuickStateAction(Bool bSave)
{
	Bool bOK;

	/* The first quick-save opens the isolated destination chooser. Its
	   selection callback remembers the target, performs this pending save
	   and closes back to the game; later L2+X presses save directly. */
	if (bSave && !MainLoopStateHasDeviceChoice())
	{
		_MainLoopStateDevicePromptOpen();
		return;
	}

	if (_MainLoop_bAudioReady)
	{
		Aud_Setvol(0);
	}

	MainLoopModalPrintf(
		1,
		bSave ? "Saving state slot %d..." : "Loading state slot %d...",
		(int)MainLoopStateGetSlot() + 1
	);

	bOK = bSave ? _MainLoopSaveState() : _MainLoopLoadState();

	if (bSave && !bOK && MainLoopStateGetUnformattedCard() >= 0)
	{
		Int32 iPort = MainLoopStateGetUnformattedCard();

		if (_MainLoop_bAudioReady)
		{
			Aud_Setvol(0x3FFF);
		}
		_MainLoopMemCardFormatPromptOpen(
			iPort,
			MAINLOOP_MEMCARDFORMAT_STATE_SAVE
		);
		return;
	}

	if (_MainLoop_bAudioReady)
	{
		Aud_Setvol(0x3FFF);
	}

	MainLoopStatusPrintf(
		bOK ? 90 : 180,
		"%s",
		MainLoopStateGetLastMessage()
	);
}

void _MainLoopInputProcess(Uint32 buttons)
{
	static Uint32 lastbuttons= ~0;
	static Uint32 repeat=0;
	static int _MenuTriggerTimeout[2] = {0,0};
	static Bool bStateHotkeyHeld = FALSE;
	Uint32 trigger;

	if (_MainLoop_bSuppressGameInputUntilRelease &&
	    !(buttons & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT |
	                 PAD_CROSS | PAD_CIRCLE | PAD_START)))
	{
		_MainLoop_bSuppressGameInputUntilRelease = FALSE;
	}

	if (!(buttons& MENU_REPEATBUTTONS))
	{
		repeat=0;
	}

	repeat++;
	if (repeat > MENU_REPEAT)
	{
		repeat -= 1;
		lastbuttons &= ~MENU_REPEATBUTTONS;
	}

	trigger = ((buttons ^ lastbuttons) & buttons);
	lastbuttons = buttons;
        if ((buttons & (PAD_L1 | PAD_L2 | PAD_R1 | PAD_R2 |
                    PAD_START | PAD_SELECT)) ==
        (PAD_L1 | PAD_L2 | PAD_R1 | PAD_R2 |
         PAD_START | PAD_SELECT))
    {
        Exit(0);
    }
	if (!(buttons & PAD_L2) ||
	    !(buttons & (PAD_CROSS | PAD_CIRCLE)))
	{
		bStateHotkeyHeld = FALSE;
	}

	/* Release-build quick states, matching the recovered iaddis controls:
	     L2 + Cross  = save
	     L2 + Circle = load
	   L2+R2 remains exclusively the menu/SRAM shortcut below.  The latch
	   prevents the menu's key-repeat logic from writing a state repeatedly
	   when the combination is held. */
	if (!_bMenu &&
	    !bStateHotkeyHeld &&
	    (buttons & PAD_L2) &&
	    !(buttons & PAD_R2))
	{
		Bool bSaveTrigger =
			(buttons & PAD_CROSS) &&
			(trigger & (PAD_L2 | PAD_CROSS));
		Bool bLoadTrigger =
			(buttons & PAD_CIRCLE) &&
			(trigger & (PAD_L2 | PAD_CIRCLE));

		if (bSaveTrigger || bLoadTrigger)
		{
			bStateHotkeyHeld = TRUE;
			_MenuTriggerTimeout[0] = 0;
			_MenuTriggerTimeout[1] = 0;
			_MainLoopQuickStateAction(bSaveTrigger);
			return;
		}
	}

	/* The one-time destination prompt is intentionally isolated from the
	   regular L1/R1 menu ring and from the L2+R2 SRAM shortcut. */
	if (_bMenu &&
	    _MainLoop_pScreen == (CScreen *)_MainLoop_pStateDeviceScreen)
	{
		if (trigger & PAD_CROSS)
		{
			_MainLoopStateDevicePromptCancel();
		}
		else
		{
			_MainLoop_pStateDeviceScreen->Input(buttons, trigger);
		}
		return;
	}

	if (_bMenu &&
	    _MainLoop_pScreen ==
	            (CScreen *)_MainLoop_pMemCardFormatScreen)
	{
		if (trigger & PAD_CROSS)
		{
			_MainLoopMemCardFormatPromptCancel();
		}
		else
		{
			_MainLoop_pMemCardFormatScreen->Input(buttons, trigger);
		}
		return;
	}

	#if 1
	/* Profiler/deep diagnostic capture: R3 (right-stick click) OR hold L2+R2.
	   L2+R2 is easy to reach on the NetherSX2 touch layout (L3/R3 usually
	   aren't shown there) and isn't used by SNES/NES games. */
	if ((trigger & PAD_R3) ||
	    (((buttons & PAD_L2) && (buttons & PAD_R2)) && (trigger & (PAD_L2 | PAD_R2))))
	{
	        #if PROF_ENABLED
		ProfStartProfile(1);
	        #endif
		#if SNDBG_DEEP
		if (_pSystem == _pSnes)
			SnesDbgRequestCapture(SNDBG_CAPTURE_MANUAL);
		#endif
	}

    #ifdef DEBUG // CODE_DEBUG
	if (trigger & PAD_L2)
	{
        if (_WavFile.IsOpen())
        {
            _WavFile.Close();
            printf("WavOut Closed\n");
        } else
        {
        /*
            if (!_WavFile.Open(_pSnesWavFileName, 32000, 16, 2))
            {
                 printf("WavOut Open\n");
            }
            */
            if (!_WavFile.Open(_pSnesWavFileName, 48000, 16, 2))
            {
                 printf("WavOut Open\n");
            }

        }
	}
    #endif

    #ifdef DEBUG // CODE_DEBUG
	if (buttons & PAD_L2)
	{
        if (trigger&PAD_TRIANGLE)
		{
            _MainLoop_uDebugDisplay++;
            _MainLoop_uDebugDisplay&=3;
		}

        if (trigger&PAD_L3)
		{

	        // stop recording if we are recording
	        if (s_pMovieClip->IsRecording())
	        {
	            printf("Movie: Record End\n");
	            s_pMovieClip->RecordEnd();
	        } else
	        // stop playing if we are playing
	        if (s_pMovieClip->IsPlaying())
	        {
	            printf("Movie: Play End\n");
	            s_pMovieClip->PlayEnd();
	        } else

	        if (_pSystem)
	        {
	            s_pMovieClip->RecordBegin(_pSystem);
	            printf("Movie: Record Begin\n");
	        }
		}

        if (trigger&PAD_R3)
		{
	        // stop recording if we are recording
	        if (s_pMovieClip->IsRecording())
	        {
	            printf("Movie: Record End\n");
	            s_pMovieClip->RecordEnd();
	        }

	        // stop playing if we are playing
	        if (s_pMovieClip->IsPlaying())
	        {
	            printf("Movie: Play End\n");
	            s_pMovieClip->PlayEnd();
	        }
	        if (_pSystem)
	        {
	            s_pMovieClip->PlayBegin(_pSystem);
	            printf("Movie: Play Begin\n");
	        }
		}

    }

			/*
	if (buttons & PAD_R2)
	{
		if (buttons & PAD_SQUARE)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.i_mul-=0.1f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.i_mul+=0.1f;
        if (trigger&PAD_UP)
			_ColorCalib.i_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.i_add-=0.005f;
		}

		if (buttons & PAD_CROSS)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.q_mul-=0.01f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.q_mul+=0.01f;
        if (trigger&PAD_UP)
			_ColorCalib.q_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.q_add-=0.005f;
		}

		if (buttons & PAD_CIRCLE)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.y_mul-=0.01f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.y_mul+=0.01f;
        if (trigger&PAD_UP)
			_ColorCalib.y_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.y_add-=0.005f;
		}

		SNPPUBlendGS::ColorCalibrate(&_ColorCalib);
    }

			  */

    #endif

	if (trigger & PAD_L2)
	{
		_MenuTriggerTimeout[0]=5;
	}

	if (trigger & PAD_R2)
	{
		_MenuTriggerTimeout[1]=5;
	}

	if (_MenuTriggerTimeout[0] > 0)
	{
		if (trigger & PAD_R2)
		{
			_MenuTriggerTimeout[0] = 0;
			 // toggle menu
			 _MenuEnable(!_bMenu);
			 return;
		}
		_MenuTriggerTimeout[0]--;
	}

	if (_MenuTriggerTimeout[1] > 0)
	{
		if (trigger & PAD_L2)
		{
			_MenuTriggerTimeout[1] = 0;
			 // toggle menu
			 _MenuEnable(!_bMenu);
			 return;
		}
		_MenuTriggerTimeout[1]--;
	}

	if (_bMenu)
	{
		if (_MainLoop_pScreen)
		{
		    if (_MainLoop_pScreen ==
		            (CScreen *)_MainLoop_pStateBrowserScreen &&
		        (trigger & PAD_L1))
		    {
		        _MainLoopStateMenuRefresh();
		        _MainLoopSetScreen((CScreen *)_MainLoop_pStateScreen);
		        return;
		    }

		    /* L1 / R1 cycle through every available screen including
		       the message Log. The previous hand-written chain stopped
		       at Menu when going right and never reached Log when going
		       left, so the Log tab was effectively unreachable from the
		       UI. _MainLoopCycleScreen iterates Browser->State Manager
		       ->Network->Menu->Log->Video Config in either direction.
		       State Manager remains available while a game is paused. */
		    if (trigger & PAD_R1)
		    {
		        _MainLoopCycleScreen(+1);
		    } else
		    if (trigger & PAD_L1)
		    {
		        _MainLoopCycleScreen(-1);
		    } else
		    {
		        _MainLoop_pScreen->Input(buttons, trigger);
		    }
		}

	}
	else
	{

		if (buttons & PAD_L2)
		{
			if (trigger & PAD_SELECT)
			{
				_MainLoop_BlackScreen^=1;
			}
		}

	}
#endif
}
