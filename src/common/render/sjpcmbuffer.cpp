
#include <stdio.h>
#include "types.h"
#include "prof.h"
#include "mixbuffer.h"
#include "sjpcmbuffer.h"
#include <string.h>

extern "C" {
#include "sjpcm.h"
};

/* Defined in sjpcm_rpc.c. Writes to EE SIO so the line shows up in
   the emulator log alongside [snes-aud] enq#... entries. */
extern "C" void DLog(const char *fmt, ...);


SJPCMMixBuffer::SJPCMMixBuffer(Uint32 uSampleRate, Bool bAsync)
{
    m_iPrevSample[0]=0;
    m_iPrevSample[1]=0;
    m_nOutSamples = 0;
    m_uSampleRate = uSampleRate;
    m_bAsync      = bAsync;
}


void SJPCMMixBuffer::GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels)
{
	*puSampleRate = m_uSampleRate;
	*pnSampleBits = 16;
	*pnChannels   = 2;
}


Int32 SJPCMMixBuffer::GetOutputSamples()
{
    Int32 nSamples;
    Int32 nRaw;

    if (!SjPCM_IsInitialized())
    {
        static int __gos_ni = 0;
        if ((__gos_ni & 0x3F) == 0)
            DLog("[snes-aud] gos: not-init #%d", __gos_ni);
        __gos_ni++;
        return 0;
    }

    PROF_ENTER("SjPCM_Available");

    /*
     * Ask the audsrv backend how many sample-frames the IOP ring
     * buffer can accept RIGHT NOW.  This replaces the old formula
     *     nRaw = 4 * 800 - SjPCM_Buffered();
     * which assumed the ring buffer held exactly 3200 frames.
     * audsrv uses a 20480-byte (5120-frame) ring, and
     * audsrv_queued() can report a non-zero initial occupancy
     * even before any audio is enqueued, so the old formula
     * chronically under-produced audio (~424 samples/frame
     * instead of the ~533 needed at 32 kHz / 60 fps).
     *
     * SjPCM_Available() -> audsrv_available() / 4  gives the
     * real free space.  We cap at 3200 so a single frame never
     * tries to mix more than the old worst-case, keeping EE CPU
     * load bounded.
     */
    nRaw = SjPCM_Available();
    if (nRaw > 4 * 800) nRaw = 4 * 800;
    nRaw &= ~3;
    if (nRaw < 0) nRaw = 0;

    switch (m_uSampleRate)
    {
        case 48000: nSamples = nRaw;                break;
        case 32000: nSamples = (nRaw / 6) * 4;      break;
        case 24000: nSamples = (nRaw / 8) * 4;      break;
        default:    nSamples = 0;                   break;
    }

    {
        static int __gos = 0;
        if ((__gos & 0x3F) == 0)
            DLog("[snes-aud] gos f=%d sr=%u avail=%d out=%d async=%d",
                 __gos, (unsigned)m_uSampleRate,
                 (int)nRaw, (int)nSamples, (int)m_bAsync);
        __gos++;
    }

    PROF_LEAVE("SjPCM_Available");

    m_uLastOutput  = nSamples;
    return nSamples;
}

/*
 * Cubic Lagrange 2:3 up-sampler (32 kHz SNES -> 48 kHz SPU2).
 *
 * For each input pair [s_i, s_{i+1}] we emit 3 output samples at
 * fractional times 0, 2/3, 4/3 (in units of one 32 kHz sample):
 *
 *   y[3k+0] = s_{2k}                                       (passthrough)
 *   y[3k+1] = cubic_lagrange(s_{2k-1}, s_{2k},   s_{2k+1}, s_{2k+2}) @ 2/3
 *   y[3k+2] = cubic_lagrange(s_{2k},   s_{2k+1}, s_{2k+2}, s_{2k+3}) @ 1/3
 *
 * Cubic Lagrange phase 2/3 coefficients (scaled by 81):
 *   c = [-4, +30, +60, -5] / 81           (sum = 81)
 * Cubic Lagrange phase 1/3 coefficients (scaled by 81):
 *   c = [-5, +60, +30, -4] / 81           (sum = 81)
 *
 * Replaces the linear 2-tap interpolator that this function used to
 * carry. Linear interpolation has a sinc^2 frequency response, which
 * leaves significant spectral images above the input Nyquist (16 kHz)
 * and is responsible for the "weird / harsh / metallic" artefacts
 * that show up on SPC700-rendered audio with high-frequency content
 * (cymbals, brass, FM-style leads). Cubic Lagrange's response is much
 * closer to an ideal low-pass at f_s_in/2 and suppresses those images
 * by ~20 dB at f_s_in, while still being cheap enough to run on the
 * EE (6 mults + 6 adds per 3 output samples).
 *
 * State carried across calls is exactly one input sample (s_{-1})
 * per channel, stored in *pPrevSample. The very last pair in a chunk
 * doesn't have its full 4-tap lookahead window available yet (we
 * haven't asked the SPC engine for those samples), so we degrade
 * that pair to plain linear interpolation. With ~800 input samples
 * per video frame this affects at most 3 output samples per frame
 * out of ~1200 (~0.25%), which is inaudible.
 */
Int32 SJPCMMixBuffer::ConvertSamples2to3(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 *pPrevSample)
{
    Int32 hist = *pPrevSample;
    Int16 *pOutStart = pOut;
    Int32 i;

    if (nSamples < 2) return 0;

    /* Main path: cubic Lagrange. Requires 2 samples of lookahead
       beyond the current pair (s_{2k+2}, s_{2k+3}). */
    for (i = 0; i + 3 < nSamples; i += 2)
    {
        Int32 s0 = pIn[i];
        Int32 s1 = pIn[i + 1];
        Int32 s2 = pIn[i + 2];
        Int32 s3 = pIn[i + 3];
        Int32 y;

        /* phase 0 - passthrough */
        pOut[0] = (Int16)s0;

        /* phase 2/3 between s0 and s1, using [hist, s0, s1, s2] */
        y = -4 * hist + 30 * s0 + 60 * s1 - 5 * s2;
        y = (y >= 0 ? y + 40 : y - 40) / 81;
        if (y > 32767)  y = 32767;
        if (y < -32768) y = -32768;
        pOut[1] = (Int16)y;

        /* phase 1/3 (= 4/3 from s0) between s1 and s2, using [s0, s1, s2, s3] */
        y = -5 * s0 + 60 * s1 + 30 * s2 - 4 * s3;
        y = (y >= 0 ? y + 40 : y - 40) / 81;
        if (y > 32767)  y = 32767;
        if (y < -32768) y = -32768;
        pOut[2] = (Int16)y;

        hist = s1;
        pOut += 3;
    }

    /* Tail path: last pair(s) without the 4-tap lookahead window.
       Fall back to plain linear interpolation. */
    for (; i + 1 < nSamples; i += 2)
    {
        Int32 s0 = pIn[i];
        Int32 s1 = pIn[i + 1];
        Int32 s2 = (i + 2 < nSamples) ? pIn[i + 2] : s1;

        pOut[0] = (Int16)s0;
        pOut[1] = (Int16)((s0 + 2 * s1) / 3);
        pOut[2] = (Int16)((2 * s1 + s2) / 3);

        hist = s1;
        pOut += 3;
    }

    *pPrevSample = hist;
    return (Int32)(pOut - pOutStart);
}


Int32 SJPCMMixBuffer::ConvertSamplesStereo_32000(Int16 *pLeftSamples, Int16 *pRightSamples, Int16 *pOutLeft, Int16 *pOutRight, Int32 nInSamples)
{
    Int32 nOutSamples;

    if (nInSamples > SJPCMMIXBUFFER_MAXENQUEUE*2/3) nInSamples = SJPCMMIXBUFFER_MAXENQUEUE*2/3;

    PROF_ENTER("SjPCM_Convert");
    ConvertSamples2to3(pOutLeft, pLeftSamples, nInSamples, &m_iPrevSample[0]);
    nOutSamples=ConvertSamples2to3(pOutRight, pRightSamples, nInSamples, &m_iPrevSample[1]);
    PROF_LEAVE("SjPCM_Convert");

    return nOutSamples;
}

void SJPCMMixBuffer::OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples)
{
    Int16 *pOutLeft, *pOutRight;
    Int32 nOutSamples;

    // determine output space required (estimate)
    switch (m_uSampleRate)
    {
        case 24000:
            nOutSamples = nSamples * 2;
            break;
        case 32000:
            nOutSamples = nSamples * 6 / 4;
            break;
        default:
        case 48000:
            nOutSamples = nSamples;
            break;
    }

    {
        static int __oss = 0;
        if ((__oss & 0x3F) == 0)
            DLog("[snes-aud] oss f=%d in=%d est=%d acc=%d max=%d",
                 __oss, (int)nSamples, (int)nOutSamples,
                 (int)m_nOutSamples, (int)SJPCMMIXBUFFER_MAXENQUEUE);
        __oss++;
    }

    // check for buffer overflow 
    if ((m_nOutSamples + nOutSamples) > SJPCMMIXBUFFER_MAXENQUEUE)
    {
        return;
    }

    // buffer samples locally
    pOutLeft    = m_OutData[0] + m_nOutSamples;
    pOutRight   = m_OutData[1] + m_nOutSamples;

    switch(m_uSampleRate)
    {
        case 32000:
            m_nOutSamples += ConvertSamplesStereo_32000(pLeftSamples, pRightSamples, pOutLeft, pOutRight, nSamples);
            break;

        default:
        case 24000:
        case 48000:
            // leave data as is
            memcpy(pOutLeft, pLeftSamples, nSamples * sizeof(Int16));
            memcpy(pOutRight, pRightSamples, nSamples * sizeof(Int16));
            m_nOutSamples += nSamples;
            break;
    }
}

void SJPCMMixBuffer::Flush()
{
    Int32 nOutSamples;

    nOutSamples = m_nOutSamples;

    {
        static int __fc = 0;
        if ((__fc & 0x3F) == 0)
        {
            DLog("[snes-aud] flush f=%d nout=%d async=%d",
                 __fc, (int)nOutSamples, (int)m_bAsync);
        }
        __fc++;
    }

    if (nOutSamples > 0)
    {
        if (nOutSamples & 1)
        {
            // uh oh
            #if CODE_DEBUG
            printf("Sample count not even! %d\n", nOutSamples);
            #endif
            nOutSamples &= ~1;
        }

        if (nOutSamples > SJPCMMIXBUFFER_MAXENQUEUE)
        {
            // uh oh
            #if CODE_DEBUG
            printf("Sample buffer overflow! %d\n", nOutSamples);
            #endif
            nOutSamples = SJPCMMIXBUFFER_MAXENQUEUE;
        }


        if (m_bAsync)
        {
            SjPCM_EnqueueAsync(m_OutData[0], m_OutData[1], nOutSamples);
        } else
        {
            SjPCM_Enqueue(m_OutData[0], m_OutData[1], nOutSamples,1);
        }
    }

    m_nOutSamples = 0;
}



void SJPCMMixBuffer::OutputSamplesMono(Int16 *pSamples,Int32 nSamples)
{
    OutputSamplesStereo(pSamples, pSamples, nSamples);
}


