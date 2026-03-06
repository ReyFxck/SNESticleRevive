#include <stdio.h>
#include <string.h>

#include "types.h"
#include "console.h"
#include "file.h"
#include "mainloop_debug.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"

extern Char _MainLoop_BootDir[];

extern void ScrPrintf(const Char *pFormat, ...);

extern "C" {
#include "ps2ip.h"
#include "netplay_ee.h"
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

Bool _MainLoopInitNetwork(Char **ppSearchPaths)
{
    int ret;
	Bool bLoadedNetwork = FALSE;

	// attempt to load ps2ips (the ps2ip iop rpc server)
	// if it does load, then it means that ps2ip is already set up (from ps2link or some other loader)
	//      and we need not load ps2ip ourselves
    ret = IOPLoadModule("PS2IPS.IRX", ppSearchPaths, 0, NULL);
    if (ret < 0)
    {
		// load ps2ip modules
        IOPLoadModule("PS2IP.IRX", ppSearchPaths, 0, NULL);
        IOPLoadModule("PS2SMAP.IRX", ppSearchPaths, 0, NULL);

        ret = IOPLoadModule("PS2IPS.IRX", ppSearchPaths, 0, NULL);
		if (ret < 0)
		{
			// network not setup
			return FALSE;
		}

		bLoadedNetwork = TRUE;
    }

	// init ps2ip
#if 0	
    printf("ps2ip_Init()\n");
    ps2ip_init();
#endif
	// return TRUE if we initialized networking ourselves
	return bLoadedNetwork;
}
