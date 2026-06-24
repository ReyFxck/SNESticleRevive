#include <stdio.h>
#include <string.h>

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
#include <netman.h>
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
	// 
	printf("netconfigload: %s\n", pConfigPath);
	return FALSE;
}

Bool _MainLoopConfigureNetwork(char **ppSearchPaths, char *pConfigFileName)
{
    t_ip_info config;

	// reset ip configuration
    memset(&config, 0, sizeof(config));

	strcpy(config.netif_name, "sm1");

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

// set configuration
	ps2ip_setconfig(&config);

	if (ps2ip_getconfig(config.netif_name,&config))
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
 *   1. SifExecModuleBuffer ps2dev9 / netman / smap            \
 *      via NetIfLoadEmbeddedIrx (src/platform/ps2/system/      | network IRX
 *      embedded_irx.cpp).                                      | stack
 *   2. NetManInit() on the EE side -- registers our SIF RPC    /
 *      bindings so netman can talk to us once smap is up.
 *   3. SifExecModuleBuffer ps2ip -- happens inside step 1.
 *   4. ip4_addr_set_zero on IP/NM/GW so ps2ipInit() starts up
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
Bool _MainLoopInitNetwork(Char **ppSearchPaths)
{
    struct ip4_addr IP, NM, GW;
    int ret;

    (void)ppSearchPaths;

    ret = NetIfLoadEmbeddedIrx();
    if (ret < 0)
    {
        // printf("[boot] NetIfLoadEmbeddedIrx failed (%d) - no network\n", ret);
        return FALSE;
    }

    /* NetManInit() lives in libnetman.a and registers the EE side
       of the netman SIF RPC.  netman.irx queues link-up events
       until the EE binds to it; if NetManInit() is skipped, smap
       loads but never delivers frames to ps2ip and every ps2ip
       socket call later times out. */
    NetManInit();

    /* Bring up lwIP with a no-address netif so the caller can
       drive DHCP / static IP through ps2ip_setconfig() in
       _MainLoopConfigureNetwork below. */
    ip4_addr_set_zero(&IP);
    ip4_addr_set_zero(&NM);
    ip4_addr_set_zero(&GW);

    // printf("[boot] ps2ipInit\n");
    ps2ipInit(&IP, &NM, &GW);
    // printf("[boot] ps2ipInit done\n");

    return TRUE;
}
