/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the netplay ee interface for the emulator netplay frontend.
 */

#ifndef _NETPLAY_RPCCLIENT_H
#define _NETPLAY_RPCCLIENT_H

#include "netplay.h"
#include "netplay_rpc.h"

int NetPlayInit(void *pCallback);
void NetPlayShutdown();
void NetPlayPuts(char *format, ...);

int NetPlayServerStart(int port, int latency);
void NetPlayServerStop();

int NetPlayClientConnect(unsigned ipaddr, int port);
int NetPlayClientDisconnect();
int NetPlayGetStatus(NetPlayRPCStatusT *pStatus);

void NetPlayClientSendLoadReq(char *pStr);
void NetPlayClientSendLoadAck(NetPlayLoadAckE eLoadAck);
void NetPlayClientInput(NetPlayRPCInputT *pInput);
int NetPlayServerPingAll();

void NetPlayRPCProcess();

#endif
