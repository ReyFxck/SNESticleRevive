#include <stdio.h>
#include <stdarg.h>

#include "types.h"
#include "console.h"
#include "mainloop_ui.h"

#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"
#include "poly.h"
#include "../../../Gep/Include/ps2/font.h"

extern void MainLoopRender();

extern CBrowserScreen *_MainLoop_pBrowserScreen;
extern CNetworkScreen *_MainLoop_pNetworkScreen;
extern CMenuScreen *_MainLoop_pMenuScreen;
extern CLogScreen *_MainLoop_pLogScreen;
extern CScreen *_MainLoop_pScreen;

extern Bool _bMenu;
extern Char _MainLoop_ModalStr[256];
extern Int32 _MainLoop_ModalCount;
extern Char _MainLoop_StatusStr[256];
extern Int32 _MainLoop_StatusCount;

void MainLoopModalPrintf(Int32 Time, const Char *pFormat, ...)
{
	va_list argptr;
	va_start(argptr,pFormat);
	vsprintf(_MainLoop_ModalStr, pFormat, argptr);
	va_end(argptr);

	_MainLoop_ModalCount = Time;

	// render frame to display text
	while (Time > 0)
	{
		MainLoopRender();
		Time--;
	}
}

void MainLoopStatusPrintf(Int32 Time, const Char *pFormat, ...)
{
	va_list argptr;
	va_start(argptr,pFormat);
	vsprintf(_MainLoop_StatusStr, pFormat, argptr);
	va_end(argptr);

	_MainLoop_StatusCount = Time;
}

void ScrPrintf(const Char *pFormat, ...)
{
	va_list argptr;
	char str[256];

	va_start(argptr,pFormat);
	vsprintf(str, pFormat, argptr);
	va_end(argptr);

//	scr_printf("%s", str);
	if (_MainLoop_pLogScreen)
		_MainLoop_pLogScreen->AddMessage(str);

	// render frame to display text
	MainLoopRender();
}

void _MainLoopSetScreen(CScreen *pScreen)
{
	_MainLoop_pScreen = pScreen;
}

static int _UIGetIdx(void)
{
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pBrowserScreen) return 0;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pNetworkScreen) return 1;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pMenuScreen)    return 2;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pLogScreen)     return 3;
    return 0;
}

static CScreen* _UIByIdx(int idx)
{
    switch (idx & 3)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pNetworkScreen;
        case 2: return (CScreen*)_MainLoop_pMenuScreen;
        case 3: return (CScreen*)_MainLoop_pLogScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _UICycle(int dir)
{
    int idx = _UIGetIdx();
    for (int n = 0; n < 4; n++)
    {
        idx = (idx + dir + 4) & 3;
        CScreen *scr = _UIByIdx(idx);
        if (scr)
        {
            _MainLoopSetScreen(scr);
            _bMenu = TRUE;
            ConPrint("UI: screen=%d (L1/R1)\n", idx);
            return;
        }
    }
}

static int _MainLoopGetScreenIndex(void)
{
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pBrowserScreen) return 0;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pNetworkScreen) return 1;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pMenuScreen)    return 2;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pLogScreen)     return 3;
    return 0;
}

static CScreen* _MainLoopGetScreenByIndex(int idx)
{
    switch (idx & 3)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pNetworkScreen;
        case 2: return (CScreen*)_MainLoop_pMenuScreen;
        case 3: return (CScreen*)_MainLoop_pLogScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _MainLoopCycleScreen(int dir)
{
    int idx = _MainLoopGetScreenIndex();
    for (int n = 0; n < 4; n++)
    {
        idx = (idx + dir + 4) & 3;
        CScreen *scr = _MainLoopGetScreenByIndex(idx);
        if (scr)
        {
            _MainLoopSetScreen(scr);
            _bMenu = TRUE;
            return;
        }
    }
}

void MainLoopShutdown()
{
    FontShutdown();
    PolyShutdown();
}
