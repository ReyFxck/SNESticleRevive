/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snspcmix interface for SNES audio processing.
 */

#ifndef _SNSPCMIX_H
#define _SNSPCMIX_H

#include <string.h>

#if CODE_PLATFORM == CODE_PS2
//#define SNSPCDSP_MAXSAMPLES 400
//#define SNSPCDSP_BUFFERSIZE 400
#define SNSPCDSP_MAXSAMPLES 544/8
#define SNSPCDSP_BUFFERSIZE 544
//#define SNSPCDSP_MAXSAMPLES 800
//#define SNSPCDSP_BUFFERSIZE 800

#else
//#define SNSPCDSP_MAXSAMPLES 800*4
#define SNSPCDSP_MAXSAMPLES 544/8
#define SNSPCDSP_BUFFERSIZE (SNSPCDSP_MAXSAMPLES)
#endif

class    SNSpcDspMix : public ISNSpcDspMix
{
	SNSpcChannelT	m_Channels[SNSPCDSP_CHANNEL_NUM];

protected:
	/* Native DSP-rate state. m_uDspCounter is the pre-misc30 counter value
	   for the next 32 kHz sample. */
	Uint32			m_nSampleRate;
	Uint16			m_uDspCounter;

protected:
	SNSpcChannelT  *GetChannel(Int32 iChannel) {return &m_Channels[iChannel]; }
	Int32	OutputEnvelope(Int32 iChannel, Uint16 *pOut, Int32 nSamples,
		Uint16 uCounterStart);

public:
	virtual Bool	GetChannelState(Int32 iChannel, Uint8 *pEnvX, Uint8 *pOutX);
	virtual void	KeyOn(Int32 iChannel);
	virtual void	KeyOff(Int32 iChannel);

	void	Reset();
	void	SaveState(struct SNStateSPCDSPT *pState);
	void	RestoreState(struct SNStateSPCDSPT *pState);
	Uint16	GetDspCounter() const { return m_uDspCounter; }
	void	SetDspCounter(Uint16 uCounter) { m_uDspCounter = uCounter; }
	void	CopyChannels(SNSpcChannelT *pOut) const
	{
		memcpy(pOut, m_Channels, sizeof(m_Channels));
	}
	void	RestoreChannels(const SNSpcChannelT *pIn)
	{
		memcpy(m_Channels, pIn, sizeof(m_Channels));
	}
};

class SNSpcDspMixSilent : public SNSpcDspMix
{
	void	FetchBlock(Int32 iChannel);
	Int32	OutputSample(Int32 iChannel, Int32 nSamples, Int32 nSampleRate,
		const Uint16 *pEnvelopeState);
public:

	void	Mix(class CMixBuffer *pOutBuffer);
};

class    SNSpcDspMixFull : public SNSpcDspMix
{
	SNSpcEchoT		m_Echo;

	Int32			m_iNoisePhase;
	Uint32			m_uNoiseGen;
	Int16			m_iNoiseSample[SNSPCDSP_BUFFERSIZE];
	Int16			m_iNoiseSampleVoice0[SNSPCDSP_BUFFERSIZE];
	/* PMON needs the previous voice's same-sample output. Keep this out of
	   the PS2 scratchpad so SNSpcDspDataT stays below the 11 KiB lookup split. */
	Int16			m_iVoiceOutput[SNSPCDSP_BUFFERSIZE];

	void	FetchBlock(Int32 iChannel);
	void	RefreshBlock(Int32 iChannel);
	Int32	OutputSample(Int32 iChannel, Int16 *pOut, Int32 nSamples,
		Int32 nSampleRate, const Int16 *pPitchMod, const Uint16 *pEnvelopeState);
	Int32   OutputNoise(Int16 *pOut, Int16 *pOutVoice0,
		Int32 nSamples, Uint16 uCounterStart);
	void	FilterEcho(Int16 *pLeftEcho, Int16 *pRightEcho, Int32 nSamples);
public:
	void	Reset();
	void	Mix(class CMixBuffer *pOutBuffer);
	void	CopyTransientState(SNSpcEchoT *pEcho, Int32 *pNoisePhase,
		Uint32 *pNoiseGen) const
	{
		*pEcho = m_Echo;
		*pNoisePhase = m_iNoisePhase;
		*pNoiseGen = m_uNoiseGen;
	}
	void	RestoreTransientState(const SNSpcEchoT *pEcho, Int32 iNoisePhase,
		Uint32 uNoiseGen)
	{
		m_Echo = *pEcho;
		m_iNoisePhase = iNoisePhase;
		m_uNoiseGen = uNoiseGen ? uNoiseGen : 0x4000;
	}
};

#endif
