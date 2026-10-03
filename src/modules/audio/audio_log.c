/* Shared EE SIO diagnostic sink, independent of the audio backend. */
#include <stdarg.h>
#include <stdio.h>
#include <sio.h>

/* Diagnostic printf helper for this project.

   Plain printf() on the EE never seems to reach NetherSX2 / PCSX2's
   emulator log file in this codebase (some piece of the libc->SIF->IOP
   stdout wiring is missing). What *does* reach the emulator log is the
   EE SIO TX FIFO at 0x1000f180: PCSX2 captures bytes written to it and
   emits them on the EE_SIO log channel, which lands in the same console
   /log file as the IOP "loadmodule:" / "audsrv_adpcm_init()" lines.

   We therefore route diagnostics through sio_putsn() (writes to EE SIO
   TX FIFO byte-by-byte) and also mirror them to ScrPrintf so the user
   sees them on the on-screen splash log. sio_init() is called lazily
   on first use with the standard 38400 8N1 setting.

   Tag: each line is prefixed with "[snes-aud] " so the user can grep
   the log file. */
static int   _sio_inited = 0;
/* Universal SNES records carry several named counters. Keep one complete
   schema line intact instead of silently cutting it at the old 256 bytes. */
static char  _dlog_buf[512];

/* Non-static so other translation units can extern it for one-off
   audio-path tracing. Mirror of the local prototype:
       extern void DLog(const char *fmt, ...);
   See mainloop_process.cpp / sjpcmbuffer.cpp where this is called
   via that extern declaration. */
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

    sio_putsn(_dlog_buf);
}
