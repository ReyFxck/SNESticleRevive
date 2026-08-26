
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#define NEWLIB_PORT_AWARE
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE
#include <ps2_filesystem_driver.h>

/* Memory-card stack is now loaded explicitly via the bin2c-embedded
   sio2man / mcman / mcserv IRXs in embedded_irx.cpp instead of going
   through ps2_drivers' init_memcard_driver(true).  See
   MemCardLoadEmbeddedIrx() for the rationale. */
#include "embedded_irx.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libcdvd.h>
#include <ps2sdkapi.h>

#include "types.h"
#include "console.h"
#include "mainloop.h"

extern "C" {
#include "excepHandler.h"
#include "cd.h"
#include "hw.h"
};

/* Optional storage is brought up on demand after the menu is visible.
   embedded_irx.cpp still owns the pinned USB BDM and streaming CDFS stacks,
   but main() must not synchronously start either one before GS init: a module
   _start() that waits forever on one hardware revision would otherwise leave
   an OPL-launched ELF on the loader's last black/white background. */

/* DLog: writes to EE SIO TX FIFO (defined in modules/sjpcm/sjpcm_rpc.c).
   Plain printf on the EE never reaches PCSX2/NetherSX2's emulator log
   in this build, so the only way to surface boot-phase diagnostics is
   via the EE SIO channel.  See sjpcm_rpc.c for the rationale. */
extern "C" void DLog(const char *fmt, ...);

/* The fileXio path ops table that libcglue routes fopen / opendir /
   stat / mkdir to.  It is populated by __fileXioOpsInitializeImpl()
   (which uses weak symbol probes to discover which newlib functions
   are linked in) and the pointer is swapped into _libcglue_fdman_path_ops
   by _ps2sdk_fileXio_init().

   We declare and call both directly here because in this build the
   pre-built libfileXio.a's automatic __attribute__((constructor)) for
   __fileXioOpsInitializeImpl appears NOT to fire (or to fire with the
   weak _open/_stat refs still resolving to 0), leaving the struct
   entirely NULL.  When _ps2sdk_fileXio_init() then swaps the libcglue
   pointer to point at it, every _libcglue_fdman_path_ops->open ==
   NULL check in glue.c::_open / _stat / etc. trips and returns
   ENOSYS=88.  Calling __fileXioOpsInitializeImpl() manually after the
   IRX modules are up forces population from the now-fully-linked
   newlib symbol table. */
extern "C" _libcglue_fdman_path_ops_t __fileXio_fdman_path_ops;
extern "C" void __fileXioOpsInitializeImpl(void);
extern "C" void _ps2sdk_fileXio_init(void);



/* Some launchers (and PCSX2's direct ELF loader in particular) are allowed
   to enter main() without a useful argv[0].  The old code kept a NULL
   pointer in that case and immediately passed it to strcpy(), crashing
   before video or the boot log existed.  Keep owned, always-valid buffers
   instead of retaining the launcher's pointer. */
static char  _Main_BootPath[256] = "host:";
static char *_Main_pBootPath     = _Main_BootPath;
static char  _Main_BootDir[256]  = "host:";


char *MainGetBootDir()
{
	return _Main_BootDir;
}

char *MainGetBootPath()
{
	return _Main_pBootPath;
}

void MainSetBootDir(const char *pPath)
{
	size_t len;
	size_t keep = 0;
	size_t i;

	if (!pPath || !pPath[0])
		pPath = "host:";

	strncpy(_Main_BootDir, pPath, sizeof(_Main_BootDir) - 1);
	_Main_BootDir[sizeof(_Main_BootDir) - 1] = 0;
	len = strlen(_Main_BootDir);

	/* Keep everything through the final device/path separator.  A launcher
	   that only supplies "SNESticle.elf" gives us no directory information;
	   host: is the only safe fallback and is also what ps2link/PCSX2 expose
	   for a directly loaded ELF. */
	for (i = len; i > 0; i--)
	{
		char c = _Main_BootDir[i - 1];
		if (c == '/' || c == '\\' || c == ':')
		{
			keep = i;
			break;
		}
	}

	if (keep == 0)
	{
		strcpy(_Main_BootDir, "host:");
	}
	else
	{
		_Main_BootDir[keep] = 0;
	}
}

/* Your program's main entry point */
int main(int argc, char **argv) 
{
    int iArg;

	if (argc > 0 && argv && argv[0] && argv[0][0])
	{
		strncpy(_Main_BootPath, argv[0], sizeof(_Main_BootPath) - 1);
		_Main_BootPath[sizeof(_Main_BootPath) - 1] = 0;
	}

	MainSetBootDir(_Main_pBootPath);

	DLog("[boot] main: argc=%d path='%s' dir='%s'",
	     argc, _Main_pBootPath, _Main_BootDir);

	SifInitRpc(0);
	DLog("[boot] SifInitRpc done");

	/* Reset the IOP so the BIOS-resident modules (sceCdvdfsv, sceSio2man,
	   sceMcMan, sceMcServ, etc.) are unloaded before ps2_drivers tries
	   to install its own modern copies.  Without this reset the two
	   sets of IRX modules end up half-overlapping in RPC tables and the
	   tail of init_ps2_filesystem_driver() - specifically the mcman /
	   poweroff hand-off - hangs silently after dev9 init prints its
	   banner.  This is exactly the sequence picodrive's plat.c follows
	   in platform/ps2/plat.c::reset_IOP.

	   This is the one and only reset for every boot device.  The old
	   memory-card path reset the IOP a second time after loading all drivers,
	   erased their RPC servers, and retained stale EE "loaded" flags.  That
	   sequence was especially fragile on Deckard-based slim consoles. */
	DLog("[boot] SifIopReset: enter");
	while (!SifIopReset("", 0)) {}
	while (!SifIopSync()) {}
	SifInitRpc(0);
	SifLoadFileInit();
	FlushCache(0);
	EmbeddedIrxResetRuntimeState();
	DLog("[boot] SifIopReset done");

	/* Patch the rom0:LOADFILE service so SifExecModuleBuffer (used by
	   our embedded-IRX loader in src/platform/ps2/system/embedded_irx.cpp)
	   and ps2_drivers' init_ps2_filesystem_driver actually work. The
	   stock retail BIOS LOADFILE module is missing LoadModuleBuffer
	   support, so without these patches the EE call "succeeds" but the
	   IRX never finishes registering its RPC server. The prefix check
	   patch additionally lets us load modules from any device, which
	   is useful for cdrom: / host: fallbacks.

	   These patches must run after SifIopReset because the reset
	   reloads rom0:LOADFILE in its pristine, unpatched state. */
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();
	DLog("[boot] sbv patches applied");

	/* Bring up the modern PS2DEV filesystem base: iomanX, fileXio,
	   poweroff and mcman/mcserv.  Once this is done, newlib
	   stdio (fopen/fread/fwrite/fclose/mkdir/opendir) routes through
	   iomanX, so paths like "mc0:/SNESticle/<rom>.srm",
	   "cdfs:/ROMS/foo.sfc", "mass:/bar/baz" all work as standard POSIX
	   file paths from the EE side after their optional device driver has
	   been loaded.

	   The legacy rom0:FILEIO RPC was the original I/O path in this
	   codebase (fioOpen / fioDopen / fioRead).  It silently dropped a
	   non-trivial fraction of memcard reads on emulators (the SRAM
	   load bug that motivated this refactor), so we switch the whole
	   EE side over to fileXio.  The fio* API stays available for callers
	   that still need it - fileXio's iomanX-based device list is a
	   superset of the legacy fileio one.

	   We deliberately do NOT call the all-in-one
	   init_ps2_filesystem_driver() that ps2_drivers ships.  That
	   helper also calls init_dev9_driver(), init_hdd_driver(),
	   mount_current_hdd_partition() and waitUntilDeviceIsReady(cwd) at
	   the end, all of which we don't need (SNESticle never touches the
	   PS2 HDD or DEV9 hardware) and at least one of which hangs
	   silently after dev9 prints "unknown dev9 hardware" on emulators
	   and most retail PS2s.  Inlining the bring-up here lets us bracket
	   every step with a DLog so the next hang, if any, can be pinpointed
	   directly from the EE_SIO emulator log. */
	DLog("[boot] init_poweroff_driver: enter");
	init_poweroff_driver();
	DLog("[boot] init_poweroff_driver: done");

	DLog("[boot] init_fileXio_driver: enter");
	init_fileXio_driver();
	DLog("[boot] init_fileXio_driver: done");

	/* Route newlib stdio (fopen / opendir / stat / mkdir / ...) through
	   fileXio -> iomanX instead of the legacy fio backend.  Must come
	   after init_fileXio_driver() (which loads fileXio.irx + iomanX.irx
	   on the IOP) and before any fopen / opendir on a cdfs: / mc0: /
	   mass: / host: path.

	   Order matters:
	   1) __fileXioOpsInitializeImpl() populates __fileXio_fdman_path_ops
	      with the fileXio*Helper trampolines (open, stat, dread, ...).
	      This MUST run from the EE main, not from libfileXio's static
	      constructor - the constructor fires before our newlib glue is
	      fully linked and the weak _open / _stat refs resolve to 0,
	      leaving the struct NULL.
	   2) _ps2sdk_fileXio_init() swaps _libcglue_fdman_path_ops to point
	      at __fileXio_fdman_path_ops so newlib stdio uses the fileXio
	      backend. */
	DLog("[fxglue] before init: open=%p stat=%p",
	     (void *)__fileXio_fdman_path_ops.open,
	     (void *)__fileXio_fdman_path_ops.stat);
	__fileXioOpsInitializeImpl();
	DLog("[fxglue] after init:  open=%p stat=%p mkdir=%p",
	     (void *)__fileXio_fdman_path_ops.open,
	     (void *)__fileXio_fdman_path_ops.stat,
	     (void *)__fileXio_fdman_path_ops.mkdir);
	DLog("[fxglue] before swap: _libcglue_fdman_path_ops=%p",
	     (void *)_libcglue_fdman_path_ops);
	_ps2sdk_fileXio_init();
	DLog("[fxglue] after swap:  _libcglue_fdman_path_ops=%p (==fx %p)",
	     (void *)_libcglue_fdman_path_ops,
	     (void *)&__fileXio_fdman_path_ops);

	/* Memory card, USB and CDFS deliberately do not start here.  The memory
	   card stack starts inside MainLoopInit after the boot screen exists;
	   USB and CDFS are loaded by the browser on the first explicit
	   mass:/cdfs: access.  State settings
	   and BGM discovery also check the loaded-state getters before touching
	   those devices.  This keeps synchronous SifExecModuleBuffer and CDVD RPC
	   waits out of the invisible pre-video boot path used by OPL Apps and ISO
	   launches on real Fat/Slim consoles. */

	/* Probes de boot DESATIVADOS (#if 0): faziam opendir/stat/fileXioDopen
	   em cdfs: durante a inicializacao (codigo de debug -- os DLog ja'
	   estavam comentados).  Num PS2 real bootando por disco isso tocava o
	   drive cedo demais (antes de pronto) e podia travar/atrasar o boot.
	   O browser le o cdfs: so' quando o usuario entra nele (drive pronto). */
#if 0
	/* Runtime FS probe: log opendir/stat for every top-level mount so
	   the next boot tells us exactly where the browser breaks. The
	   browser uses printf which never reaches the SIO log; this dup
	   via DLog does. */
	// DLog("[probe] fs probe begin");
	{
		const char *paths[] = { "cdfs:/", "cdfs:", "mc0:/", "mc0:", "mass:/", "host:/" };
		int i;
		for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
			DIR *d; struct stat st; int rc;
			errno = 0; rc = stat(paths[i], &st);
			// DLog("[probe] stat('%s') -> %d (errno=%d, mode=0%o)",
			//      paths[i], rc, errno, rc == 0 ? (unsigned)st.st_mode : 0);
			(void)rc;
			errno = 0; d = opendir(paths[i]);
			// DLog("[probe] opendir('%s') -> %p (errno=%d)", paths[i], (void *)d, errno);
			if (d) {
				struct dirent *de; int n = 0;
				while ((de = readdir(d)) != NULL && n < 8) {
					/* DLog("[probe]   readdir[%d] = '%s'", n, de->d_name); */ n++;
				}
				// DLog("[probe]   total entries listed = %d", n);
				closedir(d);
			}
		}
	}
	// DLog("[probe] fs probe end");

	/* Direct fileXio probe: bypass newlib entirely. If these work
	   where opendir() above does not, the EE newlib<->iomanX glue
	   is the issue and the browser should call fileXio* directly. */
	// DLog("[fxprobe] direct fileXio probe begin");
	{
		const char *paths[] = { "cdfs:/", "cdfs:", "mc0:/", "mass:/", "host:/" };
		int i;
		iox_dirent_t de;
		iox_stat_t st;
		for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
			int sr = fileXioGetStat(paths[i], &st);
			// DLog("[fxprobe] fileXioGetStat('%s') -> %d (mode=0x%x)",
			//      paths[i], sr, sr == 0 ? (unsigned)st.mode : 0);
			(void)sr;
			int d = fileXioDopen(paths[i]);
			// DLog("[fxprobe] fileXioDopen('%s') -> %d", paths[i], d);
			if (d >= 0) {
				int n = 0;
				while (fileXioDread(d, &de) > 0 && n < 8) {
					// DLog("[fxprobe]   dread[%d] = '%s' (mode=0x%x)", n, de.name, (unsigned)de.stat.mode);
					n++;
				}
				// DLog("[fxprobe]   total entries listed = %d", n);
				fileXioDclose(d);
			}
		}
	}
	// DLog("[fxprobe] direct fileXio probe end");
#endif

	/* cdvdInit(CDVD_INIT_NOWAIT) used to live here.  It is intentionally
	   gone now: it binds to RPC 0x80000592 (CDVD_INIT_BIND_RPC, see
	   src/platform/ps2/cdvd/cd.c) which was served by the iaddis-era
	   custom CDVD.IRX.  That IRX is no longer loaded - the modern
	   the embedded streaming cdfs.irx instead exposes
	   cdfs: through iomanX and registers a different RPC number.  With
	   no server bound to 0x80000592, cdvdInit's SifBindRpc spin loop
	   never completes and the EE hangs silently (black screen, no
	   further IOP output) before MainLoopInit even gets a chance to
	   run.  All disc I/O now goes through fopen("cdfs:/...") via the
	   refactor in src/platform/ps2/system/mainloop_load.cpp etc. */

    for (iArg=0; iArg < argc; iArg++)
    {
        DLog("[boot] argv[%d] = %s", iArg,
             (argv && argv[iArg]) ? argv[iArg] : "(null)");
    }

	DmaReset();
	DLog("[boot] DmaReset done");

    install_VRstart_handler();
    DLog("[boot] install_VRstart_handler done");

	ConInit();
	DLog("[boot] ConInit done -> MainLoopInit");

	if (MainLoopInit())
	{
		DLog("[boot] MainLoopInit OK -> entering MainLoopProcess loop");
		while (MainLoopProcess())
		{
		}

		MainLoopShutdown();
	}
	else
	{
		DLog("[boot] MainLoopInit FAILED");
	}

	ConShutdown();

	return 0;
}
