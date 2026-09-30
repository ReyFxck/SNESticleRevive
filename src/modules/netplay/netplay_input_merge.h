/*
 * Deterministic 4-peer input merge for the PS2 front-end.
 *
 * Ordinary pads keep the historic mapping:
 *   peer[0..3].uPad[0] -> controller[0..3];
 *   if peers 2/3 are missing, fall back to the second pad of peers 0/1.
 *
 * A SNES special peripheral occupies a physical controller port and carries
 * its coordinates/buttons in the original five 16-bit words. The first peer
 * with a valid special tag (lowest peer ID) owns that device for the frame.
 * Its data must be copied atomically: otherwise bits from other peers would
 * replace its serial report and light-gun aim, desynchronizing the core.
 */
#ifndef _NETPLAY_INPUT_MERGE_H
#define _NETPLAY_INPUT_MERGE_H

#include "netplay.h"
#include "emuinput.h"

static inline Bool NetPlayIsSnesSpecial(Uint16 uTag)
{
    return uTag == EMUSYS_SNES_SPECIAL_MOUSE ||
           uTag == EMUSYS_SNES_SPECIAL_SUPERSCOPE ||
           uTag == EMUSYS_SNES_SPECIAL_JUSTIFIER ||
           uTag == EMUSYS_SNES_SPECIAL_JUSTIFIERS;
}

static inline void NetPlayMergeInputs(Emu::SysInputT *pOut,
    const NetPlayFrameInputT *pPeers, Bool bSnes)
{
    for (int i = 0; i < 4; i++)
        pOut->uPad[i] = pPeers[i].uPad[0];
    pOut->uPad[4] = EMUSYS_DEVICE_DISCONNECTED;

    if (pOut->uPad[2] == EMUSYS_DEVICE_DISCONNECTED)
        pOut->uPad[2] = pPeers[0].uPad[1];
    if (pOut->uPad[3] == EMUSYS_DEVICE_DISCONNECTED)
        pOut->uPad[3] = pPeers[1].uPad[1];

    if (!bSnes)
        return;

    for (int peer = 0; peer < 4; peer++)
    {
        Uint16 uTag = pPeers[peer].uPad[4];
        if (!NetPlayIsSnesSpecial(uTag))
            continue;

        if (uTag == EMUSYS_SNES_SPECIAL_MOUSE)
            pOut->uPad[0] = pPeers[peer].uPad[0];
        else
            pOut->uPad[1] = pPeers[peer].uPad[1];

        pOut->uPad[2] = pPeers[peer].uPad[2];
        pOut->uPad[3] = pPeers[peer].uPad[3];
        pOut->uPad[4] = uTag;
        break;
    }
}

#endif
