#ifndef RFAUDS2_H
#define RFAUDS2_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RFAUDS2_OUTPUT_RATE           48000u
#define RFAUDS2_OUTPUT_BLOCK_FRAMES   512u
#define RFAUDS2_RING_CAPACITY_FRAMES 4096u
#define RFAUDS2_VOLUME_MAX            0x3FFFu
#define RFAUDS2_ASYNC_MAX_FRAMES      960u
#define RFAUDS2_PROTOCOL_VERSION      2u

/* Stable device/API errors. Other negative values can be transport or
   module-load failures and should still be treated as errors. */
#define RFAUDS2_ERROR_INVALID_ARGUMENT (-1)
#define RFAUDS2_ERROR_BUSY             (-4)
#define RFAUDS2_ERROR_PROTOCOL         (-5)

/* Rate-converter-specific initialization errors. */
#define RFAUDS2_RATE_ERROR_INVALID_RATE       (-2)
#define RFAUDS2_RATE_ERROR_INVALID_CHANNELS   (-3)
#define RFAUDS2_RATE_ERROR_INVALID_MODE       (-4)
#define RFAUDS2_RATE_ERROR_STEP_UNDERFLOW     (-5)
#define RFAUDS2_RATE_ERROR_SINC_DOWNSAMPLE    (-6)

typedef enum {
    RFAUDS2_RESAMPLE_NEAREST = 0,
    RFAUDS2_RESAMPLE_LINEAR = 1,
    RFAUDS2_RESAMPLE_CUBIC = 2,
    RFAUDS2_RESAMPLE_SINC8 = 3
} rfauds2_resample_mode;

typedef struct {
    u32 input_rate;
    u32 output_rate;
    u8 channels;
    rfauds2_resample_mode mode;
    u64 phase_q32;
    u64 step_q32;
} rfauds2_rate_converter;

typedef enum {
    RFAUDS2_BUS_GAME = 0,
    RFAUDS2_BUS_MUSIC = 1,
    RFAUDS2_BUS_SFX = 2,
    RFAUDS2_BUS_UI = 3,
    RFAUDS2_BUS_COUNT = 4
} rfauds2_bus;

typedef struct {
    s32 gain_q15[RFAUDS2_BUS_COUNT];
    u8 muted[RFAUDS2_BUS_COUNT];
} rfauds2_bus_mixer;

typedef struct {
    u32 queued_frames;
    u32 capacity_frames;
    u32 underruns;
    u32 overruns;
    u32 latency_ms;
    u32 volume;
    u32 started;
    u32 paused;
    u32 min_queued_frames;
    u32 max_queued_frames;
    u32 refill_count;
    u32 silent_frames;
    /* DMA deadlines that were missed and replaced by a dedicated silence
       block. These are also included in underruns/silent_frames. */
    u32 missed_refills;
} rfauds2_stats;

/* Load an embedded rfauds2.irx, verify the RPC protocol, bind and initialize
   the device. */
int rfauds2_init(const void *irx, u32 irx_size);

/* Bind to an already-loaded rfauds2.irx, verify the RPC protocol and
   initialize the device. */
int rfauds2_bind(void);

/* Stop playback and release RFAuds2's audio thread, semaphores and SPU2 DMA
   interrupt handler. The IRX/RPC server remains resident, so rfauds2_bind()
   can initialize it again later. */
int rfauds2_shutdown(void);

int rfauds2_submit_s16(const s16 *interleaved_stereo, u32 frames);
/* Launch one nonblocking RPC. PCM is copied before return; frames must be
   1..RFAUDS2_ASYNC_MAX_FRAMES. Returns 0 on launch, ERROR_BUSY if a previous
   request has not been collected, or another negative error. */
int rfauds2_submit_s16_async(const s16 *interleaved_stereo, u32 frames);
/* Returns 0 while RPC is pending, 1 on completion, or a negative error.
   On completion *accepted_frames is the prefix copied into the IOP ring;
   retain and retry the remaining tail, including a zero-acceptance block.
   Collect the result before any other device call. Single EE caller only. */
int rfauds2_submit_poll(u32 *accepted_frames);
int rfauds2_start(void);
int rfauds2_pause(void);
int rfauds2_resume(void);
int rfauds2_stop(void);
int rfauds2_flush(void);
int rfauds2_set_volume(u32 volume);
/* Queue capacity is quantized to the nearest 512-frame hardware block and
   clamped to the physical ring. Shrinking below current occupancy returns
   RFAUDS2_ERROR_BUSY; drain or flush first. */
int rfauds2_set_latency_ms(u32 latency_ms);
int rfauds2_get_stats(rfauds2_stats *stats);
/* Asynchronous occupancy/telemetry query, sharing the single RPC slot.
   Poll returns 0 pending, 1 complete, or a negative error. Cached stats
   returns the last completed snapshot without an RPC, even while pending. */
int rfauds2_get_stats_async(void);
int rfauds2_get_stats_poll(rfauds2_stats *stats);
int rfauds2_get_cached_stats(rfauds2_stats *stats);
int rfauds2_reset_stats(void);

int rfauds2_rate_converter_init(
    rfauds2_rate_converter *converter,
    u32 input_rate,
    u32 output_rate,
    u8 channels,
    rfauds2_resample_mode mode);

void rfauds2_rate_converter_reset(rfauds2_rate_converter *converter);

u32 rfauds2_rate_converter_process_s16(
    rfauds2_rate_converter *converter,
    const s16 *input,
    u32 input_frames,
    s16 *output,
    u32 output_frames_capacity,
    u32 *input_frames_consumed);

void rfauds2_mix_s16(
    s16 *destination,
    const s16 *source,
    u32 sample_count,
    s32 gain_q15);

void rfauds2_bus_mixer_init(rfauds2_bus_mixer *mixer);

int rfauds2_bus_mixer_set_gain(
    rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    s32 gain_q15);

int rfauds2_bus_mixer_set_mute(
    rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    int muted);

void rfauds2_bus_mixer_clear_s16(
    s16 *destination,
    u32 sample_count);

int rfauds2_bus_mixer_mix_s16(
    const rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    s16 *destination,
    const s16 *source,
    u32 sample_count);

#ifdef __cplusplus
}
#endif

#endif
