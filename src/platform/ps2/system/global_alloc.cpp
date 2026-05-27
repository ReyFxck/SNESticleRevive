/* global_alloc.cpp
 *
 * Project-wide replacements for `operator new` / `operator delete`
 * (and array variants). The PS2SDK toolchain doesn't ship the libstdc++
 * default versions in a form we want, so the project has always
 * provided its own thin wrappers around malloc / free.
 *
 * Extracted from mainloop.cpp during the Batch 3 split. Wrapping `#if 1`
 * preserved verbatim so a future maintainer can disable the project's
 * versions in one place if they later link against a libstdc++ that
 * already provides them.
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
//	printf("new %d\n", x);
	return malloc(x);
}

void operator delete[](void *ptr)
{
//	printf("delete %08X\n", (unsigned)ptr);
	free(ptr);
}

#endif
