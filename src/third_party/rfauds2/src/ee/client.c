#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <string.h>

#include <rfauds2/rfauds2.h>
#include <rfauds2/rpc.h>

static SifRpcClientData_t g_client __attribute__((aligned(64)));
static rfauds2_rpc_submit g_submit __attribute__((aligned(64)));
static rfauds2_rpc_control g_control __attribute__((aligned(64)));
/* Reserve the complete receive cache line: polling another global must not
   bring a stale DMA reply into D-cache while the RPC is outstanding. */
static union {
    rfauds2_rpc_reply reply;
    u8 cache_line[64];
} g_receive __attribute__((aligned(64)));
#define g_reply g_receive.reply
static int g_bound;
static u32 g_async_frames;
static int g_async_operation;
static int g_stats_valid;
static rfauds2_stats g_last_stats;

enum { ASYNC_NONE, ASYNC_SUBMIT, ASYNC_STATS };

typedef char rfauds2_async_frame_limit_check[
    (RFAUDS2_ASYNC_MAX_FRAMES == RFAUDS2_RPC_MAX_FRAMES) ? 1 : -1];
typedef char rfauds2_reply_cache_line_check[
    (sizeof(g_receive) == 64) ? 1 : -1];

static void async_reply_received(void *unused)
{
    /* Request the normal RPC_END path, including the reply DMA. Poll uses
       the SDK packet completion state; no audio work runs in interrupt. */
    (void)unused;
}

static void bind_retry_delay(void)
{
    volatile int i;
    for (i = 0; i < 10000; ++i)
        __asm__ volatile("nop");
}

static void remember_stats(void)
{
    g_last_stats.queued_frames = g_reply.queued_frames;
    g_last_stats.capacity_frames = g_reply.capacity_frames;
    g_last_stats.underruns = g_reply.underruns;
    g_last_stats.overruns = g_reply.overruns;
    g_last_stats.latency_ms = g_reply.latency_ms;
    g_last_stats.volume = g_reply.volume;
    g_last_stats.started =
        (g_reply.flags & RFAUDS2_RPC_FLAG_STARTED) != 0;
    g_last_stats.paused =
        (g_reply.flags & RFAUDS2_RPC_FLAG_PAUSED) != 0;
    g_last_stats.min_queued_frames = g_reply.min_queued_frames;
    g_last_stats.max_queued_frames = g_reply.max_queued_frames;
    g_last_stats.refill_count = g_reply.refill_count;
    g_last_stats.silent_frames = g_reply.silent_frames;

    g_stats_valid = 1;
}

static int rpc_simple(int function)
{
    int result;

    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    memset(&g_reply, 0, sizeof(g_reply));

    result = sceSifCallRpc(
        &g_client,
        function,
        0,
        0,
        0,
        &g_reply,
        sizeof(g_reply),
        0,
        0);

    if (result < 0)
        return result;

    if (g_reply.result >= 0) remember_stats();
    return g_reply.result;
}

static int rpc_control(int function, u32 value)
{
    int result;

    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    g_control.value = value;
    memset(&g_reply, 0, sizeof(g_reply));

    result = sceSifCallRpc(
        &g_client,
        function,
        0,
        &g_control,
        sizeof(g_control),
        &g_reply,
        sizeof(g_reply),
        0,
        0);

    if (result < 0)
        return result;

    if (g_reply.result >= 0) remember_stats();
    return g_reply.result;
}

int rfauds2_bind(void)
{
    int result;
    int tries;

    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    g_bound = 0;
    g_stats_valid = 0;
    sceSifInitRpc(0);
    memset(&g_client, 0, sizeof(g_client));

    for (tries = 0; tries < 5000; ++tries) {
        result = sceSifBindRpc(
            &g_client,
            RFAUDS2_RPC_SID,
            0);

        if (result < 0)
            return -1000 + result;

        if (g_client.server != 0)
            break;

        bind_retry_delay();
    }

    if (g_client.server == 0)
        return -2000;

    g_bound = 1;

    result = rpc_simple(RFAUDS2_RPC_INIT);
    if (result < 0) {
        g_bound = 0;
        return -3000 + result;
    }

    return 0;
}

int rfauds2_init(const void *irx, u32 irx_size)
{
    int module_id;
    int module_result = 1;

    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    if (irx == 0 || irx_size == 0)
        return -1;

    module_id = SifExecModuleBuffer(
        (void *)irx,
        irx_size,
        0,
        0,
        &module_result);

    if (module_id < 0)
        return -100 + module_id;

    if (module_result < 0 || module_result == 1)
        return -200 - module_result;

    return rfauds2_bind();
}

int rfauds2_submit_s16(const s16 *samples, u32 frames)
{
    u32 offset = 0;

    if (!g_bound || samples == 0)
        return -1;
    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    while (offset < frames) {
        u32 count = frames - offset;
        u32 sample_count;
        u32 i;
        u32 bytes;
        int result;

        if (count > RFAUDS2_RPC_MAX_FRAMES)
            count = RFAUDS2_RPC_MAX_FRAMES;

        g_submit.frames = count;
        sample_count = count * 2u;

        for (i = 0; i < sample_count; ++i)
            g_submit.samples[i] = samples[offset * 2u + i];

        bytes = sizeof(u32) + sample_count * sizeof(s16);
        memset(&g_reply, 0, sizeof(g_reply));

        result = sceSifCallRpc(
            &g_client,
            RFAUDS2_RPC_SUBMIT,
            0,
            &g_submit,
            bytes,
            &g_reply,
            sizeof(g_reply),
            0,
            0);

        if (result < 0)
            return result;

        if (g_reply.result < 0)
            return g_reply.result;

        remember_stats();

        offset += count;
    }

    return (int)frames;
}

int rfauds2_submit_s16_async(const s16 *samples, u32 frames)
{
    int result;
    u32 bytes;

    if (!g_bound || samples == 0 || frames == 0 ||
        frames > RFAUDS2_ASYNC_MAX_FRAMES)
        return -1;
    if (g_async_operation != ASYNC_NONE)
        return RFAUDS2_ERROR_BUSY;

    /* Own both DMA buffers until poll collects the reply. The producer may
       immediately reuse its source block, including a scratchpad buffer. */
    g_submit.frames = frames;
    memcpy(g_submit.samples, samples, frames * 2u * sizeof(s16));
    memset(&g_reply, 0, sizeof(g_reply));
    g_async_frames = frames;
    g_async_operation = ASYNC_SUBMIT;
    bytes = sizeof(u32) + frames * 2u * sizeof(s16);

    result = sceSifCallRpc(&g_client, RFAUDS2_RPC_TRY_SUBMIT,
        SIF_RPC_M_NOWAIT, &g_submit, bytes, &g_reply, sizeof(g_reply),
        async_reply_received, 0);
    if (result < 0) {
        g_async_frames = 0;
        g_async_operation = ASYNC_NONE;
        return result;
    }
    return 0;
}

int rfauds2_submit_poll(u32 *accepted_frames)
{
    int result;
    u32 frames;

    if (accepted_frames == 0 || g_async_operation != ASYNC_SUBMIT)
        return -1;
    if (sceSifCheckStatRpc(&g_client))
        return 0;

    frames = g_async_frames;
    result = g_reply.result;
    g_async_frames = 0;
    g_async_operation = ASYNC_NONE;
    if (result < 0)
        return result;
    if ((u32)result > frames)
        return RFAUDS2_ERROR_PROTOCOL;

    remember_stats();
    *accepted_frames = (u32)result;
    return 1;
}

int rfauds2_start(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_START);
}

int rfauds2_pause(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_PAUSE);
}

int rfauds2_resume(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_RESUME);
}

int rfauds2_stop(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_STOP);
}

int rfauds2_flush(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_FLUSH);
}

int rfauds2_set_volume(u32 volume)
{
    if (!g_bound || volume > RFAUDS2_VOLUME_MAX)
        return -1;

    return rpc_control(RFAUDS2_RPC_SET_VOLUME, volume);
}

int rfauds2_set_latency_ms(u32 latency_ms)
{
    if (!g_bound || latency_ms == 0)
        return -1;

    return rpc_control(RFAUDS2_RPC_SET_LATENCY, latency_ms);
}

int rfauds2_get_stats(rfauds2_stats *stats)
{
    int result;

    if (!g_bound || stats == 0)
        return -1;

    result = rpc_simple(RFAUDS2_RPC_STATS);
    if (result < 0)
        return result;

    *stats = g_last_stats;

    return 0;
}

int rfauds2_get_stats_async(void)
{
    int result;
    if (!g_bound) return -1;
    if (g_async_operation != ASYNC_NONE) return RFAUDS2_ERROR_BUSY;
    memset(&g_reply, 0, sizeof(g_reply));
    g_async_operation = ASYNC_STATS;
    result = sceSifCallRpc(&g_client, RFAUDS2_RPC_STATS,
        SIF_RPC_M_NOWAIT, 0, 0, &g_reply, sizeof(g_reply),
        async_reply_received, 0);
    if (result < 0) g_async_operation = ASYNC_NONE;
    return result;
}

int rfauds2_get_stats_poll(rfauds2_stats *stats)
{
    int result;
    if (stats == 0 || g_async_operation != ASYNC_STATS) return -1;
    if (sceSifCheckStatRpc(&g_client)) return 0;
    result = g_reply.result;
    g_async_operation = ASYNC_NONE;
    if (result < 0) return result;
    remember_stats();
    *stats = g_last_stats;
    return 1;
}

int rfauds2_get_cached_stats(rfauds2_stats *stats)
{
    if (!g_bound || !g_stats_valid || stats == 0) return -1;
    *stats = g_last_stats;
    return 0;
}

int rfauds2_reset_stats(void)
{
    if (!g_bound)
        return -1;
    return rpc_simple(RFAUDS2_RPC_RESET_STATS);
}
