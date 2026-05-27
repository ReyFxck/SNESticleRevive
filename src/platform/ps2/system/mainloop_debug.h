#pragma once

#include <stdio.h>
#include "console.h"

#ifndef MAINLOOP_TRACE_ENABLED
#define MAINLOOP_TRACE_ENABLED 1
#endif

#if MAINLOOP_TRACE_ENABLED
#define ML_TRACE(fmt, ...) \
    do { \
        printf("[ML] " fmt "\n", ##__VA_ARGS__); \
        ConPrint("[ML] " fmt "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define ML_TRACE(...) do { } while (0)
#endif
