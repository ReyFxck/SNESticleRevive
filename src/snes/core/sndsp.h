/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sndsp interface for the SNES emulation core.
 */

#ifndef _SNDSP_H
#define _SNDSP_H

class ISNDSP
{
public:
	virtual void Reset()=0;
	virtual void WriteData(Uint32 uAddr, Uint8 uData)=0;
	virtual Uint8 ReadData(Uint32 uAddr)=0;
	virtual Uint8 ReadStatus(Uint32 uAddr)=0;
};

#endif
