/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mixbuffer interface for shared rendering and audio buffers.
 */

#ifndef _MIXBUFFER_H
#define _MIXBUFFER_H

// stream buffer abstract class

class CMixBuffer
{
public:
	virtual void	GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels);
	virtual Int32	GetOutputSamples();
	virtual void	OutputSamplesMono(Int16 *pSamples, Int32 nSamples);
	virtual void	OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples);
    virtual void    Flush();
};

#endif
