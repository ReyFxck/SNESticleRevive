/* Shared EE SIO diagnostic sink, independent of the audio backend. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <sio.h>

#include "audio_log.h"
#include "embedded_irx.h"
#include "mainloop_bgm.h"

#ifndef SNDBG_LOG
#define SNDBG_LOG 0
#endif

/* Diagnostic printf helper for this project.

   Plain printf() on the EE never seems to reach NetherSX2 / PCSX2's
   emulator log file in this codebase. What does reach it is the EE SIO TX
   FIFO, so diagnostics continue to use sio_putsn().

   On real PS2 hardware diagnostic builds may also mirror complete DLog lines
   to a RAM staging buffer. DLog() itself never touches the filesystem.
   MainLoopProcess() calls DLogCapturePump() after frame/profiler accounting;
   that routine writes a large block to the single emulog.txt beside the ELF.
 */
static int  _sio_inited = 0;
/* Universal SNES records carry several named counters. Keep one complete
   schema line intact instead of silently cutting it at the old 256 bytes. */
static char _dlog_buf[512];

#if SNDBG_LOG
/* Flush once 64 KiB has accumulated. Keep a much larger staging area so a
   deep-capture frame can finish producing diagnostics before MainLoopProcess
   reaches the safe pump point. This memory exists only in diagnostic builds. */
#define DLOG_FLUSH_THRESHOLD   (64u * 1024u)
#define DLOG_STAGING_BYTES     (512u * 1024u)

static unsigned char _capture_buf[DLOG_STAGING_BYTES];
static unsigned int  _capture_size = 0;
static unsigned int  _capture_dropped = 0;
static unsigned int  _capture_flushes = 0;
static unsigned int  _capture_total = 0;
static int           _capture_enabled = 0;
static int           _capture_pending = 0;
static int           _capture_in_io = 0;
static int           _capture_io_error = 0;
static FILE         *_capture_fp = NULL;
static char          _capture_boot_path[256] = "";
static char          _capture_boot_dir[256] = "";
static char          _capture_log_path[320] = "host:emulog.txt";

static int _DLogStartsWith(const char *p, const char *prefix)
{
    size_t n;
    if (!p || !prefix) return 0;
    n = strlen(prefix);
    return strncasecmp(p, prefix, n) == 0;
}

static int _DLogForbiddenOrigin(const char *p)
{
    if (!p || !p[0]) return 1;
    return _DLogStartsWith(p, "mc0:") ||
           _DLogStartsWith(p, "mc1:") ||
           _DLogStartsWith(p, "mc:")  ||
           _DLogStartsWith(p, "cdfs:") ||
           _DLogStartsWith(p, "cdrom") ||
           _DLogStartsWith(p, "rom0:") ||
           _DLogStartsWith(p, "rom1:") ||
           _DLogStartsWith(p, "rom2:");
}

static int _DLogBuildLogPath(void)
{
    int n;
    size_t len;

    if (_DLogForbiddenOrigin(_capture_boot_dir))
        return 0;

    /* Require a device-style path. A bare filename gives no trustworthy
       origin and must never make us guess another storage device. */
    if (!strchr(_capture_boot_dir, ':'))
        return 0;

    len = strlen(_capture_boot_dir);
    if (!len)
        return 0;

    n = snprintf(_capture_log_path, sizeof(_capture_log_path),
                 "%s%semulog.txt",
                 _capture_boot_dir,
                 (_capture_boot_dir[len - 1] == ':' ||
                  _capture_boot_dir[len - 1] == '/' ||
                  _capture_boot_dir[len - 1] == '\\') ? "" : "/");
    return n > 0 && n < (int)sizeof(_capture_log_path);
}

/* Bring up only the filesystem class named by argv[0]. This never chooses a
   different output path. For mass: both configured BDM providers may be made
   resident because USB and MX4SIO share the massN: namespace; the exact
   argv[0] path is verified below before emulog.txt is created. */
static void _DLogPrepareOriginDevice(void)
{
    if (_DLogStartsWith(_capture_boot_dir, "mass"))
    {
        if (MassStorageIsEnabled() && !UsbBdmIsLoaded())
            (void)UsbBdmLoadEmbeddedIrx();
        if (Mx4sioIsEnabled() && !Mx4sioIsLoaded())
            (void)Mx4sioLoadIfEnabled();
        return;
    }

    if (_DLogStartsWith(_capture_boot_dir, "mmce"))
    {
        if (MmceSupportIsEnabled() && !MmceNeedsRestart() && !MmceIsLoaded())
            (void)MmceLoadEmbeddedIrx();
        return;
    }

    if (_DLogStartsWith(_capture_boot_dir, "hdd0:"))
    {
        if (HddSupportIsEnabled())
            (void)HddLoadEmbeddedIrx();
        return;
    }
}

/* Refuse to write to a mass/mmce/pfs/etc namespace unless the exact ELF path
   that launched us can still be resolved there after the app's IOP reset.
   This guards against silently writing to a different massN: device. HostFS
   is the only exception because Android direct-ELF launchers give argv[0] as
   a host:content:// URI that cannot be stat()ed through the guest namespace. */
static int _DLogVerifyExactOrigin(void)
{
    struct stat st;

    if (_DLogStartsWith(_capture_boot_dir, "host:"))
        return 1;

    if (!_capture_boot_path[0] || !strchr(_capture_boot_path, ':'))
        return 0;

    return stat(_capture_boot_path, &st) == 0 && !S_ISDIR(st.st_mode);
}

static void _DLogCaptureAppend(const char *data, unsigned int bytes)
{
    unsigned int room;
    unsigned int copy;

    if (!_capture_enabled || _capture_in_io || !data || !bytes)
        return;

    room = DLOG_STAGING_BYTES - _capture_size;
    copy = bytes < room ? bytes : room;
    if (copy)
    {
        memcpy(_capture_buf + _capture_size, data, copy);
        _capture_size += copy;
        _capture_total += copy;
    }
    if (copy < bytes)
    {
        _capture_dropped += bytes - copy;
    }

    if (_capture_size >= DLOG_FLUSH_THRESHOLD)
        _capture_pending = 1;
}

/* The only streaming write path. It is never called from DLog(). */
static int _DLogFlushBuffered(int force)
{
    unsigned int bytes;
    size_t wrote;

    if (!_capture_fp || _capture_in_io || !_capture_size)
        return 0;
    if (!force && !_capture_pending)
        return 0;

    bytes = _capture_size;
    _capture_in_io = 1;
    BgmIOBegin();

    wrote = fwrite(_capture_buf, 1, bytes, _capture_fp);

    /* Consume only the bytes that the filesystem accepted. If a device
       disappears halfway through a block, StopAndSave can retry the tail
       without duplicating the prefix that already reached disk. */
    if (wrote > 0)
    {
        unsigned int remain = bytes - (unsigned int)wrote;
        if (remain)
            memmove(_capture_buf, _capture_buf + wrote, remain);
        _capture_size = remain;
    }

    if (wrote == bytes)
    {
        _capture_flushes++;
        _capture_pending = _capture_size >= DLOG_FLUSH_THRESHOLD ? 1 : 0;
        /* setvbuf(_IONBF) makes fwrite the real transaction. fflush is still
           used as an explicit commit point, but a flush error must not cause
           us to replay an already-written block and duplicate log records. */
        if (fflush(_capture_fp) != 0)
            _capture_io_error = errno ? errno : EIO;
    }
    else
    {
        _capture_io_error = errno ? errno : EIO;
        _capture_pending = _capture_size ? 1 : 0;
    }

    BgmIOEnd();
    _capture_in_io = 0;
    return wrote == bytes && !_capture_io_error ? 0 : -1;
}
#endif /* SNDBG_LOG */

void DLogCaptureSetBootPath(const char *bootPath, const char *bootDir)
{
#if SNDBG_LOG
    _capture_boot_path[0] = 0;
    _capture_boot_dir[0] = 0;
    if (bootPath && bootPath[0])
    {
        strncpy(_capture_boot_path, bootPath, sizeof(_capture_boot_path) - 1);
        _capture_boot_path[sizeof(_capture_boot_path) - 1] = '\0';
    }
    if (bootDir && bootDir[0])
    {
        strncpy(_capture_boot_dir, bootDir, sizeof(_capture_boot_dir) - 1);
        _capture_boot_dir[sizeof(_capture_boot_dir) - 1] = '\0';
    }
    (void)_DLogBuildLogPath();
#else
    (void)bootPath;
    (void)bootDir;
#endif
}

void DLog(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!_sio_inited)
    {
        sio_init(38400, 0, 0, 0, 0);
        _sio_inited = 1;
    }

    va_start(ap, fmt);
    /* Leave one byte for a forced newline and one for the terminator. */
    n = vsnprintf(_dlog_buf, sizeof(_dlog_buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(_dlog_buf) - 1) n = sizeof(_dlog_buf) - 2;

    /* Make sure the line ends with \n so the emulator log flushes it. */
    if (n == 0 || _dlog_buf[n - 1] != '\n')
    {
        _dlog_buf[n++] = '\n';
        _dlog_buf[n]   = '\0';
    }

#if SNDBG_LOG
    _DLogCaptureAppend(_dlog_buf, (unsigned int)n);
#endif
    sio_putsn(_dlog_buf);
}

int DLogCaptureDestinationIsAllowed(void)
{
#if SNDBG_LOG
    return _DLogBuildLogPath();
#else
    return 0;
#endif
}

int DLogCaptureGetPath(char *outPath, int outPathBytes)
{
#if SNDBG_LOG
    if (!outPath || outPathBytes <= 0)
        return -1;
    outPath[0] = '\0';
    if (!_DLogBuildLogPath())
        return -2;
    snprintf(outPath, (size_t)outPathBytes, "%s", _capture_log_path);
    return 0;
#else
    if (outPath && outPathBytes > 0) outPath[0] = '\0';
    return -3;
#endif
}

int DLogCaptureIsEnabled(void)
{
#if SNDBG_LOG
    return _capture_enabled;
#else
    return 0;
#endif
}

unsigned int DLogCaptureBytes(void)
{
#if SNDBG_LOG
    return _capture_total;
#else
    return 0;
#endif
}

unsigned int DLogCaptureDroppedBytes(void)
{
#if SNDBG_LOG
    return _capture_dropped;
#else
    return 0;
#endif
}

unsigned int DLogCaptureFlushCount(void)
{
#if SNDBG_LOG
    return _capture_flushes;
#else
    return 0;
#endif
}

int DLogCaptureStart(void)
{
#if SNDBG_LOG
    if (_capture_enabled)
        return 0;
    if (!_DLogBuildLogPath())
        return -2; /* forbidden or no trustworthy origin */

    _DLogPrepareOriginDevice();
    if (!_DLogVerifyExactOrigin())
        return -3; /* exact launching ELF is not reachable */

    /* Never create directories and never try another device. The requested
       file is exactly <ELF directory>/emulog.txt. */
    _capture_fp = fopen(_capture_log_path, "wb");
    if (!_capture_fp)
        return -4;

    /* Avoid an extra libc stdio buffer on top of our explicit large-block
       staging. Each pump's fwrite is therefore the storage transaction. */
    (void)setvbuf(_capture_fp, NULL, _IONBF, 0);

    _capture_size = 0;
    _capture_dropped = 0;
    _capture_flushes = 0;
    _capture_total = 0;
    _capture_pending = 0;
    _capture_in_io = 0;
    _capture_io_error = 0;
    _capture_enabled = 1;

    /* A tiny header is written immediately so a crash before the first 64 KiB
       still leaves evidence that file logging was enabled. */
    _capture_in_io = 1;
    BgmIOBegin();
    if (fprintf(_capture_fp,
                "[snes-file-log] streaming=1 block=%u staging=%u path=%s\n",
                (unsigned int)DLOG_FLUSH_THRESHOLD,
                (unsigned int)DLOG_STAGING_BYTES,
                _capture_log_path) < 0 ||
        fflush(_capture_fp) != 0)
    {
        BgmIOEnd();
        _capture_in_io = 0;
        fclose(_capture_fp);
        _capture_fp = NULL;
        _capture_enabled = 0;
        return -5;
    }
    BgmIOEnd();
    _capture_in_io = 0;

    DLog("[snes-file-log] capture started path=%s block=%u",
         _capture_log_path, (unsigned int)DLOG_FLUSH_THRESHOLD);

    return 0;
#else
    return -3;
#endif
}

void DLogCapturePump(void)
{
#if SNDBG_LOG
    if (!_capture_enabled || _capture_io_error || !_capture_pending)
        return;
    (void)_DLogFlushBuffered(0);
#endif
}

int DLogCaptureStopAndSave(char *outPath, int outPathBytes)
{
#if SNDBG_LOG
    int result = 0;

    if (outPath && outPathBytes > 0)
        outPath[0] = '\0';

    if (!_capture_enabled && !_capture_fp)
        return -2;

    if (_capture_enabled)
    {
        DLog("[snes-file-log] capture stopping bytes=%u dropped=%u flushes=%u",
             _capture_total, _capture_dropped, _capture_flushes);
    }

    /* Retry once even after an earlier streaming error, then close. */
    _capture_io_error = 0;
    if (_DLogFlushBuffered(1) < 0)
        result = -5;

    _capture_enabled = 0;
    _capture_pending = 0;

    if (_capture_fp)
    {
        _capture_in_io = 1;
        BgmIOBegin();
        fprintf(_capture_fp,
                "[snes-file-log] end bytes=%u dropped=%u flushes=%u result=%d\n",
                _capture_total, _capture_dropped, _capture_flushes, result);
        if (fflush(_capture_fp) != 0)
            result = -6;
        if (fclose(_capture_fp) != 0)
            result = -7;
        BgmIOEnd();
        _capture_in_io = 0;
        _capture_fp = NULL;
    }

    if (outPath && outPathBytes > 0)
        snprintf(outPath, (size_t)outPathBytes, "%s", _capture_log_path);

    DLog("[snes-file-log] capture closed path=%s bytes=%u dropped=%u flushes=%u result=%d",
         _capture_log_path, _capture_total, _capture_dropped,
         _capture_flushes, result);
    return result;
#else
    (void)outPath;
    (void)outPathBytes;
    return -3;
#endif
}
