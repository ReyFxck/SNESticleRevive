#ifndef RFAUDS2_RPC_H
#define RFAUDS2_RPC_H

#include <tamtypes.h>

#define RFAUDS2_RPC_SID 0x52464132u /* "RFA2" */
#define RFAUDS2_RPC_MAX_FRAMES 960u

enum {
    RFAUDS2_RPC_INIT = 0,
    RFAUDS2_RPC_SUBMIT = 1,
    RFAUDS2_RPC_START = 2,
    RFAUDS2_RPC_STATS = 3,
    RFAUDS2_RPC_SET_VOLUME = 4,
    RFAUDS2_RPC_PAUSE = 5,
    RFAUDS2_RPC_RESUME = 6,
    RFAUDS2_RPC_STOP = 7,
    RFAUDS2_RPC_FLUSH = 8,
    RFAUDS2_RPC_SET_LATENCY = 9,
    RFAUDS2_RPC_RESET_STATS = 10,
    /* Partial, nonblocking ring admission. Existing opcodes stay unchanged. */
    RFAUDS2_RPC_TRY_SUBMIT = 11
};

#define RFAUDS2_RPC_FLAG_STARTED 0x01u
#define RFAUDS2_RPC_FLAG_PAUSED  0x02u

typedef struct {
    u32 frames;
    s16 samples[RFAUDS2_RPC_MAX_FRAMES * 2u];
} rfauds2_rpc_submit;

typedef struct {
    u32 value;
} rfauds2_rpc_control;

typedef struct {
    s32 result;
    u32 queued_frames;
    u32 capacity_frames;
    u32 underruns;
    u32 overruns;
    u32 latency_ms;
    u32 volume;
    u32 flags;
    u32 min_queued_frames;
    u32 max_queued_frames;
    u32 refill_count;
    u32 silent_frames;
} rfauds2_rpc_reply;

#endif
