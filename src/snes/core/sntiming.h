/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sntiming interface for the SNES emulation core.
 */

#ifndef _SNTIMING_H
#define _SNTIMING_H

#define SNES_CYCLESPERLINE (1364)
#define SNES_NTSC_TOTAL_LINES (262)
#define SNES_PAL_TOTAL_LINES  (312)

#define SNES_VBLANK_START_LINE          (225)
#define SNES_OVERSCAN_VBLANK_START_LINE (240)

#define SNES_NTSC_FRAME_RATE (60)
#define SNES_PAL_FRAME_RATE  (50)

/* S-CPU/PPU bus events within a normal 1364-master-clock scanline.
   DRAM refresh starts at 538-(masterClock&7) and stalls the CPU for 40.
   HVBJOY.HBlank asserts around dot 274; HDMA begins at dot 276. */
#define SNES_DRAM_REFRESH_BASE_CYCLES (538)
#define SNES_DRAM_REFRESH_CYCLES      (40)
#define SNES_HBLANK_START_CYCLES      (274 * 4)
#define SNES_HDMA_START_CYCLES        (276 * 4)

/* The S-CPU's H/V timer compare is not visible at H=HTIME*4 immediately.
   The counter reset/compare circuit and IRQ pipeline add 14 master clocks
   (Snes9x/bsnes timing); H=0 has the documented one-dot special case. */
#define SNES_IRQ_TRIGGER_CYCLES (14)
#define SNES_HIRQ_CYCLES(_htime) \
	((Int32)(_htime) * 4 + SNES_IRQ_TRIGGER_CYCLES - ((_htime) ? 0 : 4))
#define SNES_VIRQ_CYCLES (SNES_IRQ_TRIGGER_CYCLES - 4)
#define SNES_LINE_IN_VBLANK(_line) ((_line) >= SNES_VBLANK_START_LINE)
#define SNES_SPCMINCYCLES 0
#define SNES_CYCLESPERFRAME_NTSC (SNES_CYCLESPERLINE * SNES_NTSC_TOTAL_LINES)
#define SNES_CYCLESPERFRAME_PAL  (SNES_CYCLESPERLINE * SNES_PAL_TOTAL_LINES)

/* Compatibility alias for old code which explicitly assumed NTSC. */
#define SNES_CYCLESPERFRAME SNES_CYCLESPERFRAME_NTSC

#endif
