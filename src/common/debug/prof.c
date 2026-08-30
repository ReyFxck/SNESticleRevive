/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements prof behavior for shared debugging support.
 */

#include <stddef.h>
#include "types.h"
#include "prof.h"

extern void DLog(const char *fmt, ...);

ProfLogEntryT *Prof_pLogEntry;
ProfLogEntryT *Prof_pLogEnd;

static ProfLogT _Prof_Log;
static Int32 _Prof_nFrames = 0;

void ProfInit(Int32 MaxLogEntries)
{
	ProfCtrInit();

	Prof_pLogEntry = NULL;
	Prof_pLogEnd = NULL;
	_Prof_nFrames = 0;
	ProfLogNew(&_Prof_Log, MaxLogEntries);
	Prof_pLogEntry = ProfLogBegin(&_Prof_Log);
	if (Prof_pLogEntry)
		Prof_pLogEnd = Prof_pLogEntry + MaxLogEntries;
	else
		DLog("[prof] disabled: allocation failed (%d entries)",
			(int)MaxLogEntries);
}

void ProfShutdown(void)
{
	ProfLogEnd(&_Prof_Log, Prof_pLogEntry);
	ProfLogDelete(&_Prof_Log);
	Prof_pLogEntry = NULL;
	Prof_pLogEnd = NULL;

	ProfCtrShutdown();
}

void ProfStartProfile(Int32 nFrames)
{
	if (Prof_pLogEntry)
		_Prof_nFrames = nFrames;
}

void ProfProcess(void)
{
    ProfCtrReset();
	if (!Prof_pLogEntry)
		return;

	if (_Prof_nFrames > 0)
	{
		_Prof_nFrames--;

		if (_Prof_nFrames == 0)
		{
            /* close log, print log, and reopen log */
			ProfLogEnd(&_Prof_Log, Prof_pLogEntry);
			ProfLogPrint(&_Prof_Log, FALSE, TRUE);
/*			ProfLogPrint(&_Prof_Log, TRUE, TRUE); */
			Prof_pLogEntry = ProfLogBegin(&_Prof_Log);
		}
	} else
	{
        /* close and reopen log for next frame */
		ProfLogEnd(&_Prof_Log, Prof_pLogEntry);
		Prof_pLogEntry = ProfLogBegin(&_Prof_Log);
	}
}
