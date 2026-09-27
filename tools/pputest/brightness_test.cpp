/*
 * Host regression for SNES INIDISP master brightness.
 */

#include <cstdio>
#include <cstring>

#include "types.h"
#include "pixelformat.h"
#include "rendersurface.h"
#include "snmask.h"
#include "snppublend_c.h"
#include "snppucolor.h"

static int g_Failures;

static void CheckPixel(const char *pName, Uint32 uGot, Uint32 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %08X != %08X\n", pName,
			(unsigned)uGot, (unsigned)uExpected);
		g_Failures++;
	}
}

int main()
{
	SNPPUColorCalibT calib = { 0.9f, 20.0f, 0.2f };
	SNPPUBlendInfoT info;
	SNMaskT masks[3];
	Uint16 cgram[256];
	Uint32 pixels[3][256];
	CRenderSurface surface;
	SNPPUBlendC blend;

	SNPPUColorSetProfile(SNPPU_COLOR_PROFILE_ORIGINAL);
	SNPPUColorCalibrate(&calib);
	std::memset(&info, 0, sizeof(info));
	std::memset(cgram, 0, sizeof(cgram));
	std::memset(pixels, 0xCD, sizeof(pixels));
	std::memset(info.uMain8, 1, sizeof(info.uMain8));
	cgram[1] = 0x7FFF;

	SNMaskSet(&masks[0]);
	SNMaskClear(&masks[1]);
	SNMaskClear(&masks[2]);

	surface.Set((Uint8 *)pixels, 256, 3, 256 * sizeof(Uint32),
		PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
	blend.Begin(&surface);
	blend.UpdatePalette(&info, cgram, 15);

	/* Change only INIDISP between lines. This catches implementations that
	   incorrectly bake brightness into CGRAM and leave fade frames stale. */
	blend.Exec(&info, 0, 0, masks, FALSE, 15);
	blend.Exec(&info, 1, 0, masks, FALSE, 7);
	blend.Exec(&info, 2, 0, masks, FALSE, 0);
	blend.End();

	CheckPixel("brightness 15", pixels[0][0], 0x00FFFFFFu);
	CheckPixel("brightness 7", pixels[1][0], 0x00777777u);
	CheckPixel("brightness 0", pixels[2][0], 0x00000000u);
	CheckPixel("whole scanline", pixels[1][255], 0x00777777u);

	if (g_Failures)
	{
		std::printf("brightness_test: %d failure(s)\n", g_Failures);
		return 1;
	}
	std::printf("brightness_test: OK\n");
	return 0;
}
