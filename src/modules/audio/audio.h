/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the audio interface for the PlayStation 2 audio backend.
 */

/* EE-side Aud_* API. Both backends accept the existing planar 48 kHz,
 * stereo S16 output from AudMixBuffer. Select AUDIO_BACKEND at build time.
 * RFAuds2 owns PCM until admission is acknowledged; audsrv is retained for
 * comparable baseline builds. Emulated audio policy remains in the core.
 */

#ifndef _AUDIO_H
#define _AUDIO_H

void Aud_Puts(char *format, ...);
int  Aud_Init(int sync, int buffersize, int maxenqueuesamples);

void Aud_Enqueue(short *left, short *right, int size, int wait);
void Aud_Play();
void Aud_Pause();
void Aud_Setvol(unsigned int volume);
void Aud_Clearbuff();
int  Aud_Available();
int  Aud_Buffered();
void Aud_Quit();

void Aud_BufferedAsyncStart();
int  Aud_BufferedAsyncGet();
void Aud_EnqueueAsync(short *left, short *right, int size);
void Aud_Wait();

int  Aud_IsInitialized();
/* Poll/launch at most one audio RPC without waiting for the IOP queue. */
void Aud_Service(void);

typedef struct AudOutputStatsT {
    unsigned int queued_ee_frames, queued_iop_frames;
    unsigned int underruns, silent_frames, backpressure_waits;
} AudOutputStatsT;
/* Last completed telemetry, no RPC. Returns 0 when unsupported/unavailable. */
int Aud_GetOutputStats(AudOutputStatsT *stats);

#endif /* _AUDIO_H */
