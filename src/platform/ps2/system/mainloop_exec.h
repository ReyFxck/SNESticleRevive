/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop exec interface for the PlayStation 2 application runtime.
 */

#pragma once

#include "types.h"
#include "snes.h"
#include "rendersurface.h"
#include "mixbuffer.h"

Bool _ExecuteSnes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, Emu::SysInputT *pInput, Emu::System::ModeE eMode);
