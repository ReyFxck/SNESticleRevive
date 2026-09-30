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
#define SNES_SHORTLINE_CYCLES (1360)
#define SNES_VISIBLE_CYCLES (1024)
#define SNES_SHORTLINE_INDEX (240)
#define SNES_VBLANK_START_LINE (225)

/* In non-interlace mode line 240 of the odd field is four master clocks
   shorter. Interlace uses the normal 1364-clock line on both fields. */
#define SNES_LINE_MASTER_CYCLES(_line, _field, _interlace) \
	((!(_interlace) && (_field) && (_line) == SNES_SHORTLINE_INDEX) \
		? SNES_SHORTLINE_CYCLES : SNES_CYCLESPERLINE)

#define SNES_LINECYCLEDELAY (40)
#define SNES_HBLANKCYCLES  (SNES_CYCLESPERLINE - SNES_VISIBLE_CYCLES)

/* The S-CPU's H/V timer compare is not visible at H=HTIME*4 immediately.
   The counter reset/compare circuit and IRQ pipeline add 14 master clocks
   (Snes9x/bsnes timing); H=0 has the documented one-dot special case. */
#define SNES_IRQ_TRIGGER_CYCLES (14)
#define SNES_HIRQ_CYCLES(_htime) \
	((Int32)(_htime) * 4 + SNES_IRQ_TRIGGER_CYCLES - ((_htime) ? 0 : 4))
#define SNES_VIRQ_CYCLES (SNES_IRQ_TRIGGER_CYCLES - 4)
#define SNES_LINE_IN_VBLANK(_line) ((_line) >= SNES_VBLANK_START_LINE)
#define SNES_SPCMINCYCLES 0
#define SNES_CYCLESPERFRAME (SNES_CYCLESPERLINE * 262)

#endif
