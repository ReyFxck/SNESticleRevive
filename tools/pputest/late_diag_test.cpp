#include "mainloop_late_diag.h"
#include <cstdio>
#include <cassert>
int main() {
    MainLoopLateDiagT d = {};
    d.Add(100,100,80,3,1);
    d.Add(150,100,100,10,1); // exactly threshold is not late
    d.Add(200,100,105,8,2);  // report crosses work budget
    d.Add(200,100,130,8,2);
    d.Add(200,100,90,2,1);
    d.Add(200,100,101,102,17); // impossible report/work relationship
    d.Add(200,100,90,0,(Uint32)1-(Uint32)0xffffffffu); // IRQ wrap
    assert(d.Late==5 && d.WorkOver==3 && d.WorkUnder==2);
    assert(d.ReportCross==1 && d.IRQMultiple==3 && d.IRQInvalid==1);
    assert(d.ReportTotal==133 && d.ReportMax==102);
    std::puts("Paired cadence diagnostic: PASS");
}
