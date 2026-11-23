#ifndef RFAUDS2_SPU2_DIRECT_H
#define RFAUDS2_SPU2_DIRECT_H

typedef int (*rfauds2_spu2_transfer_callback)(void *arg);

int rfauds2_spu2_init(
    rfauds2_spu2_transfer_callback callback,
    void *callback_arg);

void rfauds2_spu2_set_volume(unsigned int volume);

int rfauds2_spu2_start_loop(
    void *buffer,
    unsigned int total_bytes);

int rfauds2_spu2_stop(void);
int rfauds2_spu2_shutdown(void);

/* Completed real DMA blocks are queued here for the refill thread. */
int rfauds2_spu2_take_refill_block(void);

/* Publish a refilled block only after its cache lines have been written back. */
void rfauds2_spu2_mark_block_ready(unsigned int block);

/* Return and clear the number of hardware deadlines replaced with silence. */
unsigned int rfauds2_spu2_take_missed_refills(void);

/* Diagnostic helper: 0/1 for a real block, 2 while the silence block is
   active. */
unsigned int rfauds2_spu2_active_block(void);

#endif
