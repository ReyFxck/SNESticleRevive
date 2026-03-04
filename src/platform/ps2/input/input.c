#include <stdio.h>
#include <string.h>

#include "libpad.h"
#include "libxpad.h"
#include "libxmtap.h"
#include "types.h"
#include "input.h"
#include "hw.h"

static char _Input_PadBuf[INPUT_MAXPADS][256]
    __attribute__((aligned(64)))
    __attribute__((section(".bss")));

static Uint32 _Input_PadData[INPUT_MAXPADS];
static int    _Input_bPadConnected[INPUT_MAXPADS];
static Bool   _Input_bInitialized = FALSE;
static Bool   _Input_bXPad = FALSE;
static Int32  _Input_nPads = 0;

static Uint8 _Input_PadPort[INPUT_MAXPADS][2] =
{
    {0, 0},
    {1, 0},
    {1, 1},
    {1, 2},
    {1, 3},
};

static int _Input_GetPadState(int port, int slot)
{
    if (_Input_bXPad)
        return xpadGetState(port, slot);

    return padGetState(port, slot);
}

static void _Input_WaitPadReady(int port, int slot)
{
    int ret;

    do
    {
        ret = _Input_GetPadState(port, slot);
        WaitForNextVRstart(1);
    }
    while ((ret != PAD_STATE_STABLE) &&
           (ret != PAD_STATE_FINDCTP1) &&
           (ret != PAD_STATE_DISCONN));
}

static int _Input_InitPad(int port, int slot, void *buffer)
{
    int ret;

    if (_Input_bXPad)
    {
        ret = xpadPortOpen(port, slot, buffer);
        if (ret == 0)
        {
            printf("Failed to open xpad port=%d slot=%d\n", port, slot);
            return -1;
        }

        _Input_WaitPadReady(port, slot);
        xpadExitPressMode(port, slot);
        xpadSetMainMode(port, slot, PAD_MMODE_DIGITAL, PAD_MMODE_LOCK);
    }
    else
    {
        ret = padPortOpen(port, slot, buffer);
        if (ret == 0)
        {
            printf("Failed to open pad port=%d slot=%d\n", port, slot);
            return -1;
        }

        _Input_WaitPadReady(port, slot);
        padSetMainMode(port, slot, PAD_MMODE_DIGITAL, PAD_MMODE_LOCK);
    }

    _Input_WaitPadReady(port, slot);
    return 0;
}

Bool InputIsPadConnected(Uint32 uPad)
{
    if (uPad >= (Uint32)_Input_nPads)
        return FALSE;

    return _Input_bPadConnected[uPad] ? TRUE : FALSE;
}

Uint32 InputGetPadData(Uint32 uPad)
{
    if (!InputIsPadConnected(uPad))
        return 0;

    return _Input_PadData[uPad];
}

void InputInit(Bool bXLib)
{
    int iPad;

    _Input_bXPad = bXLib;
    _Input_nPads = bXLib ? INPUT_MAXPADS : 2;

    memset(_Input_PadData, 0, sizeof(_Input_PadData));
    memset(_Input_bPadConnected, 0, sizeof(_Input_bPadConnected));
    memset(_Input_PadBuf, 0, sizeof(_Input_PadBuf));

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        _Input_InitPad(_Input_PadPort[iPad][0],
                       _Input_PadPort[iPad][1],
                       _Input_PadBuf[iPad]);
    }

    _Input_bInitialized = TRUE;
}

void InputShutdown(void)
{
    int iPad;

    if (!_Input_bInitialized)
        return;

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        if (_Input_bXPad)
            xpadPortClose(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1]);
        else
            padPortClose(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1]);
    }

    memset(_Input_PadData, 0, sizeof(_Input_PadData));
    memset(_Input_bPadConnected, 0, sizeof(_Input_bPadConnected));

    _Input_nPads = 0;
    _Input_bInitialized = FALSE;
}

void InputPoll(void)
{
    int iPad;

    if (!_Input_bInitialized)
        return;

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        int state;
        Uint32 uData = 0;
        struct padButtonStatus padStatus;

        state = _Input_GetPadState(_Input_PadPort[iPad][0],
                                   _Input_PadPort[iPad][1]);

        if ((state == PAD_STATE_STABLE) || (state == PAD_STATE_FINDCTP1))
        {
            if (_Input_bPadConnected[iPad] == 0)
            {
                printf("Input: Pad %d inserted!\n", iPad + 1);
                WaitForNextVRstart(1);
            }

            _Input_bPadConnected[iPad] = 1;
        }
        else
        {
            if (_Input_bPadConnected[iPad] == 1)
                printf("Input: Pad %d removed!\n", iPad + 1);

            _Input_bPadConnected[iPad] = 0;
            _Input_PadData[iPad] = 0;
            continue;
        }

        memset(&padStatus, 0, sizeof(padStatus));

        if (_Input_bXPad)
            xpadRead(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1], &padStatus);
        else
            padRead(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1], &padStatus);

#ifdef _EE
        uData = 0xffffU ^ (((Uint32)padStatus.btns << 8) | (Uint32)padStatus.btns);
#else
        uData = 0;
#endif

#if 0
        if (padStatus.ljoy_h < (0x80 - 0x30)) uData |= PAD_LEFT;
        if (padStatus.ljoy_h > (0x80 + 0x30)) uData |= PAD_RIGHT;
        if (padStatus.ljoy_v < (0x80 - 0x30)) uData |= PAD_UP;
        if (padStatus.ljoy_v > (0x80 + 0x30)) uData |= PAD_DOWN;
#endif

        _Input_PadData[iPad] = uData;
    }
}
