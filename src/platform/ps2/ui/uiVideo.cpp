/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements uiVideo behavior for the PlayStation 2 user interface.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#include "font.h"
#include "poly.h"
#include "texture.h"
#include "uiIcons.h"
#include "uiChrome.h"
#include "uiVideo.h"

extern "C" {
#include "gskit_backend.h"
}
#include "memcard.h"
#include "uiCover.h"
#include "mainloop_bgm.h"
#include "mainloop_smb.h"
#include "mainloop_safe_frameskip.h"
#include "audmixbuffer.h"
#include "embedded_irx.h"   /* HddSupportIsEnabled / HddSupportSetEnabled */
#include "snppucolor.h"
#include "../i18n/i18n.h"

/* mc0:/SNESticle (defined in mainloop_globals.cpp). */
extern Char _SramPath[256];
extern TextureT _OutTex;

#define VIDEO_ITEM_COUNT 20

/* Persistence                                                         */

#define VIDEOCFG_MAGIC   0x53564944u   /* 'SVID' */
#define VIDEOCFG_VERSION 21

typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;     /* volume da trilha de menu: 0=off, 1..100 */
	Int32  bgmrate;    /* frequencia de sintese da trilha (Hz)     */
	Int32  gamevol;    /* volume do audio do jogo (SNES/NES): 0..100 */
	Int32  hddenable;  /* suporte ao HD interno (hdd0:): 0=off, 1=on  */
	Int32  mmceenable; /* suporte a MMCE (mmce0/1): 0=off, 1=on       */
	Int32  massenable; /* mass/USB (mass0/1): 0=off, 1=on             */
	Int32  smbenable;  /* historical host slot; now smb: 0=off, 1=on */
	Int32  mx4sioenable; /* MX4SIO (SD via SIO2): 0=off, 1=on         */
	Int32  colorprofile; /* SNPPU_COLOR_PROFILE_*                     */
	Int32  frameskip;    /* recuperacao adaptativa: 0=off, 1=on       */
	Int32  hostenable;   /* emulator/ps2link HostFS (host:): 0=off,1=on */
	Int32  texturefilter;/* 0=Sharp/nearest, 1=Smooth/linear            */
	Int32  scanlines;    /* 0=off, 1=CRT-style overlay                  */
	Int32  language;     /* I18nLanguageE                                */
} VideoCfgT;

/* v20 is the exact prefix before UI language selection was added. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  frameskip;
	Int32  hostenable;
	Int32  texturefilter;
	Int32  scanlines;
} VideoCfgV20T;

/* v19 added HostFS as its own setting. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  frameskip;
	Int32  hostenable;
} VideoCfgV19T;

/* v18 is the exact prefix before HostFS was restored as its own option. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  frameskip;
} VideoCfgV18T;

/* v17 added colorprofile to the v16 prefix. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
} VideoCfgV17T;

/* v16 is the exact prefix written by v1.0.4 and by the first video-fix
   test build. Keep it readable so installing this build never resets the
   user's mode, offsets, audio volumes or storage choices. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  hostenable;
	Int32  mx4sioenable;
} VideoCfgV16T;

typedef struct
{
	Uint32 magic;
	Int32  version;
} VideoCfgHeaderT;

static void _VideoCfgPath(char *pOut)
{
	strcpy(pOut, _SramPath);
	strcat(pOut, "/video.cfg");
}

void VideoSettingsSave(void)
{
	VideoCfgT cfg;
	char      path[300];

	cfg.magic   = VIDEOCFG_MAGIC;
	cfg.version = VIDEOCFG_VERSION;
	cfg.mode    = g_GskVideoMode;
	cfg.offx    = g_GskDispOffX;
	cfg.offy    = g_GskDispOffY;
	cfg.overscan   = g_GskOverscan;
	cfg.widescreen = g_GskWidescreen;
	cfg.covers     = CoverIsEnabled() ? 1 : 0;
	cfg.bgmvol     = BgmGetVolume();
	cfg.bgmrate    = BgmGetRate();
	cfg.gamevol    = AudMixGameGetVolume();
	cfg.hddenable  = HddSupportIsEnabled() ? 1 : 0;
	cfg.mmceenable = MmceSupportIsEnabled() ? 1 : 0;
	cfg.massenable = MassStorageIsEnabled() ? 1 : 0;
	cfg.smbenable  = SmbSupportIsEnabled() ? 1 : 0;
	cfg.mx4sioenable = Mx4sioIsEnabled() ? 1 : 0;
	cfg.colorprofile = SNPPUColorGetProfile();
	cfg.frameskip = MainLoopSafeFrameskipIsEnabled() ? 1 : 0;
	cfg.hostenable = HostFsSupportIsEnabled() ? 1 : 0;
	cfg.texturefilter = g_GskTextureFilter ? 1 : 0;
	cfg.scanlines = g_GskScanlines ? 1 : 0;
	cfg.language = I18nGetLanguage();

	_VideoCfgPath(path);
	BgmIOBegin();
	MemCardWriteFile(path, (Uint8 *)&cfg, sizeof(cfg));
	BgmIOEnd();
}

void VideoSettingsLoad(void)
{
	VideoCfgT cfg;
	VideoCfgV20T oldcfg20;
	VideoCfgV19T oldcfg19;
	VideoCfgV18T oldcfg18;
	VideoCfgV17T oldcfg17;
	VideoCfgV16T oldcfg;
	VideoCfgHeaderT header;
	char      path[300];
	Bool      loaded = FALSE;

	memset(&cfg, 0, sizeof(cfg));
	_VideoCfgPath(path);

	memset(&header, 0, sizeof(header));
	if (MemCardReadFile(path, (Uint8 *)&header, sizeof(header)) &&
	    header.magic == VIDEOCFG_MAGIC)
	{
		if (header.version == VIDEOCFG_VERSION)
		{
			loaded = MemCardReadFile(path, (Uint8 *)&cfg, sizeof(cfg));
		}
		else if (header.version == 20)
		{
			memset(&oldcfg20, 0, sizeof(oldcfg20));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg20, sizeof(oldcfg20)))
			{
				memcpy(&cfg, &oldcfg20, sizeof(oldcfg20));
				cfg.version = VIDEOCFG_VERSION;
				cfg.language = I18N_ENGLISH;
				loaded = TRUE;
			}
		}
		else if (header.version == 19)
		{
			memset(&oldcfg19, 0, sizeof(oldcfg19));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg19, sizeof(oldcfg19)))
			{
				memcpy(&cfg, &oldcfg19, sizeof(oldcfg19));
				cfg.version = VIDEOCFG_VERSION;
				cfg.texturefilter = 0;
				cfg.scanlines = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 18)
		{
			memset(&oldcfg18, 0, sizeof(oldcfg18));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg18, sizeof(oldcfg18)))
			{
				memcpy(&cfg, &oldcfg18, sizeof(oldcfg18));
				cfg.version = VIDEOCFG_VERSION;
				cfg.hostenable = 0;
				cfg.texturefilter = 0;
				cfg.scanlines = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 17)
		{
			memset(&oldcfg17, 0, sizeof(oldcfg17));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg17, sizeof(oldcfg17)))
			{
				memcpy(&cfg, &oldcfg17, sizeof(oldcfg17));
				cfg.version = VIDEOCFG_VERSION;
				cfg.frameskip = 0;
				cfg.hostenable = 0;
				cfg.texturefilter = 0;
				cfg.scanlines = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 16)
		{
			memset(&oldcfg, 0, sizeof(oldcfg));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg, sizeof(oldcfg)))
			{
				/* v16's slot really was HostFS. Do not reinterpret that
				   preference as SMB when importing the old file. */
				memcpy(&cfg, &oldcfg, sizeof(oldcfg));
				cfg.version = VIDEOCFG_VERSION;
				cfg.smbenable = 0;
				cfg.colorprofile = SNPPU_COLOR_PROFILE_ORIGINAL;
				cfg.frameskip = 0;
				cfg.hostenable = oldcfg.hostenable ? 1 : 0;
				cfg.texturefilter = 0;
				cfg.scanlines = 0;
				loaded = TRUE;
			}
		}
	}

	if (loaded && cfg.magic == VIDEOCFG_MAGIC)
	{
		/* v1.0.2 allowed both SIO2 storage hooks to be saved at once.
		   Prefer MMCE when importing such a legacy config; all new changes
		   are mutually exclusive in the setters below. */
		if (cfg.mmceenable == 1 && cfg.mx4sioenable == 1)
			cfg.mx4sioenable = 0;

		if (cfg.mode == GSK_VIDMODE_240P ||
		    cfg.mode == GSK_VIDMODE_480I ||
		    cfg.mode == GSK_VIDMODE_1080I)
			g_GskVideoMode = cfg.mode;
		else
			g_GskVideoMode = GSK_VIDMODE_480I;

		if (cfg.offx >= -64 && cfg.offx <= 64) g_GskDispOffX = cfg.offx;
		if (cfg.offy >= -64 && cfg.offy <= 64) g_GskDispOffY = cfg.offy;
		if (cfg.overscan >= 0 && cfg.overscan <= 100) g_GskOverscan = cfg.overscan;
		if (cfg.widescreen == 0 || cfg.widescreen == 1) g_GskWidescreen = cfg.widescreen;
		if (cfg.covers == 0 || cfg.covers == 1) CoverSetEnabled(cfg.covers ? TRUE : FALSE);
		if (cfg.bgmvol >= 0 && cfg.bgmvol <= 100) BgmSetVolume(cfg.bgmvol);
		if (cfg.bgmrate >= 8000 && cfg.bgmrate <= 48000) BgmSetRate(cfg.bgmrate);
		if (cfg.gamevol >= 0 && cfg.gamevol <= 100) AudMixGameSetVolume(cfg.gamevol);
		if (cfg.hddenable == 0 || cfg.hddenable == 1) HddSupportSetEnabled(cfg.hddenable);
		if (cfg.mmceenable == 0 || cfg.mmceenable == 1) MmceSupportSetEnabled(cfg.mmceenable);
		if (cfg.massenable == 0 || cfg.massenable == 1) MassStorageSetEnabled(cfg.massenable);
		if (cfg.smbenable == 0 || cfg.smbenable == 1) SmbSupportSetEnabled(cfg.smbenable);
		if (cfg.mx4sioenable == 0 || cfg.mx4sioenable == 1) Mx4sioSetEnabled(cfg.mx4sioenable);
		if (cfg.colorprofile >= 0 && cfg.colorprofile < SNPPU_COLOR_PROFILE_COUNT)
			SNPPUColorSetProfile(cfg.colorprofile);
		if (cfg.frameskip == 0 || cfg.frameskip == 1)
			MainLoopSafeFrameskipSetEnabled(cfg.frameskip ? TRUE : FALSE);
		if (cfg.hostenable == 0 || cfg.hostenable == 1)
			HostFsSupportSetEnabled(cfg.hostenable);
		if (cfg.texturefilter == 0 || cfg.texturefilter == 1)
			g_GskTextureFilter = cfg.texturefilter;
		if (cfg.scanlines == 0 || cfg.scanlines == 1)
			g_GskScanlines = cfg.scanlines;
		if (cfg.language >= 0 && cfg.language < I18N_LANGUAGE_COUNT)
			I18nSetLanguage(cfg.language);
	}
}

/* Screen                                                              */

CVideoScreen::CVideoScreen()
{
	m_iSelect = 0;
}

void CVideoScreen::Process()
{
}

static void _VideoCenter(int x, int y, const char *pStr)
{
	FontPuts(x - FontGetStrWidth(pStr) / 2, y, pStr);
}

static void _VideoRight(int x, int y, const char *pStr)
{
	FontPuts(x - FontGetStrWidth(pStr), y, pStr);
}

static void _VideoSection(int vy, const char *pStr)
{
	/* Configurations is a scrolling viewport. Section headers must obey the
	   same clip window as rows or they bleed into the title/footer while
	   scrolling. */
	if (vy < 34 || vy > 181)
		return;
	UiChromeSection(vy, pStr);
}

static void _VideoRow(int vy, int idx, int sel, const char *pLabel, const char *pValue)
{
	if (vy < 34 || vy > 181)
		return;

	if (idx == sel)
	{
		PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
		PolyRect(44, vy - 1, 168, FontGetHeight() + 2);
	}

	FontColor4f(0.58f, 0.58f, 0.58f, 1.0f);
	FontPuts(52, vy, pLabel);

	FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	FontPuts(151, vy, pValue);
}

static const Int32 _VideoItemY[] =
{
	14, 26, 38, 50, 62, 74, 86, 98,       /* Screen */
	128, 140,                               /* Interface */
	170, 182, 194,                          /* Audio */
	224,                                    /* Performance */
	254, 266, 278, 290, 302, 314           /* Storage */
};

#define VIDEO_VIEW_TOP   34
#define VIDEO_VIEW_BOTTOM 181
#define VIDEO_CONTENT_H 326

static Int32 _VideoScrollForSelection(Int32 sel)
{
	const Int32 viewH = VIDEO_VIEW_BOTTOM - VIDEO_VIEW_TOP;
	const Int32 keepAt = viewH - 26;
	Int32 scroll = 0;
	Int32 maxScroll = VIDEO_CONTENT_H - viewH;

	if (sel < 0) sel = 0;
	if (sel >= VIDEO_ITEM_COUNT) sel = VIDEO_ITEM_COUNT - 1;
	if (_VideoItemY[sel] > keepAt)
		scroll = _VideoItemY[sel] - keepAt;
	if (scroll > maxScroll) scroll = maxScroll;
	if (scroll < 0) scroll = 0;
	return scroll;
}

static const char *_VideoMmceStatus()
{
	int slots;

	if (!MmceSupportIsEnabled()) return "Off";
	if (MmceNeedsRestart())      return "Restart";
	if (MmceGetLastError() < 0)  return "Driver Error";
	if (!MmceIsLoaded())         return "On";

	slots = MmceGetAvailableSlots();
	if (slots == 1) return "Slot 1";
	if (slots == 2) return "Slot 2";
	if (slots == 3) return "Slots 1+2";
	return "Not Found";
}

static const char *_VideoMx4sioStatus()
{
	static char errorText[24];
	int error;

	if (!Mx4sioIsEnabled())   return "Off";
	if (Mx4sioNeedsRestart()) return "Restart";

	error = Mx4sioGetLastError();
	if (error < 0)
	{
		snprintf(errorText, sizeof(errorText), "Err %d", error);
		return errorText;
	}

	return Mx4sioIsLoaded() ? "On" : "Enabled";
}

typedef struct
{
	Int32 mode;
	const char *name;
} VideoModeChoiceT;

static const VideoModeChoiceT _VideoModes[] =
{
	{ GSK_VIDMODE_240P,  "240p/288p (CRT)" },
	{ GSK_VIDMODE_480I,  "480i (default)" },
	{ GSK_VIDMODE_1080I, "1080i" }
};

static Int32 _VideoModeIndex(Int32 mode)
{
	Int32 i;
	for (i = 0; i < (Int32)(sizeof(_VideoModes) / sizeof(_VideoModes[0])); i++)
		if (_VideoModes[i].mode == mode)
			return i;
	return 0;
}

void CVideoScreen::Draw()
{
	char buf[24];
	Int32 scroll = _VideoScrollForSelection(m_iSelect);
	Int32 y;
	Int32 m = _VideoModeIndex(g_GskVideoMode);
	const char *pMode = _VideoModes[m].name;
	const char *pWide = g_GskWidescreen ? "On" : "Off";
	const char *pColor =
		(SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_COMPOSITE)
		? "Composite" : "Original";

	FontSelect(0);
	UiChromeHeader("CONFIGURATIONS", TRUE);

	/* Screen */
	_VideoSection(VIDEO_VIEW_TOP + 0 - scroll, "Screen");
	y = VIDEO_VIEW_TOP + _VideoItemY[0] - scroll;
	_VideoRow(y, 0, m_iSelect, "Video Mode", pMode);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[1] - scroll, 1, m_iSelect,
	          "Widescreen", pWide);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[2] - scroll, 2, m_iSelect,
	          "SNES Colors", pColor);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[3] - scroll, 3, m_iSelect,
	          "Filter", g_GskTextureFilter ? "Smooth" : "Sharp");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[4] - scroll, 4, m_iSelect,
	          "Scanlines", g_GskScanlines ? "On" : "Off");
	snprintf(buf, sizeof(buf), "%d", g_GskOverscan);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[5] - scroll, 5, m_iSelect,
	          "Overscan", buf);
	snprintf(buf, sizeof(buf), "%d", g_GskDispOffX);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[6] - scroll, 6, m_iSelect,
	          "Offset X", buf);
	snprintf(buf, sizeof(buf), "%d", g_GskDispOffY);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[7] - scroll, 7, m_iSelect,
	          "Offset Y", buf);

	/* Interface */
	_VideoSection(VIDEO_VIEW_TOP + 114 - scroll, "Interface");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[8] - scroll, 8, m_iSelect,
	          "Cover Art", CoverIsEnabled() ? "On" : "Off");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[9] - scroll, 9, m_iSelect,
	          "Language", I18nGetLanguageName());

	/* Audio */
	_VideoSection(VIDEO_VIEW_TOP + 156 - scroll, "Audio");
	snprintf(buf, sizeof(buf), "%d", AudMixGameGetVolume());
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[10] - scroll, 10, m_iSelect,
	          "Game Volume", buf);
	{
		int bv = BgmGetVolume();
		if (bv <= 0)
			snprintf(buf, sizeof(buf), "Off");
		else if (BgmTrackCount() <= 0)
			snprintf(buf, sizeof(buf), BgmIsSearching() ? "Searching" : "No Track");
		else
			snprintf(buf, sizeof(buf), "%d", bv);
	}
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[11] - scroll, 11, m_iSelect,
	          "Menu Music", buf);
	snprintf(buf, sizeof(buf), "%d kHz", (BgmGetRate() + 500) / 1000);
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[12] - scroll, 12, m_iSelect,
	          "Frequency", buf);

	/* Performance */
	_VideoSection(VIDEO_VIEW_TOP + 210 - scroll, "Performance");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[13] - scroll, 13, m_iSelect,
	          "Frameskip", MainLoopSafeFrameskipIsEnabled() ? "On" : "Off");

	/* Storage */
	_VideoSection(VIDEO_VIEW_TOP + 240 - scroll, "Storage / Devices");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[14] - scroll, 14, m_iSelect,
	          "Mass / USB", MassStorageIsEnabled() ? "On" : "Off");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[15] - scroll, 15, m_iSelect,
	          "HDD Support", HddSupportIsEnabled() ? "On" : "Off");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[16] - scroll, 16, m_iSelect,
	          "MMCE Cards", _VideoMmceStatus());
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[17] - scroll, 17, m_iSelect,
	          "MX4SIO (SD)", _VideoMx4sioStatus());
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[18] - scroll, 18, m_iSelect,
	          "HostFS (Emu)", HostFsSupportIsEnabled() ? "On" : "Off");
	_VideoRow(VIDEO_VIEW_TOP + _VideoItemY[19] - scroll, 19, m_iSelect,
	          "SMB (Network)", SmbGetStatusText());

	UiChromeScroll(scroll > 0,
	               scroll < VIDEO_CONTENT_H - (VIDEO_VIEW_BOTTOM - VIDEO_VIEW_TOP),
	               229, 36, 173);
	UiChromeHint(UI_ICON_UP, UI_ICON_DOWN, 12, 198, "Select");
	UiChromeHint(UI_ICON_LEFT, UI_ICON_RIGHT, 75, 198, "Change");
	UiChromeHint(UI_ICON_SQUARE, UI_ICON_COUNT, 139, 198, "Reset");
	UiChromeHint(UI_ICON_CROSS, UI_ICON_COUNT, 190, 198, "Save");

	if (g_GskVideoMode != GSK_GetActiveVideoMode() ||
	    MmceNeedsRestart() || Mx4sioNeedsRestart())
	{
		FontColor4f(1.0f, 0.86f, 0.35f, 1.0f);
		_VideoRight(238, 211, "Restart required");
	}
}

static void _VideoResetDefaults()
{
#ifndef BGM_RATE
#define BGM_RATE 24000
#endif
	/* Match the runtime's actual boot defaults, not merely the visible
	   offsets. Square is therefore a complete configuration reset. */
	g_GskVideoMode = GSK_VIDMODE_480I;
	g_GskWidescreen = 0;
	g_GskOverscan = 0;
	g_GskDispOffX = 0;
	g_GskDispOffY = 0;
	g_GskTextureFilter = 0;
	g_GskScanlines = 0;

	GSK_SetWidescreen(0);
	GSK_SetOverscan(0);
	GSK_SetDisplayOffset(0, 0);
	TextureSetFilter(&_OutTex, 0);
	SNPPUColorSetProfile(SNPPU_COLOR_PROFILE_ORIGINAL);

	CoverSetEnabled(FALSE);
	I18nSetLanguage(I18N_ENGLISH);
	AudMixGameSetVolume(100);
	BgmSetVolume(100);
	BgmSetRate(BGM_RATE);
	MainLoopSafeFrameskipSetEnabled(FALSE);

	MassStorageSetEnabled(1);
	HddSupportSetEnabled(0);
	MmceSupportSetEnabled(0);
	Mx4sioSetEnabled(0);
	HostFsSupportSetEnabled(0);
	if (SmbSupportIsEnabled())
	{
		BgmIOBegin();
		SmbDisconnect();
		BgmIOEnd();
	}
	SmbSupportSetEnabled(0);
}

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
	int dir = 0;

	/* Use the same global held-button repeat as the browser and the rest of
	   the menu. Analog-stick directions are already synthesized upstream. */
	if (trigger & PAD_UP)
	{
		m_iSelect--;
		if (m_iSelect < 0) m_iSelect = VIDEO_ITEM_COUNT - 1;
	}
	if (trigger & PAD_DOWN)
	{
		m_iSelect++;
		if (m_iSelect >= VIDEO_ITEM_COUNT) m_iSelect = 0;
	}

	if (trigger & PAD_LEFT)  dir = -1;
	if (trigger & PAD_RIGHT) dir = +1;

	if (dir != 0)
	{
		switch (m_iSelect)
		{
		case 0:
			{
				Int32 count = (Int32)(sizeof(_VideoModes) / sizeof(_VideoModes[0]));
				Int32 modeIndex = _VideoModeIndex(g_GskVideoMode) + dir;
				if (modeIndex < 0) modeIndex = count - 1;
				if (modeIndex >= count) modeIndex = 0;
				g_GskVideoMode = _VideoModes[modeIndex].mode;
			}
			break;
		case 1:
			g_GskWidescreen = !g_GskWidescreen;
			GSK_SetWidescreen(g_GskWidescreen);
			break;
		case 2:
			SNPPUColorSetProfile(
				SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_ORIGINAL
				? SNPPU_COLOR_PROFILE_COMPOSITE
				: SNPPU_COLOR_PROFILE_ORIGINAL);
			break;
		case 3:
			g_GskTextureFilter = !g_GskTextureFilter;
			TextureSetFilter(&_OutTex, g_GskTextureFilter);
			break;
		case 4:
			g_GskScanlines = !g_GskScanlines;
			break;
		case 5:
			g_GskOverscan += dir * 5;
			if (g_GskOverscan < 0) g_GskOverscan = 0;
			if (g_GskOverscan > 100) g_GskOverscan = 100;
			GSK_SetOverscan(g_GskOverscan);
			break;
		case 6:
			g_GskDispOffX += dir;
			if (g_GskDispOffX < -64) g_GskDispOffX = -64;
			if (g_GskDispOffX > 64) g_GskDispOffX = 64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;
		case 7:
			g_GskDispOffY += dir;
			if (g_GskDispOffY < -64) g_GskDispOffY = -64;
			if (g_GskDispOffY > 64) g_GskDispOffY = 64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;
		case 8:
			CoverToggle();
			break;
		case 9:
			I18nCycleLanguage(dir);
			break;
		case 10:
			{
				int v = AudMixGameGetVolume() + dir;
				if (v < 0) v = 0;
				if (v > 100) v = 100;
				AudMixGameSetVolume(v);
			}
			break;
		case 11:
			{
				int v = BgmGetVolume() + dir;
				if (v < 0) v = 0;
				if (v > 100) v = 100;
				BgmSetVolume(v);
			}
			break;
		case 12:
			BgmCycleRate(dir);
			break;
		case 13:
			MainLoopSafeFrameskipSetEnabled(
				MainLoopSafeFrameskipIsEnabled() ? FALSE : TRUE);
			break;
		case 14:
			MassStorageSetEnabled(!MassStorageIsEnabled());
			break;
		case 15:
			HddSupportSetEnabled(!HddSupportIsEnabled());
			break;
		case 16:
			MmceSupportSetEnabled(!MmceSupportIsEnabled());
			if (MmceSupportIsEnabled())
			{
				BgmIOBegin();
				MmceProbeAvailableSlots();
				BgmIOEnd();
			}
			break;
		case 17:
			Mx4sioSetEnabled(!Mx4sioIsEnabled());
			if (Mx4sioIsEnabled())
			{
				BgmIOBegin();
				Mx4sioLoadIfEnabled();
				BgmIOEnd();
			}
			break;
		case 18:
			HostFsSupportSetEnabled(!HostFsSupportIsEnabled());
			break;
		case 19:
			if (SmbSupportIsEnabled())
			{
				BgmIOBegin();
				SmbDisconnect();
				BgmIOEnd();
				SmbSupportSetEnabled(0);
			}
			else
			{
				SmbSupportSetEnabled(1);
			}
			break;
		}
	}

	if (trigger & PAD_SQUARE)
		_VideoResetDefaults();

	if (trigger & (PAD_CROSS | PAD_START))
		VideoSettingsSave();

	(void)buttons;
}
