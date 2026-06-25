#include "types.h"
#include <stdio.h>
#include <string.h>
#include <libpad.h>

#include "mainloop_input.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "mainloop_shared.h"
#include "mainloop.h"
#include "input.h"
#include "memcard.h"
#include "prof.h"


#define MENU_REPEAT (16)

//#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE)
#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE|PAD_CROSS|PAD_TRIANGLE|PAD_LEFT|PAD_RIGHT)


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
	if (pad & (PAD_R2|PAD_L2))
    {
        return 0;
    }
#if 0
    if (_pSystem==_pSnes)
    {
        return _MainLoopSnesInput(pad);
    } else
    if (_pSystem==_pNes)
    {
        return _MainLoopNesInput(pad);
    }
	   return 0;
#else
 	return _MainLoopSnesInput(pad);
#endif
 
}

void _MainLoopInputProcess(Uint32 buttons)
{
	static Uint32 lastbuttons= ~0;
	static Uint32 repeat=0;
	Uint32 trigger;


	if (!(buttons& MENU_REPEATBUTTONS))
	{
		repeat=0;
	}

	// 
	repeat++;
	if (repeat > MENU_REPEAT)
	{
		repeat -= 1;
		lastbuttons &= ~MENU_REPEATBUTTONS;
	}

	trigger = ((buttons ^ lastbuttons) & buttons);
	lastbuttons = buttons;

#if 1
	/* Profiler capture: R3 (right-stick click) OR hold L2+R2 together.
	   L2+R2 is easy to reach on the NetherSX2 touch layout (L3/R3 usually
	   aren't shown there) and isn't used by SNES/NES games or the menu.
	   Only does anything in a PROFILE=1 build. */
	if ((trigger & PAD_R3) ||
	    (((buttons & PAD_L2) && (buttons & PAD_R2)) && (trigger & (PAD_L2 | PAD_R2))))
	{
        #if PROF_ENABLED
		ProfStartProfile(1);
        #endif
//		BMPWriteFile("/pc/mnt/c/out.bmp", &_fbTexture[0]);
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
//		BMPWriteFile("/pc/mnt/c/out.bmp", &_fbTexture[0]);
	}
    #endif


    #ifdef DEBUG // CODE_DEBUG
	if (buttons & PAD_L2)
	{
        if (trigger&PAD_CROSS)
            _MainLoopSaveState();
        if (trigger&PAD_CIRCLE)
            _MainLoopLoadState();
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


	#if 0
	if (
	 	((trigger & PAD_R2) && (buttons & PAD_L2)) ||
	 	((trigger & PAD_L2) && (buttons & PAD_R2)) 
	   )
	{
		// toggle menu
		 _MenuEnable(!_bMenu);
		 return;
	}
	#endif

	static int _MenuTriggerTimeout[2] = {0,0};

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
		    /* L1 / R1 cycle through every available screen including
		       the message Log. The previous hand-written chain stopped
		       at Menu when going right and never reached Log when going
		       left, so the Log tab was effectively unreachable from the
		       UI. _MainLoopCycleScreen iterates Browser->Network->Menu
		       ->Log in either direction and skips screens that aren't
		       constructed. */
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

#if 0
		// perform cheesy non-deterministic disk switching
		if (trigger & (PAD_R1|PAD_L1) )
		{
			if (_pNesFDSDisk->IsLoaded())
			{
				if (_MainLoop_bDiskInserted)
				{
					// eject disk!
					_MainLoop_bDiskInserted = FALSE;
					_pNes->GetMMU()->InsertDisk(-1);

					MainLoopStatusPrintf(60, "NESFDS Disk Ejected");

					// pick next disk
					if (trigger & PAD_R1)
						_MainLoop_iDisk++;
					else
						_MainLoop_iDisk--;
				} else
				{
					// wrap the number of disks
					if (_MainLoop_iDisk < 0)
					{
						_MainLoop_iDisk = _pNesFDSDisk->GetNumDisks()-1;
					}

					if (_MainLoop_iDisk >= _pNesFDSDisk->GetNumDisks())
					{
						_MainLoop_iDisk = 0;
					}
					// insert disk
					_pNes->GetMMU()->InsertDisk(_MainLoop_iDisk);
					_MainLoop_bDiskInserted = TRUE;


					MainLoopStatusPrintf(60, "NESFDS Disk %d Inserted", _MainLoop_iDisk);
				}
			}
		}
#endif

	}
#endif
}
