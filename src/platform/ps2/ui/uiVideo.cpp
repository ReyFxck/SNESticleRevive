
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
		if (cfg.mode == GSK_VIDMODE_480P || cfg.mode == GSK_VIDMODE_480I)
			g_GskVideoMode = cfg.mode;

		if (cfg.offx >= -64 && cfg.offx <= 64) g_GskDispOffX = cfg.offx;
		if (cfg.offy >= -64 && cfg.offy <= 64) g_GskDispOffY = cfg.offy;
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

void CVideoScreen::Draw()
{
	Int32 vy = 40;
	char  buf[16];
	const char *pMode = (g_GskVideoMode == GSK_VIDMODE_480P)
	                  ? "480p (GSM/HDMI)" : "480i (padrao)";

	FontSelect(0);

	/* header */
	PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f);
	PolyRect(32, vy, 256 - 64, 9);
	FontColor4f(0.0f, 0.8f, 0.8f, 1.0f);
	_VideoCenter(128, vy, "Configuracoes de Video");
	vy += 26;

	_VideoRow(vy, 0, m_iSelect, "Modo video", pMode);  vy += 16;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffX);
	_VideoRow(vy, 1, m_iSelect, "Offset X", buf);      vy += 16;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffY);
	_VideoRow(vy, 2, m_iSelect, "Offset Y", buf);      vy += 22;

	FontColor4f(0.6f, 0.6f, 0.6f, 1.0f);
	_VideoCenter(128, vy, "Cima/Baixo: escolher  Esq/Dir: mudar"); vy += 12;
	_VideoCenter(128, vy, "X: salvar   Quadrado: zerar offset");   vy += 16;

	if (g_GskVideoMode == GSK_VIDMODE_480P)
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "480p aplica ao reiniciar");
	}
}

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
	int dir = 0;

	if (trigger & PAD_UP)    { m_iSelect--; if (m_iSelect < 0) m_iSelect = 2; }
	if (trigger & PAD_DOWN)  { m_iSelect++; if (m_iSelect > 2) m_iSelect = 0; }

	if (trigger & PAD_LEFT)  dir = -1;
	if (trigger & PAD_RIGHT) dir = +1;

	if (dir != 0)
	{
		switch (m_iSelect)
		{
		case 0: /* video mode (applied on reboot) */
			g_GskVideoMode = (g_GskVideoMode == GSK_VIDMODE_480P)
			               ? GSK_VIDMODE_480I : GSK_VIDMODE_480P;
			break;

		case 1: /* offset X (live) */
			g_GskDispOffX += dir;
			if (g_GskDispOffX < -64) g_GskDispOffX = -64;
			if (g_GskDispOffX >  64) g_GskDispOffX =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 2: /* offset Y (live) */
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
