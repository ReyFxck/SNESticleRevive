#include <stdio.h>
#include <stdarg.h>
#include "boot_status.h"

#if DEBUG_BOOT_SCREEN

extern "C" {
#include <debug.h>
}

static int _BootStatus_Inited = 0;
static int _BootStatus_LineCount = 0;

static void _BootStatus_EnsureInit(void)
{
    if (!_BootStatus_Inited) {
        init_scr();
        scr_clear();
        _BootStatus_Inited = 1;
        _BootStatus_LineCount = 0;
    }
}

extern "C" void BootStatusLog(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    _BootStatus_EnsureInit();

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    (void)buf;

    // scr_printf(buf);
    // scr_printf("%c", 10);
    _BootStatus_LineCount++;

    if (_BootStatus_LineCount >= 25) {
        scr_clear();
        // scr_printf("[continued...]");
        // scr_printf("%c", 10);
        // scr_printf(buf);
        // scr_printf("%c", 10);
        _BootStatus_LineCount = 2;
    }
}

extern "C" void BootProbeReclaim(const char *label)
{
    init_scr();
    // scr_printf("[probe] ");
    // scr_printf(label ? label : "(null)");
    // scr_printf("%c", 10);
    (void)label;
}

#else

extern "C" void BootStatusLog(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // printf("[boot] %s", buf);
    (void)buf;
    printf("%c", 10);
}

extern "C" void BootProbeReclaim(const char *label)
{
    (void)label;
}

#endif
