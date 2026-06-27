#pragma once

#include "types.h"

Int32 IOPLoadModule(const Char *pModuleName, Char **ppSearchPaths, int arglen, const char *pArgs);
extern Char _MainLoop_BootDir[256];
extern Char *_MainLoop_IOPModulePaths[];
/* Set TRUE by _MainLoopLoadModules() once the corresponding init call
   succeeded. Gate runtime audio/MCSave entry points on these so we
   don't call into an uninitialised RPC slot (which spins forever in
   SifCallRpc) when running on an emulator that couldn't load the
   custom IRX. */
extern Bool _MainLoop_bAudioReady;
extern Bool _MainLoop_bMCSaveReady;
void _MainLoopLoadModules(Char **ppSearchPaths);
