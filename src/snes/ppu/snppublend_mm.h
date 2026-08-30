/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snppublend mm interface for SNES picture processing.
 */

#ifndef _SNPPUBLEND_MM_H
#define _SNPPUBLEND_MM_H

#include "snppublend.h"
#include "snppublend_c.h"

class SNPPUBlendMM : public SNPPUBlendC
{
public:
    virtual void Exec(SNPPUBlendInfoT *pInfo, Int32 iLine, Uint32 uFixedColor32, SNMaskT *pColorMask, Bool bAddSub, Uint32 uIntensity);
};

#endif
