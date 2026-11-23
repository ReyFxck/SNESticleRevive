#include "refill_state.h"

void rfauds2_refill_state_init(rfauds2_refill_state *state)
{
    state->active_source = 0;
    state->next_real_block = 1;
    state->pending_mask = 0;
    state->ready_mask = 0;
    state->missed_refills = 0;
    state->running = 0;
}

void rfauds2_refill_state_start(rfauds2_refill_state *state)
{
    state->active_source = 0;
    state->next_real_block = 1;
    state->pending_mask = 0;
    /* Both halves were primed. Block 0 starts immediately; block 1 is next. */
    state->ready_mask = 1u << 1;
    state->missed_refills = 0;
    state->running = 1;
}

void rfauds2_refill_state_stop(rfauds2_refill_state *state)
{
    state->running = 0;
    state->pending_mask = 0;
    state->ready_mask = 0;
    state->active_source = 0;
    state->next_real_block = 1;
}

int rfauds2_refill_state_complete(rfauds2_refill_state *state)
{
    unsigned int completed;
    unsigned int next;
    unsigned int next_mask;

    if (!state->running)
        return 0;

    completed = state->active_source;
    if (completed < RFAUDS2_REFILL_REAL_BLOCKS)
        state->pending_mask |= 1u << completed;

    next = state->next_real_block;
    next_mask = 1u << next;

    if ((state->ready_mask & next_mask) != 0u) {
        state->ready_mask &= ~next_mask;
        state->active_source = next;
        state->next_real_block = 1u - next;
    } else {
        state->active_source = RFAUDS2_REFILL_SILENCE_BLOCK;
        if (state->missed_refills != ~0u)
            ++state->missed_refills;
    }

    return completed < RFAUDS2_REFILL_REAL_BLOCKS;
}

int rfauds2_refill_state_take_pending(rfauds2_refill_state *state)
{
    if ((state->pending_mask & 1u) != 0u) {
        state->pending_mask &= ~1u;
        return 0;
    }

    if ((state->pending_mask & 2u) != 0u) {
        state->pending_mask &= ~2u;
        return 1;
    }

    return -1;
}

void rfauds2_refill_state_mark_ready(
    rfauds2_refill_state *state,
    unsigned int block)
{
    if (state->running && block < RFAUDS2_REFILL_REAL_BLOCKS)
        state->ready_mask |= 1u << block;
}

unsigned int rfauds2_refill_state_take_missed(
    rfauds2_refill_state *state)
{
    unsigned int missed = state->missed_refills;
    state->missed_refills = 0;
    return missed;
}
