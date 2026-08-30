/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements emusys behavior for the emulator application layer.
 */

#include "types.h"
#include "emusys.h"
using namespace Emu;

System::System()
{
    m_uFrame =0;
    m_uLine  =0;
}

System::~System()
{

}
