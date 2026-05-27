#include <tamtypes.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "netplay.h"
#include "netplay_ee.h"
#include "netserver.h"
#include "netclient.h"

static int netplay_inited = 0;
static NetSysSemaT _netplay_sema;
static NetSysSemaT _clientcallback_sema;

static NetServerT _server;
static NetClientT _client;

typedef void *(*NetPlayCallbackT)(NetPlayCallbackE eCallback, char *data, int size);
static NetPlayCallbackT _user_callback = NULL;

void NetPlayPuts(char *format, ...)
{
	static char buff[4096];
    va_list args;

	if(!netplay_inited) return;

    va_start(args, format);
    vsnprintf(buff, 4096, format, args);
    va_end(args);

    printf("NetPlay: %s", buff);
}

static int _ClientCallback(NetClientT *pClient, NetPlayCallbackE eMsg, void *arg)
{
    NetSysSemaWait(_clientcallback_sema);
    
    // For local EE, we can just invoke the callback directly
    if (_user_callback)
    {
        _user_callback(eMsg, (char*)arg, arg ? strlen((char*)arg) + 1 : 0);
    }
    
    NetSysSemaSignal(_clientcallback_sema);

	switch (eMsg)
	{
	case NETPLAY_CALLBACK_LOADGAME:
		printf("NetPlay Callback: LoadGame %s\n", (char *)arg);
		break;
	case NETPLAY_CALLBACK_UNLOADGAME:
		printf("NetPlay Callback: UnloadGame\n");
		break;
	case NETPLAY_CALLBACK_CONNECTED:
		printf("NetPlay Callback: Connected\n");
		break;
	case NETPLAY_CALLBACK_DISCONNECTED:
		printf("NetPlay Callback: Disconnected\n");
		break;
	case NETPLAY_CALLBACK_STARTGAME:
		printf("NetPlay Callback: StartGame\n");
		break;
    default:
        break;
	}
	return 0;
}

int NetPlayInit(void *pCallback)
{
    if (netplay_inited) return 0;

    _user_callback = (NetPlayCallbackT)pCallback;

    _netplay_sema = NetSysSemaNew(1);
    _clientcallback_sema = NetSysSemaNew(1);
    if (!_netplay_sema || !_clientcallback_sema) return -1;

    NetServerNew(&_server);
    NetClientNew(&_client);
    NetClientSetCallback(&_client, _ClientCallback);

    netplay_inited = 1;
    printf("NetPlay Initialized directly on EE!\n");
    return 0;
}

int NetPlayServerStart(int port, int latency)
{
	if(!netplay_inited) return -100;

    _server.uStartLatency = latency;
    return NetServerStart(&_server, port);
}

void NetPlayServerStop()
{
	if(!netplay_inited) return;

    NetServerStop(&_server);
}

int NetPlayClientConnect(unsigned ipaddr, int port)
{
	if(!netplay_inited) return -100;

    return NetClientConnect(&_client, ipaddr, port);
}

int NetPlayClientDisconnect()
{
	if(!netplay_inited) return -100;

    return NetClientDisconnect(&_client);
}

int NetPlayServerPingAll()
{
	if(!netplay_inited) return -100;

    NetServerSendText(&_server, "test");
    return 0;
}

int NetPlayGetStatus(NetPlayRPCStatusT *pStatus)
{
    int iPeer;
    memset(pStatus, 0, sizeof(*pStatus));
	if(!netplay_inited) return 0;

    NetSysSemaWait(_netplay_sema);

    pStatus->eClientStatus = _client.eStatus;
    pStatus->eGameState    = _client.eGameState;
    pStatus->eServerStatus = _server.eStatus;
    
    for (iPeer=0; iPeer < 4; iPeer++)
    {
        NetPlayRPCPeerStatusT *pPeerStatus = &pStatus->peer[iPeer];
        
        pPeerStatus->eStatus =    _client.Peers[iPeer].eStatus;
        pPeerStatus->eGameState = _client.Peers[iPeer].eGameState;
        pPeerStatus->ipaddr  = _client.Peers[iPeer].Addr.sin_addr;
        pPeerStatus->udpport = _client.Peers[iPeer].Addr.sin_port;
        
        pPeerStatus->InputSize  = NetQueueGetCount(&_client.Peers[iPeer].InputQueue);
        pPeerStatus->OutputSize = NetQueueGetCount(&_client.Peers[iPeer].OutputQueue);
    }

    NetSysSemaSignal(_netplay_sema);
    return 1;
}

void NetPlayClientSendLoadReq(char *pStr)
{
	if(!netplay_inited) return;
    
    NetClientSendLoadReq(&_client, pStr);
}

void NetPlayClientSendLoadAck(NetPlayLoadAckE eLoadAck)
{
	if(!netplay_inited) return;

    NetClientSendLoadAck(&_client, eLoadAck);
}

void NetPlayClientInput(NetPlayRPCInputT *pInput)
{
    pInput->eGameState = NETPLAY_GAMESTATE_PAUSE;

	if(!netplay_inited)
    {
        pInput->eGameState = NETPLAY_GAMESTATE_IDLE;
        return;
    }

	NetSysSemaWait(_netplay_sema);
    
    pInput->uFrame = _client.uFrame + 1;
    
    if (_client.eStatus == NETPLAY_STATUS_CONNECTED)
    {
        int iPeer;
    
		// transmit/recv input data now
		if (NetClientProcess(&_client, pInput->InputSend, NETPLAY_RPC_NUMPEERS, pInput->InputRecv))
		{
            pInput->eGameState = NETPLAY_GAMESTATE_PLAY;
		} else
		{
            pInput->eGameState = NETPLAY_GAMESTATE_PAUSE;
		}
        
        for (iPeer=0; iPeer < 4; iPeer++)
        {
            pInput->InputSize[iPeer]  = NetQueueGetCount(&_client.Peers[iPeer].InputQueue);
            pInput->OutputSize[iPeer] = NetQueueGetCount(&_client.Peers[iPeer].OutputQueue);
        }
    } else
    {
        pInput->eGameState = NETPLAY_GAMESTATE_IDLE;
    }

    NetSysSemaSignal(_netplay_sema);
}

/* No-op stub.  In the RPC-based implementation this was called every
   frame from src/platform/ps2/system/mainloop_process.cpp to drain
   the EE-side SifRpc server that received async callbacks from
   NETPLAY.IRX on the IOP.  With the whole protocol now running on
   the EE there is no RPC pump to drive -- NetSocket threads invoke
   the client callback synchronously from inside their read/write
   handlers and the user callback runs under _clientcallback_sema in
   _ClientCallback above.  Keep the symbol so mainloop_process.cpp
   doesn't have to be conditionally compiled. */
void NetPlayRPCProcess(void)
{
}

/* Stub for symmetry with the old NETPLAY.IRX-backed API.  Nothing in
   the EE-side codebase currently calls NetPlayShutdown(); the engine
   keeps the netplay state machine alive for the lifetime of the
   process.  Provided so anything declaring it through netplay_ee.h
   still links. */
void NetPlayShutdown(void)
{
}
