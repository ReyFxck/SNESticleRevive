/* RFAuds2 output adapter. Emulation, mixing and 32 -> 48 kHz conversion
   remain in AudMixBuffer; this file only owns and transports stereo PCM. */
#include <kernel.h>
#include <string.h>
#include <rfauds2/rfauds2.h>
#include "audio.h"
#include "sntargetprofile.h"

extern void DLog(const char *fmt, ...);

#define AUD_EE_FRAMES 8192u
#define AUD_EE_MASK (AUD_EE_FRAMES - 1u)
#define AUD_PREBUFFER_FRAMES 1536u

static short s_pcm[AUD_EE_FRAMES * 2u] __attribute__((aligned(64)));
static unsigned int s_read, s_write, s_count, s_flight_frames;
static unsigned int s_backpressure_waits;
static int s_ready, s_play_requested, s_running;
enum { AUD_IDLE, AUD_SUBMIT, AUD_STATS };
static int s_pending;

static int Aud_Result(int result, const char *operation)
{
    if (result < 0) {
        DLog("[snes-aud] RFAuds2 %s failed %d", operation, result);
        s_ready = 0;
        return 0;
    }
    return 1;
}

/* Retain the unaccepted suffix, even when the server accepts zero frames.
   The ring prefix is not reusable until its reply has been collected. */
static int Aud_Poll(void)
{
    int result;
    unsigned int accepted;
    rfauds2_stats stats;
    if (s_pending == AUD_IDLE) return 1;
    if (s_pending == AUD_SUBMIT) {
        accepted = 0;
        result = rfauds2_submit_poll(&accepted);
        if (result == 0) return 0;
        if (!Aud_Result(result, "submit completion")) return 0;
        if (accepted > s_flight_frames || accepted > s_count) {
            Aud_Result(RFAUDS2_ERROR_PROTOCOL, "accepted prefix");
            return 0;
        }
        s_read = (s_read + accepted) & AUD_EE_MASK;
        s_count -= accepted;
        s_flight_frames = 0;
    } else {
        result = rfauds2_get_stats_poll(&stats);
        if (result == 0) return 0;
        if (!Aud_Result(result, "stats completion")) return 0;
    }
    s_pending = AUD_IDLE;
    return 1;
}

/* Controls collect the bounded outstanding TRY_SUBMIT/STATS result first.
   They do not wait for queued PCM to be played or for ring space. */
static void Aud_Collect(void)
{
    while (s_ready && !Aud_Poll()) RotateThreadReadyQueue(0);
}

static void Aud_TryStart(int force)
{
    rfauds2_stats stats;
    if (!s_ready || !s_play_requested || s_running || s_pending != AUD_IDLE)
        return;
    if (rfauds2_get_cached_stats(&stats) < 0) return;
    if (stats.queued_frames < AUD_PREBUFFER_FRAMES && !force) return;
    if (!stats.queued_frames) return;
    if (Aud_Result(rfauds2_start(), "start")) s_running = 1;
}

static void Aud_Pump(void)
{
    unsigned int count, contiguous;
    if (!s_ready) return;
    if (Aud_Poll()) {
        Aud_TryStart(0);
        if (s_ready && s_count) {
            count = s_count;
            contiguous = AUD_EE_FRAMES - s_read;
            if (count > contiguous) count = contiguous;
            if (count > RFAUDS2_ASYNC_MAX_FRAMES)
                count = RFAUDS2_ASYNC_MAX_FRAMES;
            if (Aud_Result(rfauds2_submit_s16_async(s_pcm + s_read * 2u,
                                                   count), "submit launch")) {
                s_flight_frames = count;
                s_pending = AUD_SUBMIT;
            }
        } else if (s_ready) {
            /* Occupancy changes while the EE has nothing to submit. Avoid
               synchronous queue queries in menu/BGM and frame callers. */
            if (Aud_Result(rfauds2_get_stats_async(), "stats launch"))
                s_pending = AUD_STATS;
        }
    }
}

void Aud_Service(void)
{
    SNTARGET_BEGIN(_targetAudioService);
    Aud_Pump();
    SNTARGET_END(Audio, _targetAudioService);
}

int Aud_Init(int sync, int buffersize, int maxenqueuesamples)
{
    int result;
    (void)sync; (void)buffersize; (void)maxenqueuesamples;
    if (s_ready) return 0;
    s_read = s_write = s_count = s_flight_frames = 0;
    s_backpressure_waits = 0;
    s_pending = AUD_IDLE;
    s_running = 0;
    s_play_requested = 1;
    result = rfauds2_bind();
    if (!Aud_Result(result, "bind")) return result;
    /* 43 ms rounds to a 2048-frame IOP queue. Start with both 512-frame
       SPU2 halves plus one refill already available, rather than silence. */
    result = rfauds2_set_latency_ms(43);
    if (!Aud_Result(result, "latency")) return result;
    result = rfauds2_set_volume(RFAUDS2_VOLUME_MAX);
    if (!Aud_Result(result, "initial volume")) return result;
    s_ready = 1;
    return 0;
}

void Aud_Enqueue(short *left, short *right, int size, int wait)
{
    unsigned int i, count, space, contiguous;
    if (!s_ready || !left || !right || size <= 0) return;
    SNTARGET_BEGIN(_targetAudioEnqueue);
    /* Like audsrv_play_audio, new PCM restarts playback after stop/clear. */
    s_play_requested = 1;
    while (size > 0 && s_ready) {
        Aud_Pump();
        space = AUD_EE_FRAMES - s_count;
        if (!space) {
            /* Exceptional bounded back-pressure. No overwrite or silent
               drop: yield and collect nonblocking admissions until room. */
            ++s_backpressure_waits;
            RotateThreadReadyQueue(0);
            continue;
        }
        count = (unsigned int)size;
        if (count > space) count = space;
        contiguous = AUD_EE_FRAMES - s_write;
        if (count > contiguous) count = contiguous;
        for (i = 0; i < count; ++i) {
            s_pcm[(s_write + i) * 2u] = left[i];
            s_pcm[(s_write + i) * 2u + 1u] = right[i];
        }
        s_write = (s_write + count) & AUD_EE_MASK;
        s_count += count;
        left += count; right += count; size -= (int)count;
        Aud_Pump();
    }
    SNTARGET_END(Audio, _targetAudioEnqueue);
    if (wait) Aud_Wait();
}

void Aud_EnqueueAsync(short *left, short *right, int size)
{
    Aud_Enqueue(left, right, size, 0);
}

void Aud_BufferedAsyncStart(void) { Aud_Service(); }

int Aud_Buffered(void)
{
    rfauds2_stats stats;
    if (!s_ready) return 0;
    Aud_Service();
    if (rfauds2_get_cached_stats(&stats) < 0) return (int)s_count;
    return (int)(s_count + stats.queued_frames);
}

int Aud_BufferedAsyncGet(void) { return Aud_Buffered(); }

int Aud_Available(void)
{
    rfauds2_stats stats;
    unsigned int space, ee_space;
    if (!s_ready) return 0;
    Aud_Service();
    if (rfauds2_get_cached_stats(&stats) < 0) return 0;
    /* Keep BGM's demand tied to IOP occupancy, not the 8192-frame EE FIFO.
       An in-flight prefix is still included in s_count until acknowledged. */
    space = stats.capacity_frames > stats.queued_frames
        ? stats.capacity_frames - stats.queued_frames : 0;
    space = space > s_count ? space - s_count : 0;
    ee_space = AUD_EE_FRAMES - s_count;
    return (int)(space < ee_space ? space : ee_space);
}

void Aud_Wait(void)
{
    while (s_ready && (s_count || s_pending == AUD_SUBMIT)) {
        Aud_Collect();
        /* A legacy small blocking block need not wait for more producers. */
        Aud_TryStart(1);
        if (s_count) {
            Aud_Service();
            RotateThreadReadyQueue(0);
        }
    }
    Aud_Collect();
    Aud_TryStart(1);
}

void Aud_Setvol(unsigned int volume)
{
    if (!s_ready) return;
    Aud_Collect();
    if (s_ready) Aud_Result(rfauds2_set_volume(volume & 0x3fffu), "volume");
}

void Aud_Clearbuff(void)
{
    if (!s_ready) return;
    Aud_Collect();
    if (!s_ready) return;
    if (!Aud_Result(rfauds2_stop(), "stop")) return;
    if (!Aud_Result(rfauds2_flush(), "flush")) return;
    s_read = s_write = s_count = s_flight_frames = 0;
    s_running = 0;
    s_play_requested = 0;
}

void Aud_Pause(void) { Aud_Clearbuff(); }
void Aud_Play(void) { if (s_ready) s_play_requested = 1; }
void Aud_Quit(void) { Aud_Clearbuff(); s_ready = 0; }
int Aud_IsInitialized(void) { return s_ready; }

int Aud_GetOutputStats(AudOutputStatsT *output)
{
    rfauds2_stats stats;
    if (!s_ready || !output || rfauds2_get_cached_stats(&stats) < 0) return 0;
    output->queued_ee_frames = s_count;
    output->queued_iop_frames = stats.queued_frames;
    output->underruns = stats.underruns;
    output->silent_frames = stats.silent_frames;
    output->backpressure_waits = s_backpressure_waits;
    return 1;
}
