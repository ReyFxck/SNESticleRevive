/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements emumovie behavior for the emulator application layer.
 */

#include "types.h"
#include "emumovie.h"
using namespace Emu;

MovieClip::MovieClip(Uint32 uStateSize, Uint32 uMaxFrames)
{
    m_uStateSize    = 0;
    m_uMaxStateSize = uStateSize;
    m_pStateData    = (void *) malloc(uStateSize);

    m_pFrames    = (SysInputT *)malloc(sizeof(SysInputT) * uMaxFrames);
    m_uMaxFrames = uMaxFrames;

    m_bRecording        = FALSE;
    m_nRecordedFrames   = 0;

    m_bPlaying          = FALSE;
    m_uPlayFrame        = 0;
}

MovieClip::~MovieClip()
{
    free(m_pStateData);
}

void MovieClip::RecordBegin(System *pSystem)
{
    assert(!IsRecording());
    assert(!IsPlaying());

    m_uStateSize = pSystem->GetStateSize();
    assert(m_uStateSize <= m_uMaxStateSize);

    // save the state
    pSystem->SaveState(m_pStateData, m_uStateSize);

    // reset recorded pointer
    m_nRecordedFrames = 0;
    m_bRecording      = TRUE;
}

void MovieClip::RecordEnd()
{
    assert(IsRecording());

    m_bRecording      = FALSE;
}

Bool MovieClip::RecordFrame(SysInputT &input)
{
    if (m_nRecordedFrames < m_uMaxFrames)
    {
        m_pFrames[m_nRecordedFrames] = input;
        m_nRecordedFrames++;
        return TRUE;
    }
    return FALSE;
}

void MovieClip::PlayBegin(System *pSystem)
{
    assert(!IsRecording());
    assert(!IsPlaying());
    assert(m_uStateSize > 0);

    // restore the state
    pSystem->RestoreState(m_pStateData, m_uStateSize);

    // reset Played pointer
    m_uPlayFrame     = 0;
    m_bPlaying      = TRUE;
}
void MovieClip::PlayEnd()
{
    assert(IsPlaying());

    m_bPlaying      = FALSE;
}

Bool MovieClip::PlayFrame(SysInputT &input)
{
    if (m_uPlayFrame < m_nRecordedFrames)
    {
        input = m_pFrames[m_uPlayFrame];
        m_uPlayFrame++;
        return TRUE;
    }
    return FALSE;
}
