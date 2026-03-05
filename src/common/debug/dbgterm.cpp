#include "dbgterm.h"

#include <debug.h>
#include <kernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef DBGTERM_MAX_LINES
#define DBGTERM_MAX_LINES 64
#endif

#ifndef DBGTERM_LINE_LEN
#define DBGTERM_LINE_LEN 120
#endif

static char g_lines[DBGTERM_MAX_LINES][DBGTERM_LINE_LEN];
static int  g_head = 0;
static int  g_count = 0;
static int  g_inited = 0;
static char g_last_phase[DBGTERM_LINE_LEN];

static void dbg_store(const char *msg)
{
    if (!msg) msg = "(null)";
    snprintf(g_lines[g_head], DBGTERM_LINE_LEN, "%s", msg);
    g_head = (g_head + 1) % DBGTERM_MAX_LINES;
    if (g_count < DBGTERM_MAX_LINES) g_count++;
}

static void dbg_vlog(const char *fmt, va_list ap)
{
    char buf[DBGTERM_LINE_LEN];
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "(null)", ap);
    dbg_store(buf);
}

static void dbg_print_all(const char *title)
{
    init_scr();
    scr_printf("=== %s ===\n", title ? title : "DBG");
    scr_printf("LAST: %s\n", g_last_phase[0] ? g_last_phase : "(none)");

    int start = g_head - g_count;
    while (start < 0) start += DBGTERM_MAX_LINES;

    for (int i = 0; i < g_count; i++)
    {
        int idx = (start + i) % DBGTERM_MAX_LINES;
        if (g_lines[idx][0])
            scr_printf("%02d %s\n", i, g_lines[idx]);
    }
}

void DbgInit(void)
{
    if (g_inited) return;

    memset(g_lines, 0, sizeof(g_lines));
    memset(g_last_phase, 0, sizeof(g_last_phase));
    g_head = 0;
    g_count = 0;
    g_inited = 1;

    init_scr();
    scr_printf("[dbgterm] init\n");
    dbg_store("dbgterm: init");
}

void DbgSetPhase(const char *func, const char *msg)
{
    if (!g_inited) DbgInit();

    snprintf(g_last_phase, sizeof(g_last_phase),
             "%s: %s",
             func ? func : "(func)",
             msg ? msg : "(msg)");

    dbg_store(g_last_phase);
}

void DbgLog(const char *fmt, ...)
{
    if (!g_inited) DbgInit();

    va_list ap;
    va_start(ap, fmt);
    dbg_vlog(fmt, ap);
    va_end(ap);
}

void DbgOk(const char *func, const char *msg)
{
    DbgLog("%s: OK %s", func ? func : "(func)", msg ? msg : "(msg)");
}

void DbgErr(const char *func, const char *msg)
{
    DbgLog("%s: ERROR %s", func ? func : "(func)", msg ? msg : "(msg)");
}

void DbgDumpStack(void)
{
    unsigned int trace[16];
    memset(trace, 0, sizeof(trace));

    ps2GetStackTrace(trace, 16);

    DbgLog("stack trace:");
    for (int i = 0; i < 16; i++)
    {
        if (!trace[i]) break;
        DbgLog("  #%02d 0x%08x", i, trace[i]);
    }
}

void DbgShow(void)
{
    if (!g_inited) DbgInit();
    dbg_print_all("DBG SHOW");
    for (;;) { SleepThread(); }
}

void DbgPanic(const char *func, const char *msg)
{
    DbgErr(func, msg);
    DbgDumpStack();
    dbg_print_all("PANIC");
    for (;;) { SleepThread(); }
}
