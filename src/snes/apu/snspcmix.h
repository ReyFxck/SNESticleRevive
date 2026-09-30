/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snspcmix interface for SNES audio processing.
 */

#ifndef _SNSPCMIX_H
#define _SNSPCMIX_H

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
};

class SNSpcDspMixSilent : public SNSpcDspMix
{
	void	FetchBlock(Int32 iChannel);
	Int32	OutputSample(Int32 iChannel, Int32 nSamples, Int32 nSampleRate);
public:

	void	Mix(class CMixBuffer *pOutBuffer);
};

class    SNSpcDspMixFull : public SNSpcDspMix
{
	SNSpcEchoT		m_Echo;
	Int16			m_EchoBuffer[SNSPCDSP_ECHOBUFFER_SIZE];

	Int32			m_iNoisePhase;
	Uint32			m_uNoiseGen;
	Int16			m_iNoiseSample[SNSPCDSP_BUFFERSIZE];
	Int16			m_iNoiseSampleVoice0[SNSPCDSP_BUFFERSIZE];
	/* PMON needs the previous voice's same-sample output. Keep this out of
	   the PS2 scratchpad so SNSpcDspDataT stays below the 11 KiB lookup split. */
	Int16			m_iVoiceOutput[SNSPCDSP_BUFFERSIZE];

	void	FetchBlock(Int32 iChannel);
	void	RefreshBlock(Int32 iChannel);
	Int32	OutputSample(Int32 iChannel, Int16 *pOut, Int32 nSamples, Int32 nSampleRate, const Int16 *pPitchMod);
	Int32   OutputNoise(Int16 *pOut, Int16 *pOutVoice0,
		Int32 nSamples, Uint16 uCounterStart);
	void	FilterEcho(Int16 *pLeftEcho, Int16 *pRightEcho, Int32 nSamples, Int32 nSampleRate, Bool bEchoSPCMem);
public:
	void	Reset();
	void	Mix(class CMixBuffer *pOutBuffer);
};

#endif
