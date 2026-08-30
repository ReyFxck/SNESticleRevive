/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the prof interface for shared debugging support.
 */

#ifndef _PROF_H
#define _PROF_H

#include "proflog.h"
#include "profctr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern ProfLogEntryT *Prof_pLogEntry;
extern ProfLogEntryT *Prof_pLogEnd;

#ifndef PROF_ENABLED
#define PROF_ENABLED ((CODE_PROFILE==TRUE) || FALSE)
#endif

// profile section macros

#if PROF_ENABLED

/* PROFILE used to dereference Prof_pLogEntry unconditionally. If the large
   PS2 allocation failed, the first emulated frame wrote to 0x0, 0x4, ... and
   killed the EE thread. Keep profiling optional and bounded instead. */
static inline void ProfWriteEntry(const char *pName, Int32 uUser)
{
	if (Prof_pLogEntry && Prof_pLogEntry < Prof_pLogEnd)
	{
		ProfLogEntryT *pEntry = Prof_pLogEntry++;
		pEntry->pName = (char *)pName;
		pEntry->Counter[PROF_COUNTER_CYCLE] = ProfCtrGetCycle();
		pEntry->Counter[PROF_COUNTER_COUNTER0] = ProfCtrGetCounter0();
		pEntry->Counter[PROF_COUNTER_COUNTER1] = ProfCtrGetCounter1();
		pEntry->Counter[PROF_COUNTER_USER] = uUser;
	}
}

#define PROF_ENTER(__SectionName) \
	ProfWriteEntry("!" __SectionName, 0)

#define PROF_LEAVE(__SectionName) \
	ProfWriteEntry("~" __SectionName, 0)

#define PROF_ENTER2(__SectionName, _counter) \
	ProfWriteEntry("!" __SectionName, (_counter))

#define PROF_LEAVE2(__SectionName, _counter) \
	ProfWriteEntry("~" __SectionName, (_counter))

#else

#define PROF_ENTER(__SectionName)
#define PROF_LEAVE(__SectionName)
#define PROF_ENTER2(__SectionName,__x)
#define PROF_LEAVE2(__SectionName,__x)

#endif

void ProfInit(Int32 MaxLogEntries);
void ProfShutdown(void);
void ProfProcess(void);
void ProfStartProfile(Int32 nFrames);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
