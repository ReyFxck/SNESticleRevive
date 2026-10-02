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
#include "snppumathidentity.h"

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

static void CheckIdentityMath()
{
	SNMaskT math = {}, half = {}, sub = {};
	CheckPixel("empty math", SnesPPUMathIsIdentity(&math, &half, &sub, TRUE, 0x123456), TRUE);
	for (unsigned bit = 0; bit < 256; ++bit)
	{
		std::memset(&math, 0, sizeof(math));
		math.uMask8[bit >> 3] = (Uint8)(1u << (bit & 7u));
		CheckPixel("black fixed operand", SnesPPUMathIsIdentity(&math, &half, &sub, FALSE, 0), TRUE);
		CheckPixel("transparent sub uses fixed black", SnesPPUMathIsIdentity(&math, &half, &sub, TRUE, 0), TRUE);
		for (unsigned channel = 0; channel < 24; ++channel)
			CheckPixel("nonblack converted fixed operand",
				SnesPPUMathIsIdentity(&math, &half, &sub, TRUE, 1u << channel), FALSE);
		half = math;
		CheckPixel("halving black is not identity", SnesPPUMathIsIdentity(&math, &half, &sub, FALSE, 0), FALSE);
		std::memset(&half, 0, sizeof(half));
		sub = math;
		CheckPixel("opaque sub is not proven black", SnesPPUMathIsIdentity(&math, &half, &sub, TRUE, 0), FALSE);
		CheckPixel("fixed selection ignores TS", SnesPPUMathIsIdentity(&math, &half, &sub, FALSE, 0), TRUE);
		std::memset(&sub, 0, sizeof(sub));
	}

	/* Independent portable blender oracle: every SNES color, addition and
	   subtraction, full and partial math masks, transparent sub/fixed black.
	   CGRAM[0] deliberately contains a different, nonblack color. */
	SNPPUBlendInfoT info = {};
	SNMaskT masks[3] = {};
	SNMaskSet(&masks[0]);
	Uint16 cgram[256] = {};
	cgram[0] = 0x7FFF;
	std::memset(info.uMain8, 1, sizeof(info.uMain8));
	Uint32 pixels[256];
	CRenderSurface surface;
	surface.Set((Uint8 *)pixels, 256, 1, sizeof(pixels), PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
	SNPPUBlendC blend;
	blend.Begin(&surface);
	blend.UpdatePalette(&info, cgram, 15);
	for (unsigned color = 0; color < 32768; ++color)
	{
		blend.UpdatePaletteEntry(&info, 1, color, 15);
		for (unsigned subtract = 0; subtract < 2; ++subtract)
		{
			std::memset(&masks[1], subtract ? 0xA5 : 0xFF, sizeof(masks[1]));
			blend.Exec(&info, 0, 0, masks, subtract, 15);
			for (unsigned x = 0; x < 256; ++x)
				CheckPixel("black math portable oracle", pixels[x], SNPPUColorConvert15to32((Uint16)color));
		}
	}
	blend.End();
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
	CheckIdentityMath();

	if (g_Failures)
	{
		std::printf("brightness_test: %d failure(s)\n", g_Failures);
		return 1;
	}
	std::printf("brightness_test: OK\n");
	return 0;
}
