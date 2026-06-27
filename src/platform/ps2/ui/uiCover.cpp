/*
 * uiCover.cpp - Cover-art (capas) display for the ROM browser.
 *
 * Two-layer design so the (slow) cdfs/USB filesystem is touched as
 * little as possible:
 *
 *   1. Directory INDEX: when the browser enters a folder we list the
 *      candidate cover directories ONCE (opendir/readdir) and remember
 *      every "*.png" file name in RAM. Answering "does ROM X have a
 *      cover?" is then a pure in-memory string compare - zero fopen per
 *      ROM, which keeps scrolling smooth even on a cdfs ISO.
 *
 *   2. Image CACHE: decoded + scaled 256x256 covers kept in a bounded
 *      LRU pool, so revisiting / prefetched entries display instantly.
 *
 * Image variants: pressing Square cycles through the artwork a ROM has,
 * using the common "-N" suffix convention (boxart / title / gameplay):
 *      Game.png      (variant 0, the default boxart)
 *      Game-1.png    (variant 1)
 *      Game-2.png    (variant 2) ...
 * Only the variants that actually exist are visited.
 *
 * Transparency: PNG alpha is honoured (mapped to the GS 0..0x80 range)
 * and the cover is drawn with blending on, so transparent logos show
 * the panel through their see-through areas.
 *
 * Candidate cover directories (same base name as the ROM):
 *      $(COVERS_PATH)/    (only if built with -DCOVERS_PATH)
 *      <rom directory>/
 *
 * Index and cache are malloc'd lazily and freed on disable and on ROM
 * launch (CoverFreeCache). PNG decode is upng (zlib license).
 *
 * NOTE (untested on real PS2): the cover texture lives in a hard-coded
 * VRAM slot from the legacy GS layout (COVER_TEX in mainloop_init.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

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

/* Force the GS texture cache to be flushed before the next textured
   primitive. Defined in gskit_backend.c. We call it right before the
   cover draw so a freshly re-uploaded cover (same VRAM address as the
   previous one) is never served stale texels by the GS - the classic
   "first texture shows, later ones don't" PS2 bug. */
extern "C" void GSK_InvalidateTextureCache(void);

/* Fixed cover texture size (power-of-two for the GS sampler). 256 keeps
   it close to 1:1 with the on-screen panel so it is not blocky; bilinear
   filtering (set in CoverInit) smooths the rest. */
#define COVER_TEX_W 256
#define COVER_TEX_H 256
#define COVER_RGBA_BYTES (COVER_TEX_W * COVER_TEX_H * 4)

/* Decoded-image LRU cache. 256x256 RGBA = 256 KB/slot. 16 slots = 4 MB
   (menu only; freed on launch/off). */
#define COVER_CACHE_SLOTS 16
#define COVER_KEY_MAX     1024

/* Directory index. */
#define COVER_INDEX_MAX   2048
#define COVER_NAME_MAX    208
#define COVER_DIRS_MAX    2

/* Highest "-N" variant we look for (0 = base boxart). */
#define COVER_VARIANT_MAX 9

typedef struct
{
	char   key[COVER_KEY_MAX];   /* full cover FILE path this image is */
	Int32  usedW, usedH;
	Bool   valid;
	Uint32 lru;
	Uint8  rgba[COVER_RGBA_BYTES];
} CoverEntT;

typedef struct
{
	Uint8 dir;
	char  name[COVER_NAME_MAX];
} CoverIdxT;

typedef enum { RES_UNKNOWN = 0, RES_FOUND, RES_NONE } ResolveE;
typedef enum { COVER_PENDING = 0, COVER_HASIMG, COVER_NOIMG } CoverStateE;

/* ---- module state -------------------------------------------------- */

static CoverEntT *s_cache    = 0;
static Uint32     s_lruClock = 0;

static CoverIdxT *s_index    = 0;
static Int32      s_indexCount = 0;
static char       s_indexDir[COVER_KEY_MAX] = "";
static char       s_dirs[COVER_DIRS_MAX][COVER_KEY_MAX];
static Int32      s_nDirs    = 0;

static Uint32   s_uVramTBP   = 0;
static Bool     s_bEnabled   = FALSE;
static Bool     s_bTexInited = FALSE;
static TextureT s_Tex;

static CoverStateE s_state   = COVER_PENDING;
static Int32       s_dispW   = 0;
static Int32       s_dispH   = 0;
static char        s_vramKey[COVER_KEY_MAX] = "";

static int  s_variant = 0;                  /* current artwork variant   */
static char s_curRom[COVER_KEY_MAX] = "";   /* ROM the variant belongs to */

/* ---- path helpers -------------------------------------------------- */

static void _SplitRomPath(const char *romPath, char *dir, size_t dirSz,
                          char *base, size_t baseSz)
{
	const char *slash = strrchr(romPath, '/');
	const char *fname = slash ? slash + 1 : romPath;
	size_t dlen = slash ? (size_t)(fname - romPath) : 0;
	char *dot;

	if (dlen >= dirSz)
		dlen = dirSz - 1;
	memcpy(dir, romPath, dlen);
	dir[dlen] = '\0';

	snprintf(base, baseSz, "%s", fname);
	dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
}

/* ---- scaling / decode ---------------------------------------------- */

static Bool _ScaleInto(Uint8 *dst, const unsigned char *src,
                       unsigned sw, unsigned sh, unsigned comp, unsigned bd,
                       Int32 *pW, Int32 *pH)
{
	unsigned outW, outH, x, y;
	unsigned bps = (bd == 16) ? 2u : 1u;     /* bytes per channel sample */
	unsigned stride = comp * bps;            /* bytes per source pixel   */

	if (!dst || !src || sw == 0 || sh == 0)
		return FALSE;

	/* Clear first: the aspect-fit image leaves unused area, and the
	   bilinear sampler reads one texel past the edge - an uninitialised
	   border would show a fringe. Cleared to fully-transparent black. */
	memset(dst, 0, COVER_RGBA_BYTES);

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
			const unsigned char *p = src + ((unsigned long)syi * sw + sxi) * stride;
			Uint8 r, g, b, a;

			/* Read each channel's high byte (16-bit) or byte (8-bit),
			   then expand to RGBA by component count:
			     1 = grayscale, 2 = gray+alpha, 3 = RGB, 4 = RGBA. */
			#define CH(i) (p[(i) * bps])     /* bps=2 -> big-endian high byte */
			if (comp <= 2) {
				r = g = b = CH(0);
				a = (comp == 2) ? CH(1) : 0xFF;
			} else {
				r = CH(0); g = CH(1); b = CH(2);
				a = (comp == 4) ? CH(3) : 0xFF;
			}
			#undef CH

			d[x * 4 + 0] = r;
			d[x * 4 + 1] = g;
			d[x * 4 + 2] = b;
			/* GS treats texel alpha 0x80 as fully opaque (1.0); map the
			   source 0..255 alpha into 0..0x80. */
			d[x * 4 + 3] = (Uint8)(((unsigned)a * 128u) / 255u);
		}
	}

	*pW = (Int32)outW;
	*pH = (Int32)outH;
	return TRUE;
}

static Bool _DecodeFileInto(const char *path, Uint8 *dst, Int32 *pW, Int32 *pH)
{
	upng_t *u;
	unsigned comp, bd, w, h;
	const unsigned char *buf;
	Bool ok;

	u = upng_new_from_file(path);
	if (!u)
		return FALSE;
	if (upng_get_error(u) != UPNG_EOK) { upng_free(u); return FALSE; }
	if (upng_decode(u) != UPNG_EOK)    { upng_free(u); return FALSE; }

	/* upng decodes grayscale (8-bit), gray+alpha (8-bit), and RGB/RGBA
	   at 8 OR 16 bits per channel. We handle all of those (16-bit takes
	   the high byte). Sub-byte (1/2/4-bit) grayscale, palette/indexed
	   and interlaced PNGs are NOT decodable by upng -> skipped. */
	comp = upng_get_components(u);
	bd   = upng_get_bitdepth(u);
	if ((bd != 8 && bd != 16) || comp < 1 || comp > 4) {
		upng_free(u);
		return FALSE;
	}

	w   = upng_get_width(u);
	h   = upng_get_height(u);
	buf = upng_get_buffer(u);

	ok = _ScaleInto(dst, buf, w, h, comp, bd, pW, pH);
	upng_free(u);
	return ok;
}

/* ---- directory index ----------------------------------------------- */

static Bool _IndexAlloc(void)
{
	if (s_index)
		return TRUE;
	s_index = (CoverIdxT *)malloc(sizeof(CoverIdxT) * COVER_INDEX_MAX);
	return s_index ? TRUE : FALSE;
}

static void _ScanDir(int dirIdx, const char *path)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir(path);
	if (!dir)
		return;
	while ((de = readdir(dir)) != NULL && s_indexCount < COVER_INDEX_MAX) {
		const char *n = de->d_name;
		size_t L = strlen(n);
		if (L < 5 || L >= COVER_NAME_MAX)
			continue;
		if (strcasecmp(n + L - 4, ".png") != 0)
			continue;
		s_index[s_indexCount].dir = (Uint8)dirIdx;
		snprintf(s_index[s_indexCount].name, COVER_NAME_MAX, "%s", n);
		s_indexCount++;
	}
	closedir(dir);
}

static void _EnsureIndex(const char *romDir)
{
	if (s_index && strcmp(s_indexDir, romDir) == 0)
		return;
	if (!_IndexAlloc())
		return;

	s_indexCount = 0;
	s_nDirs      = 0;
	snprintf(s_indexDir, sizeof(s_indexDir), "%s", romDir);

#ifdef COVERS_PATH
	{
		const char *cp = COVERS_PATH;
		size_t cl = strlen(cp);
		if (cl > 0 && cp[cl - 1] != '/' && cp[cl - 1] != ':')
			snprintf(s_dirs[s_nDirs], COVER_KEY_MAX, "%s/", cp);
		else
			snprintf(s_dirs[s_nDirs], COVER_KEY_MAX, "%s", cp);
		s_nDirs++;
	}
#endif

	snprintf(s_dirs[s_nDirs], COVER_KEY_MAX, "%s", romDir);
	s_nDirs++;

	{
		int d;
		for (d = 0; d < s_nDirs; d++)
			_ScanDir(d, s_dirs[d]);
	}
	DLog("[cover] indexed %d png(s) in %s\n", (int)s_indexCount, romDir);
}

/* Resolve (romPath, variant) -> existing cover file path, in-memory
   only. variant 0 = "base.png"; variant N>0 = "base-N.png". */
static ResolveE _ResolveCoverV(const char *romPath, int variant,
                               char *out, size_t outSz)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char want[512 + 16];
	int i;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));

	if (!s_index || strcmp(s_indexDir, dir) != 0)
		return RES_UNKNOWN;

	if (variant <= 0)
		snprintf(want, sizeof(want), "%s.png", base);
	else
		snprintf(want, sizeof(want), "%s-%d.png", base, variant);

	for (i = 0; i < s_indexCount; i++) {
		if (strcasecmp(s_index[i].name, want) == 0) {
			snprintf(out, outSz, "%s%s",
			         s_dirs[s_index[i].dir], s_index[i].name);
			return RES_FOUND;
		}
	}
	return RES_NONE;
}

/* ---- image cache (keyed by cover file path) ------------------------ */

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

static CoverEntT *_CacheDecode(const char *coverFile)
{
	CoverEntT *e;
	Int32 w = 0, h = 0;

	if (!_CacheAlloc())
		return 0;
	e = _CachePickSlot();
	if (!_DecodeFileInto(coverFile, e->rgba, &w, &h)) {
		e->valid = FALSE;
		return 0;
	}
	snprintf(e->key, sizeof(e->key), "%s", coverFile);
	e->usedW = w;
	e->usedH = h;
	e->valid = TRUE;
	e->lru   = ++s_lruClock;
	return e;
}

static void _ApplyEntry(CoverEntT *e)
{
	if (!e || e->usedW <= 0) {
		s_state = COVER_NOIMG;
		return;
	}
	if (strcmp(s_vramKey, e->key) != 0) {
		TextureUpload(&s_Tex, e->rgba);
		snprintf(s_vramKey, sizeof(s_vramKey), "%s", e->key);
	}
	s_dispW = e->usedW;
	s_dispH = e->usedH;
	s_state = COVER_HASIMG;
}

/* Reset the variant to 0 (boxart) whenever the selected ROM changes. */
static void _TrackRom(const char *romPath)
{
	if (strcmp(romPath, s_curRom) != 0) {
		snprintf(s_curRom, sizeof(s_curRom), "%s", romPath);
		s_variant = 0;
	}
}

/* ---- public API ---------------------------------------------------- */

void CoverInit(Uint32 uVramTBP)
{
	s_uVramTBP = uVramTBP;

	TextureNew(&s_Tex, COVER_TEX_W, COVER_TEX_H, GS_PSMCT32);
	TextureSetAddr(&s_Tex, uVramTBP);
	TextureSetFilter(&s_Tex, 1);   /* bilinear */

	s_bTexInited = TRUE;
	s_state      = COVER_PENDING;
	s_dispW = s_dispH = 0;
	s_vramKey[0] = '\0';
}

void CoverFreeCache(void)
{
	if (s_cache) { free(s_cache); s_cache = 0; }
	if (s_index) { free(s_index); s_index = 0; }
	s_indexCount = 0;
	s_indexDir[0] = '\0';
	s_nDirs = 0;
	s_state = COVER_PENDING;
	s_dispW = s_dispH = 0;
	s_vramKey[0] = '\0';
	s_variant = 0;
	s_curRom[0] = '\0';
}

void CoverSetEnabled(Bool bEnabled)
{
	Bool b = bEnabled ? TRUE : FALSE;
	if (b == s_bEnabled)
		return;
	s_bEnabled = b;
	if (!b)
		CoverFreeCache();
}

Bool CoverIsEnabled(void) { return s_bEnabled; }

void CoverToggle(void) { CoverSetEnabled(s_bEnabled ? FALSE : TRUE); }

/* Current selection: may build the index and decode (disk allowed). */
void CoverShow(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char coverFile[COVER_KEY_MAX];
	CoverEntT *e;
	ResolveE r;

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	_TrackRom(romPath);
	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);

	r = _ResolveCoverV(romPath, s_variant, coverFile, sizeof(coverFile));
	if (r != RES_FOUND && s_variant != 0) {
		/* requested variant missing - fall back to the base boxart */
		s_variant = 0;
		r = _ResolveCoverV(romPath, 0, coverFile, sizeof(coverFile));
	}
	if (r == RES_FOUND) {
		e = _CacheFind(coverFile);
		if (!e)
			e = _CacheDecode(coverFile);
		if (e) { _ApplyEntry(e); return; }
	}
	s_state = COVER_NOIMG;
}

/* Every-frame display: in-memory only, never touches the disk. */
void CoverShowCached(const char *romPath)
{
	char coverFile[COVER_KEY_MAX];
	CoverEntT *e;

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	_TrackRom(romPath);

	switch (_ResolveCoverV(romPath, s_variant, coverFile, sizeof(coverFile))) {
	case RES_FOUND:
		e = _CacheFind(coverFile);
		if (e) _ApplyEntry(e);
		else   s_state = COVER_PENDING;   /* exists, not decoded yet */
		break;
	case RES_NONE:
		s_state = COVER_NOIMG;
		break;
	default:
		s_state = COVER_PENDING;          /* index not built yet */
		break;
	}
}

/* Warm a neighbour's base boxart. Returns TRUE only if it decoded. */
Bool CoverPrefetch(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char coverFile[COVER_KEY_MAX];

	if (!s_bEnabled || !s_bTexInited || !romPath || !romPath[0])
		return FALSE;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);

	if (_ResolveCoverV(romPath, 0, coverFile, sizeof(coverFile)) != RES_FOUND)
		return FALSE;
	if (_CacheFind(coverFile))
		return FALSE;

	return _CacheDecode(coverFile) ? TRUE : FALSE;
}

/* Square: advance to the next artwork variant that exists for the
   current ROM (wrapping), and display it now. */
void CoverCycleVariant(void)
{
	char coverFile[COVER_KEY_MAX];
	int v, cand, found = -1;

	if (!s_bEnabled || !s_bTexInited || !s_curRom[0])
		return;

	for (v = 1; v <= COVER_VARIANT_MAX; v++) {
		cand = (s_variant + v) % (COVER_VARIANT_MAX + 1);
		if (_ResolveCoverV(s_curRom, cand, coverFile, sizeof(coverFile)) == RES_FOUND) {
			found = cand;
			break;
		}
	}
	if (found < 0)
		return;   /* only one image: nothing to cycle */

	s_variant = found;

	{
		CoverEntT *e = _CacheFind(coverFile);
		if (!e)
			e = _CacheDecode(coverFile);
		if (e)
			_ApplyEntry(e);
	}
}

Bool CoverHasImage(void) { return (s_state == COVER_HASIMG) ? TRUE : FALSE; }
Bool CoverNoImage(void)  { return (s_state == COVER_NOIMG)  ? TRUE : FALSE; }

void CoverDraw(Float32 bx, Float32 by, Float32 bw, Float32 bh)
{
	Float32 ar, boxAr, dw, dh, dx, dy;

	if (s_state != COVER_HASIMG || s_dispW <= 0 || s_dispH <= 0)
		return;
	if (bw <= 0.0f || bh <= 0.0f)
		return;

	ar    = (Float32)s_dispW / (Float32)s_dispH;
	boxAr = bw / bh;
	if (ar >= boxAr) { dw = bw; dh = bw / ar; }
	else             { dh = bh; dw = bh * ar; }
	dx = bx + (bw - dw) * 0.5f;
	dy = by + (bh - dh) * 0.5f;

	/* Invalidate the GS texture cache so the cover we just (re)uploaded
	   to the shared VRAM slot is read fresh, not served stale from a
	   previous cover at the same address. GPPrimTexRect (reached via
	   PolyRect below) consumes this and emits the TEXFLUSH. */
	GSK_InvalidateTextureCache();

	PolyTexture(&s_Tex);
	PolyUV(0, 0, s_dispW, s_dispH);
	/* Opaque draw. Alpha blending of the PSMCT32 cover made the image
	   render invisible on real PS2 (the GS texel-alpha blend config is
	   not what the flat-colour panels use, and is untestable here), so
	   we draw opaque for now. Transparent PNGs show their RGB. */
	PolyBlend(FALSE);
	PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	PolyRect(dx, dy, dw, dh);

	PolyTexture(NULL);
}
