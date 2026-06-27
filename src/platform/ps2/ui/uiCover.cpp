/*
 * uiCover.cpp - Cover-art (capas) display for the ROM browser.
 *
 * PNG decoding via upng (src/third_party/upng, zlib license). Each cover
 * is nearest-neighbour scaled (aspect-preserving) into a 128x128 RGBA
 * buffer and kept in a bounded LRU RAM cache, so revisiting / prefetched
 * entries display instantly with no disk access. One fixed 128x128 GS
 * VRAM slot holds whatever cover is currently on screen.
 *
 * The cache is malloc'd lazily and freed on disable and on ROM launch
 * (see CoverFreeCache), so it never holds RAM the emulator core needs.
 *
 * A cache entry with usedW==0 is a "negative" entry: we probed the disk
 * for that ROM and found no cover. Keeping it stops us re-probing the
 * (slow) cdfs/USB filesystem for the same ROM over and over.
 *
 * Alpha: the GS treats texel alpha 0x80 as fully opaque; covers are
 * opaque rectangles, so we force every texel's alpha to 0x80 and accept
 * both RGB8 and RGBA8 PNGs uniformly.
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
#define COVER_RGBA_BYTES (COVER_TEX_W * COVER_TEX_H * 4)

/* Bounded LRU cache. 24 * ~65 KB ~= 1.5 MB, malloc'd only while covers
   are enabled and freed on disable / ROM launch. Holds the visible
   neighbourhood plus recently-visited entries so back-and-forth and
   single steps are cache hits. Tunable. */
#define COVER_CACHE_SLOTS 24
#define COVER_KEY_MAX     1024

typedef struct
{
	char   key[COVER_KEY_MAX];   /* full ROM path; "" => slot unused   */
	Int32  usedW, usedH;         /* filled sub-rect; 0 => no cover here */
	Bool   valid;
	Uint32 lru;                  /* last-use tick for eviction          */
	Uint8  rgba[COVER_RGBA_BYTES];
} CoverEntT;

/* Display state for the on-screen panel. */
typedef enum { COVER_PENDING = 0, COVER_HASIMG, COVER_NOIMG } CoverStateE;

/* ---- module state -------------------------------------------------- */

static CoverEntT *s_cache    = 0;       /* malloc'd pool, NULL when freed */
static Uint32     s_lruClock = 0;

static Uint32   s_uVramTBP   = 0;
static Bool     s_bEnabled   = FALSE;   /* default OFF: original layout   */
static Bool     s_bTexInited = FALSE;
static TextureT s_Tex;

static CoverStateE s_state   = COVER_PENDING;
static Int32       s_dispW   = 0;       /* used dims of the displayed cover */
static Int32       s_dispH   = 0;
static char        s_vramKey[COVER_KEY_MAX] = "";  /* key currently in VRAM */

/* ---- scaling / decode ---------------------------------------------- */

/* Nearest-neighbour scale src (sw x sh, comp bytes/pixel: 3=RGB, 4=RGBA)
   into dst (a COVER_TEX_W*COVER_TEX_H*4 buffer), preserving aspect.
   Writes the filled region to pW / pH. Returns TRUE on success. */
static Bool _ScaleInto(Uint8 *dst, const unsigned char *src,
                       unsigned sw, unsigned sh, unsigned comp,
                       Int32 *pW, Int32 *pH)
{
	unsigned outW, outH, x, y;

	if (!dst || !src || sw == 0 || sh == 0)
		return FALSE;

	if ((unsigned)(sw * COVER_TEX_H) >= (unsigned)(sh * COVER_TEX_W)) {
		outW = COVER_TEX_W;
		outH = (sh * COVER_TEX_W) / sw;
	} else {
		outH = COVER_TEX_H;
		outW = (sw * COVER_TEX_H) / sh;
	}
	if (outW == 0) outW = 1;
	if (outH == 0) outH = 1;
	if (outW > COVER_TEX_W) outW = COVER_TEX_W;
	if (outH > COVER_TEX_H) outH = COVER_TEX_H;

	for (y = 0; y < outH; y++) {
		unsigned syi = (y * sh) / outH;
		Uint8 *d = dst + (y * COVER_TEX_W) * 4;
		for (x = 0; x < outW; x++) {
			unsigned sxi = (x * sw) / outW;
			const unsigned char *p = src + ((unsigned long)syi * sw + sxi) * comp;
			d[x * 4 + 0] = p[0];   /* R */
			d[x * 4 + 1] = p[1];   /* G */
			d[x * 4 + 2] = p[2];   /* B */
			d[x * 4 + 3] = 0x80;   /* A = GS-opaque */
		}
	}

	*pW = (Int32)outW;
	*pH = (Int32)outH;
	return TRUE;
}

/* Decode one PNG file into dst. Returns TRUE + fills pW / pH on success. */
static Bool _DecodeFileInto(const char *path, Uint8 *dst, Int32 *pW, Int32 *pH)
{
	upng_t *u;
	upng_format fmt;
	unsigned comp, w, h;
	const unsigned char *buf;
	Bool ok;

	u = upng_new_from_file(path);
	if (!u)
		return FALSE;
	if (upng_get_error(u) != UPNG_EOK) { upng_free(u); return FALSE; }
	if (upng_decode(u) != UPNG_EOK)    { upng_free(u); return FALSE; }

	fmt = upng_get_format(u);
	if (fmt == UPNG_RGB8)       comp = 3;
	else if (fmt == UPNG_RGBA8) comp = 4;
	else { upng_free(u); return FALSE; }

	w   = upng_get_width(u);
	h   = upng_get_height(u);
	buf = upng_get_buffer(u);

	ok = _ScaleInto(dst, buf, w, h, comp, pW, pH);
	upng_free(u);
	return ok;
}

/* Try the candidate PNG paths for a ROM and decode the first that
   exists into dst. Returns TRUE on success. */
static Bool _DecodeCoverForRom(const char *romPath, Uint8 *dst, Int32 *pW, Int32 *pH)
{
	char dir[1024];
	char base[512];
	char cand[1600];
	const char *slash, *fname;
	size_t dlen;
	char *dot;

	if (!romPath || !romPath[0])
		return FALSE;

	slash = strrchr(romPath, '/');
	fname = slash ? slash + 1 : romPath;
	dlen  = slash ? (size_t)(fname - romPath) : 0;
	if (dlen >= sizeof(dir))
		dlen = sizeof(dir) - 1;
	memcpy(dir, romPath, dlen);
	dir[dlen] = '\0';

	snprintf(base, sizeof(base), "%s", fname);
	dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';

#ifdef COVERS_PATH
	{
		const char *cp = COVERS_PATH;
		size_t cl = strlen(cp);
		if (cl > 0 && (cp[cl - 1] == '/' || cp[cl - 1] == ':'))
			snprintf(cand, sizeof(cand), "%s%s.png", cp, base);
		else
			snprintf(cand, sizeof(cand), "%s/%s.png", cp, base);
		if (_DecodeFileInto(cand, dst, pW, pH)) return TRUE;
	}
#endif

	snprintf(cand, sizeof(cand), "%s%s.png", dir, base);
	if (_DecodeFileInto(cand, dst, pW, pH)) return TRUE;

	snprintf(cand, sizeof(cand), "%scovers/%s.png", dir, base);
	if (_DecodeFileInto(cand, dst, pW, pH)) return TRUE;

	snprintf(cand, sizeof(cand), "%sNamed_Boxarts/%s.png", dir, base);
	if (_DecodeFileInto(cand, dst, pW, pH)) return TRUE;

	return FALSE;
}

/* ---- cache --------------------------------------------------------- */

static Bool _CacheAlloc(void)
{
	if (s_cache)
		return TRUE;
	s_cache = (CoverEntT *)malloc(sizeof(CoverEntT) * COVER_CACHE_SLOTS);
	if (!s_cache)
		return FALSE;
	memset(s_cache, 0, sizeof(CoverEntT) * COVER_CACHE_SLOTS);
	return TRUE;
}

static CoverEntT *_CacheFind(const char *key)
{
	int i;
	if (!s_cache || !key || !key[0])
		return 0;
	for (i = 0; i < COVER_CACHE_SLOTS; i++)
		if (s_cache[i].valid && strcmp(s_cache[i].key, key) == 0)
			return &s_cache[i];
	return 0;
}

static CoverEntT *_CachePickSlot(void)
{
	CoverEntT *lru = &s_cache[0];
	int i;
	for (i = 0; i < COVER_CACHE_SLOTS; i++) {
		if (!s_cache[i].valid)
			return &s_cache[i];
		if (s_cache[i].lru < lru->lru)
			lru = &s_cache[i];
	}
	return lru;
}

/* Find or load romPath into the cache. Loads from disk on a miss (this
   is the expensive path). Returns the entry, or NULL on hard failure
   (cache alloc failed). A loaded-but-coverless ROM yields a negative
   entry (usedW==0), not NULL. *pLoaded is set TRUE if a disk load was
   performed on this call. */
static CoverEntT *_CacheGet(const char *romPath, Bool *pLoaded)
{
	CoverEntT *e;
	Int32 w = 0, h = 0;

	if (pLoaded) *pLoaded = FALSE;
	if (!romPath || !romPath[0])
		return 0;
	if (!_CacheAlloc())
		return 0;

	e = _CacheFind(romPath);
	if (e) {
		e->lru = ++s_lruClock;
		return e;          /* hit (may be a negative entry) */
	}

	/* miss: take a slot (free or LRU) and probe the disk */
	e = _CachePickSlot();
	snprintf(e->key, sizeof(e->key), "%s", romPath);
	e->valid = TRUE;
	e->lru   = ++s_lruClock;
	if (_DecodeCoverForRom(romPath, e->rgba, &w, &h)) {
		e->usedW = w;
		e->usedH = h;
	} else {
		e->usedW = 0;      /* negative: checked, no cover */
		e->usedH = 0;
	}
	if (pLoaded) *pLoaded = TRUE;
	return e;
}

/* Point the display (and the GS texture) at a cache entry. */
static void _ApplyEntry(CoverEntT *e, const char *key)
{
	if (!e) {
		s_state = COVER_PENDING;
		return;
	}
	if (e->usedW > 0) {
		if (strcmp(s_vramKey, key) != 0) {
			TextureUpload(&s_Tex, e->rgba);
			snprintf(s_vramKey, sizeof(s_vramKey), "%s", key);
		}
		s_dispW = e->usedW;
		s_dispH = e->usedH;
		s_state = COVER_HASIMG;
	} else {
		s_state = COVER_NOIMG;
	}
}

/* ---- public API ---------------------------------------------------- */

void CoverInit(Uint32 uVramTBP)
{
	s_uVramTBP = uVramTBP;

	TextureNew(&s_Tex, COVER_TEX_W, COVER_TEX_H, GS_PSMCT32);
	TextureSetAddr(&s_Tex, uVramTBP);

	s_bTexInited = TRUE;
	s_state      = COVER_PENDING;
	s_dispW = s_dispH = 0;
	s_vramKey[0] = '\0';
	/* leaves s_bEnabled untouched so a persisted setting survives a
	   video reinit that re-runs CoverInit */
}

void CoverFreeCache(void)
{
	if (s_cache) {
		free(s_cache);
		s_cache = 0;
	}
	s_state = COVER_PENDING;
	s_dispW = s_dispH = 0;
	s_vramKey[0] = '\0';
}

void CoverSetEnabled(Bool bEnabled)
{
	Bool b = bEnabled ? TRUE : FALSE;
	if (b == s_bEnabled)
		return;
	s_bEnabled = b;
	if (!b)
		CoverFreeCache();   /* turning off hands the RAM back */
}

Bool CoverIsEnabled(void)
{
	return s_bEnabled;
}

void CoverToggle(void)
{
	CoverSetEnabled(s_bEnabled ? FALSE : TRUE);
}

void CoverShow(const char *romPath)
{
	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }
	_ApplyEntry(_CacheGet(romPath, 0), romPath);
}

void CoverShowCached(const char *romPath)
{
	CoverEntT *e;

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	e = _CacheFind(romPath);
	if (e) {
		_ApplyEntry(e, romPath);
		return;
	}

	/* Not cached. If the live VRAM copy still happens to be this exact
	   ROM (entry evicted but texture not overwritten), keep showing it;
	   otherwise we are waiting on a load. */
	if (strcmp(s_vramKey, romPath) == 0 && s_dispW > 0)
		s_state = COVER_HASIMG;
	else
		s_state = COVER_PENDING;
}

Bool CoverPrefetch(const char *romPath)
{
	Bool loaded = FALSE;

	if (!s_bEnabled || !s_bTexInited || !romPath || !romPath[0])
		return FALSE;
	if (s_cache && _CacheFind(romPath))
		return FALSE;       /* already cached: nothing to do */

	_CacheGet(romPath, &loaded);
	return loaded;          /* TRUE only if a disk load happened */
}

Bool CoverHasImage(void)
{
	return (s_state == COVER_HASIMG) ? TRUE : FALSE;
}

Bool CoverNoImage(void)
{
	return (s_state == COVER_NOIMG) ? TRUE : FALSE;
}

void CoverDraw(Float32 bx, Float32 by, Float32 bw, Float32 bh)
{
	Float32 ar, boxAr, dw, dh, dx, dy;

	if (s_state != COVER_HASIMG || s_dispW <= 0 || s_dispH <= 0)
		return;
	if (bw <= 0.0f || bh <= 0.0f)
		return;

	ar    = (Float32)s_dispW / (Float32)s_dispH;
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

	PolyTexture(&s_Tex);
	PolyUV(0, 0, s_dispW, s_dispH);
	PolyBlend(FALSE);
	PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	PolyRect(dx, dy, dw, dh);

	PolyTexture(NULL);
}
