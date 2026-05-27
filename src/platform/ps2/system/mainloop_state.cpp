#include <stdio.h>
#include <string.h>

#include "types.h"
#include "console.h"
#include "file.h"
#include "prof.h"
#include "memcard.h"
#include "mainloop_debug.h"

extern "C" {
int MCSave_Write(char *pPath, char *pData, int nBytes);
int MCSave_WriteSync(int block, int *pResult);
}

#include "mainloop_shared.h"
#include "mainloop_state.h"

/* The iaddis-era custom MCSAVE.IRX async memory-card writer has been
   retired -- see embedded_irx.cpp.  mainloop_iop.cpp no longer
   attempts to IOPLoadModule("MCSAVE.IRX") and never calls
   MCSave_Init(), so _MainLoop_bMCSaveReady always stays FALSE and
   the synchronous newlib-stdio-via-iomanX path below is always
   taken.  The flag itself is kept as a vestigial extern so the
   build doesn't have to touch every call site that still tests it;
   it is effectively dead code that branch predictors will fold out.
   Defined in mainloop_iop.cpp. */
extern Bool _MainLoop_bMCSaveReady;

#if MAINLOOP_HISTORY
extern Uint32 _nHistory;
#endif


/* MAINLOOP_STATEPATH lives in mainloop_shared.h (included above). */

static Uint32 _PathCalcHash(const char *pStr)
{
    Uint32 hash = 0;

    while (*pStr)
    {
        hash *= 33;
        hash += *pStr;
        pStr++;
    }

    return hash;
}

void PathTruncFileName(Char *pOut, Char *pStr, Int32 nMaxChars)
{
    Uint32 hash;

    hash = _PathCalcHash(pStr);

    // copy string up to maxchars length
    while (*pStr && nMaxChars > 0)
    {
        *pOut++ = *pStr++;
        nMaxChars--;
    }

    // terminate
    *pOut = 0;

    if (nMaxChars <= 0)
    {
        // mangle end of name
        sprintf(pOut - 3, "%03d", hash % 1000);
    }
}

int PathGetMaxFileNameLength(const char *pPath)
{
    if (pPath[0] == 'm' && pPath[1] == 'c')
    {
        return 32;
    }

    return 256;
}

static Uint32 _CalcChecksum(Uint32 *pData, Uint32 nWords)
{
    Uint32 uSum = 0;

    while (nWords > 0)
    {
        uSum += pData[0];
        pData++;
        nWords--;
    }

    return uSum;
}

Bool _MainLoopHasSRAM()
{
    return _pSystem ? (_pSystem->GetSRAMBytes() > 0) : FALSE;
}

Bool _MainLoopSaveSRAM(Bool bSync)
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    if (nSramBytes > 0)
    {
        Char Path[1024];
        Char SaveName[256];
        Uint8 *pSRAM;

        pSRAM = _pSystem->GetSRAMData();

        {
            static Bool bDirEnsured = FALSE;
            if (!bDirEnsured)
            {
                int rc = MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, TRUE);
                printf("[SRAM] lazy MemCardCreateSave('%s') -> %d\n", _SramPath, rc);
                bDirEnsured = TRUE;
            }
        }

        PathTruncFileName(SaveName, _RomName, PathGetMaxFileNameLength(_SramPath) - 4);
        snprintf(
            Path,
            sizeof(Path),
            "%s/%s.%s",
            _SramPath,
            SaveName,
            _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT)
        );

        ML_TRACE("SRAM save begin: rom='%s' bytes=%d sync=%d", _RomName, (int)nSramBytes, (int)bSync);
        ML_TRACE("SRAM save path: %s", Path);
        printf("[SRAM] save path='%s' nBytes=%d mcsaveready=%d first16=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
               Path, (int)nSramBytes, (int)_MainLoop_bMCSaveReady,
               pSRAM[0], pSRAM[1], pSRAM[2], pSRAM[3],
               pSRAM[4], pSRAM[5], pSRAM[6], pSRAM[7],
               pSRAM[8], pSRAM[9], pSRAM[10], pSRAM[11],
               pSRAM[12], pSRAM[13], pSRAM[14], pSRAM[15]);

        if (_MainLoop_bMCSaveReady)
        {
            /* Async path via the custom MCSAVE.IRX RPC server. Only
               reachable when the IRX actually loaded (real PS2 with
               the file shipped next to the ELF). */
            MCSave_WriteSync(TRUE, NULL);
            MCSave_Write((char *)Path, (char *)pSRAM, nSramBytes);

            if (bSync)
            {
                int result;

                MCSave_WriteSync(TRUE, &result);
                ML_TRACE("SRAM save sync result: %d", result);
                return result ? TRUE : FALSE;
            }

            return TRUE;
        }
        else
        {
            /* Sync fallback for NetherSX2 / any setup where
               MCSAVE.IRX failed to load. Goes through newlib stdio
               (fopen/fwrite/fclose) which routes to mcman/mcserv
               via iomanX -- the same path _MainLoopLoadSRAM uses
               for reads and the same path MemCardCreateSave used
               at boot to write icon.sys / icon.icn into
               mc0:/SNESticle/, so if the save directory exists at
               all on the card then this write will reach it. */
            Bool bOk = MemCardWriteFile(Path, pSRAM, nSramBytes);
            ML_TRACE("SRAM save (memcard fallback): %d", (int)bOk);
            return bOk;
        }
    }

    ML_TRACE("SRAM save skipped: no SRAM");
    return FALSE;
}

void _MainLoopLoadSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    printf("[SRAM] LoadSRAM enter: pSystem=%p nSramBytes=%d romname='%s'\n",
           (void *)_pSystem, (int)nSramBytes, _RomName);

    if (nSramBytes > 0)
    {
        Char Path[1024];
        Char SaveName[256];
        Uint8 *pSRAM;

        pSRAM = _pSystem->GetSRAMData();

        PathTruncFileName(SaveName, _RomName, PathGetMaxFileNameLength(_SramPath) - 4);
        snprintf(
            Path,
            sizeof(Path),
            "%s/%s.%s",
            _SramPath,
            SaveName,
            _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT)
        );

        printf("[SRAM] load path='%s' pSRAM=%p nBytes=%d\n",
               Path, (void *)pSRAM, (int)nSramBytes);

        ML_TRACE("SRAM load begin: rom='%s' bytes=%d", _RomName, (int)nSramBytes);
        ML_TRACE("SRAM load path: %s", Path);

        Bool bOk = MemCardReadFile(Path, pSRAM, nSramBytes);
        printf("[SRAM] MemCardReadFile -> %d\n", (int)bOk);

        if (bOk)
        {
            _MainLoop_SRAMChecksum = _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);
            ConPrint("SRAM loaded: %s\n", Path);
            printf("[SRAM] load OK checksum=%08X first16=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
                   (unsigned int)_MainLoop_SRAMChecksum,
                   pSRAM[0], pSRAM[1], pSRAM[2], pSRAM[3],
                   pSRAM[4], pSRAM[5], pSRAM[6], pSRAM[7],
                   pSRAM[8], pSRAM[9], pSRAM[10], pSRAM[11],
                   pSRAM[12], pSRAM[13], pSRAM[14], pSRAM[15]);
            ML_TRACE("SRAM load checksum: %08X", (unsigned int)_MainLoop_SRAMChecksum);
        }
        else
        {
            printf("[SRAM] load FAILED path='%s' (file missing or short read)\n", Path);
            ML_TRACE("SRAM load failed or file missing: %s", Path);
        }

        _MainLoop_SRAMUpdated = FALSE;
    }

    _MainLoop_SaveCounter = 0;
    _bStateSaved = FALSE;
}

/* Force-update the SRAM dirty flag (_MainLoop_SRAMUpdated) right now,
   ignoring the throttle in _MainLoopCheckSRAM. Used by _MenuEnable
   before it decides whether to fire the synchronous save: if the
   user wrote to SRAM in the last <CHECK_INTERVAL frames and pressed
   L2+R2 before _MainLoopCheckSRAM ran its next sampled checksum,
   the dirty flag would still be FALSE and the menu-open save would
   be skipped without this. The cost is one full-SRAM checksum at
   menu-open time, which is already a moment we accept a hitch for
   (the modal "Saving SRAM..." is already shown there). */
Bool _MainLoopForceCheckSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    if (nSramBytes > 0)
    {
        Uint8 *pSRAM = _pSystem->GetSRAMData();
        Uint32 uChecksum;

        uChecksum = _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);

        if (_MainLoop_SRAMChecksum != uChecksum)
        {
            ML_TRACE(
                "SRAM force-check: dirty (old=%08X new=%08X)",
                (unsigned int)_MainLoop_SRAMChecksum,
                (unsigned int)uChecksum
            );
            _MainLoop_SRAMUpdated = TRUE;
            _MainLoop_SRAMChecksum = uChecksum;
        }
    }

    return TRUE;
}

Bool _MainLoopCheckSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    if (nSramBytes > 0)
    {
        /* The inline auto-save trigger (decrement SaveCounter -> call
           _MainLoopSaveSRAM(FALSE) when it hits zero) was removed
           deliberately. On the !_MainLoop_bMCSaveReady fallback path
           (NetherSX2 / any setup without MCSAVE.IRX next to the ELF)
           _MainLoopSaveSRAM ends up in MemCardWriteFile, which
           drives fopen/fwrite/fclose on the EE main thread and
           blocks the per-frame loop for the full duration of the
           memcard write. Games that keep the SRAM continuously
           dirty (RPG stats counters, HUD timers, etc.) caused this
           to fire at unpredictable moments and showed up as a
           gameplay hitch, while games that don't keep it dirty just
           saved at a different unpredictable point.

           The user-visible save path is now exclusively the
           synchronous one in _MenuEnable(TRUE) (mainloop_menu_runtime.cpp):
           opening the in-game menu with L2+R2 still calls
           _MainLoopSaveSRAM(TRUE) and shows the "Saving SRAM..." modal,
           so the save still happens at a deterministic, user-driven
           moment. _MainLoop_SRAMUpdated and _MainLoop_SRAMChecksum
           below are still maintained because _MenuEnable reads
           _MainLoop_SRAMUpdated to decide whether to actually run
           the save block at all. */

        /* The full-SRAM checksum used to run every frame (60Hz).
           For larger carts (up to MAINLOOP_MAXSRAMSIZE = 64 KB,
           i.e. 16k u32 adds) that's pure busywork: the only
           consumer is the _MainLoop_SRAMUpdated dirty bit that
           _MenuEnable polls when the user opens the menu, which
           never needs frame-accurate freshness. Run the check
           once every CHECK_INTERVAL frames (~0.5s @ 60Hz) so the
           dirty flag is still set well before any plausible
           L2+R2 press, without paying the cost on every frame. */
        static Uint32 sCheckFrame = 0;
        const Uint32 CHECK_INTERVAL = 30;
        if ((sCheckFrame++ % CHECK_INTERVAL) != 0)
        {
            return TRUE;
        }

        Uint8 *pSRAM = _pSystem->GetSRAMData();
        Uint32 uChecksum;

        PROF_ENTER("_MainLoopCheckSRAM");

        uChecksum = _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);

        if (_MainLoop_SRAMChecksum != uChecksum)
        {
#if CODE_DEBUG
            printf("SRAM changed!\n");
#endif
            ML_TRACE(
                "SRAM checksum changed: old=%08X new=%08X",
                (unsigned int)_MainLoop_SRAMChecksum,
                (unsigned int)uChecksum
            );

            _MainLoop_SRAMUpdated = TRUE;
            _MainLoop_SRAMChecksum = uChecksum;
        }

        PROF_LEAVE("_MainLoopCheckSRAM");
    }

    return TRUE;
}

void _MainLoopLoadState()
{
    Char Path[1024];

    /*
    printf("%d\n", sizeof(_SnesState));
    printf("SNStateCPUT %d\n",sizeof(SNStateCPUT ));
    printf("SNStatePPUT %d\n",sizeof(SNStatePPUT ));
    printf("SNStateIOT %d\n",sizeof(SNStateIOT ));
    printf("SNStateDMACT %d\n",sizeof(SNStateDMACT ));
    printf("SNStateSPCT %d\n",sizeof(SNStateSPCT ));
    printf("SNStateSPCDSPT %d\n",sizeof(SNStateSPCDSPT ));
    */

    if (!_pSystem)
        return;

    if (_pSystem == _pSnes)
    {
        snprintf(Path, sizeof(Path), "%s%s.sns", MAINLOOP_STATEPATH, _RomName);
        ML_TRACE("State load path: %s", Path);

        if (FileReadMem(Path, &_SnesState, sizeof(_SnesState)))
        {
            _bStateSaved = TRUE;
            ConPrint("State loaded from %s\n", Path);
            ML_TRACE("State load ok");
        }
        else
        {
            ML_TRACE("State load failed or file missing");
        }

        if (_bStateSaved)
        {
            _pSnes->RestoreState(&_SnesState);
            ML_TRACE("State restore applied");
        }
    }

#if 0
    else if (_pSystem == _pNes)
    {
        sprintf(Path, "%s%s.nst", MAINLOOP_STATEPATH, _RomName);

        if (FileReadMem(Path, &_NesState, sizeof(_NesState)))
        {
            _bStateSaved = TRUE;
            ConPrint("State loaded from %s\n", Path);
        }

        if (_bStateSaved)
        {
            _pNes->RestoreState(&_NesState);
        }
    }
#endif
}

void _MainLoopSaveState()
{
    Char Path[1024];

    if (!_pSystem)
        return;

    if (_pSystem == _pSnes)
    {
        snprintf(Path, sizeof(Path), "%s%s.sns", MAINLOOP_STATEPATH, _RomName);
        ML_TRACE("State save path: %s", Path);

        _pSnes->SaveState(&_SnesState);
        _bStateSaved = TRUE;

        if (FileWriteMem(Path, &_SnesState, sizeof(_SnesState)))
        {
            ConPrint("State saved to %s\n", Path);
            ML_TRACE("State save ok");
        }
        else
        {
            ML_TRACE("State save failed");
        }
    }

#if 0
    else if (_pSystem == _pNes)
    {
        sprintf(Path, "%s%s.nst", MAINLOOP_STATEPATH, _RomName);

        _pNes->SaveState(&_NesState);
        _bStateSaved = TRUE;

        if (FileWriteMem(Path, &_NesState, sizeof(_NesState)))
        {
            ConPrint("State saved to %s\n", Path);
        }
    }
#endif
}


void _MainLoopResetHistory()
{
#if MAINLOOP_HISTORY
    _nHistory = 0;
#endif
}


void _MainLoopResetInputChecksums()
{
	_uInputFrame =0;
	memset(_uInputChecksum, 0, sizeof(_uInputChecksum));
}

#if MAINLOOP_HISTORY
Uint32 _History[16384 * 2];
Uint32 _nHistory = 0;
#endif

#if MAINLOOP_HISTORY

void _MainLoopSaveHistory()
{
    FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32));
    printf("History written\n");
}
#endif
