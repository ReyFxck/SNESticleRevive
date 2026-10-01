/* Conservative VRAM dependency stamps for decoded Mode 1/5 BG lines. */
#ifndef _SNPPUVRAMSTAMP_H
#define _SNPPUVRAMSTAMP_H

#include <string.h>
#include "types.h"
#include "snppurender.h"

struct SnesPPUVramStampT
{
	struct RegionT
	{
		Uint32 uEpoch, uKey, uStamp;
	};
	Uint32 uEpoch;
	Uint32 uPages[32]; // 1024 words per page; VRAM wraps at 32768 words
	RegionT Regions[4];

	SnesPPUVramStampT()
	{
		memset(this, 0, sizeof(*this));
		uEpoch = 1;
	}

	/* Returns TRUE on counter wrap, when callers must discard old entries. */
	Bool Invalidate(Uint32 uWordAddress, Uint32 nWords)
	{
		if (!nWords) return FALSE;
		Bool bWrapped = ++uEpoch == 0;
		if (bWrapped)
		{
			memset(uPages, 0, sizeof(uPages));
			memset(Regions, 0, sizeof(Regions));
			uEpoch = 1;
		}
		if (nWords >= 0x8000u)
		{
			for (Uint32 i = 0; i < 32; ++i) uPages[i] = uEpoch;
			return bWrapped;
		}
		while (nWords)
		{
			Uint32 uAddress = uWordAddress & 0x7FFFu;
			Uint32 nStep = 1024u - (uAddress & 1023u);
			uPages[uAddress >> 10] = uEpoch;
			if (nStep > nWords) nStep = nWords;
			uWordAddress = uAddress + nStep;
			nWords -= nStep;
		}
		return bWrapped;
	}

	Uint32 GetBGGeneration(Uint32 iBG, Uint32 uMode,
		const SnesBGInfoT *pInfo)
	{
		Uint32 uScreenPage = (pInfo->uScrAddr >> 10) & 31u;
		Uint32 nScreenPages = pInfo->uScrSize == 3u ? 4u :
			(pInfo->uScrSize ? 2u : 1u);
		Uint32 uChrPage = (pInfo->uChrAddr >> 10) & 31u;
		Uint32 nChrPages = pInfo->uBitDepth == 2u ? 8u : 16u;
		/* Normal 16x16 fetch can add 17 to the 10-bit tile number.
		   Native-hires fetch masks each physical half back to 10 bits. */
		if (uMode == 1u && pInfo->uChrSize) ++nChrPages;
		Uint32 uKey = uScreenPage | (nScreenPages << 5) |
			(uChrPage << 8) | (nChrPages << 13);
		RegionT *pRegion = &Regions[iBG];
		if (pRegion->uEpoch != uEpoch || pRegion->uKey != uKey)
		{
			Uint32 uStamp = 0;
			for (Uint32 i = 0; i < nScreenPages; ++i)
			{
				Uint32 uPageStamp = uPages[(uScreenPage + i) & 31u];
				if (uPageStamp > uStamp) uStamp = uPageStamp;
			}
			for (Uint32 i = 0; i < nChrPages; ++i)
			{
				Uint32 uPageStamp = uPages[(uChrPage + i) & 31u];
				if (uPageStamp > uStamp) uStamp = uPageStamp;
			}
			pRegion->uEpoch = uEpoch;
			pRegion->uKey = uKey;
			pRegion->uStamp = uStamp;
		}
		return pRegion->uStamp;
	}
};

#endif
