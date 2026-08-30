/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop net behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "console.h"
#include "file.h"
#include "mainloop_debug.h"
#include "mainloop_iop.h"
#include "mainloop_load.h"
#include "mainloop_net.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop.h"
#include "embedded_irx.h"

extern "C" {
#include "ps2ip.h"
#include "netplay_ee.h"
}

/* MAINLOOP_NETPORT lives in mainloop_shared.h (included above). */

int _MainLoopNetworkEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
    NetPlayRPCStatusT status;
	switch (Type)
	{
		case 1:
            printf("Connecting to %08X\n", Parm1);
            NetPlayClientConnect(Parm1, MAINLOOP_NETPORT);
			break;
		case 2:
            NetPlayGetStatus(&status);
            if (status.eServerStatus == NETPLAY_STATUS_IDLE)
            {
               NetPlayServerStart(MAINLOOP_NETPORT, Parm1);
               NetPlayClientConnect(0x0100007F, MAINLOOP_NETPORT);
           }
           else
           NetPlayServerStop();
			break;
		case 3:
            NetPlayGetStatus(&status);
            if (status.eClientStatus == NETPLAY_STATUS_IDLE)
            {
				return 1;
            } else
            {
                NetPlayClientDisconnect();
				return 0;
            }
			break;
	}

	return 0;
}

void *_MainLoopNetCallback(NetPlayCallbackE eCallback, char *data, int size)
{
    switch (eCallback)
    {
        case NETPLAY_CALLBACK_NONE:
            break;

        case NETPLAY_CALLBACK_CONNECTED:
            printf("NetClientEE: Connected\n");
            break;

        case NETPLAY_CALLBACK_DISCONNECTED:
            printf("NetClientEE: Disconnected\n");
            break;

        case NETPLAY_CALLBACK_LOADGAME:
            {
                Bool result = FALSE;

                printf("NetClientEE: Loading the netgame %s\n", data);
                if (size > 0)
                {
                    //  load here (no-sram)
					result = _MainLoopExecuteFile(data, FALSE);
                }

                if (!result)
                {
                    NetPlayClientSendLoadAck(NETPLAY_LOADACK_ERROR);
                }  else
                {
                    NetPlayClientSendLoadAck(NETPLAY_LOADACK_OK);
                }
            }
            break;

        case NETPLAY_CALLBACK_UNLOADGAME:
            printf("NetClientEE: Unloading the netgame\n");
            _MainLoopUnloadRom();
            break;

        case NETPLAY_CALLBACK_STARTGAME:
            printf("NetClientEE: Starting the netgame\n");
            _MenuEnable(FALSE);
            break;

        default:
            printf("NetClientEE: Callback %d\n", eCallback);
            break;

    }
	return NULL;
}

char *_MainLoop_NetConfigPaths[]=
{
	(char *)"mc0:/SNESticle/",
	_MainLoop_BootDir,
    NULL
};

static Bool _MainLoopLoadNetConfig(t_ip_info *pConfig, const char *pConfigPath)
{
	printf("netconfigload: %s\n", pConfigPath);
	return FALSE;
}

Bool _MainLoopConfigureNetwork(char **ppSearchPaths, char *pConfigFileName)
{
    t_ip_info config;

	// reset ip configuration
    memset(&config, 0, sizeof(config));

	/* Modern PS2SDK SMAP registers as sm0. The original iaddis tree used
	   sm1 with an older stack; keeping that name makes setconfig silently
	   miss the only interface and DHCP never starts. */
	strcpy(config.netif_name, "sm0");

	// setup default config to have dhcp enabled
	config.dhcp_enabled = 1;
	config.ipaddr.s_addr = 0;
	config.netmask.s_addr = 0;
	config.gw.s_addr = 0;

	// go through all search paths
	while (*ppSearchPaths!=NULL)
	{
		if (strlen(*ppSearchPaths) > 0)
		{
		    char Path[1024];

	sprintf(Path, "%s%s", *ppSearchPaths, pConfigFileName);

			// attempt to load configuration information
			if (_MainLoopLoadNetConfig(&config, Path))
			{
				// loaded!
				break;
			}
		}
		ppSearchPaths++;
	}

	/* Apply DHCP/static configuration to the actual SMAP interface. */
	/* This PS2SDK API returns 1 on success and 0 when sm0 was not found. */
	if (ps2ip_setconfig(&config) <= 0)
		return FALSE;

	if (ps2ip_getconfig(config.netif_name,&config) > 0)
	{
		// print info about network configuration
		printf("%08X %08X %08X %d\n", config.ipaddr.s_addr, config.netmask.s_addr, config.gw.s_addr, config.dhcp_enabled);
	}

	return TRUE;
}

/* Modern netman + ps2ip + lwIP bring-up, mirroring
 * hugorsgarcia/PS2SNESticle/SNESticle/Source/ps2/mainloop.cpp::
 * _MainLoopInitNetwork.
 *
 * Sequence:
 *   1. SifExecModuleBuffer ps2dev9 / netman, NetManInit, smap \
 *      via NetIfLoadEmbeddedIrx (src/platform/ps2/system/      | network IRX
 *      embedded_irx.cpp).                                      | stack
 *   2. SifExecModuleBuffer ps2ip -- happens inside step 1.
 *   3. ip4_addr_set_zero on IP/NM/GW so ps2ipInit() starts up
 *      with a no-IP netif we can re-configure later via
 *      ps2ip_setconfig() (which _MainLoopConfigureNetwork does
 *      from `ipconfig.dat` or a hard-coded DHCP default).
 *
 * The `ppSearchPaths` argument used to pass host: / cdrom: /
 * mc0: hints to IOPLoadModule when the IRXs lived on disk; with
 * the bin2c'd images embedded in the ELF those hints are no
 * longer needed.  We keep the argument so callers don't have to
 * change, but otherwise ignore it.
 *
 * Returns TRUE when the whole stack came up.  On failure (no
 * Network Adapter, dev9 not present, ...) returns FALSE and the
 * caller (mainloop_iop.cpp::_MainLoopLoadModules) skips the
 * netplay init that depends on the IP stack being live.
 */
static int s_network_init_result = 1; /* 1=not attempted, 0=ready, -1=failed */

Bool _MainLoopInitNetwork(Char **ppSearchPaths)
{
    struct ip4_addr IP, NM, GW;
    int ret;

    (void)ppSearchPaths;

    if (s_network_init_result != 1)
        return s_network_init_result == 0 ? TRUE : FALSE;

    ret = NetIfLoadEmbeddedIrx();
    if (ret < 0)
    {
        s_network_init_result = -1;
        return FALSE;
    }

    /* Bring up lwIP with a no-address netif so the caller can
       drive DHCP / static IP through ps2ip_setconfig() in
       _MainLoopConfigureNetwork below. */
    ip4_addr_set_zero(&IP);
    ip4_addr_set_zero(&NM);
    ip4_addr_set_zero(&GW);

    ret = ps2ipInit(&IP, &NM, &GW);

    if (ret < 0)
    {
        printf("_MainLoopInitNetwork: ps2ipInit failed (%d)\n", ret);
        s_network_init_result = -1;
        return FALSE;
    }

    s_network_init_result = 0;
    return TRUE;
}

/* Wait only after the user explicitly opens a network feature. Network is
 * never touched during boot. A finite timeout avoids reproducing the old
 * black-screen hang when no cable or DHCP server is present. */
Bool _MainLoopWaitForNetwork(Int32 timeoutMs)
{
    t_ip_info config;
    Int32 elapsed = 0;

    if (timeoutMs < 0)
        timeoutMs = 0;

    while (elapsed <= timeoutMs)
    {
        memset(&config, 0, sizeof(config));
        if (ps2ip_getconfig((char *)"sm0", &config) > 0 &&
            config.ipaddr.s_addr != 0 &&
            (!config.dhcp_enabled ||
             config.dhcp_status == DHCP_STATE_BOUND ||
             config.dhcp_status == DHCP_STATE_OFF))
        {
            printf("Network ready: ip=%08X dhcp=%u\n",
                   config.ipaddr.s_addr, config.dhcp_status);
            return TRUE;
        }

        if (elapsed == timeoutMs)
            break;
        usleep(100000);
        elapsed += 100;
        if (elapsed > timeoutMs)
            elapsed = timeoutMs;
    }

    printf("Network timeout after %d ms\n", timeoutMs);
    return FALSE;
}
