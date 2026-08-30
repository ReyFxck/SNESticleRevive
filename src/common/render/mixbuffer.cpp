/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mixbuffer behavior for shared rendering and audio buffers.
 */

#include "types.h"
#include "mixbuffer.h"

void CMixBuffer::GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels)
{
	*puSampleRate = 0;
	*pnSampleBits = 0;
	*pnChannels   = 0;
}

Int32 CMixBuffer::GetOutputSamples()
{
	return 0;
}

void CMixBuffer::OutputSamplesMono(Int16 *pSamples, Int32 nSamples)
{
}

void CMixBuffer::OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples)
{
}

void CMixBuffer::Flush()
{

}
