/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements global alloc behavior for the PlayStation 2 application runtime.
 */

#include <stdio.h>
#include <stdlib.h>

#if 1
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
