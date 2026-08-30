/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the console interface for shared base utilities.
 */

#ifndef _CONSOLE_H
#define _CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum
{
	CON_STANDARD,
	CON_WARNING,
	CON_ERROR,
	CON_SYSTEM,
	CON_DEBUG,
	CON_NETWORK,

	CON_MAXCHANNELS
};

void ConInit();
void ConShutdown();

#define ConPrint printf
#define ConWarning printf
#define ConError printf
#define ConDebug printf

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
