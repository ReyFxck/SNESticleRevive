/*
    ---------------------------------------------------------------------
    sjpcm_rpc.c - audsrv-backed reimplementation of the SjPCM EE-side API.
    ---------------------------------------------------------------------

    The original SjPCM library (Nick Van Veen "Sjeep", 2002) talked to a
    custom IOP-side IRX (SJPCM2.IRX) over SIF RPC. That precompiled IRX
    pre-dates a number of changes in modern PS2SDK / IOP rom builds and
    its RPC server either fails to register or hangs SifBindRpc on
    emulators (NetherSX2/PCSX2 Qt) and stripped-down PS2 setups, which
    is why the project had to disable the embedded SJPCM2.IRX entirely
    (see src/platform/ps2/system/embedded_irx.cpp). Result: silent audio.

    The PS2DEV team replaced SjPCM with **audsrv** back in 2005:
        https://forums.ps2dev.org/viewtopic.php?t=1500
            "Audsrv comes to replace sjpcm, and provide an easy and
             stable way to utilize the SPU2."
    audsrv.irx ships with every modern PS2SDK at
        $(PS2SDK)/iop/irx/audsrv.irx
    and is the standard audio service used by SDL, ScummVM, OPL, etc.

    This file keeps the SjPCM_* API surface that SJPCMMixBuffer and
    mainloop_iop.cpp call into, but the backend is now audsrv. Sample
    format is fixed at the SPU2's native 48000 Hz / 16 bit / stereo,
    which matches what SJPCMMixBuffer already converts the SNES output
    to (see src/common/render/sjpcmbuffer.cpp). Left/right separated
    channels are interleaved into a stereo buffer before being passed
    to audsrv_play_audio().

    audsrv exposes audsrv_queued() / audsrv_available() (in bytes), so
    SjPCM_Buffered() / SjPCM_Available() map directly without needing
    any time-based estimation.
*/

#include <tamtypes.h>
#include <kernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <audsrv.h>
#include <sio.h>

#include "sjpcm.h"

/* ScrPrintf goes to the on-screen log (and stays there during the
   splash). Plain printf on EE-side never reaches the emulator console
   in this project, but ScrPrintf does survive long enough to be seen
   in a screenshot of the boot screen. Forward-declare it here so we
   don't have to drag the C++ mainloop_ui.h into this C file. */
extern void ScrPrintf(const char *pFormat, ...);

/* Diagnostic printf helper for this project.

   Plain printf() on the EE never seems to reach NetherSX2 / PCSX2's
   emulator log file in this codebase (some piece of the libc->SIF->IOP
   stdout wiring is missing). What *does* reach the emulator log is the
   EE SIO TX FIFO at 0x1000f180: PCSX2 captures bytes written to it and
   emits them on the EE_SIO log channel, which lands in the same console
   /log file as the IOP "loadmodule:" / "audsrv_adpcm_init()" lines.

   We therefore route diagnostics through sio_putsn() (writes to EE SIO
   TX FIFO byte-by-byte) and also mirror them to ScrPrintf so the user
   sees them on the on-screen splash log. sio_init() is called lazily
   on first use with the standard 38400 8N1 setting.

   Tag: each line is prefixed with "[snes-aud] " so the user can grep
   the log file. */
static int   _sio_inited = 0;
static char  _dlog_buf[256];

/* Non-static so other translation units can extern it for one-off
   audio-path tracing. Mirror of the local prototype:
       extern void DLog(const char *fmt, ...);
   See mainloop_process.cpp / sjpcmbuffer.cpp where this is called
   via that extern declaration. */
void DLog(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!_sio_inited)
    {
        sio_init(38400, 0, 0, 0, 0);
        _sio_inited = 1;
    }

    va_start(ap, fmt);
    n = vsnprintf(_dlog_buf, sizeof(_dlog_buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(_dlog_buf) - 2) n = sizeof(_dlog_buf) - 2;

    /* Make sure the line ends with \n so the emulator log flushes it. */
    if (n == 0 || _dlog_buf[n - 1] != '\n')
    {
        _dlog_buf[n++] = '\n';
        _dlog_buf[n]   = '\0';
    }

#if DEBUG_BOOT_SCREEN
    {
        static int _dlog_scr_inited = 0;
        if (!_dlog_scr_inited) {
            extern void init_scr(void);
            extern void scr_clear(void);
            init_scr();
            scr_clear();
            _dlog_scr_inited = 1;
        }
        {
            extern void scr_printf(const char *fmt, ...);
            scr_printf("%s", _dlog_buf);
        }
    }
#endif
    sio_putsn(_dlog_buf);
}


/*
    Output is fixed 48000 Hz / 16 bit / stereo (SPU2 native).
    SJPCMMixBuffer already up-samples 32000 Hz SNES audio to 48000 Hz
    before calling SjPCM_Enqueue, so audsrv runs without any internal
    upsampling.
*/
#define SJPCM_AUDSRV_FREQ      48000
#define SJPCM_AUDSRV_BITS      16
#define SJPCM_AUDSRV_CHANNELS  2
#define SJPCM_BYTES_PER_SAMPLE (SJPCM_AUDSRV_CHANNELS * (SJPCM_AUDSRV_BITS / 8)) /* 4 */


/*
    Static interleave scratch sized for the worst case the engine will
    ever pass into SjPCM_Enqueue. SJPCMMIXBUFFER_MAXENQUEUE in
    sjpcmbuffer.h is currently (800 * 5) = 4000 samples per channel.
    Round up to 4096 for alignment headroom.
*/
#define SJPCM_MAX_ENQUEUE_SAMPLES 4096
static short _interleave_buf[SJPCM_MAX_ENQUEUE_SAMPLES * SJPCM_AUDSRV_CHANNELS]
    __attribute__((aligned(64)));


static int sjpcm_inited = 0;


int SjPCM_Init(int sync, int numsamples, int maxenqueuesamples)
{
    struct audsrv_fmt_t fmt;
    int ret;

    (void)sync;
    (void)numsamples;
    (void)maxenqueuesamples;

    if (sjpcm_inited)
    {
        return 0;
    }

    /* Mirror init progression to the on-screen splash log too -- on
       real PS2 hardware without an SIO cable, the screen is the only
       way to see where init died.  If audsrv_init() blocks inside
       SifBindRpc (because the IOP audsrv RPC server never registered),
       the user will see "audsrv_init..." on screen as the last line
       and we know exactly which step deadlocked. */
    ScrPrintf("audsrv_init...\n");
    DLog("[snes-aud] audsrv_init() ...");
    ret = audsrv_init();
    DLog("[snes-aud] audsrv_init() = %d", ret);
    ScrPrintf("audsrv_init = %d\n", ret);
    if (ret != 0)
    {
        DLog("[snes-aud] init FAILED %d (%s)",
             ret, audsrv_get_error_string());
        ScrPrintf("audsrv_init FAILED %d - continuing silent\n", ret);
        return -1;
    }

    fmt.freq     = SJPCM_AUDSRV_FREQ;
    fmt.bits     = SJPCM_AUDSRV_BITS;
    fmt.channels = SJPCM_AUDSRV_CHANNELS;

    ret = audsrv_set_format(&fmt);
    DLog("[snes-aud] set_format(48000,16,2) = %d", ret);
    ScrPrintf("audsrv set_format = %d\n", ret);
    if (ret != 0)
    {
        DLog("[snes-aud] set_format FAILED %d (%s)",
             ret, audsrv_get_error_string());
        ScrPrintf("audsrv set_format FAILED %d\n", ret);
        audsrv_quit();
        return -1;
    }

    /* Default to full volume. SjPCM_Setvol() may override. */
    ret = audsrv_set_volume(MAX_VOLUME);
    DLog("[snes-aud] set_volume(%d) = %d", MAX_VOLUME, ret);
    ScrPrintf("audsrv set_volume = %d\n", ret);

    sjpcm_inited = 1;
    return 0;
}


void SjPCM_Quit(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
    audsrv_quit();
    sjpcm_inited = 0;
}


/*
    The original API was pause/play; audsrv plays continuously while
    samples are queued, so Play is effectively "make sure not stopped"
    and Pause is "drop the queue". SjPCMMixBuffer calls
    SjPCM_Clearbuff() + SjPCM_Play() once at boot.
*/
void SjPCM_Play(void)
{
    /* nothing to do - audsrv plays as soon as samples are enqueued */
}


void SjPCM_Pause(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
}


void SjPCM_Clearbuff(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
}


/*
    SjPCM_Setvol took a 14-bit hardware-style volume (0..0x3FFF) where
    0x3FFF was full scale. audsrv's volume is 0..MAX_VOLUME (100), so
    rescale.
*/
void SjPCM_Setvol(unsigned int volume)
{
    int v;

    if (!sjpcm_inited) return;

    volume &= 0x3FFF;
    v = (int)((volume * MAX_VOLUME) / 0x3FFF);
    if (v < MIN_VOLUME) v = MIN_VOLUME;
    if (v > MAX_VOLUME) v = MAX_VOLUME;

    audsrv_set_volume(v);
}


/*
    Bytes already queued in audsrv's IOP-side ring buffer, expressed as
    stereo sample-frames so the math in SJPCMMixBuffer::GetOutputSamples
    keeps working unchanged.
*/
int SjPCM_Buffered(void)
{
    int bytes;

    if (!sjpcm_inited) return 0;

    bytes = audsrv_queued();
    if (bytes < 0) return 0;

    return bytes / SJPCM_BYTES_PER_SAMPLE;
}


int SjPCM_Available(void)
{
    int bytes;

    if (!sjpcm_inited) return 0;

    bytes = audsrv_available();
    if (bytes < 0) return 0;

    return bytes / SJPCM_BYTES_PER_SAMPLE;
}


/*
    Interleave separate left/right channels and push to audsrv. `wait`
    selects between blocking until enough room is available
    (audsrv_wait_audio) and best-effort (drop overflow if the IOP ring
    is full).
*/
void SjPCM_Enqueue(short *left, short *right, int size, int wait)
{
    int i;
    int bytes;
    int ret;
    static int call_count = 0;

    if (!sjpcm_inited) return;
    if (size <= 0) return;
    if (size > SJPCM_MAX_ENQUEUE_SAMPLES) size = SJPCM_MAX_ENQUEUE_SAMPLES;

    for (i = 0; i < size; i++)
    {
        _interleave_buf[i * 2 + 0] = left[i];
        _interleave_buf[i * 2 + 1] = right[i];
    }

    bytes = size * SJPCM_BYTES_PER_SAMPLE;

    if (wait)
    {
        audsrv_wait_audio(bytes);
    }

    ret = audsrv_play_audio((const char *)_interleave_buf, bytes);

    /* DLog only goes through SIO so we can spam it at a few-Hz cadence
       without tanking the framerate (sio_putsn is a few hundred MMIO
       writes, no GS render). Print every 64th call (~1 s of audio at
       60 Hz) so we can see the queued/available counters move. */
    if ((call_count & 0x3F) == 0)
    {
        int q = audsrv_queued();
        int a = audsrv_available();
        DLog("[snes-aud] enq#%d size=%d bytes=%d ret=%d queued=%d avail=%d",
             call_count, size, bytes, ret, q, a);
    }
    call_count++;
}


/*
    The original async API let SjPCMMixBuffer overlap RPC traffic with
    the next SNES frame via a SIF callback + semaphore handshake.
    audsrv_play_audio is already non-blocking when there is room in the
    ring buffer, and audsrv_wait_audio handles back-pressure when there
    isn't, so the async path collapses into the synchronous one.
*/
void SjPCM_BufferedAsyncStart(void)
{
    /* nothing to do - audsrv tracks queued bytes internally */
}


int SjPCM_BufferedAsyncGet(void)
{
    return SjPCM_Buffered();
}


void SjPCM_EnqueueAsync(short *left, short *right, int size)
{
    SjPCM_Enqueue(left, right, size, 0);
}


void SjPCM_Wait(void)
{
    /* audsrv ring-buffer back-pressure is handled inside Enqueue via
       audsrv_wait_audio when the caller passes wait=1, so there is no
       extra synchronisation to perform here. */
}


int SjPCM_IsInitialized(void)
{
    return sjpcm_inited;
}
