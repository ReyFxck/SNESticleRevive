#ifndef SNPPUVU0_H
#define SNPPUVU0_H

#include "snppumode7.h"
#include <string.h>

/* Experimental shadow renderer. No VU1, GIF, OAM or emulated state writes.
   Keep all intermediate coordinates in an exactly representable range.
   The target's non-IEEE arithmetic still requires on-target verification. */
struct SnesVU0CoordGroup {
    Int32 x[4];
    Int32 y[4];
} _ALIGN(16);

_INLINE bool SnesVU0Mode7Eligible(const SnesPPUMode7LineT &line)
{
    const Int64 limit = 1LL << 23;
    const Int64 endX = (Int64)line.x + (Int64)line.dx * 255;
    const Int64 endY = (Int64)line.y + (Int64)line.dy * 255;
    return line.x >= -limit && line.x <= limit &&
           line.y >= -limit && line.y <= limit &&
           endX >= -limit && endX <= limit && endY >= -limit && endY <= limit &&
           line.dx >= -32768 && line.dx <= 32768 &&
           line.dy >= -32768 && line.dy <= 32768;
}

_INLINE void SnesVU0Mode7Reference(Uint8 *out, const Uint8 *vram,
                                   const SnesPPUMode7LineT &line, unsigned mode)
{
    switch (mode & 3u) {
    case 0: case 1: SnesPPUMode7FetchRepeat(out, 256, vram, line.x, line.y, line.dx, line.dy); break;
    case 3: SnesPPUMode7FetchClamp(out, 256, vram, line.x, line.y, line.dx, line.dy); break;
    default: SnesPPUMode7FetchBlack(out, 256, vram, line.x, line.y, line.dx, line.dy); break;
    }
}

/* Scalar address/VRAM stage: preserve the original tile and pixel mapping. */
_INLINE void SnesVU0Mode7FetchCoords(Uint8 *out, const Uint8 *vram,
                                    const SnesVU0CoordGroup *coords, unsigned mode)
{
    Uint32 last = 0xffffffffu, chr = 0;
    for (unsigned g = 0; g < 64; ++g) {
        for (unsigned i = 0; i < 4; ++i) {
            const Int32 x = coords[g].x[i], y = coords[g].y[i];
            if ((mode & 2u) && ((Uint32)x | (Uint32)y) >= 0x40000u)
                *out++ = (mode & 1u) ? vram[SnesPPUMode7PixelByteOffset(x, y)] : 0;
            else
                *out++ = SnesPPUMode7RepeatPixel(vram,
                    SnesPPUMode7TileByteAddress(x, y), x, y, last, chr);
        }
    }
}

/* Host model tests the lane layout and arithmetic only, not COP2 semantics. */
_INLINE void SnesVU0Mode7ModelCoords(SnesVU0CoordGroup *coords,
                                     const SnesPPUMode7LineT &line)
{
    Float32 x[4], y[4];
    for (unsigned i = 0; i < 4; ++i) {
        x[i] = (Float32)(line.x + line.dx * (Int32)i);
        y[i] = (Float32)(line.y + line.dy * (Int32)i);
    }
    const Float32 dx = (Float32)(line.dx * 4), dy = (Float32)(line.dy * 4);
    for (unsigned g = 0; g < 64; ++g) {
        for (unsigned i = 0; i < 4; ++i) {
            coords[g].x[i] = (Int32)x[i]; coords[g].y[i] = (Int32)y[i];
            x[i] += dx; y[i] += dy;
        }
    }
}

#if CODE_PLATFORM == CODE_PS2 && defined(SNES_VU0_SHADOW) && SNES_VU0_SHADOW
/* Macro mode uses COP2 scratch VF1..VF6 on the renderer thread. No microcode
   or VIF channel ownership. All inputs and outputs are 16-byte aligned. */
_INLINE void SnesVU0Mode7TargetCoords(SnesVU0CoordGroup *coords,
                                      const SnesPPUMode7LineT &line)
{
    Float32 input[16] _ALIGN(16);
    for (unsigned i = 0; i < 4; ++i) {
        input[i] = (Float32)(line.x + line.dx * (Int32)i);
        input[4+i] = (Float32)(line.y + line.dy * (Int32)i);
        input[8+i] = (Float32)(line.dx * 4);
        input[12+i] = (Float32)(line.dy * 4);
    }
    unsigned count = 64;
    __asm__ __volatile__(
        ".set push\n\t.set noreorder\n\t"
        "lqc2 $vf1, 0(%2)\n\t"
        "lqc2 $vf2, 16(%2)\n\t"
        "lqc2 $vf3, 32(%2)\n\t"
        "lqc2 $vf4, 48(%2)\n\t"
        "1:\n\t"
        "vftoi0.xyzw $vf5, $vf1\n\t"
        "vftoi0.xyzw $vf6, $vf2\n\t"
        "vadd.xyzw $vf1, $vf1, $vf3\n\t"
        "vadd.xyzw $vf2, $vf2, $vf4\n\t"
        "sqc2 $vf5, 0(%0)\n\t"
        "sqc2 $vf6, 16(%0)\n\t"
        "addiu %1, %1, -1\n\t"
        "bnez %1, 1b\n\t"
        "addiu %0, %0, 32\n\t"
        ".set pop\n\t"
        : "+&r"(coords), "+&r"(count) : "r"(input) : "memory");
}
#endif

/* A mismatch latches off the experiment. The caller keeps the original
   output even on success, so no experimental data reaches the compositor. */
_INLINE bool SnesVU0Mode7Compare(const Uint8 *reference, const Uint8 *candidate,
                                 bool &disabled)
{
    if (disabled) return false;
    if (memcmp(reference, candidate, 256) != 0) {
        disabled = true;
        return false;
    }
    return true;
}
#endif
