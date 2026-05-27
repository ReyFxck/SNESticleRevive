/* gpfifo.c
 *
 * The original GPFifo was a hand-rolled double-buffered DMA queue
 * for GIF path-3 commands, with the body built incrementally via
 * gslist.c (GSGifTagOpenAD / GSGifRegAD / GSDmaCntOpen / ...).
 *
 * After the Fase 1 GS->gsKit migration, regular UI / font / poly
 * drawing goes through gsKit's own queue (see gpprim.c). The legacy
 * gslist mechanism is still required by the SNES blender
 * (snes/ppu/snppublend_gs.cpp), which builds raw GIF chains for its
 * Begin / End register writes. To keep the blender working without
 * touching it, we keep the old double-buffered gslist alive here and
 * simply make sure gsKit's DMA is drained any time we flip from the
 * gsKit queue to the legacy chain (or vice versa) on the GIF
 * channel.
 */

#include <stdio.h>

#include <tamtypes.h>
#include <kernel.h>

#include "types.h"
#include "gs.h"
#include "gslist.h"
#include "ps2dma.h"
#include "gpfifo.h"
#include "gskit_backend.h"

static Uint128 *_GPFifo_pLists[2];
static Uint32   _GPFifo_nListQwords;
static Uint32   _GPFifo_iCurList;
static Bool     _GPFifo_bInited = FALSE;

/* Kept for debugging the bridged gslist path. Marked unused so
   GCC stops warning about it; flip the call site in GPFifoPause to
   (re-)enable. */
static void _GPFifoDumpList(void) __attribute__((unused));
static void _GPFifoDumpList(void)
{
    Uint32 *pStart, *pEnd;

    pStart = (Uint32 *)GSListGetStart();
    pEnd   = (Uint32 *)GSListGetPtr();

    printf("GPFifo: List %08X -> %08X\n",
        (Uint32)pStart, (Uint32)pEnd);
}

void GPFifoFlush(void)
{
    GPFifoPause();
    GPFifoResume();
}

void GPFifoPause(void)
{
    if (!_GPFifo_bInited) {
        /* Nothing to flush - just make sure gsKit's queue is drained
           so the blender's raw DMA chain has the GIF channel free. */
        GSK_DrainAndWait();
        return;
    }

    /* Make sure gsKit isn't mid-transfer on the GIF channel. */
    GSK_DrainAndWait();

    /* close current dma cnt */
    GSDmaCntClose();

    /* add end tag */
    GSDmaEnd();

    /* end list */
    GSListEnd();

    /* flush cache */
    FlushCache(0);

    /* wait for previous dma to finish */
    DmaSyncGIF();

    /* transfer current list */
    DmaExecGIFChain(_GPFifo_pLists[_GPFifo_iCurList]);

    /* swap lists */
    _GPFifo_iCurList ^= 1;

    /* GS state has been touched outside gsKit; force a TEXFLUSH on
       the next gsKit textured prim. */
    GSK_InvalidateTextureCache();
}

void GPFifoResume(void)
{
    if (!_GPFifo_bInited) {
        return;
    }

    /* start new list */
    GSListBegin(_GPFifo_pLists[_GPFifo_iCurList],
                _GPFifo_nListQwords,
                (GSListFlushFuncT)GPFifoFlush);

    /* open a dma cnt */
    GSDmaCntOpen();
}

void GPFifoSync(void)
{
    DmaSyncGIF();
    GSK_DrainAndWait();
}

Uint64 *GPFifoOpen(Uint32 nMinQwords)
{
    if (!_GPFifo_bInited) {
        return NULL;
    }

    if (GSListSpace(nMinQwords)) {
        return (Uint64 *)GSListGetPtr();
    }
    return NULL;
}

void GPFifoClose(Uint64 *pPtr)
{
    if (!_GPFifo_bInited) {
        return;
    }
    GSListSetPtr((Uint128 *)pPtr);
}

void GPFifoInit(Uint128 *pMem, Int32 nBytes)
{
    assert(!(((Uint32)pMem) & 0xF));

    /* calculate size of each list */
    _GPFifo_nListQwords = (nBytes / sizeof(Uint128)) / 2;

    /* set address of each list */
    _GPFifo_pLists[0] = pMem;
    _GPFifo_pLists[1] = pMem + _GPFifo_nListQwords;

    _GPFifo_iCurList = 0;
    _GPFifo_bInited  = TRUE;

    GPFifoResume();

    printf("GPFifo: Init %08X %08X (%d qwords) [gsKit-bridge]\n",
        (Uint32)_GPFifo_pLists[0],
        (Uint32)_GPFifo_pLists[1],
        _GPFifo_nListQwords);
}

void GPFifoShutdown(void)
{
    printf("GPFifo: Shutdown\n");
}
