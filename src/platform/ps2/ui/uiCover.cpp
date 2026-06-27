/*
 * uiCover.cpp - Cover-art (capas) display for the ROM browser.
 *
 * PNG decoding via upng (src/third_party/upng, zlib license). The
 * decoded image is nearest-neighbour scaled (aspect-preserving) into a
 * fixed 128x128 RGBA texture and uploaded to a GS VRAM slot, then drawn
 * as a single textured PolyRect. We keep the texture small and fixed so
 * the VRAM footprint is predictable (128*128*4 = 64 KB = 0x100 TBP
 * units) - VRAM here is tight and hand-laid-out (see mainloop_init.cpp).
 *
 * Alpha: the GS treats texel alpha 0x80 as fully opaque (1.0); values
 * above 0x80 over-saturate. Covers are opaque rectangles, so we force
 * every texel's alpha to 0x80 regardless of the PNG's own alpha. That
 * also lets us accept both RGB8 and RGBA8 PNGs uniformly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "texture.h"
#include "poly.h"

extern "C" {
#include "gs.h"
}

extern "C" {
#include "upng.h"
}

extern "C" void DLog(const char *fmt, ...);

/* Fixed cover texture size (power-of-two for the GS sampler). */
#define COVER_TEX_W 128
#define COVER_TEX_H 128

/* ---- module state -------------------------------------------------- */

static Uint32   s_uVramTBP   = 0;
static Bool     s_bEnabled   = FALSE;   /* default OFF: original layout */
static Bool     s_bTexInited = FALSE;
static Bool     s_bHasImage  = FALSE;

static TextureT s_Tex;

/* Sub-rectangle of the 128x128 texture actually filled by the current
   cover (aspect-preserving scale leaves the rest unused). We sample
   only [0,0]..[s_usedW,s_usedH] when drawing. */
static Int32    s_usedW = 0;
static Int32    s_usedH = 0;

/* Path we last attempted, so re-selecting the same ROM does not redo
   the file IO + decode every frame. */
static char     s_curKey[1024] = "";

/* Scratch RGBA buffer for the GS upload. Lives in .bss (64 KB). 128-byte
   aligned for the EE->VRAM DMA inside GPPrimUploadTexture. */
static Uint8 s_TexBuf[COVER_TEX_W * COVER_TEX_H * 4] __attribute__((aligned(128)));

/* ---- helpers ------------------------------------------------------- */

/* Nearest-neighbour scale src (sw x sh, `comp` bytes/pixel: 3=RGB,
   4=RGBA) into the top-left of s_TexBuf, preserving aspect ratio.
   Records the filled region in s_usedW/s_usedH. Returns TRUE on ok. */
static Bool _ScaleIntoTex(const unsigned char *src, unsigned sw, unsigned sh, unsigned comp)
{
	unsigned outW, outH;
	unsigned y, x;

	if (!src || sw == 0 || sh == 0)
		return FALSE;

	/* Fit (sw x sh) inside (TEX_W x TEX_H) keeping aspect. Compare
	   cross-products to avoid float: sw/sh vs TEX_W/TEX_H. */
	if ((unsigned)(sw * COVER_TEX_H) >= (unsigned)(sh * COVER_TEX_W)) {
		/* width-bound */
		outW = COVER_TEX_W;
		outH = (sh * COVER_TEX_W) / sw;
	} else {
		/* height-bound */
		outH = COVER_TEX_H;
		outW = (sw * COVER_TEX_H) / sh;
	}
	if (outW == 0) outW = 1;
	if (outH == 0) outH = 1;
	if (outW > COVER_TEX_W) outW = COVER_TEX_W;
	if (outH > COVER_TEX_H) outH = COVER_TEX_H;

	for (y = 0; y < outH; y++) {
		unsigned syi = (y * sh) / outH;
		Uint8 *d = s_TexBuf + (y * COVER_TEX_W) * 4;
		for (x = 0; x < outW; x++) {
			unsigned sxi = (x * sw) / outW;
			const unsigned char *p = src + ((unsigned long)syi * sw + sxi) * comp;
			d[x * 4 + 0] = p[0];   /* R */
			d[x * 4 + 1] = p[1];   /* G */
			d[x * 4 + 2] = p[2];   /* B */
			d[x * 4 + 3] = 0x80;   /* A = GS-opaque */
		}
	}

	s_usedW = (Int32)outW;
	s_usedH = (Int32)outH;
	return TRUE;
}

/* Try to load one candidate PNG path. Returns TRUE if decoded+uploaded. */
static Bool _TryLoadFile(const char *path)
{
	upng_t *u;
	upng_format fmt;
	unsigned comp, w, h;
	const unsigned char *buf;
	Bool ok;

	u = upng_new_from_file(path);
	if (!u)
		return FALSE;

	if (upng_get_error(u) != UPNG_EOK) {
		upng_free(u);
		return FALSE;
	}
	if (upng_decode(u) != UPNG_EOK) {
		upng_free(u);
		return FALSE;
	}

	fmt = upng_get_format(u);
	if (fmt == UPNG_RGB8)       comp = 3;
	else if (fmt == UPNG_RGBA8) comp = 4;
	else { /* 16-bit / luminance / interlaced unsupported here */
		upng_free(u);
		return FALSE;
	}

	w   = upng_get_width(u);
	h   = upng_get_height(u);
	buf = upng_get_buffer(u);

	ok = _ScaleIntoTex(buf, w, h, comp);
	upng_free(u);
	if (!ok)
		return FALSE;

	/* GPPrimUploadTexture (via TextureUpload) does FlushCache(0) and
	   invalidates the GS texture cache, so a per-selection re-upload is
	   safe. */
	TextureUpload(&s_Tex, s_TexBuf);
	s_bHasImage = TRUE;
	DLog("[cover] loaded %s (%ux%u -> %dx%d)\n", path, w, h, s_usedW, s_usedH);
	return TRUE;
}

/* ---- public API ---------------------------------------------------- */

void CoverInit(Uint32 uVramTBP)
{
	s_uVramTBP = uVramTBP;

	TextureNew(&s_Tex, COVER_TEX_W, COVER_TEX_H, GS_PSMCT32);
	TextureSetAddr(&s_Tex, uVramTBP);

	s_bTexInited = TRUE;
	s_bHasImage  = FALSE;
	s_usedW = s_usedH = 0;
	s_curKey[0] = '\0';
}

void CoverSetEnabled(Bool bEnabled)
{
	s_bEnabled = bEnabled ? TRUE : FALSE;
}

Bool CoverIsEnabled(void)
{
	return s_bEnabled;
}

void CoverToggle(void)
{
	s_bEnabled = s_bEnabled ? FALSE : TRUE;
}

void CoverClearCurrent(void)
{
	s_bHasImage = FALSE;
	s_usedW = s_usedH = 0;
	s_curKey[0] = '\0';
}

Bool CoverHasImage(void)
{
	return s_bHasImage;
}

Bool CoverLoadForRomPath(const char *pRomPath)
{
	char dir[1024];
	char base[512];
	char cand[1600];
	const char *slash;
	const char *fname;
	size_t dlen;
	char *dot;

	if (!s_bTexInited || !pRomPath || !pRomPath[0])
		return FALSE;

	/* Same path as last time: nothing to do. */
	if (strcmp(pRomPath, s_curKey) == 0)
		return s_bHasImage;

	snprintf(s_curKey, sizeof(s_curKey), "%s", pRomPath);
	s_bHasImage = FALSE;
	s_usedW = s_usedH = 0;

	/* Split into directory (with trailing slash) and base name. */
	slash = strrchr(pRomPath, '/');
	fname = slash ? slash + 1 : pRomPath;
	dlen  = slash ? (size_t)(fname - pRomPath) : 0;
	if (dlen >= sizeof(dir))
		dlen = sizeof(dir) - 1;
	memcpy(dir, pRomPath, dlen);
	dir[dlen] = '\0';

	snprintf(base, sizeof(base), "%s", fname);
	dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';

#ifdef COVERS_PATH
	/* Build-time configured cover folder (Makefile: COVERS_PATH=...).
	   Tried first so a single shared folder works regardless of where
	   the ROM lives. Normalize the join so a path ending in '/' or a
	   bare device like "mass:" does not get a doubled / missing slash. */
	{
		const char *cp = COVERS_PATH;
		size_t cl = strlen(cp);
		if (cl > 0 && (cp[cl - 1] == '/' || cp[cl - 1] == ':'))
			snprintf(cand, sizeof(cand), "%s%s.png", cp, base);
		else
			snprintf(cand, sizeof(cand), "%s/%s.png", cp, base);
		if (_TryLoadFile(cand)) return TRUE;
	}
#endif

	snprintf(cand, sizeof(cand), "%s%s.png", dir, base);
	if (_TryLoadFile(cand)) return TRUE;

	snprintf(cand, sizeof(cand), "%scovers/%s.png", dir, base);
	if (_TryLoadFile(cand)) return TRUE;

	snprintf(cand, sizeof(cand), "%sNamed_Boxarts/%s.png", dir, base);
	if (_TryLoadFile(cand)) return TRUE;

	return FALSE;
}

void CoverDraw(Float32 bx, Float32 by, Float32 bw, Float32 bh)
{
	Float32 ar, boxAr, dw, dh, dx, dy;

	if (!s_bEnabled || !s_bHasImage || s_usedW <= 0 || s_usedH <= 0)
		return;
	if (bw <= 0.0f || bh <= 0.0f)
		return;

	/* Fit used region into the box, preserving aspect, centered. */
	ar    = (Float32)s_usedW / (Float32)s_usedH;
	boxAr = bw / bh;
	if (ar >= boxAr) {
		dw = bw;
		dh = bw / ar;
	} else {
		dh = bh;
		dw = bh * ar;
	}
	dx = bx + (bw - dw) * 0.5f;
	dy = by + (bh - dh) * 0.5f;

	/* PolyTexture resets UV to the full texture; override to sample
	   only the filled sub-rectangle. */
	PolyTexture(&s_Tex);
	PolyUV(0, 0, s_usedW, s_usedH);
	PolyBlend(FALSE);
	PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	PolyRect(dx, dy, dw, dh);

	/* Restore untextured state for whatever draws next. */
	PolyTexture(NULL);
}
