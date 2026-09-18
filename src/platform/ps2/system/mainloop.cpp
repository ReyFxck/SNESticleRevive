/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Coordinates clean system-exit actions for the PlayStation 2 runtime.
 */

#include <stdio.h>

#include <kernel.h>
#include <libpwroff.h>
#include <ps2_poweroff_driver.h>

#include "types.h"
#include "mainloop.h"
#include "mainloop_bgm.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"

static MainLoopSystemActionE s_SystemAction = MAINLOOP_SYSTEM_NONE;
static Char s_SystemBootPath[64] = { 0 };

static Bool _MainLoopFileExists(const Char *pPath)
{
    FILE *pFile;

    if (!pPath || !pPath[0])
        return FALSE;

    pFile = fopen(pPath, "rb");
    if (!pFile)
        return FALSE;

    fclose(pFile);
    return TRUE;
}

static Bool _MainLoopSaveBeforeSystemExit()
{
    if (!_pSystem)
        return TRUE;

    _MainLoopForceCheckSRAM();
    if (!_MainLoopHasSRAM() || !_MainLoop_SRAMUpdated)
        return TRUE;

    MainLoopStatusPrintf(180, "Saving SRAM before exit...");
    if (_MainLoopSaveSRAM(TRUE))
        return TRUE;

    MainLoopStatusPrintf(240, "SRAM save failed - exit cancelled.");
    return FALSE;
}

Bool MainLoopRequestSystemAction(MainLoopSystemActionE eAction)
{
    Char BootPath[64] = { 0 };

    if (eAction <= MAINLOOP_SYSTEM_NONE ||
        eAction > MAINLOOP_SYSTEM_POWEROFF)
        return FALSE;

    if (s_SystemAction != MAINLOOP_SYSTEM_NONE)
        return TRUE;

    if (eAction == MAINLOOP_SYSTEM_BOOT_ELF)
    {
        static const Char *pCandidates[] =
        {
            "mc0:/BOOT/BOOT.ELF",
            "mc1:/BOOT/BOOT.ELF",
            NULL
        };

        for (Int32 i = 0; pCandidates[i]; ++i)
        {
            if (_MainLoopFileExists(pCandidates[i]))
            {
                snprintf(BootPath, sizeof(BootPath), "%s", pCandidates[i]);
                break;
            }
        }

        if (!BootPath[0])
        {
            MainLoopStatusPrintf(240, "BOOT.ELF not found on mc0: or mc1:.");
            return FALSE;
        }
    }

    if (!_MainLoopSaveBeforeSystemExit())
        return FALSE;

    if (eAction == MAINLOOP_SYSTEM_POWEROFF)
    {
        enum POWEROFF_INIT_STATUS status = init_poweroff_driver();
        if (status < POWEROFF_INIT_STATUS_OK)
        {
            MainLoopStatusPrintf(
                240,
                "Power-off driver failed (%d).",
                (int)status
            );
            return FALSE;
        }
    }

    BgmStop();

    if (BootPath[0])
        snprintf(s_SystemBootPath, sizeof(s_SystemBootPath), "%s", BootPath);
    else
        s_SystemBootPath[0] = 0;

    s_SystemAction = eAction;
    return TRUE;
}

MainLoopSystemActionE MainLoopGetSystemAction()
{
    return s_SystemAction;
}

const Char *MainLoopGetSystemBootPath()
{
    return s_SystemBootPath;
}

void MainLoopPerformSystemAction()
{
    switch (s_SystemAction)
    {
        case MAINLOOP_SYSTEM_BROWSER:
            ExecOSD(0, NULL);
            break;

        case MAINLOOP_SYSTEM_BOOT_ELF:
            if (s_SystemBootPath[0])
                LoadExecPS2(s_SystemBootPath, 0, NULL);

            /* If the selected ELF disappeared between probing and exec,
               fall back to the PS2 Browser rather than hanging here. */
            ExecOSD(0, NULL);
            break;

        case MAINLOOP_SYSTEM_POWEROFF:
            poweroffShutdown();
            for (;;)
                SleepThread();
            break;

        default:
            break;
    }
}
