#include <string.h>

#include "types.h"
#include "path.h"
#include "uiBrowser.h"
#include "mainloop_browser.h"
#include "mainloop_net.h"
#include "mainloop_ui.h"

extern Bool _MainLoopExecuteFile(const Char *pFileName, Bool bLoadSRAM);
extern void _MenuEnable(Bool bEnable);

int _MainLoopBrowserEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        switch (Type)
        {
                case 1:
                {
                        Char *str = (Char *)Parm2;
                        NetPlayRPCStatusT status;
                        NetPlayGetStatus(&status);

                        if (status.eClientStatus == NETPLAY_STATUS_CONNECTED)
                        {
                                NetPlayClientSendLoadReq(str);
                        }
                        else
                        {
                                // load rom with sram load
                                if (_MainLoopExecuteFile(str, TRUE))
                                {
                                        _MenuEnable(FALSE);
                                }
                                else
                                {
                                        MainLoopModalPrintf(60*1, "ERROR: %s\n", str);
                                }
                        }
                        return 1;
                }

                case 2:
                {
                        char str[256];
                        char *pName = (char *)Parm2;
                        PathExtTypeE eType;

                        strcpy(str, pName);

                        // figure out what type of file this is
                        if (PathExtResolve(str, &eType, TRUE))
                        {
                                return BROWSER_ENTRYTYPE_EXECUTABLE;
                        }

                        return BROWSER_ENTRYTYPE_OTHER;
                }
        }
        return 0;
}
