/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements audmixbuffer behavior for shared rendering and audio buffers.
 */

#include <stdio.h>
#include "types.h"
#include "prof.h"
#include "mixbuffer.h"
#include "audmixbuffer.h"
#include "audframeschedule.h"
#include <string.h>

extern "C" {
#include "audio.h"
};

/* Game Volume 100 preserves the mixer PCM. A 200% software boost clipped
   valid loud mixes before audsrv, and also processed every output sample
   at the default setting. Lower settings attenuate; listening-volume
   amplification belongs after the emulator's PCM, not in the SNES DSP. */
#define AUDMIXBUFFER_BASE_GAIN_PCT 100

static int s_gameVolume = 100;   /* 0..100 (Video Config); 100 = base gain */

extern "C" void AudMixGameSetVolume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    s_gameVolume = vol;
}

extern "C" int AudMixGameGetVolume(void)
{
    return s_gameVolume;
}

AudMixBuffer::AudMixBuffer(Uint32 uSampleRate, Bool bAsync)
{
    m_uSampleRate = uSampleRate;
    m_uFrameRate  = 60;
    m_bAsync      = bAsync;
    Reset();
}

void AudMixBuffer::Reset()
{
    m_iPrevSample[0] = 0;
    m_iPrevSample[1] = 0;
    m_nOutSamples = 0;
    m_nResamplePending = 0;
    memset(m_ResamplePending, 0, sizeof(m_ResamplePending));
    m_uLastOutput = 0;
    m_uFrameSamplePhase = 0;
    memset(m_OutData, 0, sizeof(m_OutData));
}

void AudMixBuffer::GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels)
{
	*puSampleRate = m_uSampleRate;
	*pnSampleBits = 16;
	*pnChannels   = 2;
}

Int32 AudMixBuffer::GetOutputSamples()
{
    Int32 nSamples;

    if (!Aud_IsInitialized())
    {
        return 0;
    }

    /*
     * Audio acompanha TEMPO EMULADO, nao o espaco livre do ring do IOP.
     * Consultar Aud_Available() fazia um quadro lento encontrar o ring mais
     * vazio e misturar amostras extras, criando feedback positivo.
     *
     * Distribua a taxa de audio por QUADROS DO CONSOLE. NTSC usa 60 e PAL
     * usa 50; o host PS2 pode estar no padrao oposto e isso nao deve mudar
     * pitch/tempo. Em 32 kHz temos 532/532/536 no NTSC e 640 amostras
     * exatas por quadro PAL. O main loop decide apenas quais quadros chegam
     * ao display quando fonte e host usam cadencias diferentes.
     */
    nSamples = AudFrameScheduleNext(&m_uFrameSamplePhase,
                                    m_uSampleRate, m_uFrameRate, 4);

    m_uLastOutput  = nSamples;
    return nSamples;
}

/* Cubic Lagrange 32 -> 48 kHz, at phases 0, 2/3 and 4/3 per input
   pair. Keep the two lookahead samples rather than changing the filter at
   each caller's boundary. This adds at most three source samples of delay
   and makes the PCM independent of input chunk size, including odd chunks. */
static void AudConvertPair(Int16 *pOut, Int32 hist, const Int16 *pIn)
{
    Int32 s0 = pIn[0], s1 = pIn[1], s2 = pIn[2], s3 = pIn[3];
    Int32 y;
    pOut[0] = (Int16)s0;
    y = -4 * hist + 30 * s0 + 60 * s1 - 5 * s2;
    y = (y >= 0 ? y + 40 : y - 40) / 81;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    pOut[1] = (Int16)y;
    y = -5 * s0 + 60 * s1 + 30 * s2 - 4 * s3;
    y = (y >= 0 ? y + 40 : y - 40) / 81;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    pOut[2] = (Int16)y;
}

Int32 AudMixBuffer::ConvertSamples2to3(Int16 *pOut, Int16 *pIn,
    Int32 nSamples, Int32 *pPrevSample, Int16 *pPending, Int32 *pPendingCount)
{
    Int16 *pOutStart = pOut;
    Int32 hist = *pPrevSample;
    Int32 pending = *pPendingCount;
    Int32 consumed = 0;

    /* Only boundary pairs use this staging path. As soon as the pending
       pair comes entirely from pIn, process contiguous input directly. */
    while (pending)
    {
        while (pending < 4 && consumed < nSamples)
            pPending[pending++] = pIn[consumed++];
        if (pending < 4)
        {
            *pPendingCount = pending;
            *pPrevSample = hist;
            return (Int32)(pOut - pOutStart);
        }
        AudConvertPair(pOut, hist, pPending);
        pOut += 3;
        hist = pPending[1];
        pPending[0] = pPending[2];
        pPending[1] = pPending[3];
        pending = 2;
        if (consumed >= 2)
        {
            consumed -= 2;
            pending = 0;
        }
    }
    for (; consumed + 3 < nSamples; consumed += 2)
    {
        AudConvertPair(pOut, hist, pIn + consumed);
        hist = pIn[consumed + 1];
        pOut += 3;
    }
    while (consumed < nSamples)
        pPending[pending++] = pIn[consumed++];
    *pPendingCount = pending;
    *pPrevSample = hist;
    return (Int32)(pOut - pOutStart);
}

Int32 AudMixBuffer::ConvertSamplesStereo_32000(Int16 *pLeftSamples, Int16 *pRightSamples, Int16 *pOutLeft, Int16 *pOutRight, Int32 nInSamples)
{
    Int32 leftPending = m_nResamplePending;
    Int32 rightPending = m_nResamplePending;
    PROF_ENTER("Aud_Convert");
    ConvertSamples2to3(pOutLeft, pLeftSamples, nInSamples,
        &m_iPrevSample[0], m_ResamplePending[0], &leftPending);
    Int32 nOutSamples = ConvertSamples2to3(pOutRight, pRightSamples, nInSamples,
        &m_iPrevSample[1], m_ResamplePending[1], &rightPending);
    m_nResamplePending = rightPending;
    PROF_LEAVE("Aud_Convert");
    return nOutSamples;
}

void AudMixBuffer::OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples)
{
    while (nSamples > 0)
    {
        Int32 freeSamples = AUDMIXBUFFER_MAXENQUEUE - m_nOutSamples;
        /* With <=3 saved source samples, every two additional samples
           produce at most three output frames. Reserve whole pairs. */
        Int32 maxInput = m_uSampleRate == 32000 ? (freeSamples / 3) * 2 : freeSamples;
        if (!maxInput)
        {
            Flush();
            continue;
        }
        Int32 count = nSamples < maxInput ? nSamples : maxInput;
        Int16 *pOutLeft = m_OutData[0] + m_nOutSamples;
        Int16 *pOutRight = m_OutData[1] + m_nOutSamples;
        if (m_uSampleRate == 32000)
            m_nOutSamples += ConvertSamplesStereo_32000(
                pLeftSamples, pRightSamples, pOutLeft, pOutRight, count);
        else
        {
            memcpy(pOutLeft, pLeftSamples, count * sizeof(Int16));
            memcpy(pOutRight, pRightSamples, count * sizeof(Int16));
            m_nOutSamples += count;
        }
        pLeftSamples += count;
        pRightSamples += count;
        nSamples -= count;
    }
}

void AudMixBuffer::Flush()
{
    Int32 nOutSamples;

    nOutSamples = m_nOutSamples;

    if (nOutSamples > 0)
    {
        /* Apply volume after resampling for every sample-rate path.
           Game Volume 100 is unity and skips the complete gain pass. */
        {
            Int32 gainPct = (s_gameVolume * AUDMIXBUFFER_BASE_GAIN_PCT) / 100;
            if (gainPct != 100)
            {
                Int32 ch, i;
                for (ch = 0; ch < 2; ch++)
                {
                    Int16 *p = m_OutData[ch];
                    for (i = 0; i < nOutSamples; i++)
                    {
                        Int32 v = ((Int32)p[i] * gainPct) / 100;
                        if (v >  32767) v =  32767;
                        if (v < -32768) v = -32768;
                        p[i] = (Int16)v;
                    }
                }
            }
        }

        if (m_bAsync)
        {
            Aud_EnqueueAsync(m_OutData[0], m_OutData[1], nOutSamples);
        } else
        {
            Aud_Enqueue(m_OutData[0], m_OutData[1], nOutSamples,1);
        }
    }

    m_nOutSamples = 0;
}

void AudMixBuffer::OutputSamplesMono(Int16 *pSamples,Int32 nSamples)
{
    OutputSamplesStereo(pSamples, pSamples, nSamples);
}
