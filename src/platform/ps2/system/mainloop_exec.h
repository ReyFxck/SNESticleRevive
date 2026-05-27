#pragma once

#include "types.h"
#include "snes.h"
#include "rendersurface.h"
#include "mixbuffer.h"

Bool _ExecuteSnes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, Emu::SysInputT *pInput, Emu::System::ModeE eMode);
