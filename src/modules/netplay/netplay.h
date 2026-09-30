/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the netplay interface for the emulator netplay frontend.
 */

#ifndef _NETPLAY_H
#define _NETPLAY_H

#include "types.h"

/* v0x101 transmits the full five-word frame input instead of silently
   truncating peripheral payload to one 32-bit controller pair.
   The server rejects 0x100 peers so old clients cannot decode v2 packets
   as corrupted ordinary controller input. */
#define NETPLAY_VERSION 0x101

#define NETPLAY_FRAME_PADS 5
typedef struct NetPlayFrameInput_t
{
    Uint16 uPad[NETPLAY_FRAME_PADS];
} NetPlayFrameInputT;

typedef enum
{
    NETPLAY_GAMESTATE_IDLE,
    NETPLAY_GAMESTATE_LOADING,
    NETPLAY_GAMESTATE_LOADED,
    NETPLAY_GAMESTATE_LOADERROR,
    NETPLAY_GAMESTATE_UNLOAD,

    NETPLAY_GAMESTATE_PLAY,
    NETPLAY_GAMESTATE_PAUSE,

} NetPlayGameStateE;

typedef enum
{
	NETPLAY_STATUS_IDLE,
	NETPLAY_STATUS_CONNECTING,
	NETPLAY_STATUS_CONNECTED
} NetPlayStatusE;

typedef enum
{
	NETPLAY_CALLBACK_NONE,
	NETPLAY_CALLBACK_CONNECTED,
	NETPLAY_CALLBACK_DISCONNECTED,
	NETPLAY_CALLBACK_LOADGAME,
	NETPLAY_CALLBACK_UNLOADGAME,
	NETPLAY_CALLBACK_STARTGAME,
	NETPLAY_CALLBACK_TEXTMSG,
} NetPlayCallbackE;

typedef enum
{
    NETPLAY_LOADACK_OK,
    NETPLAY_LOADACK_ERROR,
    NETPLAY_LOADACK_NOTFOUND,
    NETPLAY_LOADACK_CHECKSUM,
} NetPlayLoadAckE;

#endif
