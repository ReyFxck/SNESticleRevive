
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#include "font.h"
#include "poly.h"
#include "texture.h"
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

/* mc0:/SNESticle (defined in mainloop_globals.cpp). */
extern Char _SramPath[256];

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */

#define VIDEOCFG_MAGIC   0x53564944u   /* 'SVID' */
#define VIDEOCFG_VERSION 19

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
	Int32  resolution;   /* 0=High (sharp), 1=Low (smooth)            */
	Int32  effect;       /* 0=Normal, 1=Scanlines                     */
} VideoCfgT;

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

/* v18 added frameskip to the v17 prefix. */
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
	cfg.resolution = g_GskResolution;
	cfg.effect     = g_GskEffect;

	_VideoCfgPath(path);
	BgmIOBegin();
	MemCardWriteFile(path, (Uint8 *)&cfg, sizeof(cfg));
	BgmIOEnd();
}

void VideoSettingsLoad(void)
{
	VideoCfgT cfg;
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
		else if (header.version == 18)
		{
			memset(&oldcfg18, 0, sizeof(oldcfg18));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg18, sizeof(oldcfg18)))
			{
				memcpy(&cfg, &oldcfg18, sizeof(oldcfg18));
				cfg.version = VIDEOCFG_VERSION;
				cfg.resolution = 0;
				cfg.effect = 0;
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
				cfg.resolution = 0;
				cfg.effect = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 16)
		{
			memset(&oldcfg, 0, sizeof(oldcfg));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg, sizeof(oldcfg)))
			{
				/* VideoCfgT only appends colorprofile to the v16 prefix. */
				memcpy(&cfg, &oldcfg, sizeof(oldcfg));
				cfg.version = VIDEOCFG_VERSION;
				cfg.colorprofile = SNPPU_COLOR_PROFILE_ORIGINAL;
				cfg.frameskip = 0;
				cfg.resolution = 0;
				cfg.effect = 0;
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
		if (cfg.resolution == 0 || cfg.resolution == 1)
			g_GskResolution = cfg.resolution;
		if (cfg.effect == 0 || cfg.effect == 1)
			g_GskEffect = cfg.effect;
	}
}

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/* ------------------------------------------------------------------ */

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

static void _VideoRow(int vy, int idx, int sel, const char *pLabel, const char *pValue)
{
	if (idx == sel)
	{
		PolyColor4f(0.0f, 0.5f, 0.0f, 0.5f);
		PolyRect(48, vy - 1, 160, FontGetHeight() + 2);
	}

	FontColor4f(0.5f, 0.5f, 0.5f, 1.0f);
	FontPuts(56, vy, pLabel);

	FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	FontPuts(150, vy, pValue);
}

static void _VideoHeader(int vy, const char *pStr)
{
	PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f);
	PolyRect(32, vy, 256 - 64, 9);
	FontColor4f(0.0f, 0.8f, 0.8f, 1.0f);
	_VideoCenter(128, vy, pStr);
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
	if (!Mx4sioIsEnabled())       return "Off";
	if (Mx4sioNeedsRestart())     return "Restart";
	if (Mx4sioGetLastError() < 0) return "Driver Error";
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
	Int32 vy = 15;
	char  buf[16];
	int   m = _VideoModeIndex(g_GskVideoMode);
	const char *pMode = _VideoModes[m].name;
	int   page = (m_iSelect >= 16) ? 2 : (m_iSelect >= 10) ? 1 : 0;
	const char *pWide = "Off";
	const char *pColor = (SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_COMPOSITE)
	                   ? "Composite" : "Original";

	if (g_GskWidescreen)
		pWide = "On";

	FontSelect(0);

	if (page == 0)
		_VideoHeader(vy, "Video Config (1/3)");
	else if (page == 1)
		_VideoHeader(vy, "Video Config (2/3)");
	else
		_VideoHeader(vy, "Video Config (3/3)");
	vy += 18;

	if (page == 0) {
	_VideoHeader(vy, "Screen");
	vy += 14;

	_VideoRow(vy, 0, m_iSelect, "Video Mode", pMode);  vy += 12;

	_VideoRow(vy, 1, m_iSelect, "Widescreen", pWide); vy += 12;

	_VideoRow(vy, 2, m_iSelect, "SNES Colors", pColor); vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskOverscan);
	_VideoRow(vy, 3, m_iSelect, "Overscan", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffX);
	_VideoRow(vy, 4, m_iSelect, "Offset X", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffY);
	_VideoRow(vy, 5, m_iSelect, "Offset Y", buf);      vy += 12;

	_VideoRow(vy, 6, m_iSelect, "Cover Art", CoverIsEnabled() ? "On" : "Off"); vy += 12;

	_VideoHeader(vy, "Audio"); vy += 14;

	snprintf(buf, sizeof(buf), "%d", AudMixGameGetVolume());
	_VideoRow(vy, 7, m_iSelect, "Game Volume", buf); vy += 12;

	{
		int bv = BgmGetVolume();
		if (bv <= 0)                 snprintf(buf, sizeof(buf), "Off");
		else if (BgmTrackCount() <= 0)
			snprintf(buf, sizeof(buf),
			         BgmIsSearching() ? "Searching" : "No Track");
		else                         snprintf(buf, sizeof(buf), "%d", bv);
	}
	_VideoRow(vy, 8, m_iSelect, "Menu Music", buf); vy += 12;

	snprintf(buf, sizeof(buf), "%d kHz", (BgmGetRate() + 500) / 1000);
	_VideoRow(vy, 9, m_iSelect, "Frequency", buf); vy += 12;
	}
	else if (page == 1)
	{
		_VideoHeader(vy, "Performance"); vy += 14;

		_VideoRow(vy, 10, m_iSelect, "Frameskip",
		          MainLoopSafeFrameskipIsEnabled() ? "On" : "Off"); vy += 12;

		_VideoHeader(vy, "Storage / Devices"); vy += 14;

		_VideoRow(vy, 11, m_iSelect, "Mass / USB",
		          MassStorageIsEnabled() ? "On" : "Off"); vy += 12;
		_VideoRow(vy, 12, m_iSelect, "HDD Support",
		          HddSupportIsEnabled() ? "On" : "Off"); vy += 12;
		_VideoRow(vy, 13, m_iSelect, "MMCE Cards",
		          _VideoMmceStatus()); vy += 12;
		_VideoRow(vy, 14, m_iSelect, "SMB (Network)",
		          SmbGetStatusText()); vy += 12;
		_VideoRow(vy, 15, m_iSelect, "MX4SIO (SD)",
		          _VideoMx4sioStatus()); vy += 12;
	}
	else
	{
		_VideoHeader(vy, "Video Effects"); vy += 14;

		_VideoRow(vy, 16, m_iSelect, "Resolution",
		          g_GskResolution ? "Low" : "High"); vy += 12;

		_VideoRow(vy, 17, m_iSelect, "Effect",
		          g_GskEffect ? "Scanlines" : "Normal"); vy += 12;
	}

	/* controls / hints (clear of the vy=215 footer) */
	vy = 184;
	FontColor4f(0.6f, 0.6f, 0.6f, 1.0f);
	_VideoCenter(128, vy, "Up/Dn: select   L/R: change   X: save"); vy += 12;
	_VideoCenter(128, vy, "O (Circle): switch page"); vy += 12;

	if (g_GskVideoMode != GSK_GetActiveVideoMode())
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "mode applies after reboot");
	}
	else if (MmceNeedsRestart() || Mx4sioNeedsRestart())
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "storage applies after reboot");
	}
}

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
	int dir = 0;

	/* Circle (O) cycles through 3 pages:
	   Page 1 (idx 0-9):  Video/Audio
	   Page 2 (idx 10-15): Performance/Devices
	   Page 3 (idx 16-17): Video Effects */
	if (trigger & PAD_CIRCLE)
	{
		if (m_iSelect < 10)
			m_iSelect = 10;
		else if (m_iSelect < 16)
			m_iSelect = 16;
		else
			m_iSelect = 0;
	}

	{
		int lo, hi;
		if (m_iSelect < 10)      { lo = 0;  hi = 9;  }
		else if (m_iSelect < 16) { lo = 10; hi = 15; }
		else                     { lo = 16; hi = 17; }
		if (trigger & PAD_UP)    { m_iSelect--; if (m_iSelect < lo) m_iSelect = hi; }
		if (trigger & PAD_DOWN)  { m_iSelect++; if (m_iSelect > hi) m_iSelect = lo; }
	}

	if (trigger & PAD_LEFT)  dir = -1;
	if (trigger & PAD_RIGHT) dir = +1;

	if (dir != 0)
	{
		switch (m_iSelect)
		{
		case 0: /* video mode (applied on reboot) */
			{
				Int32 count = (Int32)(sizeof(_VideoModes) / sizeof(_VideoModes[0]));
				Int32 modeIndex = _VideoModeIndex(g_GskVideoMode) + dir;
				if (modeIndex < 0)      modeIndex = count - 1;
				if (modeIndex >= count) modeIndex = 0;
				g_GskVideoMode = _VideoModes[modeIndex].mode;
			}
			break;

		case 1: /* widescreen on/off (live) */
			g_GskWidescreen = !g_GskWidescreen;
			GSK_SetWidescreen(g_GskWidescreen);
			break;

		case 2: /* SNES colour profile (live) */
			SNPPUColorSetProfile(
				SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_ORIGINAL
				? SNPPU_COLOR_PROFILE_COMPOSITE
				: SNPPU_COLOR_PROFILE_ORIGINAL);
			break;

		case 3: /* overscan 0..100 (live, step 5) */
			g_GskOverscan += dir * 5;
			if (g_GskOverscan < 0)   g_GskOverscan = 0;
			if (g_GskOverscan > 100) g_GskOverscan = 100;
			GSK_SetOverscan(g_GskOverscan);
			break;

		case 4: /* offset X (live) */
			g_GskDispOffX += dir;
			if (g_GskDispOffX < -64) g_GskDispOffX = -64;
			if (g_GskDispOffX >  64) g_GskDispOffX =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 5: /* offset Y (live) */
			g_GskDispOffY += dir;
			if (g_GskDispOffY < -64) g_GskDispOffY = -64;
			if (g_GskDispOffY >  64) g_GskDispOffY =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 6: /* cover art on/off (live; persisted on X like the rest) */
			CoverToggle();
			break;

		case 7: /* game (emulator) audio volume 0..100, step 1, live */
			{
				int v = AudMixGameGetVolume() + dir;
				if (v < 0)   v = 0;
				if (v > 100) v = 100;
				AudMixGameSetVolume(v);
			}
			break;

		case 8: /* menu music volume 0..100 (0 = off), step 1, live */
			{
				int v = BgmGetVolume() + dir;
				if (v < 0)   v = 0;
				if (v > 100) v = 100;
				BgmSetVolume(v);
			}
			break;

		case 9: /* frequencia de sintese da trilha (cicla a lista) */
			BgmCycleRate(dir);
			break;

		case 10: /* adaptive frameskip on/off (live, persisted on save) */
			MainLoopSafeFrameskipSetEnabled(
				MainLoopSafeFrameskipIsEnabled() ? FALSE : TRUE);
			break;

		case 11: /* Mass / USB on/off */
			MassStorageSetEnabled(!MassStorageIsEnabled());
			break;

		case 12: /* HDD interno (hdd0:) on/off */
			HddSupportSetEnabled(!HddSupportIsEnabled());
			break;

		case 13: /* MMCE (mmce0/1) on/off */
			MmceSupportSetEnabled(!MmceSupportIsEnabled());
			if (MmceSupportIsEnabled())
			{
				BgmIOBegin();
				MmceProbeAvailableSlots();
				BgmIOEnd();
			}
			break;

		case 14: /* SMB on/off */
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

		case 15: /* MX4SIO (SD via SIO2) on/off */
			Mx4sioSetEnabled(!Mx4sioIsEnabled());
			if (Mx4sioIsEnabled())
			{
				BgmIOBegin();
				Mx4sioLoadIfEnabled();
				BgmIOEnd();
			}
			break;

		case 16: /* Resolution: High (0) / Low (1) -- toggles texture filter */
			g_GskResolution = !g_GskResolution;
			TextureSetFilter(&_OutTex, g_GskResolution);
			break;

		case 17: /* Effect: Normal (0) / Scanlines (1) */
			g_GskEffect = !g_GskEffect;
			break;
		}
	}

	/* Square: reset the display offset (live). */
	if (trigger & PAD_SQUARE)
	{
		g_GskDispOffX = 0;
		g_GskDispOffY = 0;
		GSK_SetDisplayOffset(0, 0);
	}

	/* Cross / Start: persist all video settings to the memory card. */
	if (trigger & (PAD_CROSS | PAD_START))
	{
		VideoSettingsSave();
	}
}
