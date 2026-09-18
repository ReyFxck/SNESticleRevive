/*
 * Host-side smoke tests for SNESticleRevive's NEC uPD7725 DSP-4 core.
 *
 * No Nintendo/NEC firmware is included. One test uses synthetic uPD7725
 * instructions to validate the raw CPU path; the other validates the
 * self-contained replacement command program used by normal builds.
 */

#include "sndsp4.h"

#include <cstdio>
#include <cstring>

static void buildSyntheticFirmware(Uint8 *image, bool littleEndian)
{
    std::memset(image, 0, 0x2000);

    // OP: SRC=DR (source 8), DST=NON, ALU=0 => opcode 0x000080.
    for (int i = 0; i < 0x800; ++i)
    {
        Uint8 *p = image + i * 3;
        if (littleEndian)
        {
            p[0] = 0x80;
            p[1] = 0x00;
            p[2] = 0x00;
        }
        else
        {
            p[0] = 0x00;
            p[1] = 0x00;
            p[2] = 0x80;
        }
    }
}

static int exerciseRawNec(const char *name, Uint8 *image)
{
    SNDSP4 dsp;
    int failed = 0;

    if (!dsp.LoadFirmware(image, 0x2000))
    {
        std::printf("FAIL %s: synthetic program did not boot\n", name);
        return 1;
    }

    if ((dsp.ReadStatus(0) & 0x80) == 0)
    {
        std::printf("FAIL %s: RQM not set after boot\n", name);
        failed++;
    }

    dsp.WriteData(0, 0x34);
    dsp.WriteData(0, 0x12);
    Uint8 lo = dsp.ReadData(0);
    Uint8 hi = dsp.ReadData(0);

    if (lo != 0x34 || hi != 0x12)
    {
        std::printf("FAIL %s: DR roundtrip %02X %02X\n", name, lo, hi);
        failed++;
    }

    if (!failed)
        std::printf("PASS %s\n", name);
    return failed;
}

static void writeWord(SNDSP4 &dsp, Uint16 v)
{
    dsp.WriteData(0, (Uint8)v);
    dsp.WriteData(0, (Uint8)(v >> 8));
}

static Uint16 readWord(SNDSP4 &dsp)
{
    Uint16 lo = dsp.ReadData(0);
    Uint16 hi = dsp.ReadData(0);
    return (Uint16)(lo | (hi << 8));
}

static int exerciseReplacement()
{
    SNDSP4 dsp;
    int failed = 0;

    dsp.UseReplacementProgram();

    if ((dsp.ReadStatus(0) & 0x80) == 0)
    {
        std::printf("FAIL replacement: RQM not ready\n");
        failed++;
    }

    // Op 0000: signed 16-bit multiplication -> signed 32-bit product.
    writeWord(dsp, 0x0000);
    writeWord(dsp, 3);
    writeWord(dsp, (Uint16)(Int16)-7);
    Uint32 mul = (Uint32)readWord(dsp);
    mul |= (Uint32)readWord(dsp) << 16;
    if ((Int32)mul != -21)
    {
        std::printf("FAIL replacement: multiply=%08X\n", (unsigned)mul);
        failed++;
    }

    // Op 000A: documented nibble mapping.
    writeWord(dsp, 0x000a);
    writeWord(dsp, 0);
    writeWord(dsp, 0x0123);
    writeWord(dsp, 0);
    Uint16 a0 = readWord(dsp);
    Uint16 a1 = readWord(dsp);
    Uint16 a2 = readWord(dsp);
    Uint16 a3 = readWord(dsp);
    if (a0 != 0x0030 || a1 != 0x0000 || a2 != 0x0090 || a3 != 0x0060)
    {
        std::printf("FAIL replacement: op0A=%04X %04X %04X %04X\n",
                    a0, a1, a2, a3);
        failed++;
    }

    // Op 0011: horizontal packing helper; zeroes must remain zero.
    writeWord(dsp, 0x0011);
    writeWord(dsp, 0);
    writeWord(dsp, 0);
    writeWord(dsp, 0);
    writeWord(dsp, 0);
    if (readWord(dsp) != 0)
    {
        std::printf("FAIL replacement: op11 zero vector\n");
        failed++;
    }

    // OAM command sequence: select 1P, clear, add one sprite, fetch high table.
    writeWord(dsp, 0x0003);
    writeWord(dsp, 0x0005);
    writeWord(dsp, 0x000b);
    writeWord(dsp, 10);
    writeWord(dsp, 20);
    writeWord(dsp, 0x1234);

    if (readWord(dsp) != 1)
    {
        std::printf("FAIL replacement: op0B sprite rejected\n");
        failed++;
    }
    (void)readWord(dsp); // packed x/y
    (void)readWord(dsp); // attr

    writeWord(dsp, 0x0006);
    for (int i = 0; i < 16; ++i)
        (void)readWord(dsp);

    if (!failed)
        std::printf("PASS self-contained replacement program\n");
    return failed;
}

int main()
{
    Uint8 little[0x2000];
    Uint8 big[0x2000];
    int failed = 0;

    buildSyntheticFirmware(little, true);
    buildSyntheticFirmware(big, false);

    failed += exerciseRawNec("little-endian synthetic NEC", little);
    failed += exerciseRawNec("big-endian synthetic NEC", big);
    failed += exerciseReplacement();

    std::printf("%s (%d failure%s)\n",
                failed ? "FAILED" : "PASSED",
                failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
