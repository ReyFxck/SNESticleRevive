/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snppucolor interface for SNES picture processing.
 */

#ifndef _SNPPUCOLOR_H
#define _SNPPUCOLOR_H

#define SNPPUCOLOR_NUM (0x8000)

struct SNPPUColorCalibT
{
	float	fBrightness;
	float	fIQAngle;
	float	fMaxExcursion;
};

#define SNPPU_COLOR_PROFILE_ORIGINAL  0
#define SNPPU_COLOR_PROFILE_COMPOSITE 1
#define SNPPU_COLOR_PROFILE_COUNT     2

Uint32 SNPPUColorConvert15to32(Uint16 uColor15);
void SNPPUColorCalibrate(const SNPPUColorCalibT *pCalib);
void SNPPUColorSetProfile(Int32 iProfile);
Int32 SNPPUColorGetProfile(void);
void SNPPUColorSetColors(const Uint32 *pColors, Int32 nColors);
Uint32 *SNPPUColorGetPalette();

#endif
