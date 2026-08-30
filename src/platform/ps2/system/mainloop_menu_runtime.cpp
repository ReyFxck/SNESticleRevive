/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop menu runtime behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <string.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "mainloop_iop.h"

#include "types.h"
#include "console.h"
#include "font.h"
#include "poly.h"
#include "memcard.h"
#include "uiScreen.h"
#include "mainloop_bgm.h"

extern "C" {
#include "audio.h"
};

/* The L2+R2 path must return control immediately. The old implementation did
   all memory-card work plus a fixed 60-frame success modal before setting
   _bMenu, which made the shortcut look frozen. Schedule the write a couple of
   already-visible menu frames later; BgmIO keeps the tracker alive during the
   still-synchronous device operation. */
static Bool s_sramSavePending = FALSE;
static Int32 s_sramSaveDelay = 0;

static void _MenuSavePendingSRAM(void)
{
	Bool bSaved;

	if (!s_sramSavePending)
		return;
	s_sramSavePending = FALSE;

	BgmIOBegin();
	#if MAINLOOP_MEMCARD
	if (MemCardGetStatus(0) == MEMCARD_STATUS_UNFORMATTED)
	{
		BgmIOEnd();
		_MainLoopMemCardFormatPromptOpen(
			0,
			MAINLOOP_MEMCARDFORMAT_SRAM_SAVE
		);
		return;
	}
	#endif

	bSaved = _MainLoopSaveSRAM(TRUE);
	BgmIOEnd();
	MainLoopStatusPrintf(
		bSaved ? 90 : 180,
		bSaved ? "SRAM saved." : "Error saving SRAM!"
	);
}

void _MenuRuntimeUpdate(void)
{
	if (!_bMenu || !s_sramSavePending)
		return;
	if (s_sramSaveDelay > 0)
	{
		s_sramSaveDelay--;
		return;
	}
	_MenuSavePendingSRAM();
}

void _MenuEnable(Bool bEnable)
{
	if (bEnable!=_bMenu)
	{
		if (bEnable)
		{
			/* Publish the menu state before any storage RPC. MainLoopProcess
			   will render two frames, then run the pending save below. */
			_bMenu = TRUE;
			BgmMenuEnter();
			if (_MainLoop_bAudioReady)
				Aud_Setvol(0);

			/* Preserve a write performed in the <30-frame checksum window. */
			_MainLoopForceCheckSRAM();
			if (_MainLoopHasSRAM() && _MainLoop_SRAMUpdated)
			{
				s_sramSavePending = TRUE;
				s_sramSaveDelay = 2;
				MainLoopStatusPrintf(180, "Saving SRAM...");
			}
		}
		else
		{
			/* Normal input cannot close the menu again before the two-frame
			   delay expires. Clear defensively if another subsystem launches a
			   game directly while a save was queued for the previous ROM. */
			s_sramSavePending = FALSE;
			s_sramSaveDelay = 0;
			_bMenu = FALSE;
			BgmStop();
			if (_MainLoop_bAudioReady)
				Aud_Setvol(0x3FFF);
		}
	}
}

void _MenuDraw()
{
	FontSelect(0);

	PolyTexture(NULL);
    PolyBlend(TRUE);

	// draw current screen
	if (_MainLoop_pScreen)
	{
		_MainLoop_pScreen->Draw();
	}

	/* Restore a distinct lower status area using the same dark teal as
	   the original iaddis title bars. Drawing it after the screen also
	   guarantees that a browser row can never paint over the footer. */
	const int footerY = 211;
	const int vy = 215;
	PolyTexture(NULL);
	PolyBlend(TRUE);
	PolyColor4f(0.0f, 0.2f, 0.2f, 0.9f);
	PolyRect(0, footerY, 256, 224 - footerY);

	FontSelect(2);
	FontColor4f(0.2, 0.6f, 0.2f, 1.0f);

    /* Status bar (green): compiler version on the left and app version
       right-aligned. Network details already live on the Host settings
       screen, so the redundant IP field no longer consumes this row. */
    FontPrintf(8, vy, "GCC%d.%d", __GNUC__, __GNUC_MINOR__);

#ifdef APP_VERSION
    static const char *_AppVersionStr =
        "SNESticle Revive PS2 v" APP_VERSION;
#else
    static const char *_AppVersionStr = "SNESticle Revive PS2 v1.0.7";
#endif
    FontPuts(256 - 16 - FontGetStrWidth(_AppVersionStr),
             vy, _AppVersionStr);

	FontSelect(0);
}
