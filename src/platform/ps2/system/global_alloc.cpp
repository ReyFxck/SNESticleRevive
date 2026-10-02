/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements global alloc behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <stdlib.h>

/* The supplied C++20 core catches allocation failures. Its standard-library
   containers require throwing operator new; the historical null-returning
   overrides cannot implement that contract. Keep them for the legacy build. */
#if !NES_MESENCE
void *operator new(unsigned x)
{
	void *ptr = malloc(x);
	#if CODE_DEBUG
	printf("new %d %08X\n", x, (unsigned)ptr);
	#endif
	return ptr;
}

void operator delete(void *ptr)
{
	#if CODE_DEBUG
	printf("delete %08X\n", (unsigned)ptr);
	#endif
	free(ptr);
}

void *operator new[](unsigned x)
{
	return malloc(x);
}

void operator delete[](void *ptr)
{
	free(ptr);
}

#endif
