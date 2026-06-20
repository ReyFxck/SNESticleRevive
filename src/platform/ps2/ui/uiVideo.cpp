
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#include "font.h"
#include "poly.h"
#include "uiVideo.h"

extern "C" {
#include "gskit_backend.h"
}
#include "memcard.h"

/* mc0:/SNESticle (defined in mainloop_globals.cpp). */
extern Char _SramPath[256];

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */

#define VIDEOCFG_MAGIC   0x53564944u   /* 'SVID' */
#define VIDEOCFG_VERSION 1

typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
} VideoCfgT;

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

	_VideoCfgPath(path);
	MemCardWriteFile(path, (Uint8 *)&cfg, sizeof(cfg));
}

void VideoSettingsLoad(void)
{
	VideoCfgT cfg;
	char      path[300];

	memset(&cfg, 0, sizeof(cfg));
	_VideoCfgPath(path);

	if (MemCardReadFile(path, (Uint8 *)&cfg, sizeof(cfg)) &&
	    cfg.magic == VIDEOCFG_MAGIC)
	{
		if (cfg.mode >= 0 && cfg.mode < GSK_VIDMODE_COUNT)
			g_GskVideoMode = cfg.mode;

		if (cfg.offx >= -64 && cfg.offx <= 64) g_GskDispOffX = cfg.offx;
		if (cfg.offy >= -64 && cfg.offy <= 64) g_GskDispOffY = cfg.offy;
		if (cfg.overscan >= 0 && cfg.overscan <= 100) g_GskOverscan = cfg.overscan;
		g_GskWidescreen = cfg.widescreen ? 1 : 0;
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

void CVideoScreen::Draw()
{
	static const char *names[GSK_VIDMODE_COUNT] = {
		"240p (CRT, exp)", "480i (default)", "480p (GSM/HDMI)", "576i (PAL)", "288p (PAL exp)"
	};
	Int32 vy = 15;
	char  buf[16];
	int   m = (g_GskVideoMode >= 0 && g_GskVideoMode < GSK_VIDMODE_COUNT)
	        ? g_GskVideoMode : 0;
	const char *pMode = names[m];

	FontSelect(0);

	_VideoHeader(vy, "Video Config");
	vy += 26;

	_VideoHeader(vy, "Screen");
	vy += 14;

	_VideoRow(vy, 0, m_iSelect, "Video Mode", pMode);  vy += 12;

	_VideoRow(vy, 1, m_iSelect, "Widescreen",
	          g_GskWidescreen ? "On (16:9)" : "Off (4:3)"); vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskOverscan);
	_VideoRow(vy, 2, m_iSelect, "Overscan", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffX);
	_VideoRow(vy, 3, m_iSelect, "Offset X", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffY);
	_VideoRow(vy, 4, m_iSelect, "Offset Y", buf);

	/* controls / hints, in the empty middle (clear of the vy=215 footer) */
	vy = 120;
	FontColor4f(0.6f, 0.6f, 0.6f, 1.0f);
	_VideoCenter(128, vy, "Up/Down: select   Left/Right: change"); vy += 12;
	_VideoCenter(128, vy, "X: save     Square: reset offset");     vy += 12;

	if (g_GskVideoMode != GSK_GetActiveVideoMode())
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "mode applies after reboot");
	}
}

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
	int dir = 0;

	if (trigger & PAD_UP)    { m_iSelect--; if (m_iSelect < 0) m_iSelect = 4; }
	if (trigger & PAD_DOWN)  { m_iSelect++; if (m_iSelect > 4) m_iSelect = 0; }

	if (trigger & PAD_LEFT)  dir = -1;
	if (trigger & PAD_RIGHT) dir = +1;

	if (dir != 0)
	{
		switch (m_iSelect)
		{
		case 0: /* video mode (applied on reboot) */
			g_GskVideoMode += dir;
			if (g_GskVideoMode < 0)                  g_GskVideoMode = GSK_VIDMODE_COUNT - 1;
			if (g_GskVideoMode >= GSK_VIDMODE_COUNT)  g_GskVideoMode = 0;
			break;

		case 1: /* widescreen on/off (live) */
			GSK_SetWidescreen(!g_GskWidescreen);
			break;

		case 2: /* overscan 0..100 (live, step 5) */
			g_GskOverscan += dir * 5;
			if (g_GskOverscan < 0)   g_GskOverscan = 0;
			if (g_GskOverscan > 100) g_GskOverscan = 100;
			GSK_SetOverscan(g_GskOverscan);
			break;

		case 3: /* offset X (live) */
			g_GskDispOffX += dir;
			if (g_GskDispOffX < -64) g_GskDispOffX = -64;
			if (g_GskDispOffX >  64) g_GskDispOffX =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 4: /* offset Y (live) */
			g_GskDispOffY += dir;
			if (g_GskDispOffY < -64) g_GskDispOffY = -64;
			if (g_GskDispOffY >  64) g_GskDispOffY =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
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
