#include <cstdio>
#include "mainloop_region_cadence.h"

static int failures;
static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        std::printf("FAIL %s: %d != %d\n", name, got, expected);
        failures++;
    }
}
int main()
{
    MainLoopRegionCadenceT s = {0,0,0};
    Uint32 extra = 0;
    Bool hold = FALSE;
    int extras = 0, holds = 0;

    for (int i=0;i<50;i++) {
        MainLoopRegionCadenceStep(&s, TRUE, 60, 50, &extra, &hold);
        extras += (int)extra; holds += hold ? 1 : 0;
    }
    check("60->50 extras", extras, 10);
    check("60->50 holds", holds, 0);

    MainLoopRegionCadenceReset(&s); extras = holds = 0;
    for (int i=0;i<60;i++) {
        MainLoopRegionCadenceStep(&s, TRUE, 50, 60, &extra, &hold);
        extras += (int)extra; holds += hold ? 1 : 0;
    }
    check("50->60 extras", extras, 0);
    check("50->60 holds", holds, 10);

    MainLoopRegionCadenceReset(&s);
    MainLoopRegionCadenceStep(&s, TRUE, 60, 50, &extra, &hold);
    MainLoopRegionCadenceStep(&s, FALSE, 60, 50, &extra, &hold);
    check("disabled phase", (int)s.phase, 0);
    check("disabled source", (int)s.sourceHz, 0);
    check("disabled host", (int)s.hostHz, 0);

    std::puts(failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
