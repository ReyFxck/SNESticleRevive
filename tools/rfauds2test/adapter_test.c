/* Exercise the production adapter against a delayed, prefix-admitting RPC
   peer. The separate RFAuds2 suite tests its actual client and IOP server. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <rfauds2/rfauds2.h>
#include "audio.h"

#define CAPACITY 2048u
#define STREAM 262144u
static short ring[CAPACITY * 2], request[RFAUDS2_ASYNC_MAX_FRAMES * 2];
static short expected[STREAM * 2], played[STREAM * 2];
static unsigned int read_pos, write_pos, queued, played_frames, expected_frames;
static unsigned int request_frames, accepted, delay, consume_rate = 173;
static unsigned int sync_controls, sync_queries, async_submits, async_queries;
static unsigned int zero_accepts, partial_accepts, yield_count, underruns;
static int pending, completed, running, cached_valid;
static int fail_poll, errors;
static rfauds2_stats cache;

static void snapshot(void)
{
    memset(&cache, 0, sizeof(cache));
    cache.capacity_frames = CAPACITY;
    cache.queued_frames = queued;
    cache.started = running;
    cache.underruns = underruns;
    cached_valid = 1;
}
static void consume(unsigned int frames)
{
    unsigned int i;
    if (frames > queued) frames = queued;
    assert(played_frames + frames <= STREAM);
    for (i = 0; i < frames; ++i) {
        played[(played_frames + i)*2] = ring[read_pos*2];
        played[(played_frames + i)*2+1] = ring[read_pos*2+1];
        read_pos = (read_pos+1) % CAPACITY;
    }
    queued -= frames;
    played_frames += frames;
}
static void advance(void)
{
    unsigned int i;
    if (running) consume(consume_rate);
    if (!pending || completed) return;
    if (delay && --delay) return;
    if (pending == 1) {
        accepted = request_frames;
        if (accepted > CAPACITY-queued) accepted = CAPACITY-queued;
        if (!accepted) ++zero_accepts;
        else if (accepted < request_frames) ++partial_accepts;
        for (i = 0; i < accepted; ++i) {
            ring[write_pos*2] = request[i*2];
            ring[write_pos*2+1] = request[i*2+1];
            write_pos = (write_pos+1) % CAPACITY;
        }
        queued += accepted;
    }
    completed = 1;
    /* DMA reply becomes visible to the client only when poll collects it. */
}
int RotateThreadReadyQueue(int priority)
{
    assert(priority == 0);
    assert(++yield_count < 2000000);
    advance();
    return 0;
}
void DLog(const char *fmt, ...) { (void)fmt; ++errors; }
static int control(void)
{
    assert(pending == 0);
    ++sync_controls;
    snapshot();
    return 0;
}
int rfauds2_bind(void) { return control(); }
int rfauds2_set_latency_ms(u32 latency) { assert(latency == 43); return control(); }
int rfauds2_set_volume(u32 volume) { assert(volume <= RFAUDS2_VOLUME_MAX); return control(); }
int rfauds2_start(void)
{
    control();
    running = 1;
    consume(1024); /* two primed SPU2 halves, in the same channel order */
    snapshot();
    return 0;
}
int rfauds2_stop(void) { control(); running = 0; snapshot(); return 0; }
int rfauds2_flush(void)
{
    control(); queued = read_pos = write_pos = 0; snapshot(); return 0;
}
int rfauds2_submit_s16_async(const s16 *pcm, u32 frames)
{
    assert(!pending && frames > 0 && frames <= RFAUDS2_ASYNC_MAX_FRAMES);
    memcpy(request, pcm, frames * 4);
    request_frames = frames;
    delay = 2;
    pending = 1; completed = 0;
    ++async_submits;
    return 0;
}
int rfauds2_submit_poll(u32 *result)
{
    assert(pending == 1);
    if (!completed) return 0;
    if (fail_poll) { fail_poll = 0; pending = completed = 0; return -9; }
    *result = accepted;
    pending = completed = 0;
    snapshot();
    return 1;
}
int rfauds2_get_stats_async(void)
{
    assert(!pending);
    pending = 2; completed = 0; delay = 2;
    ++async_queries;
    return 0;
}
int rfauds2_get_stats_poll(rfauds2_stats *stats)
{
    assert(pending == 2);
    if (!completed) return 0;
    pending = completed = 0;
    snapshot(); *stats = cache;
    return 1;
}
int rfauds2_get_cached_stats(rfauds2_stats *stats)
{
    if (!cached_valid) return -1;
    *stats = cache;
    return 0;
}
/* The steady producer is forbidden to enter the synchronous query API. */
int rfauds2_get_stats(rfauds2_stats *stats)
{
    ++sync_queries; (void)stats; assert(!"synchronous occupancy query"); return -1;
}

static void enqueue(unsigned int frames, int wait)
{
    short left[4000], right[4000];
    unsigned int i, origin = expected_frames;
    assert(frames <= 4000 && origin+frames <= STREAM);
    for (i = 0; i < frames; ++i) {
        left[i] = (short)((origin+i)*109u ^ 0x399u);
        right[i] = (short)((origin+i)*731u ^ 0x9825u);
        expected[(origin+i)*2] = left[i];
        expected[(origin+i)*2+1] = right[i];
    }
    expected_frames += frames;
    Aud_Enqueue(left, right, frames, wait);
    /* AudMixBuffer immediately reuses its output: the adapter must own it. */
    memset(left, 0x55, sizeof(left));
    memset(right, 0xaa, sizeof(right));
}
static void drain(void)
{
    AudOutputStatsT stats;
    unsigned int tries = 0;
    for (;;) {
        advance(); Aud_Service();
        assert(Aud_GetOutputStats(&stats));
        if (!stats.queued_ee_frames && !queued) break;
        assert(++tries < 100000);
    }
    assert(played_frames == expected_frames);
    assert(!memcmp(played, expected, played_frames * 4));
}
static void reset_oracle(void)
{
    played_frames = expected_frames = 0;
}
int main(void)
{
    unsigned int i, controls;
    AudOutputStatsT stats;
    assert(!Aud_IsInitialized());
    assert(Aud_Init(0, 960*25, 4000) == 0);
    assert(Aud_IsInitialized());
    assert(Aud_Available() == CAPACITY);
    consume_rate = 0;
    enqueue(4000, 0);
    for (i = 0; i < 40; ++i) { advance(); Aud_Service(); }
    assert(zero_accepts);
    consume_rate = 173;
    /* Full queue and delayed replies force zero and partial admissions.
       Many wraps must preserve every stereo frame exactly once. */
    for (i = 0; i < 80; ++i) {
        enqueue(3071, 0);
        advance(); Aud_BufferedAsyncStart();
    }
    drain();
    assert(zero_accepts && partial_accepts);
    assert(Aud_GetOutputStats(&stats) && stats.backpressure_waits);
    assert(async_submits && async_queries && !sync_queries);
    /* Small legacy blocking packets must start without a second producer. */
    Aud_Clearbuff(); Aud_Play(); reset_oracle();
    enqueue(37, 1); drain();
    assert(played_frames == 37);
    /* Change volume with a pending stats or submit: collect first. */
    Aud_Clearbuff(); reset_oracle(); controls = sync_controls;
    enqueue(4000, 0);
    Aud_Setvol(0x1234);
    assert(sync_controls == controls+1);
    drain();
    /* Explicit pause/clear discard old PCM, then new enqueue restarts. */
    enqueue(4000, 0);
    Aud_Pause();
    assert(!queued && !running && Aud_Buffered() == 0);
    reset_oracle(); enqueue(3071, 0); drain();
    controls = sync_controls;
    for (i = 0; i < 20; ++i) {
        advance(); Aud_Service(); (void)Aud_Available(); (void)Aud_Buffered();
    }
    assert(sync_controls == controls && !sync_queries);
    assert(!errors);
    Aud_Quit(); assert(!Aud_IsInitialized());
    assert(Aud_Available() == 0 && Aud_Buffered() == 0);
    /* A fatal completion is surfaced once and disables the adapter; do
       not overwrite/retry an unknown admission and duplicate audio. */
    assert(Aud_Init(0, 0, 0) == 0);
    enqueue(37, 0);
    while (pending != 1) { advance(); Aud_Service(); }
    fail_poll = 1;
    advance(); advance(); Aud_Service();
    assert(errors == 1 && !Aud_IsInitialized());
    Aud_Service(); assert(errors == 1);
    printf("adapter_test: PASS (249680 stereo frames, prefix retries, source reuse, wrap, controls, small blocking packet, async occupancy, transport failure)\n");
    return 0;
}
