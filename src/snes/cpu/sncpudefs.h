/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sncpudefs interface for SNES CPU emulation.
 */

#ifndef _SNCPUDEFS_H
#define _SNCPUDEFS_H

// cpu flags
#define SNCPU_FLAG_C	0x01
#define SNCPU_FLAG_Z	0x02
#define SNCPU_FLAG_I	0x04
#define SNCPU_FLAG_D	0x08
#define SNCPU_FLAG_X	0x10
#define SNCPU_FLAG_M	0x20
#define SNCPU_FLAG_V	0x40
#define SNCPU_FLAG_N	0x80
#define SNCPU_FLAG_B	0x10		// e=1

// e=0 vector locations
#define SNCPU_VECTORE_ABORT 0x00FFF8
#define SNCPU_VECTORE_BRK   0x00FFF6
#define SNCPU_VECTORE_COP   0x00FFF4
#define SNCPU_VECTORE_IRQ   0x00FFFE
#define SNCPU_VECTORE_NMI   0x00FFFA

// e=1
#define SNCPU_VECTOR_ABORT 0x00FFE8
#define SNCPU_VECTOR_BRK   0x00FFE6
#define SNCPU_VECTOR_COP   0x00FFE4
#define SNCPU_VECTOR_IRQ   0x00FFEE
#define SNCPU_VECTOR_NMI   0x00FFEA

#define SNCPU_VECTOR_RESET 0x00FFFC

// cpu signals (external pins)
#define SNCPU_SIGNAL_NMI		0x80
#define SNCPU_SIGNAL_NMIEDGE	0x40
/* Internal stopped state.  The old core reserved this bit for RDY but never
   implemented that external pin, so STP can use it without changing SNCpuT. */
#define SNCPU_SIGNAL_STP		0x20
#define SNCPU_SIGNAL_RESET		0x10
#define SNCPU_SIGNAL_DMA		0x08
#define SNCPU_SIGNAL_IRQ		0x04
#define SNCPU_SIGNAL_WAI		0x02
#define SNCPU_SIGNAL_RUN		0x01

// memory configuration
#define SNCPU_MEM_SIZE	  0x1010000
#define SNCPU_BANK_SHIFT  (13)
#define SNCPU_BANK_SIZE   (1<<SNCPU_BANK_SHIFT)
#define SNCPU_BANK_NUM    (SNCPU_MEM_SIZE / SNCPU_BANK_SIZE)
#define SNCPU_BANK_MASK   (SNCPU_BANK_SIZE - 1)

#define SNCPU_CYCLE_SLOW (8)
#define SNCPU_CYCLE_FAST (6)

/* SA-1 bus tags live in SNCpuBankT::uPad[0].  They keep the bank structure
   ABI unchanged for sn65816.S while giving the R5900 path O(1) arbitration. */
#define SNCPU_SA1_BUS_NONE       0
#define SNCPU_SA1_BUS_ROM        1
#define SNCPU_SA1_BUS_BWRAM      2
#define SNCPU_SA1_BUS_IRAM       3
#define SNCPU_SA1_BUS_MIX_LOW    4
#define SNCPU_SA1_BUS_MIX_HIGH   5
#define SNCPU_SA1_BUS_UNIT_MAX   (1024 * SNCPU_CYCLE_FAST)

#endif
