
#ifndef _PS2MEM_H
#define _PS2MEM_H


#define PS2MEM_ADDR_SCRATCHPAD      (0x70000000)
#define PS2MEM_ADDR_VU0MICROMEM     (0x11000000)
#define PS2MEM_ADDR_VU0DATAMEM      (0x11004000)
#define PS2MEM_ADDR_VU1MICROMEM     (0x11008000)
#define PS2MEM_ADDR_VU1DATAMEM      (0x1100C000)

#define PS2MEM_SCRATCHPAD           (PS2MEM_ADDR_SCRATCHPAD)

/* O mixer SPC usa o inicio do scratchpad e o renderer/GS ocupa a regiao
   abaixo de 8 KiB. Os 4,5 KiB finais guardam os lookups planares quentes
   do SNES sem disputar o cache de dados de 8 KiB da EE. */
#define PS2MEM_SNES_LOOKUP_OFFSET   (11 * 1024)
#define PS2MEM_SNES_LOOKUP_ADDR     (PS2MEM_SCRATCHPAD + PS2MEM_SNES_LOOKUP_OFFSET)
#define PS2MEM_SNES_LOOKUP_SIZE     (4608)


#define PS2MEM_CACHED(_Addr)        (((Uint32)_Addr)&0x0FFFFFFF)
#define PS2MEM_UNCACHED(_Addr)      (((Uint32)_Addr)|0x20000000)
#define PS2MEM_UNCACHEDACCEL(_Addr) (((Uint32)_Addr)|0x30000000)


#endif
