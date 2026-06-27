#include <string.h>

#include "types.h"
#include "path.h"
#include "pathext.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
#include "mainloop_net.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop_bgm.h"

extern "C" {
#include "audio.h"
}

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
                                /* Antes do load (que bloqueia a EE por mais
                                   tempo que o ring de ~107ms do audsrv): para
                                   e MUTA a trilha de menu.  Senao a cauda da
                                   musica fica "picotando" (underrun) durante o
                                   carregamento da ROM.  O _MenuEnable(FALSE)
                                   abaixo restaura o volume com a ROM ja'
                                   carregada; no erro, restauramos na mao. */
                                BgmStop();
                                if (Aud_IsInitialized()) Aud_Setvol(0);

                                // load rom with sram load
                                if (_MainLoopExecuteFile(str, TRUE))
                                {
                                        _MenuEnable(FALSE);
                                }
                                else
                                {
                                        if (Aud_IsInitialized()) Aud_Setvol(0x3FFF);
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
