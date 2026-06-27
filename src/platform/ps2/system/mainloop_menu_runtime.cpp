/* mainloop_menu_runtime.cpp
 *
 * Hosts the runtime menu helpers used by MainLoopRender() and the input
 * path:
 *
 *   - _MenuEnable() : toggle the in-game menu, flushing SRAM to memcard
 *                     when the menu is brought up.
 *   - _MenuDraw()   : per-frame menu overlay (called from
 *                     MainLoopRender()).
 *
 * _MenuDraw() used to be a file-static helper inside mainloop.cpp.
 * After the Batch 3 split it has external linkage so MainLoopRender()
 * (now in mainloop_render.cpp) can still reach it through the
 * declaration in mainloop_shared.h.
 *
 * Extracted from mainloop.cpp during the Batch 3 split. Behaviour and
 * the surrounding `#if MAINLOOP_MEMCARD` / `#if 0` gating are unchanged.
 */

#include <stdio.h>
#include <string.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "mainloop_iop.h"

#include "types.h"
#include "console.h"
#include "font.h"
#include "poly.h"
#include "memcard.h"
#include "uiScreen.h"

extern "C" {
#include "ps2ip.h"
};

extern "C" {
#include "mcsave_ee.h"
};

extern "C" {
#include "sjpcm.h"
};


/* MAINLOOP_MEMCARD lives in mainloop_shared.h (included above) and
   gates the memcard SRAM / save-file path inside _MenuEnable(). */


void _MenuEnable(Bool bEnable)
{
	if (bEnable!=_bMenu)
	{
		/* Mute audsrv BEFORE the SRAM save block below runs.
		   MainLoopModalPrintf() (see mainloop_ui.cpp) spins
		   MainLoopRender() inline for N frames and MCSave_WriteSync()
		   also blocks the EE for the duration of the memcard write,
		   so the SNES core is starved of CPU and stops feeding
		   audsrv. The IOP-side play_thread keeps running, drains the
		   legitimate ~107 ms tail, and then re-reads stale samples
		   from the same ring buffer region — audible as a drone of
		   the last SNES audio for the full duration of the
		   "Saving SRAM..." / "SRAM saved.\n" / "Error Saving SRAM!\n"
		   modals (~1 - 1.5 s). Muting here, before any of those
		   blocking calls, silences both the legitimate tail and the
		   drone. The matching unmute on menu exit is at the bottom
		   of this function. See the unmute comment for why we mute
		   via SjPCM_Setvol rather than SjPCM_Clearbuff. */
		if (bEnable && _MainLoop_bSjPCMReady)
		{
			SjPCM_Setvol(0);
		}

		// if menu is enabled, then attempt to save sram immediately
		if (bEnable)
		{
            #if 1 
			/* Force a fresh dirty-flag check before deciding to save.
			   _MainLoopCheckSRAM() throttles its full-SRAM checksum
			   to once every ~30 frames (~0.5s) since its only job
			   here is to keep _MainLoop_SRAMUpdated current for this
			   exact decision -- without the force-check, an SRAM
			   write that the game performed in the same 30-frame
			   window as the user's L2+R2 press could leave
			   _MainLoop_SRAMUpdated still FALSE and skip the save
			   the user explicitly requested. */
			_MainLoopForceCheckSRAM();

			if (_MainLoopHasSRAM() && _MainLoop_SRAMUpdated)
			{
			   	MainLoopModalPrintf(10, "Saving SRAM...");

			    if (_MainLoopHasSRAM())
			    {
					#if MAINLOOP_MEMCARD
					if (_MainLoop_bMCSaveReady) MCSave_WriteSync(1, NULL);

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

		/* Restore audsrv output when leaving the menu. The entry-side
		   mute happens at the top of this function (above the SRAM
		   save block) so the modal-printf loops there also play
		   silent; we only handle the leave side here.

		   We mute via SjPCM_Setvol(0) rather than SjPCM_Clearbuff
		   (audsrv_stop_audio) because the latter sets audsrv's
		   `playing` flag to 0, which freezes the IOP-side readpos
		   and writepos and prevents audsrv_available() from ever
		   advancing. The next SjPCM_Enqueue(...,wait=1) issued by
		   SJPCMMixBuffer::Flush after the menu is dismissed then
		   deadlocks inside audsrv_wait_audio, which is what made the
		   audio stay dead until an emulator reset. Volume mute keeps
		   audsrv in its normal playing state: the queue keeps
		   draining, audsrv_wait_audio stays unblocked, and audio
		   resumes the moment the SNES core writes new samples after
		   the menu is closed. 0x3FFF is full scale in the 14-bit
		   SjPCM_Setvol scale (rescaled to audsrv's MAX_VOLUME). Gated
		   on _MainLoop_bSjPCMReady for symmetry with the boot
		   sequence in mainloop_init.cpp. */
		if (!bEnable && _MainLoop_bSjPCMReady)
		{
			SjPCM_Setvol(0x3FFF);
		}
	}
}




void _MenuDraw()
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

    /* Status bar (green): compiler version on the left, IP in the
       middle, app version right-aligned. Replaces the #if 0 block
       above which depended on VersionGetInfo (also #if 0). */
    FontPrintf(8, vy, "GCC%d.%d", __GNUC__, __GNUC_MINOR__);

    FontPrintf(48,vy,"IP: %d.%d.%d.%d", 
            (config.ipaddr.s_addr >> 0) & 0xFF,
            (config.ipaddr.s_addr >> 8) & 0xFF,
            (config.ipaddr.s_addr >>16) & 0xFF,
            (config.ipaddr.s_addr >>24) & 0xFF
                    );

    static const char *_AppVersionStr = "SNESticle Revive v1.0.0";
    FontPuts(256 - 16 - FontGetStrWidth(_AppVersionStr),
             vy, _AppVersionStr);



	FontSelect(0);
}
