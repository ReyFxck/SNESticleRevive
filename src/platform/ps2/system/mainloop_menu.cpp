#include <stdio.h>

#include "types.h"
#include "mainloop_install.h"
#include "mainloop_menu.h"
#include "mainloop_shared.h"

extern "C" int list_title_db(char *pPath);

int _MainLoopMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        switch (Type)
        {
                case 1:
                        {
                                char mc0[1024];
                                char mc1[1024];
                                char exploit_dir[256];
                                char **ppInstallFiles = _MainLoop_pInstallFiles;

                                _GetExploitDir(exploit_dir);

                                snprintf(mc0, sizeof(mc0), "mc0:/%s", exploit_dir);
                                snprintf(mc1, sizeof(mc1), "mc1:/%s", exploit_dir);

                                // hack in default destination name for elf
                                ppInstallFiles[0] = (char *)"BOOT.ELF"; // dest
                                switch (Parm1)
                                {
#if 0
                                        case 0:
                                                // cdrom->mc0
                                                ppInstallFiles[1] = VersionGetElfName(); // src
                                                InstallFiles(mc0, "cdrom0:\\", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 1:
                                                ppInstallFiles[1] = VersionGetElfName(); // src
                                                InstallFiles(mc1, "cdrom0:\\", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
#endif
                                        case 2:
                                                ppInstallFiles[1] = (char *)"SNESTICLE.ELF"; // src
                                                InstallFiles(mc0, (char *)"host:", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 3:
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles(mc1, mc0, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 4:
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles(mc0, mc1, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 5:
                                                ppInstallFiles[0] = (char *)"SNESTICLE.ELF"; // dest
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles((char *)"host:", mc0, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 6:
                                                _DumpMemory();
                                                break;
                                        case 7:
                                                snprintf(mc0, sizeof(mc0), "mc0:/%s/TITLE.DB", exploit_dir);
                                                _AddTitleDB(mc0);
                                                break;
                                        case 8:
                                                snprintf(mc0, sizeof(mc0), "mc0:/%s/TITLE.DB", exploit_dir);
                                                list_title_db(mc0);
                                                break;
                                        case 9: // copy rom0:libsd -> host
                                                CopyFile((char *)"host:LIBSD.IRX", (char *)"rom0:LIBSD", NULL);
                                                break;
                                        default:
                                                return 0;
                                }
                                _MainLoop_pMenuScreen->SetText(0, "");
                                _MainLoop_pMenuScreen->SetText(1, "");
                                _MainLoop_pMenuScreen->SetText(2, "");
                        }
                        break;
        }

        return 0;
}


int _MainLoopLogEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        return 0;
}


const char *_MainLoopMenuEntries[]=
{
        (char *)"Copy cdrom0: -> mc0:",
        (char *)"Copy cdrom0: -> mc1:",
        (char *)"Copy host: -> mc0:",
        (char *)"Copy mc0: -> mc1:",
        (char *)"Copy mc1: -> mc0:",
        (char *)"Copy mc0: -> host:",
        (char *)"Dump memory -> host:",
        (char *)"Add PSX CD to mc0:title.db",
        (char *)"Dump mc0:title.db -> tty0:",
        (char *)"Copy rom0:libsd -> host:",
        NULL
};


char *_MainLoop_pInstallFiles[] =
{
        (char *)"BOOT.ELF", (char *)"BOOT.ELF",
        (char *)"TITLE.DB", (char *)"TITLE.DB",
        (char *)"ICON.SYS", (char *)"ICON.SYS",
        (char *)"PS2IP.IRX", (char *)"PS2IP.IRX",
        (char *)"PS2IPS.IRX", (char *)"PS2IPS.IRX",
        (char *)"PS2LINK.IRX", (char *)"PS2LINK.IRX",
        (char *)"PS2SMAP.IRX", (char *)"PS2SMAP.IRX",
        (char *)"CDVD.IRX", (char *)"CDVD.IRX",
        (char *)"SJPCM2.IRX", (char *)"SJPCM2.IRX",
        (char *)"MCSAVE.IRX", (char *)"MCSAVE.IRX",
        (char *)"NETPLAY.IRX", (char *)"NETPLAY.IRX",
        NULL
};
