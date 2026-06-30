/*
 * uiCover.cpp - Cover-art (capas) display for the ROM browser.
 *
 * Two-layer design so the (slow) cdfs/USB filesystem is touched as
 * little as possible:
 *
 *   1. Directory INDEX: when the browser enters a folder we list the
 *      candidate cover directories ONCE (opendir/readdir) and remember
 *      every "*.png" file name in RAM, tagged with which folder it came
 *      from. "Does ROM X have a cover?" is then a pure in-memory string
 *      compare - no fopen per ROM, so scrolling stays smooth on a CD.
 *
 *   2. Image CACHE: decoded + scaled 256x256 covers kept in a bounded
 *      LRU pool, so revisiting / prefetched entries display instantly.
 *
 * Cover sources (same base name as the ROM). Searched both next to the
 * ROM and, if built with -DCOVERS_PATH, in that shared folder; each can
 * also use the libretro thumbnail subfolders:
 *      <dir>/<rom>.png                      (plain, next to the ROM)
 *      <dir>/Named_Boxarts/<rom>.png        (libretro box art)
 *      <dir>/Named_Titles/<rom>.png         (libretro title screen)
 *      <dir>/Named_Snaps/<rom>.png          (libretro gameplay snap)
 *      <dir>/<rom>-1.png, <rom>-2.png ...   (extra images)
 * Square cycles through whichever of these exist for the selected ROM.
 *
 * PNG decode is upng (zlib license): RGB/RGBA 8/16-bit, grayscale, and
 * palette/indexed. Interlaced (Adam7) is not supported.
 *
 * Index and cache are malloc'd lazily and freed on disable and on ROM
 * launch (CoverFreeCache).
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
#include "embedded_irx.h"   /* HddMapPath (capas no HD: hdd0:->pfs0:) */

extern "C" {
#include "gs.h"
}

extern "C" {
#include "upng.h"
}

extern "C" void DLog(const char *fmt, ...);

/* Flush the GS texture cache before the next textured primitive (the
   "first texture shows, later ones don't" PS2 bug). */
extern "C" void GSK_InvalidateTextureCache(void);

#define COVER_TEX_W 256
#define COVER_TEX_H 256
#define COVER_RGBA_BYTES (COVER_TEX_W * COVER_TEX_H * 4)

#define COVER_CACHE_SLOTS 16
#define COVER_KEY_MAX     1024

#define COVER_INDEX_MAX   2048
#define COVER_NAME_MAX    208
/* up to three bases (COVERS_PATH + ROM dir + ELF dir) x 4 kinds */
#define COVER_DIRS_MAX    12
/* max distinct artwork images we list per ROM (boxart/title/snap + -N) */
#define COVER_FOUND_MAX   12
/* highest "-N" suffix variant we look for */
#define COVER_SUFFIX_MAX  9

/* Directory "kinds" - which thumbnail role a scanned folder plays. */
enum { DK_ROOT = 0, DK_BOX, DK_TITLE, DK_SNAP };

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

typedef enum { COVER_PENDING = 0, COVER_HASIMG, COVER_NOIMG } CoverStateE;

/* ---- module state -------------------------------------------------- */

static CoverEntT *s_cache    = 0;
static Uint32     s_lruClock = 0;

static CoverIdxT *s_index    = 0;
static Int32      s_indexCount = 0;
static char       s_indexDir[COVER_KEY_MAX] = "";
static char       s_dirs[COVER_DIRS_MAX][COVER_KEY_MAX];
static Uint8      s_dirKind[COVER_DIRS_MAX];
static Int32      s_nDirs    = 0;
static Uint32     s_indexGen = 0;   /* bumped every time the index is rebuilt */

static Uint32   s_uVramTBP   = 0;
static Bool     s_bEnabled   = FALSE;
static Bool     s_bTexInited = FALSE;
static TextureT s_Tex;

static CoverStateE s_state   = COVER_PENDING;
static Int32       s_dispW   = 0;
static Int32       s_dispH   = 0;
static char        s_vramKey[COVER_KEY_MAX] = "";

/* Per-ROM list of artwork that actually exists (full paths), and which
   one is currently shown (the variant index). */
static char    s_found[COVER_FOUND_MAX][COVER_KEY_MAX];
static int     s_foundCount = 0;
static char    s_foundRom[COVER_KEY_MAX] = "";
static Uint32  s_foundGen   = 0xFFFFFFFFu;
static int     s_variant    = 0;
static char    s_curRom[COVER_KEY_MAX] = "";

/* ---- path helpers -------------------------------------------------- */

static void _SplitRomPath(const char *romPath, char *dir, size_t dirSz,
                          char *base, size_t baseSz)
{
	/* HD interno (APA): traduz "hdd0:/PART/..." -> "pfs0:/..." (particao ja'
	   montada pelo browser) para que as pastas de capa resolvam no HD. */
	char hddPath[1024];
	if (HddMapPath(romPath, hddPath, sizeof(hddPath)) == 1)
		romPath = hddPath;

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
	unsigned bps = (bd == 16) ? 2u : 1u;
	unsigned stride = comp * bps;

	if (!dst || !src || sw == 0 || sh == 0)
		return FALSE;

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
			#define CH(i) (p[(i) * bps])
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

static void _AddDir(const char *path, int kind)
{
	if (s_nDirs >= COVER_DIRS_MAX)
		return;
	snprintf(s_dirs[s_nDirs], COVER_KEY_MAX, "%s", path);
	s_dirKind[s_nDirs] = (Uint8)kind;
	_ScanDir(s_nDirs, s_dirs[s_nDirs]);
	s_nDirs++;
}

/* Add a base folder (must end with '/') and its libretro subfolders. */
static void _AddBase(const char *base)
{
	char sub[COVER_KEY_MAX];
	_AddDir(base, DK_ROOT);
	snprintf(sub, sizeof(sub), "%sNamed_Boxarts/", base); _AddDir(sub, DK_BOX);
	snprintf(sub, sizeof(sub), "%sNamed_Titles/",  base); _AddDir(sub, DK_TITLE);
	snprintf(sub, sizeof(sub), "%sNamed_Snaps/",   base); _AddDir(sub, DK_SNAP);
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
		char b[COVER_KEY_MAX];
		if (cl > 0 && cp[cl - 1] != '/' && cp[cl - 1] != ':')
			snprintf(b, sizeof(b), "%s/", cp);
		else
			snprintf(b, sizeof(b), "%s", cp);
		_AddBase(b);
	}
#endif
	_AddBase(romDir);   /* romDir already carries its trailing slash */

	/* Pasta "covers/" AO LADO DO ELF (boot dir): permite uma pasta de capas
	   junto do ELF no dispositivo, sem recompilar com COVERS_PATH.  Fallback
	   apos as capas ao lado da propria ROM. */
	{
		extern "C" char *MainGetBootDir();
		const char *bd = MainGetBootDir();
		if (bd && bd[0])
		{
			char b[COVER_KEY_MAX];
			int n = 0;
			while (bd[n] && n < (int)sizeof(b) - 10)
			{
				b[n] = (bd[n] == '\\') ? '/' : bd[n];   /* normaliza '\' */
				n++;
			}
			if (n > 0 && b[n - 1] != '/') b[n++] = '/';
			b[n] = 0;
			strncat(b, "covers/", sizeof(b) - strlen(b) - 1);
			_AddBase(b);
		}
	}

	s_indexGen++;
	DLog("[cover] indexed %d png(s) across %d dir(s) in %s\n",
	     (int)s_indexCount, (int)s_nDirs, romDir);
}

static Bool _IndexBuiltFor(const char *romDir)
{
	return (s_index && strcmp(s_indexDir, romDir) == 0) ? TRUE : FALSE;
}

/* Find file `name` in a folder of the given kind; build its full path. */
static Bool _ResolveKind(const char *name, int kind, char *out, size_t outSz)
{
	int i;
	if (!s_index)
		return FALSE;
	for (i = 0; i < s_indexCount; i++) {
		if (s_dirKind[s_index[i].dir] == (Uint8)kind &&
		    strcasecmp(s_index[i].name, name) == 0) {
			snprintf(out, outSz, "%s%s", s_dirs[s_index[i].dir], s_index[i].name);
			return TRUE;
		}
	}
	return FALSE;
}

/* ---- per-ROM artwork list (s_found) -------------------------------- */

static void _FoundAdd(const char *path)
{
	int i;
	if (s_foundCount >= COVER_FOUND_MAX)
		return;
	for (i = 0; i < s_foundCount; i++)
		if (strcmp(s_found[i], path) == 0)
			return;   /* dedupe */
	snprintf(s_found[s_foundCount], COVER_KEY_MAX, "%s", path);
	s_foundCount++;
}

static void _RebuildFound(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	char want[512 + 16];
	char path[COVER_KEY_MAX];
	int  n;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	s_foundCount = 0;

	/* Order = cycle order: boxart, title, snap, then extra -N images. */
	snprintf(want, sizeof(want), "%s.png", base);
	if (_ResolveKind(want, DK_ROOT,  path, sizeof(path))) _FoundAdd(path);
	if (_ResolveKind(want, DK_BOX,   path, sizeof(path))) _FoundAdd(path);
	if (_ResolveKind(want, DK_TITLE, path, sizeof(path))) _FoundAdd(path);
	if (_ResolveKind(want, DK_SNAP,  path, sizeof(path))) _FoundAdd(path);

	for (n = 1; n <= COVER_SUFFIX_MAX; n++) {
		snprintf(want, sizeof(want), "%s-%d.png", base, n);
		if (_ResolveKind(want, DK_ROOT, path, sizeof(path))) _FoundAdd(path);
	}

	snprintf(s_foundRom, sizeof(s_foundRom), "%s", romPath);
	s_foundGen = s_indexGen;
	if (s_variant >= s_foundCount)
		s_variant = 0;
}

static void _RebuildFoundIfNeeded(const char *romPath)
{
	if (strcmp(romPath, s_foundRom) != 0 || s_foundGen != s_indexGen)
		_RebuildFound(romPath);
}

/* Reset the variant to 0 whenever the selected ROM changes. */
static void _TrackRom(const char *romPath)
{
	if (strcmp(romPath, s_curRom) != 0) {
		snprintf(s_curRom, sizeof(s_curRom), "%s", romPath);
		s_variant = 0;
	}
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

/* Decode coverFile into the cache (always returns an entry; a decode
   failure yields a NEGATIVE entry, usedW==0, so we show "No Covers"
   instead of a blank panel and never re-probe the file). */
static CoverEntT *_CacheDecode(const char *coverFile)
{
	CoverEntT *e;
	Int32 w = 0, h = 0;

	if (!_CacheAlloc())
		return 0;
	e = _CachePickSlot();
	snprintf(e->key, sizeof(e->key), "%s", coverFile);
	e->valid = TRUE;
	e->lru   = ++s_lruClock;
	if (_DecodeFileInto(coverFile, e->rgba, &w, &h)) {
		e->usedW = w;
		e->usedH = h;
	} else {
		e->usedW = 0;
		e->usedH = 0;
	}
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
	s_foundCount = 0;
	s_foundRom[0] = '\0';
	s_foundGen = 0xFFFFFFFFu;
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
	CoverEntT *e;

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	_TrackRom(romPath);
	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);
	_RebuildFoundIfNeeded(romPath);

	if (s_foundCount == 0) { s_state = COVER_NOIMG; return; }
	if (s_variant >= s_foundCount) s_variant = 0;

	e = _CacheFind(s_found[s_variant]);
	if (!e)
		e = _CacheDecode(s_found[s_variant]);
	_ApplyEntry(e);
}

/* Every-frame display: in-memory only, never touches the disk. */
void CoverShowCached(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];
	CoverEntT *e;

	if (!s_bEnabled || !s_bTexInited) { s_state = COVER_PENDING; return; }
	if (!romPath || !romPath[0])      { s_state = COVER_PENDING; return; }

	_TrackRom(romPath);
	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));

	if (!_IndexBuiltFor(dir)) { s_state = COVER_PENDING; return; }

	_RebuildFoundIfNeeded(romPath);
	if (s_foundCount == 0) { s_state = COVER_NOIMG; return; }
	if (s_variant >= s_foundCount) s_variant = 0;

	e = _CacheFind(s_found[s_variant]);
	if (e) _ApplyEntry(e);
	else   s_state = COVER_PENDING;   /* listed but not decoded yet */
}

/* Warm a neighbour's first (default) cover. TRUE only if it decoded. */
Bool CoverPrefetch(const char *romPath)
{
	char dir[COVER_KEY_MAX];
	char base[512];

	if (!s_bEnabled || !s_bTexInited || !romPath || !romPath[0])
		return FALSE;

	_SplitRomPath(romPath, dir, sizeof(dir), base, sizeof(base));
	_EnsureIndex(dir);

	{
		char want[512 + 16];
		char path[COVER_KEY_MAX];
		Bool found;
		snprintf(want, sizeof(want), "%s.png", base);
		found = _ResolveKind(want, DK_ROOT,  path, sizeof(path))
		     || _ResolveKind(want, DK_BOX,   path, sizeof(path))
		     || _ResolveKind(want, DK_TITLE, path, sizeof(path))
		     || _ResolveKind(want, DK_SNAP,  path, sizeof(path));
		if (!found)
			return FALSE;
		if (_CacheFind(path))
			return FALSE;
		return _CacheDecode(path) ? TRUE : FALSE;
	}
}

/* Square: show the next artwork that exists for the current ROM. */
void CoverCycleVariant(void)
{
	CoverEntT *e;

	if (!s_bEnabled || !s_bTexInited || !s_curRom[0])
		return;

	_RebuildFoundIfNeeded(s_curRom);
	if (s_foundCount <= 1)
		return;   /* zero or one image: nothing to cycle */

	s_variant = (s_variant + 1) % s_foundCount;

	e = _CacheFind(s_found[s_variant]);
	if (!e)
		e = _CacheDecode(s_found[s_variant]);
	_ApplyEntry(e);
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

	GSK_InvalidateTextureCache();

	PolyTexture(&s_Tex);
	PolyUV(0, 0, s_dispW, s_dispH);
	PolyBlend(FALSE);
	PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	PolyRect(dx, dy, dw, dh);

	PolyTexture(NULL);
}
