#include <stdio.h>
#include <stdarg.h>

#include "types.h"
#include "console.h"
#include "mainloop_ui.h"
#include "mainloop_shared.h"

#include "poly.h"
#include "font.h"

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

extern "C" void ScrPrintf(const Char *pFormat, ...)
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

/* ---- Boot import log -------------------------------------------------
 * Resumo limpo dos imports de modulo do IOP, no lugar do spam linha-a-linha.
 * BootImport(name, ret): silencioso em sucesso (ret>=0); guarda a falha se
 * ret<0.  BootImportFlush(): imprime "IOP imported: OK" ou "IOP imported:
 * BAD" seguido de uma linha "[modulo] err=N" por falha.  Os nomes devem
 * ser literais (guardamos o ponteiro direto). */
#define BOOT_MAXFAIL 12
static const char *s_BootFailName[BOOT_MAXFAIL];
static int         s_BootFailRet [BOOT_MAXFAIL];
static int         s_BootNFail = 0;

extern "C" void BootImport(const char *pName, int ret)
{
	if (ret >= 0) return;
	if (s_BootNFail < BOOT_MAXFAIL)
	{
		s_BootFailName[s_BootNFail] = pName ? pName : "?";
		s_BootFailRet [s_BootNFail] = ret;
		s_BootNFail++;
	}
}

extern "C" void BootImportFlush(void)
{
	int i;
	if (s_BootNFail == 0)
	{
		ScrPrintf("IOP imported: OK");
		return;
	}
	ScrPrintf("IOP imported: BAD");
	for (i = 0; i < s_BootNFail; i++)
		ScrPrintf("  [%s] err=%d", s_BootFailName[i], s_BootFailRet[i]);
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
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pVideoScreen)   return 4;
    return 0;
}

static CScreen* _UIByIdx(int idx)
{
    switch (idx % 5)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pNetworkScreen;
        case 2: return (CScreen*)_MainLoop_pMenuScreen;
        case 3: return (CScreen*)_MainLoop_pLogScreen;
        case 4: return (CScreen*)_MainLoop_pVideoScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _UICycle(int dir)
{
    int idx = _UIGetIdx();
    for (int n = 0; n < 5; n++)
    {
        idx = (idx + dir + 5) % 5;
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
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pVideoScreen)   return 4;
    return 0;
}

static CScreen* _MainLoopGetScreenByIndex(int idx)
{
    switch (idx % 5)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pNetworkScreen;
        case 2: return (CScreen*)_MainLoop_pMenuScreen;
        case 3: return (CScreen*)_MainLoop_pLogScreen;
        case 4: return (CScreen*)_MainLoop_pVideoScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _MainLoopCycleScreen(int dir)
{
    int idx = _MainLoopGetScreenIndex();
    for (int n = 0; n < 5; n++)
    {
        idx = (idx + dir + 5) % 5;
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
