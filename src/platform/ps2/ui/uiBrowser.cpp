
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <libpad.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "types.h"
#if 0
#include "font.h"
#else
#include "font.h"
#endif
#include "poly.h"
#include "uiBrowser.h"
#include "uiCover.h"

extern "C" void DLog(const char *fmt, ...);

extern "C" {
#include "mcsave_ee.h"
};

static const char *_MenuEntries[]=
{
	"Copy File",
	"Paste File",
	"Delete file",
	NULL
};

/* ------------------------------------------------------------------
   Browser long-name UX: ellipsis truncation + marquee scroll.

   Ported from the ReyFxck/InfinityStation launcher (ps2boot/ui/
   launcher/pages/browser.c). InfinityStation renders into a 640x448
   surface with a fixed-width font, so it does its budgeting in char
   counts. We instead budget in pixels via FontGetStrWidth() so that
   the variable-width 04b16b font used by SNESticleRevive lines the
   text up consistently regardless of glyph mix.
   ------------------------------------------------------------------ */

/* Pixel budget for the visible portion of an entry name. The browser
   starts entries at vx=4 and the underlying viewport is 256 px wide,
   so 240 leaves a 12 px right gutter that keeps the selection bar
   from clipping the panel edge. The size column on the right is
   currently commented out (line ~334), so this is the full column. */
#define BROWSER_NAME_MAXPIXELS (240)

/* When cover art (capas) is enabled the ROM list name column shrinks to
   leave room for the cover box on the right side of the 256px-wide
   browser; when disabled the list goes back to its full original width
   (BROWSER_NAME_MAXPIXELS). The cover box itself is defined below. */
#define BROWSER_NAME_MAXPIXELS_COVER (148)

/* Cover box geometry (browser logical space is 256x240). Sits to the
   right of the shrunken list when cover art is on. */
#define BROWSER_COVER_X   (158)
#define BROWSER_COVER_Y   (44)
#define BROWSER_COVER_W   (92)
#define BROWSER_COVER_H   (132)

/* Frames the selection must sit still before we go probe the disk for
   its cover. Probing up to 4 candidate PNG paths means up to 4 fopen()
   calls, and a *failed* fopen on cdfs/USB costs real time (CD seek), so
   doing it on every d-pad step makes scrolling crawl - even on an ISO
   with zero PNGs. ~12 frames (~0.2s @ 60Hz) means fast scrolling never
   touches the disk; the cover only loads once you pause on an entry. */
#define BROWSER_COVER_LOAD_DELAY (12)

/* Marquee tuning (frame counts at the browser's 60 Hz draw rate):
     - DELAY_FRAMES: how long the name sits with a static ellipsis on
       the selected row before it starts scrolling. ~0.3 s is enough
       for the user to register "this name is longer" without making
       the scroll feel laggy when they're dwelling on one entry.
     - STEP_FRAMES: frames between marquee advance ticks. Higher =
       slower scroll. 5 frames = ~12 chars/s (gentler than the old 3).
     - PAUSE_AT_END: frames to hold when the scroll reaches the end
       of the name before snapping back to the start. Gives the user
       time to read the tail end.
     - SCROLL_PX: pixels to advance per tick (sub-char smooth scroll).
       Using 2px per tick gives a smoother glide than jumping a full
       char width at once. */
#define BROWSER_MARQUEE_DELAY_FRAMES (18)
#define BROWSER_MARQUEE_STEP_FRAMES  (3)
#define BROWSER_MARQUEE_PAUSE_END    (40)
#define BROWSER_MARQUEE_SCROLL_PX    (1)

/* Width of a single space in the current font, used to size the
   marquee gap. We grab it lazily once per Draw() so the cost is one
   FontGetStrWidth call per frame instead of per entry. */
static Int32 _BrowserSpaceWidth(void)
{
	static Int32 s_w = 0;
	if (s_w == 0)
	{
		Char tmp[2] = { ' ', 0 };
		s_w = FontGetStrWidth(tmp);
		if (s_w <= 0)
			s_w = 4; /* defensive fallback: 04b16b's space is 4 px */
	}
	return s_w;
}

/* Build a trimmed copy of src into out, sized so it fits in max_px
   pixels (including the trailing "..." if truncation was needed).
   When the source already fits, it is copied verbatim. Drop-in
   replacement for the previous fixed `str[120] = 0` hard cap. */
static void BrowserCopyEllipsis(Char *out, size_t out_size, const Char *src, Int32 max_px)
{
	if (!out || out_size == 0)
		return;

	if (!src)
	{
		out[0] = '\0';
		return;
	}

	/* First copy verbatim into out, then probe its rendered width. The
	   common case (name fits) terminates here without any extra
	   FontGetStrWidth scans. */
	snprintf(out, out_size, "%s", src);
	if (FontGetStrWidth(out) <= max_px)
		return;

	{
		Char dots[] = "...";
		Int32 dotsW = FontGetStrWidth(dots);

		/* Degenerate budget: not even "..." fits. Render whatever bit
		   does fit char-by-char and bail. */
		if (dotsW > max_px)
		{
			size_t i = 0;
			while (i + 1 < out_size && src[i])
			{
				out[i] = src[i];
				out[i + 1] = '\0';
				if (FontGetStrWidth(out) > max_px)
				{
					if (i > 0)
						out[i] = '\0';
					break;
				}
				i++;
			}
			return;
		}

		/* Find the longest prefix of src that fits in (max_px - dotsW)
		   pixels, then append "...". O(n) FontGetStrWidth probes since
		   each char is appended once and we keep the rolling width. */
		{
			size_t i = 0;
			out[0] = '\0';
			while (src[i] && i + 4 < out_size)
			{
				out[i] = src[i];
				out[i + 1] = '\0';
				if (FontGetStrWidth(out) > max_px - dotsW)
				{
					if (i > 0)
						out[i] = '\0';
					break;
				}
				i++;
			}
			{
				size_t n = strlen(out);
				if (n + 4 <= out_size)
				{
					out[n + 0] = '.';
					out[n + 1] = '.';
					out[n + 2] = '.';
					out[n + 3] = '\0';
				}
			}
		}
	}
}

/* Build a scrolled view of src, clipped to max_px pixels. `tick` is a
   PIXEL offset into the string. We start at a CHARACTER boundary (never
   a sub-pixel/negative x) because the PS2 GS wraps negative sprite
   coordinates into a huge positive value, which draws a stray
   horizontal streak across the screen. So the scroll steps by whole
   chars - glitch-free, at the cost of not being sub-pixel smooth. At
   the end the longest fitting suffix is shown so ".nes" is never cut. */
static void BrowserCopyMarquee(Char *out, size_t out_size, const Char *src,
                               Int32 max_px, Uint32 tick)
{
	if (!out || out_size == 0)
		return;
	if (!src)
	{
		out[0] = '\0';
		return;
	}

	/* Fits as-is: no scrolling needed. */
	snprintf(out, out_size, "%s", src);
	if (FontGetStrWidth(out) <= max_px)
		return;

	Int32 fullW = FontGetStrWidth(src);
	Int32 maxOffset = fullW - max_px;
	if (maxOffset < 0) maxOffset = 0;

	Int32 pxOffset = (Int32)tick;
	if (pxOffset > maxOffset)
		pxOffset = maxOffset;

	{
		size_t startChar = 0;
		size_t i = 0, idx;

		if (pxOffset >= maxOffset)
		{
			/* End: longest suffix that fits, so the final chars (".nes")
			   are never clipped. */
			while (src[startChar] && FontGetStrWidth(src + startChar) > max_px)
				startChar++;
		}
		else
		{
			/* Mid-scroll: first char whose cumulative width crosses
			   pxOffset (char-boundary start, x stays >= vx). */
			Int32 cumW = 0;
			Char  tmp[2] = {0, 0};
			while (src[startChar])
			{
				tmp[0] = src[startChar];
				Int32 cw = FontGetStrWidth(tmp);
				if (cumW + cw > pxOffset)
					break;
				cumW += cw;
				startChar++;
			}
		}

		out[0] = '\0';
		for (idx = startChar; src[idx] && i + 1 < out_size; idx++)
		{
			out[i] = src[idx];
			out[i + 1] = '\0';
			if (FontGetStrWidth(out) > max_px)
			{
				if (i > 0)
					out[i] = '\0';
				break;
			}
			i++;
		}
	}
}

/* ------------------------------------------------------------------
   Procedural starfield background.

   Ported from InfinityStation (ps2boot/ui/launcher/background/
   background.c) and re-targeted from gsKit's draw_rect_filled qword
   chain onto SNESticleRevive's existing Poly* abstraction. The Poly*
   layer already wraps GPPrimRect for us (see common/render/poly.cpp),
   so each star is one untextured PolyRect.

   Math is pure s32 fixed-point: a deterministic LCG places stars in a
   `[-XY_RANGE, XY_RANGE)` world cube, a pinhole projection (sx = cx +
   x * FOCAL / z) brings them onto the 256x240 viewport, and a per-z
   brightness ramp picks the rect color. The LCG seed is constant so
   the field looks identical across boots - no save state needed.
   ------------------------------------------------------------------ */

#define BROWSER_STAR_COUNT      128       /* halved vs InfinityStation's 256: 256x240 is
                                              one quarter the screen area of
                                              640x448, so this matches their density. */
#define BROWSER_STAR_FOCAL      128       /* projection focal length, half of
                                              InfinityStation's 256 since our screen
                                              and world coords scale 1:2. */
#define BROWSER_STAR_Z_MIN      32
#define BROWSER_STAR_Z_NEAR     256
#define BROWSER_STAR_Z_MAX      2048
#define BROWSER_STAR_XY_RANGE   256
#define BROWSER_STAR_DRIFT_NUM  1
#define BROWSER_STAR_DRIFT_DEN  2
#define BROWSER_STAR_PALETTES   3

typedef struct StarS
{
	int32_t x;
	int32_t y;
	int32_t z;
	uint8_t palette;
} StarT;

typedef struct StarTintS
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
} StarTintT;

static const StarTintT _StarTints[BROWSER_STAR_PALETTES] = {
	{255, 255, 255},
	{180, 200, 255},
	{255, 240, 180}
};

static StarT    _Stars[BROWSER_STAR_COUNT];
static int      _StarsInited = 0;
static uint32_t _StarsLcg = 0xDEADBEEFu;

static uint32_t _StarLcgNext(void)
{
	_StarsLcg = _StarsLcg * 1664525u + 1013904223u;
	return _StarsLcg;
}

static int32_t _StarRandSigned(int32_t half)
{
	uint32_t r = _StarLcgNext();
	int32_t v = (int32_t)(r % (uint32_t)(half * 2));
	return v - half;
}

static int32_t _StarRandPos(int32_t lo, int32_t hi)
{
	uint32_t r = _StarLcgNext();
	int32_t span = hi - lo;
	return lo + (int32_t)(r % (uint32_t)span);
}

static void _StarRespawn(StarT *s)
{
	s->x = _StarRandSigned(BROWSER_STAR_XY_RANGE);
	s->y = _StarRandSigned(BROWSER_STAR_XY_RANGE);
	s->z = _StarRandPos(BROWSER_STAR_Z_NEAR, BROWSER_STAR_Z_MAX);
	s->palette = (uint8_t)(_StarLcgNext() % BROWSER_STAR_PALETTES);
}

static void _StarfieldInit(void)
{
	int i;
	for (i = 0; i < BROWSER_STAR_COUNT; i++)
	{
		_Stars[i].x = _StarRandSigned(BROWSER_STAR_XY_RANGE);
		_Stars[i].y = _StarRandSigned(BROWSER_STAR_XY_RANGE);
		_Stars[i].z = _StarRandPos(BROWSER_STAR_Z_MIN + 1, BROWSER_STAR_Z_MAX);
		_Stars[i].palette = (uint8_t)(_StarLcgNext() % BROWSER_STAR_PALETTES);
	}
	_StarsInited = 1;
}

static void _StarfieldAdvance(void)
{
	static int s_acc = 0;
	int i, dz;

	if (!_StarsInited)
		_StarfieldInit();

	s_acc += BROWSER_STAR_DRIFT_NUM;
	dz = s_acc / BROWSER_STAR_DRIFT_DEN;
	s_acc -= dz * BROWSER_STAR_DRIFT_DEN;

	if (dz <= 0)
		return;

	for (i = 0; i < BROWSER_STAR_COUNT; i++)
	{
		StarT *s = &_Stars[i];
		s->z -= dz;
		if (s->z <= BROWSER_STAR_Z_MIN)
			_StarRespawn(s);
	}
}

static void _StarfieldDraw(void)
{
	const int32_t cx = 128; /* 256 / 2 */
	const int32_t cy = 120; /* 240 / 2 */
	int i;

	if (!_StarsInited)
		_StarfieldInit();

	PolyTexture(NULL);
	PolyBlend(FALSE);

	for (i = 0; i < BROWSER_STAR_COUNT; i++)
	{
		StarT *s = &_Stars[i];
		int32_t z = s->z;
		int32_t sx, sy;
		uint32_t bri;
		uint32_t rr, gg, bb;
		const StarTintT *tint;
		Float32 fr, fg, fb;
		int big;

		if (z <= 0)
			continue;

		sx = cx + (s->x * BROWSER_STAR_FOCAL) / z;
		sy = cy + (s->y * BROWSER_STAR_FOCAL) / z;

		if (sx < 0 || sy < 0)
			continue;
		if (sx >= 256 || sy >= 240)
			continue;

		if (z >= BROWSER_STAR_Z_MAX)
			bri = 32u;
		else if (z <= BROWSER_STAR_Z_NEAR)
			bri = 256u;
		else
		{
			uint32_t span = (uint32_t)(BROWSER_STAR_Z_MAX - BROWSER_STAR_Z_NEAR);
			uint32_t off  = (uint32_t)(BROWSER_STAR_Z_MAX - z);
			bri = 32u + (off * (256u - 32u)) / span;
		}

		tint = &_StarTints[s->palette < BROWSER_STAR_PALETTES ? s->palette : 0];
		rr = ((uint32_t)tint->r * bri) >> 8;
		gg = ((uint32_t)tint->g * bri) >> 8;
		bb = ((uint32_t)tint->b * bri) >> 8;
		if (rr > 255u) rr = 255u;
		if (gg > 255u) gg = 255u;
		if (bb > 255u) bb = 255u;

		/* PolyColor4f wants 0..1 floats; the LUT keeps the fixed-point
		   math on the integer side and only crosses into FPU here. */
		fr = ((Float32)rr) * (1.0f / 255.0f);
		fg = ((Float32)gg) * (1.0f / 255.0f);
		fb = ((Float32)bb) * (1.0f / 255.0f);
		PolyColor4f(fr, fg, fb, 1.0f);

		big = (z <= BROWSER_STAR_Z_NEAR);
		PolyRect((Float32)sx, (Float32)sy, big ? 2.0f : 1.0f, big ? 2.0f : 1.0f);
	}
}

int CBrowserScreen::GetEntryPath(char *pStr, int nChars)
{
	if (m_iSelect >=0 && m_iSelect < m_nEntries)
		return snprintf(pStr, nChars, "%s%s", m_Dir, m_pDirEntries[m_iSelect].name);
	else 
		return 0;
}

char *CBrowserScreen::GetEntryName()
{
	if (m_iSelect >=0 && m_iSelect < m_nEntries)
		return m_pDirEntries[m_iSelect].name;
	else 
		return NULL;
}

BrowserEntryTypeE CBrowserScreen::GetEntryType()
{
	if (m_iSelect >=0 && m_iSelect < m_nEntries)
		return m_pDirEntries[m_iSelect].eType;
	else 
		return BROWSER_ENTRYTYPE_OTHER;
}

typedef int (*CopyProgressCallBackT)(char *pDestName, char *pSrcName, int Position, int Total);
int CopyFile(char *pDest, char *pSrc, CopyProgressCallBackT pCallBack);
int PathGetMaxFileNameLength(const char *pPath);
void PathTruncFileName(Char *pOut, Char *pStr, Int32 nMaxChars);

int CBrowserScreen::MenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
	CBrowserScreen *pBrowser = (CBrowserScreen *)Parm2;
	/* m_Dir is 512 chars and an entry name can now be up to 255; pick
	   1024 so the joined path can never wrap on us regardless of how
	   deep the user has nested their ROM library. */
	Char str[1024];

	if (pBrowser->GetEntryPath(str, sizeof(str)) == 0)
	{
		return 0;
	}

	switch (Type)
	{
		case 1:
			switch (Parm1)
			{
				case 0: // copy file
					switch (pBrowser->GetEntryType())
					{
						case BROWSER_ENTRYTYPE_DRIVE:
							break;
						case BROWSER_ENTRYTYPE_DIR:
							break;
						default:
							pBrowser->m_SubMenu.SetText(0, str);
							pBrowser->m_SubMenu.SetText(1, pBrowser->GetEntryName());
							break;
					}
					break;
				case 1: // Paste file
					{
						char strDestPath[1024];
						char strSrcPath[1024];
						char strDestShortName[256];
						char strDestFileName[256];
						char strDestFileExt[256];
						char *pExt;

						// get dest file name
						strcpy(strDestFileName, pBrowser->m_SubMenu.GetText(1));
						strDestFileExt[0] = '\0';

						// split dest file name by extension
						pExt = strrchr(strDestFileName, '.');
						if (pExt)
						{
							// special case .gz extensions							
							if (!strcmp(pExt, ".gz"))
							{
								*pExt = '\0';
								pExt = strrchr(strDestFileName, '.');
								if (pExt)
								{
									strcpy(strDestFileExt, pExt);
									*pExt = '\0';
								}
								strcat(strDestFileExt, ".gz");

							} else
							{
								strcpy(strDestFileExt, pExt);
								*pExt = '\0';
							}
						}
						// truncate file name
						PathTruncFileName(strDestShortName, strDestFileName, PathGetMaxFileNameLength(pBrowser->m_Dir) - strlen(strDestFileExt));
						
						snprintf(strDestPath, sizeof(strDestPath), "%s%s%s", pBrowser->m_Dir, strDestShortName, strDestFileExt);
						snprintf(strSrcPath, sizeof(strSrcPath), "%s", pBrowser->m_SubMenu.GetText(0));


						printf("src: %s\n", strSrcPath );
						printf("dest: %s\n", strDestPath);
						CopyFile(strDestPath, strSrcPath, NULL);

						pBrowser->Chdir(".");
					}
					break;
				case 2: // delete file

			        printf("Deleting %s...\n", str);
					switch (pBrowser->GetEntryType())
					{
						case BROWSER_ENTRYTYPE_DRIVE:
							break;
						case BROWSER_ENTRYTYPE_DIR:
							rmdir(str);
							break;
						default:
							unlink(str);
							rmdir(str);
							break;
					}
					pBrowser->Chdir(".");

					break;
			}

			pBrowser->m_bSubMenu = FALSE;

		break;
	}

	return 0;
}


CBrowserScreen::CBrowserScreen(Uint32 uMaxEntries)
{
	m_Dir[0]=0;
	m_nEntries=0;
	m_MaxEntries = uMaxEntries;
	m_iSelect=0;
	m_iScroll=0;
	m_MaxLines = (209 / 11 - 1); // umm, hacked
	m_bMCDir = FALSE;
	m_bSubMenu = FALSE;
	m_pDirEntries = new BrowserEntryT[uMaxEntries];

	m_SubMenu.SetTitle("File Menu");
	m_SubMenu.SetEntries((char **)_MenuEntries);
	m_SubMenu.SetMsgFunc(MenuEvent);
	m_SubMenu.SetUserData(this);
}

CBrowserScreen::~CBrowserScreen()
{
	delete m_pDirEntries;
}

void CBrowserScreen::ResetEntries()
{
	m_iSelect  = 0;
	m_nEntries = 0;
	m_iScroll  = 0;
}



static Int32 _BrowserEntryQSort(const void *pA, const void *pB)
{
	BrowserEntryT *pDirA, *pDirB;
	pDirA = (BrowserEntryT *)pA;
	pDirB = (BrowserEntryT *)pB;

	if (pDirA->eType == pDirB->eType)
	{
		return strcasecmp(pDirA->name, pDirB->name);
	} 
	else
	{
		return pDirA->eType - pDirB->eType;
	}	
}

void CBrowserScreen::SortEntries()
{
	qsort(m_pDirEntries, m_nEntries, sizeof(m_pDirEntries[0]), _BrowserEntryQSort);
}


void CBrowserScreen::AddEntry(const Char *pName, BrowserEntryTypeE eType, Int32 size)
{
	if (m_nEntries < m_MaxEntries)
	{
		strncpy(m_pDirEntries[m_nEntries].name, pName, BROWSER_ENTRY_MAXCHARS - 1);
		m_pDirEntries[m_nEntries].name[BROWSER_ENTRY_MAXCHARS-1] = '\0';
		m_pDirEntries[m_nEntries].size = size;
		m_pDirEntries[m_nEntries].eType = eType;
		m_nEntries++;
	}
}


void CBrowserScreen::Draw()
{
	Int32 iEntry;
	Int32 vx=4, vy = 20;
	Int32 iLine;

	/* Name column width: shrinks when cover art is on (to leave room
	   for the cover box on the right), full width when off so the
	   screen looks exactly like the original. */
	Bool bHasRoms = FALSE;
	{
		Int32 _i;
		for (_i = 0; _i < m_nEntries; _i++)
			if (m_pDirEntries[_i].eType == BROWSER_ENTRYTYPE_EXECUTABLE)
			{
				bHasRoms = TRUE;
				break;
			}
	}
	Bool bCoverUI = (CoverIsEnabled() && bHasRoms) ? TRUE : FALSE;
	Int32 nameMaxPx = bCoverUI ? BROWSER_NAME_MAXPIXELS_COVER : BROWSER_NAME_MAXPIXELS;

	/* Marquee state retained across frames. Reset whenever the
	   selected entry changes OR the user CDs into a new directory, so
	   long names always start in their static-ellipsis "rest" pose
	   instead of mid-scroll. This is the InfinityStation behavior the
	   user explicitly called out as "so we don't lag" - the heavy work
	   (per-entry width-budget marquee build) is gated to the selected
	   row only, and only after a dwell delay. */
	static Int32  s_prev_iSelect = -1;
	static Char   s_prev_dir[sizeof(m_Dir)] = "";
	static Uint32 s_marquee_delay  = 0;
	static Uint32 s_marquee_tick   = 0;
	static Uint32 s_marquee_hold   = 0;

	iEntry = m_iScroll;

	FontSelect(0);

	/* Starfield background. We replace the (faded) SNES output
	   underneath us with a solid black sheet first so stars sit on a
	   true black backdrop instead of getting tinted by the residual
	   emulator frame. Then advance + draw the field. Both calls
	   touch the PolyTexture/PolyBlend state, so the existing setup
	   below has to (and does) reassert what it needs. */
	PolyTexture(NULL);
	PolyBlend(FALSE);
	PolyColor4f(0.0f, 0.0f, 0.0f, 1.0f);
	PolyRect(0, 0, 256, 240);

	_StarfieldAdvance();
	_StarfieldDraw();

	PolyTexture(NULL);
    PolyBlend(TRUE);


//    PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f); 
    PolyColor4f(0.0f, 0.2f, 0.2f, 0.9f); 
	PolyRect(0, vy, 256, 9);

	FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
    FontPrintf(vx, vy, "%s", m_Dir);
    vy+=12;

	/* Detect selection / directory change before the per-row loop so
	   the marquee state reset happens exactly once per frame. */
	{
		Bool bChanged = FALSE;
		if (m_iSelect != s_prev_iSelect)
			bChanged = TRUE;
		else if (strcmp(m_Dir, s_prev_dir) != 0)
			bChanged = TRUE;

		if (bChanged)
		{
			s_prev_iSelect = m_iSelect;
			snprintf(s_prev_dir, sizeof(s_prev_dir), "%s", m_Dir);
			s_marquee_delay = 0;
			s_marquee_tick  = 0;
			s_marquee_hold  = 0;
		}
		else
		{
			/* Advance the tick only while the currently-selected entry
			   actually overflows the column. Cheap-out for non-selected
			   rows is automatic since they always take the ellipsis
			   branch below. */
			Bool bScroll = FALSE;
			if (m_iSelect >= 0 && m_iSelect < m_nEntries)
			{
				BrowserEntryT *pSel = &m_pDirEntries[m_iSelect];
				Char probe[BROWSER_ENTRY_MAXCHARS + 4];
				if (pSel->eType == BROWSER_ENTRYTYPE_DIR)
					snprintf(probe, sizeof(probe), "/%s", pSel->name);
				else
					snprintf(probe, sizeof(probe), "%s", pSel->name);
				if (FontGetStrWidth(probe) > nameMaxPx)
					bScroll = TRUE;
			}

			if (bScroll)
			{
				if (s_marquee_delay < BROWSER_MARQUEE_DELAY_FRAMES)
				{
					s_marquee_delay++;
				}
				else
				{
					/* Check if scroll reached the end (tick in px >=
					   fullW - maxpx). We compute fullW here cheaply
					   since BrowserCopyMarquee clamps internally. */
					Char probe2[BROWSER_ENTRY_MAXCHARS + 4];
					BrowserEntryT *pSel2 = &m_pDirEntries[m_iSelect];
					if (pSel2->eType == BROWSER_ENTRYTYPE_DIR)
						snprintf(probe2, sizeof(probe2), "/%s", pSel2->name);
					else
						snprintf(probe2, sizeof(probe2), "%s", pSel2->name);
					Int32 fullW2 = FontGetStrWidth(probe2);
					Int32 maxOff2 = fullW2 - nameMaxPx;
					if (maxOff2 < 0) maxOff2 = 0;

					if ((Int32)s_marquee_tick >= maxOff2)
					{
						/* Reached end: pause then snap back. */
						s_marquee_hold++;
						if (s_marquee_hold >= BROWSER_MARQUEE_PAUSE_END)
						{
							s_marquee_tick  = 0;
							s_marquee_hold  = 0;
							s_marquee_delay = 0; /* re-do initial delay */
						}
					}
					else
					{
						/* Normal scroll: advance by SCROLL_PX every
						   STEP_FRAMES frames. */
						s_marquee_hold++;
						if (s_marquee_hold >= BROWSER_MARQUEE_STEP_FRAMES)
						{
							s_marquee_hold = 0;
							s_marquee_tick += BROWSER_MARQUEE_SCROLL_PX;
						}
					}
				}
			}
			else
			{
				/* Selected name fits as-is: keep state reset so the next
				   long name starts cleanly from the beginning. */
				s_marquee_delay = 0;
				s_marquee_tick  = 0;
				s_marquee_hold  = 0;
			}
		}
	}

	(void)_BrowserSpaceWidth; /* helper kept for future per-gap pixel tuning */

	/* Cover art. A cache hit shows instantly (no disk). Cold loads are
	   debounced so fast scrolling never touches the (slow) cdfs/USB
	   filesystem, and the selection's neighbours are prefetched one per
	   frame while idle, so single d-pad steps land on a warm cache. */
	if (bCoverUI)
	{
		char curPath[1024];
		Bool curIsRom = (m_iSelect >= 0 && m_iSelect < m_nEntries &&
		                 m_pDirEntries[m_iSelect].eType == BROWSER_ENTRYTYPE_EXECUTABLE);
		curPath[0] = '\0';
		if (curIsRom)
			GetEntryPath(curPath, sizeof(curPath));

		/* instant: display now if already cached (never hits the disk) */
		CoverShowCached(curIsRom ? curPath : (const char *)0);

		static Int32  s_cov_sel    = -1;
		static Char   s_cov_dir[sizeof(m_Dir)] = "";
		static Uint32 s_cov_settle = 0;
		static Bool   s_cov_done   = FALSE;

		if (m_iSelect != s_cov_sel || strcmp(m_Dir, s_cov_dir) != 0)
		{
			/* selection / directory changed: restart the settle timer */
			s_cov_sel = m_iSelect;
			snprintf(s_cov_dir, sizeof(s_cov_dir), "%s", m_Dir);
			s_cov_settle = 0;
			s_cov_done   = FALSE;
		}
		else if (!s_cov_done)
		{
			/* wait for the selection to settle, then cold-load it once */
			if (s_cov_settle < BROWSER_COVER_LOAD_DELAY)
				s_cov_settle++;
			else
			{
				CoverShow(curIsRom ? curPath : (const char *)0);
				s_cov_done = TRUE;
			}
		}
		else
		{
			/* settled: warm one un-cached neighbour per frame (at most
			   one disk load/frame) so the next d-pad step is instant. */
			static const int offs[8] = { 1, -1, 2, -2, 3, -3, 4, -4 };
			unsigned k;
			for (k = 0; k < 8; k++)
			{
				Int32 ni = m_iSelect + offs[k];
				if (ni < 0 || ni >= m_nEntries)
					continue;
				if (m_pDirEntries[ni].eType != BROWSER_ENTRYTYPE_EXECUTABLE)
					continue;

				char npath[1024];
				snprintf(npath, sizeof(npath), "%s%s", m_Dir, m_pDirEntries[ni].name);
				if (CoverPrefetch(npath))
					break;   /* performed one disk load this frame */
			}
		}
	}

	for (iLine=0; iLine < m_MaxLines; iLine++)
	{
		/* Raw `prefix + entry name` lives here; the visible portion
		   gets shortened (ellipsis or marquee) into `view` below.
		   BROWSER_ENTRY_MAXCHARS + 4 is sized so snprintf("%s", name)
		   plus a leading "/" cannot trip -Wformat-truncation. */
		Char str[BROWSER_ENTRY_MAXCHARS + 4];
		Char view[BROWSER_ENTRY_MAXCHARS + 8];
		Char sizestr[32];

		if (iEntry>=0 && iEntry < m_nEntries)
		{
			BrowserEntryT *pEntry = &m_pDirEntries[iEntry];
			if (pEntry->eType==BROWSER_ENTRYTYPE_DIR)
			{
				snprintf(str, sizeof(str), "/%s", pEntry->name);
				sprintf(sizestr, " ");
			}
			else
			if (pEntry->eType==BROWSER_ENTRYTYPE_DRIVE)
			{
				snprintf(str, sizeof(str), "%s", pEntry->name);
				sprintf(sizestr, " ");
			}
			else
			{
				snprintf(str, sizeof(str), "%s", pEntry->name);
				sprintf(sizestr, "%3dK", pEntry->size / 1024);
			}

			/* Display path:
			    - Non-selected rows OR selected row still in its dwell
			      delay -> static ellipsis ("Super Mario All...sfc").
			    - Selected row after the delay -> marquee scroll using
			      the shared tick counter.
			   GetEntryPath() / GetEntryName() still read directly from
			   m_pDirEntries[i].name, so the truncation here is purely
			   cosmetic and never affects fopen(). */
			if (iEntry == m_iSelect && s_marquee_delay >= BROWSER_MARQUEE_DELAY_FRAMES)
			{
				BrowserCopyMarquee(view, sizeof(view), str, nameMaxPx, s_marquee_tick);
			}
			else
			{
				BrowserCopyEllipsis(view, sizeof(view), str, nameMaxPx);
			}

			// render selection bar
			if (iEntry == m_iSelect)
			{
				if (iEntry == m_iSelect)
					PolyColor4f(0.0f, 1.0f, 0.0f, 0.5f); 
					else
					PolyColor4f(0.0f, 0.0f, 0.0f, 0.25f); 

				Int32 selW = FontGetStrWidth(str);
				if (selW > nameMaxPx) selW = nameMaxPx;
				PolyRect(vx-1, vy-1, selW + 2, FontGetHeight() + 2);
//				PolyRect(vx-2, vy-0, strlen(str) * 12 + 2, 13 + 0);
			}

			// render menu entry
			switch(pEntry->eType)
			{
				case BROWSER_ENTRYTYPE_DRIVE:
					FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
					break;
				case BROWSER_ENTRYTYPE_DIR:
					FontColor4f(1.0, 1.0f, 0.0f, 1.0f);
					break;
				case BROWSER_ENTRYTYPE_OTHER:
					FontColor4f(0.25, 0.25f, 0.25f, 1.0f);
					break;
                default:
					FontColor4f(0.8, 0.8f, 0.8f, 1.0f);
					break;
			}

		   //			FontColor4f(0.8, 0.8f, 0.8f, 1.0f);

			FontPuts(vx, vy, view);
//			FontPuts(vx+480, vy, sizestr);
		}

		vy += FontGetHeight() + 2;
		iEntry++;
	}

	/* Thin vertical divider between the ROM names and the cover area,
	   in the same faint light-gray as the row backdrops. Only shown when
	   the folder actually contains ROMs; it starts just below the title
	   bar (never crosses the title) and ends at the last visible entry. */
	if (bCoverUI)
	{
		{
			Int32   visRows = m_nEntries - m_iScroll;
			Float32 dlTop, dlBot;
			if (visRows > m_MaxLines) visRows = m_MaxLines;
			if (visRows < 1)          visRows = 1;
			dlTop = 32.0f;                                       /* below title */
			dlBot = 32.0f + (Float32)(visRows * (FontGetHeight() + 2));

			PolyTexture(NULL);
			PolyBlend(TRUE);
			PolyColor4f(0.85f, 0.85f, 0.90f, 0.22f);
			PolyRect(154.0f, dlTop, 1.0f, dlBot - dlTop);
		}
	}


/*
	FontSelect(0);
	FontColor4f(0.5, 0.5f, 0.5f, 1.0f);
	FontPuts(10, 220, "Select=Network");
  */

/*
	FontPuts(0, 210, "+ - \"");


    PolyBlend(TRUE);
    PolyTexture(&_FontTexture);
    PolyUV(0,0,256,32);
	PolyColor4f(1.0, 1.0, 1.0, 1.0f);
    PolyRect(0,120,256,32);
*/



	FontSelect(0);

	/* Cover art panel (right side). A dark backing panel + the cover,
	   or a "No Covers" placeholder when no matching PNG was found. Only
	   shown while enabled - otherwise the browser is unchanged. */
	if (bCoverUI)
	{
		PolyTexture(NULL);
		PolyBlend(TRUE);
		PolyColor4f(0.85f, 0.85f, 0.90f, 0.18f);
		PolyRect(BROWSER_COVER_X - 3, BROWSER_COVER_Y - 3,
		         BROWSER_COVER_W + 6, BROWSER_COVER_H + 6);

		if (CoverHasImage())
		{
			CoverDraw(BROWSER_COVER_X, BROWSER_COVER_Y,
			          BROWSER_COVER_W, BROWSER_COVER_H);
		}
		else if (CoverNoImage())
		{
			const Char *msg = "No Covers";
			FontColor4f(0.55f, 0.55f, 0.55f, 1.0f);
			FontPrintf(BROWSER_COVER_X + (BROWSER_COVER_W - FontGetStrWidth(msg)) / 2,
			           BROWSER_COVER_Y + BROWSER_COVER_H / 2 - 6, "%s", msg);
		}
		/* else: still loading -> just the empty panel (avoids a
		   "sem capa" flash that would resolve into a real cover) */
	}

	FontSelect(0);

	if (m_bSubMenu)
	{
		PolyTexture(NULL);
		PolyBlend(TRUE);
		PolyColor4f(0,0,0,0.8f);
		PolyRect(0, 0, 256, 224);

		m_SubMenu.Draw();
	}
}

void CBrowserScreen::Process()
{
}


void CBrowserScreen::Input(Uint32 buttons, Uint32 trigger)
{
	if (trigger & PAD_SELECT)
	{
		m_bSubMenu = !m_bSubMenu;
		  /*
		if (m_bSubMenu)
		{
	    	Char str[256];

//	        sprintf(str, "%s%s", m_Dir, m_pDirEntries[m_iSelect].name);
	        sprintf(str, "%s", m_pDirEntries[m_iSelect].name);
			m_SubMenu.SetTitle(str);
		}
		*/
	}

	if (m_bSubMenu)
	{
		m_SubMenu.Input(buttons,trigger);
		return;
	}

	if (trigger & PAD_UP)
	{
		m_iSelect--;
	}

	if (trigger & PAD_DOWN)
	{
		m_iSelect++;
	}

	if (trigger & (PAD_SQUARE))
	{
		/* With covers on, Square swaps the artwork (boxart / title /
		   gameplay). With covers off it keeps its old job: page up. */
		if (CoverIsEnabled())
			CoverCycleVariant();
		else
			m_iSelect-= m_MaxLines-1;
	}

	if (trigger & (PAD_CIRCLE))
	{
		m_iSelect+= m_MaxLines-1;
	}

	// scroll
	if (m_iSelect < 0) m_iSelect = 0;
 	if (m_iSelect > (m_nEntries - 1)) m_iSelect = (m_nEntries - 1);

	// scroll
	if (m_iSelect < m_iScroll)
	{
		m_iScroll = m_iSelect;
	}

	if (m_iSelect >= (m_iScroll + m_MaxLines - 1))
	{
		m_iScroll = m_iSelect - m_MaxLines + 1;
	}

	if (trigger & PAD_TRIANGLE)
    {
        Chdir("..");
    }

	if (trigger & (PAD_CROSS | PAD_START))
	{
		/* Same sizing rationale as in MenuEvent: m_Dir up to 512 + entry
		   name up to 255 fits comfortably in 1024, and this is the path
		   that eventually reaches fopen() through _MainLoopExecuteFile. */
		char str[1024];

		if (GetEntryPath(str, sizeof(str))!=0)
		{

			/* Modern cdfs.irx handles cache invalidation internally on
			   directory re-open; the legacy CDVD_FlushCache() RPC is no
			   longer needed (and the IRX it talked to is no longer
			   loaded). */

	        switch(m_pDirEntries[m_iSelect].eType)
	        {
	        case BROWSER_ENTRYTYPE_DIR:
	        case BROWSER_ENTRYTYPE_DRIVE:
				Chdir(m_pDirEntries[m_iSelect].name);
	            break;

	        default:
		        printf("exec: %s\n", str);
				/* Hand the cover-cache RAM (~1.5MB) back before the
				   emulator core spins up for the game. */
				CoverFreeCache();
				SendMessage(1, m_pDirEntries[m_iSelect].eType, (void *)str);
	            break;
	            
			}
		}
		return;
	}



}


/* Directory iteration via newlib stdio + dirent.h. opendir/readdir
   route through iomanX once init_ps2_filesystem_driver has run,
   so cdfs:/, mc0:/, mass:/, host:/ all use the same API path.

   We can no longer rely on dirent->d_type alone to tell files apart
   from subdirectories - cdfs.irx leaves it at DT_UNKNOWN, and a
   subset of older iomanX backends also under-fill it. To stay robust
   across every device we always confirm directories with a follow-up
   stat() on the joined path. The same trick handles the legacy
   CDVD.IRX bug where a stray SUBDIR bit leaked into regular files
   (see PR #76 for the original symptom). */

void CBrowserScreen::SetDir(const Char *pDir)
{
    DIR *dir;

    // DLog("[ui] MenuDir: '%s'", pDir);

	ResetEntries();

	strcpy(m_Dir, pDir);
	/* Kept for legacy callers that read m_bMCDir; with iomanX the
	   directory iteration path is the same for every device. */
	m_bMCDir = (pDir[0] == 'm' && pDir[1] == 'c' && pDir[3] == ':');
	m_iScroll = 0;
	m_iSelect = 0;

	ForceDraw();


	if (strlen(pDir) > 0)
	{
		dir = opendir(pDir);
		// DLog("[ui] opendir('%s') -> %p (errno=%d)", pDir, (void *)dir, dir ? 0 : errno);
		if (dir != NULL)
		{
			struct dirent *de;
			while ((de = readdir(dir)) != NULL)
			{
				BrowserEntryTypeE eType;
				Char childPath[1024];
				struct stat st;
				bool bIsDir = false;
				Int32 nSize = 0;

				if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
					continue;

				/* Hide cover-art PNGs from the browser list - they are
				   artwork for the cover system, not ROMs. */
				{
					size_t _nl = strlen(de->d_name);
					if (_nl >= 4 && strcasecmp(de->d_name + _nl - 4, ".png") == 0)
						continue;
				}

				/* Trust d_type only when it is concrete; otherwise stat
				   the joined path. cdfs.irx leaves d_type=DT_UNKNOWN. */
				bool typeKnown = false;
#ifdef DT_DIR
				if (de->d_type == DT_DIR) { bIsDir = true; typeKnown = true; }
				else if (de->d_type == DT_REG) { typeKnown = true; }
#endif
				if (!typeKnown)
				{
					snprintf(childPath, sizeof(childPath),
					         "%s%s", m_Dir, de->d_name);
					if (stat(childPath, &st) == 0)
					{
						bIsDir = S_ISDIR(st.st_mode) ? true : false;
						nSize  = (Int32)st.st_size;
					}
				}

				if (bIsDir)
				{
					eType = BROWSER_ENTRYTYPE_DIR;
				}
				else
				{
					eType = (BrowserEntryTypeE)SendMessage(
						2, 0, (void *)de->d_name);
					if (eType != BROWSER_ENTRYTYPE_EXECUTABLE)
						eType = BROWSER_ENTRYTYPE_OTHER;

					/* Pick up size if we did not stat above. */
					if (nSize == 0)
					{
						snprintf(childPath, sizeof(childPath),
						         "%s%s", m_Dir, de->d_name);
						if (stat(childPath, &st) == 0)
							nSize = (Int32)st.st_size;
					}
				}

				AddEntry(de->d_name, eType, nSize);
			}
			closedir(dir);
		}
	} else
	{
        AddEntry("cdfs:", BROWSER_ENTRYTYPE_DRIVE, 0);
//        AddEntry("cdrom:", BROWSER_ENTRYTYPE_DRIVE, 0);
        AddEntry("host:", BROWSER_ENTRYTYPE_DRIVE, 0);
        /* USB/HD via BDM: cada pendrive, HD externo USB e o HD INTERNO
           (FAT/exFAT, via ata_bd) viram uma unidade massN:.  A ordem
           depende da deteccao, entao listamos algumas; as vazias so'
           abrem sem conteudo. */
        AddEntry("mass0:", BROWSER_ENTRYTYPE_DRIVE, 0);
        AddEntry("mass1:", BROWSER_ENTRYTYPE_DRIVE, 0);
        AddEntry("hdd0:", BROWSER_ENTRYTYPE_DRIVE, 0);   /* HD interno (APA); pfs montado por dentro */
        AddEntry("mmce0:", BROWSER_ENTRYTYPE_DRIVE, 0);  /* MemCard PRO2 / SD2PSX */
        AddEntry("mc0:", BROWSER_ENTRYTYPE_DRIVE, 0);
        AddEntry("mc1:", BROWSER_ENTRYTYPE_DRIVE, 0);
        AddEntry("mmce1:", BROWSER_ENTRYTYPE_DRIVE, 0);  /* MemCard PRO2 / SD2PSX */
//        AddEntry("rom:", BROWSER_ENTRYTYPE_DRIVE, 0);
	}

	SortEntries();

    // DLog("[ui] BrowserEntries: %d (dir='%s')", m_nEntries, m_Dir);
}

void CBrowserScreen::Chdir(const Char *pSubDir)
{
	/* m_Dir is 512 and pSubDir can be a 255-char entry name; pick 1024
	   so the strcat below cannot overflow even on the deepest nested
	   ROM directories. */
	Char dir[1024];

	strcpy(dir, m_Dir);

	if (!strcmp(pSubDir, "."))
	{
	} else
	if (!strcmp(pSubDir, ".."))
	{
        if (strcmp(dir,"/"))
        {
		    Int32 i = strlen(dir) - 2;

		    // backup
		    while (i >= 0 && dir[i]!='/')
		    {
			    i--;
		    }
            i++;

		    dir[i] = 0;
        }
	}
	else
	{
		strcat(dir, pSubDir);
		strcat(dir, "/");
	}

	SetDir(dir);
}


