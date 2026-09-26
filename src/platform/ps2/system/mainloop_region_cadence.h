/*
 * Host-testable 50/60 Hz presentation cadence converter.
 */
#ifndef _MAINLOOP_REGION_CADENCE_H
#define _MAINLOOP_REGION_CADENCE_H
#include "types.h"

typedef struct
{
    Uint32 phase;
    Uint32 sourceHz;
    Uint32 hostHz;
} MainLoopRegionCadenceT;

_INLINE void MainLoopRegionCadenceReset(MainLoopRegionCadenceT *pState)
{
    if (!pState) return;
    pState->phase = 0;
    pState->sourceHz = 0;
    pState->hostHz = 0;
}

_INLINE void MainLoopRegionCadenceStep(
    MainLoopRegionCadenceT *pState, Bool bAllowed,
    Uint32 uSourceHz, Uint32 uHostHz,
    Uint32 *puExtraHidden, Bool *pbHoldPrevious)
{
    Uint32 uDiff;
    if (!puExtraHidden || !pbHoldPrevious) return;
    *puExtraHidden = 0;
    *pbHoldPrevious = FALSE;

    if (!pState || !bAllowed || !uSourceHz || !uHostHz || uSourceHz == uHostHz)
    {
        MainLoopRegionCadenceReset(pState);
        return;
    }

    if (pState->sourceHz != uSourceHz || pState->hostHz != uHostHz)
    {
        pState->phase = 0;
        pState->sourceHz = uSourceHz;
        pState->hostHz = uHostHz;
    }

    uDiff = (uSourceHz > uHostHz) ? (uSourceHz - uHostHz) : (uHostHz - uSourceHz);
    pState->phase += uDiff;
    if (pState->phase >= uHostHz)
    {
        Uint32 uEvents = pState->phase / uHostHz;
        pState->phase %= uHostHz;
        if (uSourceHz > uHostHz) *puExtraHidden = uEvents;
        else *pbHoldPrevious = TRUE;
    }
}
#endif
