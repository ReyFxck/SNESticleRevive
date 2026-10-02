#include "irx_imports.h"
#include "spu2_direct.h"
#include <rfauds2/rpc.h>

#define RFAUDS2_RING_FRAMES 4096u
#define RFAUDS2_BLOCK_FRAMES 512u
#define RFAUDS2_MAX_VOLUME 0x3FFFu
#define RFAUDS2_RPC_INPUT_BYTES \
    ((sizeof(rfauds2_rpc_submit) + 63u) & ~63u)

IRX_ID("rfauds2", 1, 0);

static SifRpcDataQueue_t g_rpc_queue;
static SifRpcServerData_t g_rpc_server;
static unsigned char g_rpc_input[RFAUDS2_RPC_INPUT_BYTES]
    __attribute__((aligned(64)));
static rfauds2_rpc_reply g_rpc_reply __attribute__((aligned(64)));

static s16 g_ring[RFAUDS2_RING_FRAMES * 2u]
    __attribute__((aligned(64)));
static u32 g_read_frame;
static u32 g_write_frame;
static u32 g_queued_frames;

static u32 g_underruns;
static u32 g_overruns;
static u32 g_min_queued_frames;
static u32 g_max_queued_frames;
static u32 g_refill_count;
static u32 g_silent_frames;

static unsigned char g_spu_buffer[4096] __attribute__((aligned(64)));
static s16 g_render_left[RFAUDS2_BLOCK_FRAMES]
    __attribute__((aligned(64)));
static s16 g_render_right[RFAUDS2_BLOCK_FRAMES]
    __attribute__((aligned(64)));

static int g_ring_mutex = -1;
static int g_space_sema = -1;
static int g_transfer_sema = -1;
static int g_rpc_ready_sema = -1;
static int g_play_thread = -1;

static int g_initialized;
static int g_started;
static int g_paused;
static u32 g_volume = RFAUDS2_MAX_VOLUME;
static u32 g_queue_limit_frames = RFAUDS2_RING_FRAMES;

static void clear_bytes(void *ptr, u32 size)
{
    unsigned char *p = (unsigned char *)ptr;
    u32 i;

    for (i = 0; i < size; ++i)
        p[i] = 0;
}

static int create_semaphore(int initial, int max)
{
    iop_sema_t sema;

    sema.attr = 0;
    sema.option = 0;
    sema.initial = initial;
    sema.max = max;

    return CreateSema(&sema);
}

static void stats_reset_locked(void)
{
    g_underruns = 0;
    g_overruns = 0;
    g_min_queued_frames = g_queued_frames;
    g_max_queued_frames = g_queued_frames;
    g_refill_count = 0;
    g_silent_frames = 0;
}

static void stats_note_queue_locked(void)
{
    if (g_queued_frames < g_min_queued_frames)
        g_min_queued_frames = g_queued_frames;

    if (g_queued_frames > g_max_queued_frames)
        g_max_queued_frames = g_queued_frames;
}

static void update_volume(void)
{
    unsigned int volume = 0;

    if (g_started && !g_paused)
        volume = g_volume;

    rfauds2_spu2_set_volume(volume);
}

static int transfer_complete(void *arg)
{
    (void)arg;

    if (g_transfer_sema >= 0)
        iSignalSema(g_transfer_sema);

    return 1;
}

static void fill_render_block(void)
{
    u32 take;
    u32 i;

    WaitSema(g_ring_mutex);

    if (g_paused) {
        take = 0;
    } else {
        take = g_queued_frames;
        if (take > RFAUDS2_BLOCK_FRAMES)
            take = RFAUDS2_BLOCK_FRAMES;
    }

    for (i = 0; i < RFAUDS2_BLOCK_FRAMES; ++i) {
        s16 left = 0;
        s16 right = 0;

        if (i < take) {
            u32 position =
                ((g_read_frame + i) % RFAUDS2_RING_FRAMES) * 2u;

            left = g_ring[position];
            right = g_ring[position + 1u];
        }

        g_render_left[i] = left;
        g_render_right[i] = right;
    }

    if (take != 0) {
        g_read_frame =
            (g_read_frame + take) % RFAUDS2_RING_FRAMES;
        g_queued_frames -= take;
        stats_note_queue_locked();
    }

    if (!g_paused) {
        ++g_refill_count;

        if (take < RFAUDS2_BLOCK_FRAMES) {
            ++g_underruns;
            g_silent_frames += RFAUDS2_BLOCK_FRAMES - take;
        }
    }

    SignalSema(g_ring_mutex);

    if (take != 0)
        SignalSema(g_space_sema);
}

static void copy_render_to_spu(unsigned char *block)
{
    s16 *left0 = (s16 *)(block + 0);
    s16 *right0 = (s16 *)(block + 512);
    s16 *left1 = (s16 *)(block + 1024);
    s16 *right1 = (s16 *)(block + 1536);
    u32 i;

    for (i = 0; i < 256u; ++i) {
        left0[i] = g_render_left[i];
        right0[i] = g_render_right[i];
        left1[i] = g_render_left[i + 256u];
        right1[i] = g_render_right[i + 256u];
    }
}

static void play_thread(void *arg)
{
    (void)arg;

    for (;;) {
        u32 active_block;
        u32 idle_block;
        int interrupt_state;

        WaitSema(g_transfer_sema);
        fill_render_block();

        CpuSuspendIntr(&interrupt_state);

        active_block = rfauds2_spu2_active_block();
        idle_block = 1u - active_block;

        copy_render_to_spu(g_spu_buffer + (idle_block << 11));

        CpuResumeIntr(interrupt_state);
        FlushDcache();
    }
}

static int audio_initialize(void)
{
    iop_thread_t thread;
    int spu2_result;

    if (g_initialized)
        return 0;

    g_ring_mutex = create_semaphore(1, 1);
    g_space_sema = create_semaphore(0, 1);
    g_transfer_sema = create_semaphore(0, 1);

    if (g_ring_mutex < 0 ||
        g_space_sema < 0 ||
        g_transfer_sema < 0)
        return -1;

    spu2_result = rfauds2_spu2_init(transfer_complete, 0);
    if (spu2_result < 0)
        return -2;

    clear_bytes(g_ring, sizeof(g_ring));
    clear_bytes(g_spu_buffer, sizeof(g_spu_buffer));
    clear_bytes(g_render_left, sizeof(g_render_left));
    clear_bytes(g_render_right, sizeof(g_render_right));

    g_read_frame = 0;
    g_write_frame = 0;
    g_queued_frames = 0;
    stats_reset_locked();

    update_volume();

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = play_thread;
    thread.stacksize = 0x1000;
    thread.priority = 38;

    g_play_thread = CreateThread(&thread);
    if (g_play_thread < 0)
        return -3;

    if (StartThread(g_play_thread, 0) < 0)
        return -4;

    g_initialized = 1;
    return 0;
}

static int audio_submit(const s16 *samples, u32 frames, int wait_for_space)
{
    u32 source_frame = 0;

    if (!g_initialized)
        return -1;

    if (frames > RFAUDS2_RPC_MAX_FRAMES)
        return -2;

    while (source_frame < frames) {
        u32 space;
        u32 count;
        u32 i;

        WaitSema(g_ring_mutex);

        if (g_queued_frames >= g_queue_limit_frames)
            space = 0;
        else
            space = g_queue_limit_frames - g_queued_frames;

        if (space == 0) {
            ++g_overruns;
            SignalSema(g_ring_mutex);
            /* Leave the RPC thread available for resume/start/flush even
               if the queue is full while playback is paused or stopped. */
            if (!wait_for_space)
                return (int)source_frame;
            WaitSema(g_space_sema);
            continue;
        }

        count = frames - source_frame;
        if (count > space)
            count = space;

        for (i = 0; i < count; ++i) {
            u32 position =
                ((g_write_frame + i) % RFAUDS2_RING_FRAMES) * 2u;
            u32 source = (source_frame + i) * 2u;

            g_ring[position] = samples[source];
            g_ring[position + 1u] = samples[source + 1u];
        }

        g_write_frame =
            (g_write_frame + count) % RFAUDS2_RING_FRAMES;
        g_queued_frames += count;
        source_frame += count;

        stats_note_queue_locked();
        SignalSema(g_ring_mutex);
    }

    return (int)frames;
}

static int audio_start(void)
{
    int transfer_result;

    if (!g_initialized)
        return -1;

    if (g_started)
        return 0;

    clear_bytes(g_spu_buffer, sizeof(g_spu_buffer));
    clear_bytes(g_render_left, sizeof(g_render_left));
    clear_bytes(g_render_right, sizeof(g_render_right));

    /*
     * Prime both 512-frame DMA halves before enabling the SPU2 loop.
     * Starting with two zeroed halves forced ~21 ms of silence before the
     * first queued PCM could reach the hardware and made every stop/flush/
     * restart transition audibly discontinuous.
     */
    fill_render_block();
    copy_render_to_spu(g_spu_buffer + 0);
    fill_render_block();
    copy_render_to_spu(g_spu_buffer + (1u << 11));
    FlushDcache();

    g_started = 1;
    g_paused = 0;
    update_volume();

    transfer_result = rfauds2_spu2_start_loop(
        g_spu_buffer,
        sizeof(g_spu_buffer));

    if (transfer_result < 0) {
        g_started = 0;
        update_volume();
        return -2;
    }

    return 0;
}

static int audio_pause(void)
{
    if (!g_initialized || !g_started)
        return -1;

    g_paused = 1;
    update_volume();
    return 0;
}

static int audio_resume(void)
{
    if (!g_initialized || !g_started)
        return -1;

    g_paused = 0;
    update_volume();
    return 0;
}

static int audio_stop(void)
{
    int result;

    if (!g_initialized)
        return -1;

    if (!g_started) {
        g_paused = 0;
        update_volume();
        return 0;
    }

    result = rfauds2_spu2_stop();
    if (result < 0)
        return -2;

    g_started = 0;
    g_paused = 0;
    update_volume();
    return 0;
}

static int audio_flush(void)
{
    WaitSema(g_ring_mutex);

    g_read_frame = 0;
    g_write_frame = 0;
    g_queued_frames = 0;

    clear_bytes(g_ring, sizeof(g_ring));
    clear_bytes(g_render_left, sizeof(g_render_left));
    clear_bytes(g_render_right, sizeof(g_render_right));

    stats_note_queue_locked();
    SignalSema(g_ring_mutex);
    SignalSema(g_space_sema);

    return 0;
}

static int audio_set_volume(u32 volume)
{
    if (volume > RFAUDS2_MAX_VOLUME)
        return -1;

    g_volume = volume;
    update_volume();
    return 0;
}

static int audio_set_latency_ms(u32 latency_ms)
{
    u32 frames;

    if (latency_ms == 0 || latency_ms > 1000u)
        return -1;

    frames = latency_ms * 48u;
    frames =
        ((frames + RFAUDS2_BLOCK_FRAMES - 1u) /
         RFAUDS2_BLOCK_FRAMES) *
        RFAUDS2_BLOCK_FRAMES;

    if (frames < RFAUDS2_BLOCK_FRAMES)
        frames = RFAUDS2_BLOCK_FRAMES;
    if (frames > RFAUDS2_RING_FRAMES)
        frames = RFAUDS2_RING_FRAMES;

    g_queue_limit_frames = frames;
    SignalSema(g_space_sema);

    return 0;
}

static int audio_reset_stats(void)
{
    WaitSema(g_ring_mutex);
    stats_reset_locked();
    SignalSema(g_ring_mutex);
    return 0;
}

static void fill_reply(int result)
{
    g_rpc_reply.result = result;
    g_rpc_reply.queued_frames = g_queued_frames;
    g_rpc_reply.capacity_frames = g_queue_limit_frames;
    g_rpc_reply.underruns = g_underruns;
    g_rpc_reply.overruns = g_overruns;
    g_rpc_reply.latency_ms =
        (g_queue_limit_frames * 1000u + 47999u) / 48000u;
    g_rpc_reply.volume = g_volume;
    g_rpc_reply.flags = 0;
    g_rpc_reply.min_queued_frames = g_min_queued_frames;
    g_rpc_reply.max_queued_frames = g_max_queued_frames;
    g_rpc_reply.refill_count = g_refill_count;
    g_rpc_reply.silent_frames = g_silent_frames;

    if (g_started)
        g_rpc_reply.flags |= RFAUDS2_RPC_FLAG_STARTED;
    if (g_paused)
        g_rpc_reply.flags |= RFAUDS2_RPC_FLAG_PAUSED;
}

static void *rpc_handler(int function, void *buffer, int length)
{
    int result = 0;

    switch (function) {
        case RFAUDS2_RPC_INIT:
            result = audio_initialize();
            break;

        case RFAUDS2_RPC_SUBMIT:
        case RFAUDS2_RPC_TRY_SUBMIT:
        {
            rfauds2_rpc_submit *submit =
                (rfauds2_rpc_submit *)buffer;
            u32 expected;

            if (length < (int)sizeof(u32)) {
                result = -10;
                break;
            }

            expected =
                sizeof(u32) +
                submit->frames * 2u * sizeof(s16);

            if (submit->frames > RFAUDS2_RPC_MAX_FRAMES ||
                (u32)length < expected) {
                result = -11;
                break;
            }

            result = audio_submit(submit->samples, submit->frames,
                function == RFAUDS2_RPC_SUBMIT);
            break;
        }

        case RFAUDS2_RPC_START:
            result = audio_start();
            break;

        case RFAUDS2_RPC_STATS:
            result = 0;
            break;

        case RFAUDS2_RPC_SET_VOLUME:
        {
            rfauds2_rpc_control *control =
                (rfauds2_rpc_control *)buffer;

            if (length < (int)sizeof(*control))
                result = -12;
            else
                result = audio_set_volume(control->value);
            break;
        }

        case RFAUDS2_RPC_PAUSE:
            result = audio_pause();
            break;

        case RFAUDS2_RPC_RESUME:
            result = audio_resume();
            break;

        case RFAUDS2_RPC_STOP:
            result = audio_stop();
            break;

        case RFAUDS2_RPC_FLUSH:
            result = audio_flush();
            break;

        case RFAUDS2_RPC_SET_LATENCY:
        {
            rfauds2_rpc_control *control =
                (rfauds2_rpc_control *)buffer;

            if (length < (int)sizeof(*control))
                result = -13;
            else
                result = audio_set_latency_ms(control->value);
            break;
        }

        case RFAUDS2_RPC_RESET_STATS:
            result = audio_reset_stats();
            break;

        default:
            result = -100;
            break;
    }

    fill_reply(result);
    return &g_rpc_reply;
}

static void rpc_thread(void *arg)
{
    int tid;

    (void)arg;

    tid = GetThreadId();
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&g_rpc_queue, tid);
    sceSifRegisterRpc(
        &g_rpc_server,
        RFAUDS2_RPC_SID,
        rpc_handler,
        g_rpc_input,
        0,
        0,
        &g_rpc_queue);

    SignalSema(g_rpc_ready_sema);
    sceSifRpcLoop(&g_rpc_queue);
}

int _start(int argc, char *argv[])
{
    iop_thread_t thread;
    int thread_id;

    (void)argc;
    (void)argv;

    FlushDcache();
    CpuEnableIntr();

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = rpc_thread;
    thread.stacksize = 0x1000;
    thread.priority = 40;

    g_rpc_ready_sema = create_semaphore(0, 1);
    if (g_rpc_ready_sema < 0)
        return MODULE_NO_RESIDENT_END;

    thread_id = CreateThread(&thread);
    if (thread_id < 0)
        return MODULE_NO_RESIDENT_END;

    if (StartThread(thread_id, 0) < 0)
        return MODULE_NO_RESIDENT_END;

    WaitSema(g_rpc_ready_sema);
    return MODULE_RESIDENT_END;
}
