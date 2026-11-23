#ifndef RFAUDS2_REFILL_STATE_H
#define RFAUDS2_REFILL_STATE_H

enum {
    RFAUDS2_REFILL_REAL_BLOCKS = 2,
    RFAUDS2_REFILL_SILENCE_BLOCK = 2
};

typedef struct rfauds2_refill_state {
    volatile unsigned int active_source;
    volatile unsigned int next_real_block;
    volatile unsigned int pending_mask;
    volatile unsigned int ready_mask;
    volatile unsigned int missed_refills;
    volatile int running;
} rfauds2_refill_state;

void rfauds2_refill_state_init(rfauds2_refill_state *state);
void rfauds2_refill_state_start(rfauds2_refill_state *state);
void rfauds2_refill_state_stop(rfauds2_refill_state *state);
int rfauds2_refill_state_complete(rfauds2_refill_state *state);
int rfauds2_refill_state_take_pending(rfauds2_refill_state *state);
void rfauds2_refill_state_mark_ready(
    rfauds2_refill_state *state,
    unsigned int block);
unsigned int rfauds2_refill_state_take_missed(
    rfauds2_refill_state *state);

#endif
