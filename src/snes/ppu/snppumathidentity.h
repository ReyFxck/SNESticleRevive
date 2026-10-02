/* Exact identity test for already resolved normal-resolution color math. */
#ifndef _SNPPUMATHIDENTITY_H
#define _SNPPUMATHIDENTITY_H

#include "types.h"
#include "snmask.h"
#include "snmaskop.h"

_INLINE Bool SnesPPUMathIsIdentity(const SNMaskT *pMath,
	const SNMaskT *pHalf, const SNMaskT *pSubOpaque,
	Bool bUseSubScreen, Uint32 uFixedRGB)
{
	/* An empty math mask wins regardless of the unused second operand. */
	if (SNMaskIsEmpty(pMath)) return TRUE;
	/* Adding/subtracting black preserves each channel only without halving.
	   A transparent sub screen falls back to fixed color, not CGRAM[0].
	   Use the backend's converted RGB so custom color profiles stay exact. */
	return (uFixedRGB & 0x00FFFFFFu) == 0 && SNMaskIsEmpty(pHalf) &&
		(!bUseSubScreen || SNMaskIsEmpty(pSubOpaque));
}

#endif
