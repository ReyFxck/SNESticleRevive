#pragma once

#include "types.h"

extern char *_MainLoop_NetConfigPaths[];

Bool _MainLoopConfigureNetwork(char **ppSearchPaths, char *pConfigFileName);
Bool _MainLoopInitNetwork(Char **ppSearchPaths);
