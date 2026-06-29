#pragma once

#include "types.h"

class CScreen;

#ifdef __cplusplus
extern "C" {
#endif

void MainLoopModalPrintf(Int32 Time, const Char *pFormat, ...);
void MainLoopStatusPrintf(Int32 Time, const Char *pFormat, ...);
void ScrPrintf(const Char *pFormat, ...);

/* Boot import log: resumo limpo "IOP imported: OK/BAD" no boot splash.
   BootImport(name, ret) guarda falha (ret<0); BootImportFlush() imprime. */
void BootImport(const char *pName, int ret);
void BootImportFlush(void);
void BootMark(const char *pLabel);

#ifdef __cplusplus
}
#endif

void _MainLoopSetScreen(CScreen *pScreen);
void _UICycle(int dir);
void _MainLoopCycleScreen(int dir);
void MainLoopShutdown();
