/*
 * Netplay v0x101 frame codec.  One frame is five exact 16-bit words, matching
 * SysInputT without depending on the SNES app's C++ namespace.
 *
 * The wire format retains the original frame/ack sequence numbers, but each
 * repeated input sample is now a 16-byte record.  Decode the complete packet
 * before enqueuing anything: malformed UDP must not partially advance a
 * deterministic input queue.
 */
#ifndef _NETINPUT_CODEC_H
#define _NETINPUT_CODEC_H

#include <string.h>
#include "types.h"
#include "netpacket.h"

static Uint32 NetInputEncodeRuns(NetPacketInputRunT *pRuns,
    Uint32 nMaxRuns, const NetPlayFrameInputT *pInput, Uint32 nInput,
    Uint32 *pConsumed)
{
    Uint32 uRun = 0;
    Uint32 uPos = 0;
    while (uPos < nInput && uRun < nMaxRuns)
    {
        Uint32 uLength = 1;
        while (uPos + uLength < nInput &&
               !memcmp(&pInput[uPos], &pInput[uPos + uLength],
                       sizeof(NetPlayFrameInputT)))
        {
            uLength++;
        }
        pRuns[uRun].uLength = uLength;
        pRuns[uRun].Input = pInput[uPos];
        pRuns[uRun].uReserved = 0;
        uRun++;
        uPos += uLength;
    }
    if (pConsumed)
        *pConsumed = uPos;
    return uRun;
}

static Bool NetInputDecodeRuns(NetPlayFrameInputT *pOutput,
    Uint32 nCapacity, const NetPacketInputRunT *pRuns, Uint32 nRuns,
    Uint32 *pDecoded)
{
    Uint32 uRun, uCount = 0;
    if (pDecoded)
        *pDecoded = 0;

    if (nRuns > NETPACKET_INPUT_RUNS_MAX)
        return FALSE;

    /* First validate every run; no partial writes on malformed packets. */
    for (uRun = 0; uRun < nRuns; uRun++)
    {
        Uint32 n = pRuns[uRun].uLength;
        if (!n || n > nCapacity - uCount ||
            pRuns[uRun].uReserved != 0)
            return FALSE;
        uCount += n;
    }
    for (uRun = 0, uCount = 0; uRun < nRuns; uRun++)
    {
        Uint32 uLen = pRuns[uRun].uLength;
        while (uLen--)
            pOutput[uCount++] = pRuns[uRun].Input;
    }
    if (pDecoded)
        *pDecoded = uCount;
    return TRUE;
}

#endif
