/*
 * audio.h - EE-side audio output API (audsrv backend).
 *
 * A small Aud_* API over the PS2SDK **audsrv** service (SPU2 native:
 * 48000 Hz / 16-bit / stereo). This was historically the SjPCM API by
 * Nick Van Veen ("Sjeep", 2002), which talked to a custom SJPCM2.IRX
 * over SIF RPC - that IRX is gone. The implementation here
 * (audio_audsrv.c) is a from-scratch audsrv version; the API was
 * renamed from SjPCM_* to Aud_* so the name matches what it actually
 * does. MIT (this repo).
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

#endif /* _AUDIO_H */
