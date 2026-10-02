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

unsigned int rfauds2_spu2_active_block(void);

#endif
