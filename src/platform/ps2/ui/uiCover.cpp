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
 *      ROM. This is what makes scrolling smooth even on a cdfs ISO with
 *      no covers at all (the old code did up to 4 failed fopen()s per
 *      ROM, and a failed open on CD is slow).
 *
 *   2. Image CACHE: decoded + scaled 128x128 covers kept in a bounded
 *      LRU pool, so revisiting / prefetched entries display instantly.
 *
 * Candidate cover directories (a cover for "Game.sfc" is "Game.png"):
 *      $(COVERS_PATH)/    (only if built with -DCOVERS_PATH)
 *      <rom directory>/
 *
 * Both the index and the cache are malloc'd lazily and freed on disable
 * and on ROM launch (CoverFreeCache), so they never hold RAM the
 * emulator core needs. PNG decode is upng (zlib license).
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

/* Fixed cover texture size (power-of-two for the GS sampler). */
#define COVER_TEX_W 128
#define COVER_TEX_H 128
#define COVER_RGBA_BYTES (COVER_TEX_W * COVER_TEX_H * 4)

/* Decoded-image LRU cache. 24 * ~65 KB ~= 1.5 MB. */
#define COVER_CACHE_SLOTS 24
#define COVER_KEY_MAX     1024

/* Directory index. */
#define COVER_INDEX_MAX   2048   /* max PNG names remembered per folder  */
#define COVER_NAME_MAX    208    /* max PNG file name length             */
#define COVER_DIRS_MAX    2      /* COVERS_PATH + the ROM's own folder   */

typedef struct
{
	char   key[COVER_KEY_MAX];   /* ROM path this image was decoded for */
	Int32  usedW, usedH;
	Bool   valid;
	Uint32 lru;
	Uint8  rgba[COVER_RGBA_BYTES];
} CoverEntT;

typedef struct
{
	Uint8 dir;                   /* index into s_dirs[]                  */
	char  name[COVER_NAME_MAX];  /* e.g. "Super Mario World (USA).png"   */
} CoverIdxT;

typedef enum { RES_UNKNOWN = 0, RES_FOUND, RES_NONE } ResolveE;
typedef enum { COVER_PENDING = 0, COVER_HASIMG, COVER_NOIMG } CoverStateE;

/* ---- module state -------------------------------------------------- */

static CoverEntT *s_cache    = 0;        /* image LRU pool, NULL = freed */
static Uint32     s_lruClock = 0;

static CoverIdxT *s_index    = 0;        /* directory PNG index          */
static Int32      s_indexCount = 0;
static char       s_indexDir[COVER_KEY_MAX] = "";  /* folder it was built for */
static char       s_dirs[COVER_DIRS_MAX][COVER_KEY_MAX];
static Int32      s_nDirs    = 0;

static Uint32   s_uVramTBP   = 0;
static Bool     s_bEnabled   = FALSE;    /* default OFF                  */
static Bool     s_bTexInited = FALSE;
static TextureT s_Tex;

static CoverStateE s_state   = COVER_PENDING;
static Int32       s_dispW   = 0;
static Int32       s_dispH   = 0;
static char        s_vramKey[COVER_KEY_MAX] = "";

/* ---- path helpers -------------------------------------------------- */

/* Split romPath into directory (with trailing slash) and base name
   without extension. */
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
			d[x * 4 + 0] = p[0];
			d[x * 4 + 1] = p[1];
			d[x * 4 + 2] = p[2];
			d[x * 4 + 3] = 0x80;   /* GS-opaque */
		}
	}

	*pW = (Int32)outW;
	*pH = (Int32)outH;
	return TRUE;
}

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

/* Build the PNG index for romDir if it is not already built for it.
   Touches the disk (opendir/readdir) - once per folder. */
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

	/* romDir already carries its trailing slash (browser m_Dir). */
	snprintf(s_dirs[s_nDirs], COVER_KEY_MAX, "%s", romDir);
	s_nDirs++;

	{
		int d;
		for (d = 0; d < s_nDirs; d++)
			_ScanDir(d, s_dirs[d]);
	}
	DLog("[cover] indexed %d png(s) in %s\n", (int)s_indexCount, romDir);
}

/* Resolve romPath -> existing cover file path using ONLY the in-memory
   index (no disk). RES_UNKNOWN if the index is not built for this dir. */
static ResolveE _ResolveCover(const char *romPath, char *out, size_t outSz)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char want[512 + 8];
	int i;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));

	if (!s_index || strcmp(s_indexDir, dir) != 0)
		return RES_UNKNOWN;

	snprintf(want, sizeof(want), "%s.png", base);
	for (i = 0; i < s_indexCount; i++) {
		if (strcasecmp(s_index[i].name, want) == 0) {
			snprintf(out, outSz, "%s%s",
			         s_dirs[s_index[i].dir], s_index[i].name);
			return RES_FOUND;
		}
	}
	return RES_NONE;
}

/* ---- image cache --------------------------------------------------- */

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

/* Decode coverFile into the image cache, keyed by romPath. Returns the
   entry or NULL on decode failure. */
static CoverEntT *_CacheDecode(const char *romPath, const char *coverFile)
{
	CoverEntT *e;
	Int32 w = 0, h = 0;

	if (!_CacheAlloc())
		return 0;
	e = _CachePickSlot();
	if (!_DecodeFileInto(coverFile, e->rgba, &w, &h)) {
		e->valid = FALSE;   /* slot now free; evicted entry (if any) lost */
		return 0;
	}
	snprintf(e->key, sizeof(e->key), "%s", romPath);
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

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);

	e = _CacheFind(romPath);
	if (e) { _ApplyEntry(e); return; }

	if (_ResolveCover(romPath, coverFile, sizeof(coverFile)) == RES_FOUND) {
		e = _CacheDecode(romPath, coverFile);
		if (e) { _ApplyEntry(e); return; }
	}
	s_state = COVER_NOIMG;
}

/* Every-frame display: in-memory only, never touches the disk. */
void CoverShowCached(const char *romPath)
{
	CoverEntT *e;
	char tmp[COVER_KEY_MAX];

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	e = _CacheFind(romPath);
	if (e) { _ApplyEntry(e); return; }

	switch (_ResolveCover(romPath, tmp, sizeof(tmp))) {
	case RES_NONE: s_state = COVER_NOIMG;  break;  /* known: no cover  */
	default:       s_state = COVER_PENDING; break; /* found-not-decoded / unknown */
	}
}

/* Warm a neighbour. Returns TRUE only if it actually decoded (one such
   per frame keeps prefetch from doing many decodes in a single frame).
   Index lookups and "no cover" answers are free (no disk). */
Bool CoverPrefetch(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char coverFile[COVER_KEY_MAX];

	if (!s_bEnabled || !s_bTexInited || !romPath || !romPath[0])
		return FALSE;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);

	if (_CacheFind(romPath))
		return FALSE;
	if (_ResolveCover(romPath, coverFile, sizeof(coverFile)) != RES_FOUND)
		return FALSE;       /* no cover: nothing decoded */

	return _CacheDecode(romPath, coverFile) ? TRUE : FALSE;
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

	PolyTexture(&s_Tex);
	PolyUV(0, 0, s_dispW, s_dispH);
	PolyBlend(FALSE);
	PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	PolyRect(dx, dy, dw, dh);

	PolyTexture(NULL);
}
