#ifndef MAINLOOP_LATE_DIAG_H
#define MAINLOOP_LATE_DIAG_H
#include "types.h"
// Each sample pairs start-to-start interval with the preceding iteration.
// Unsigned subtraction permits one counter wrap. IRQs are observational only.
struct MainLoopLateDiagT {
    Uint32 Late, WorkOver, WorkUnder, ReportCross, IRQMultiple, IRQInvalid;
    Uint32 ReportMax;
    Uint64 ReportTotal;
    void Add(Uint32 interval, Uint32 budget, Uint32 work, Uint32 report,
             Uint32 irqDelta) {
        ReportTotal += report;
        if (report > ReportMax) ReportMax = report;
        if (interval <= budget + budget / 2) return;
        ++Late;
        if (work > budget) {
            ++WorkOver;
            if (report <= work && work - report <= budget) ++ReportCross;
        } else ++WorkUnder;
        if (irqDelta > 16) ++IRQInvalid;
        else if (irqDelta > 1) ++IRQMultiple;
    }
};
#endif
