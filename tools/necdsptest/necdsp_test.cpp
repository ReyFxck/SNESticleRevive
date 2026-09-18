/*
 * Host-side smoke test for SNESticleRevive's NEC uPD7725 DSP core.
 *
 * The test firmware is synthetic and contains no Nintendo/NEC firmware data.
 * Every program word is a tiny OP instruction whose source is DR; reading DR
 * inside the NEC core asserts RQM. This is enough to validate boot, the
 * 16-bit host handshake and firmware byte-order handling.
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

static int exercise(const char *name, Uint8 *image)
{
    SNDSP4 dsp;
    int failed = 0;

    if (!dsp.LoadFirmware(image, 0x2000))
    {
        std::printf("FAIL %s: firmware did not boot\n", name);
        return 1;
    }

    if ((dsp.ReadStatus(0) & 0x80) == 0)
    {
        std::printf("FAIL %s: RQM not set after boot\n", name);
        failed++;
    }

    // Default DRC=0 => 16-bit transfer, low byte then high byte.
    dsp.WriteData(0, 0x34);
    if ((dsp.ReadStatus(0) & 0x80) == 0)
    {
        std::printf("FAIL %s: RQM dropped after first 16-bit byte\n", name);
        failed++;
    }

    dsp.WriteData(0, 0x12);
    if ((dsp.ReadStatus(0) & 0x80) == 0)
    {
        std::printf("FAIL %s: RQM not restored after complete word\n", name);
        failed++;
    }

    Uint8 lo = dsp.ReadData(0);
    Uint8 hi = dsp.ReadData(0);
    if (lo != 0x34 || hi != 0x12)
    {
        std::printf("FAIL %s: DR roundtrip %02X %02X (expected 34 12)\n",
                    name, lo, hi);
        failed++;
    }

    if (!failed)
        std::printf("PASS %s\n", name);
    return failed;
}

int main()
{
    Uint8 little[0x2000];
    Uint8 big[0x2000];
    int failed = 0;

    buildSyntheticFirmware(little, true);
    buildSyntheticFirmware(big, false);

    failed += exercise("little-endian MesenCE layout", little);
    failed += exercise("big-endian fallback", big);

    std::printf("%s (%d failure%s)\n",
                failed ? "FAILED" : "PASSED",
                failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
