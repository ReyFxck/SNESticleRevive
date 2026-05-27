/* mainloop.cpp
 *
 * After the Batch 3 split, the live PS2 main-loop code that used to
 * sit in this file has been broken out by responsibility:
 *
 *   - mainloop_globals.cpp       : definitions of every cross-file
 *                                  symbol declared in mainloop_shared.h
 *   - mainloop_init.cpp          : MainLoopInit()
 *   - mainloop_render.cpp        : MainLoopRender()
 *   - mainloop_process.cpp       : MainLoopProcess()
 *   - mainloop_menu_runtime.cpp  : _MenuEnable() and _MenuDraw()
 *   - global_alloc.cpp           : project replacements for
 *                                  operator new / operator delete
 *
 * Nothing in this file is compiled today. What remains below is the
 * historical, never-compiled `#if 0` reference code from the original
 * SNESticle source -- including the legacy NES code path and a couple
 * of stub helpers that were preserved during the SNES revival but
 * never re-enabled. It is kept here intentionally so a future
 * maintainer can find it next to the rest of the SNESticle archaeology
 * instead of having to dig it out of git history.
 */


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

#if 0
static NesStateT _NesTestState[3];
#endif

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
