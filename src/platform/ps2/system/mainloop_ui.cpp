#include <stdio.h>
#include <stdarg.h>

#include "types.h"
#include "console.h"
#include "mainloop_ui.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"

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
 * Loga CADA modulo do IOP (e os passos que podem travar) -- facilita a
 * manutencao e o diagnostico no PS2 real.
 *
 * BootImport(name, ret): acumula o resultado de um modulo (OK se ret>=0).
 * BootImportFlush(): imprime a lista "[modulo] OK" / "[modulo] BAD (err=N)"
 *   + um veredito final "IOP imported: OK/BAD".
 * BootMark(label): marcador de etapa impresso NA HORA -- sobrevive a um
 *   travamento, entao a ultima linha na tela mostra onde o boot parou.
 *
 * Memory card agora e' carregado depois que a tela existe; USB/CDFS sao sob
 * demanda. A lista continua acumulada para que o flush apresente um unico
 * resumo de todos os modulos obrigatorios ao fim do boot. */
#define BOOT_MAXLOG 32
static const char *s_BootName[BOOT_MAXLOG];
static int         s_BootRet [BOOT_MAXLOG];
static int         s_BootN = 0;

extern "C" void BootImport(const char *pName, int ret)
{
	if (s_BootN < BOOT_MAXLOG)
	{
		s_BootName[s_BootN] = pName ? pName : "?";
		s_BootRet [s_BootN] = ret;
		s_BootN++;
	}
}

extern "C" void BootImportFlush(void)
{
	int i, nfail = 0;
	for (i = 0; i < s_BootN; i++)
	{
		if (s_BootRet[i] >= 0)
			ScrPrintf("[IOP] %-11s imported OK", s_BootName[i]);
		else
		{
			ScrPrintf("[IOP] %-11s imported BAD (err=%d)",
			          s_BootName[i], s_BootRet[i]);
			nfail++;
		}
	}
	ScrPrintf(nfail == 0 ? "[IOP] imported: OK" : "[IOP] imported: BAD");
}

extern "C" void BootMark(const char *pLabel)
{
	if (pLabel) ScrPrintf("%s", pLabel);
}
void _MainLoopSetScreen(CScreen *pScreen)
{
	_MainLoop_pScreen = pScreen;
}

static int _UIGetIdx(void)
{
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pBrowserScreen) return 0;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pStateScreen ||
        _MainLoop_pScreen == (CScreen*)_MainLoop_pStateBrowserScreen) return 1;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pNetworkScreen) return 2;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pMenuScreen)    return 3;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pLogScreen)     return 4;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pVideoScreen)   return 5;
    return 0;
}

static CScreen* _UIByIdx(int idx)
{
    switch (idx % 6)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pStateScreen;
        case 2: return (CScreen*)_MainLoop_pNetworkScreen;
        case 3: return (CScreen*)_MainLoop_pMenuScreen;
        case 4: return (CScreen*)_MainLoop_pLogScreen;
        case 5: return (CScreen*)_MainLoop_pVideoScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _UICycle(int dir)
{
    int idx = _UIGetIdx();
    for (int n = 0; n < 6; n++)
    {
        idx = (idx + dir + 6) % 6;
        CScreen *scr = _UIByIdx(idx);
        if (scr)
        {
            if (scr == (CScreen*)_MainLoop_pStateScreen)
            {
                _MainLoopStateMenuRefresh();
            }
            else if (scr == (CScreen*)_MainLoop_pBrowserScreen)
            {
                _MainLoop_pBrowserScreen->RefreshRootDevices();
            }
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
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pStateScreen ||
        _MainLoop_pScreen == (CScreen*)_MainLoop_pStateBrowserScreen) return 1;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pNetworkScreen) return 2;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pMenuScreen)    return 3;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pLogScreen)     return 4;
    if (_MainLoop_pScreen == (CScreen*)_MainLoop_pVideoScreen)   return 5;
    return 0;
}

static CScreen* _MainLoopGetScreenByIndex(int idx)
{
    switch (idx % 6)
    {
        case 0: return (CScreen*)_MainLoop_pBrowserScreen;
        case 1: return (CScreen*)_MainLoop_pStateScreen;
        case 2: return (CScreen*)_MainLoop_pNetworkScreen;
        case 3: return (CScreen*)_MainLoop_pMenuScreen;
        case 4: return (CScreen*)_MainLoop_pLogScreen;
        case 5: return (CScreen*)_MainLoop_pVideoScreen;
    }
    return (CScreen*)_MainLoop_pBrowserScreen;
}

void _MainLoopCycleScreen(int dir)
{
    int idx = _MainLoopGetScreenIndex();
    for (int n = 0; n < 6; n++)
    {
        idx = (idx + dir + 6) % 6;
        CScreen *scr = _MainLoopGetScreenByIndex(idx);
        if (scr)
        {
            if (scr == (CScreen*)_MainLoop_pStateScreen)
            {
                _MainLoopStateMenuRefresh();
            }
            else if (scr == (CScreen*)_MainLoop_pBrowserScreen)
            {
                _MainLoop_pBrowserScreen->RefreshRootDevices();
            }
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
