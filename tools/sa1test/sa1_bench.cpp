/* Synthetic scheduler benchmark: no commercial ROM required. Host C only. */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "snsa1.h"
#include "sntiming.h"

int main(int argc, char **argv)
{
    const unsigned frames = argc > 1 ? (unsigned)std::strtoul(argv[1], NULL, 10) : 200u;
    if (!frames || frames > 5000) return 2;
    std::vector<Uint8> rom(0x200000, 0xEA), bwram(0x8000, 0);
    // LDA $00; BEQ back to LDA. Synthetic, legal polling loop.
    rom[0] = 0xA5; rom[1] = 0; rom[2] = 0xF0; rom[3] = 0xFC;
    SNSA1 sa1;
    sa1.SetMemory(rom.data(), (Uint32)rom.size(), bwram.data(), (Uint32)bwram.size());
    sa1.WriteRegister(0x2204, 0x80);
    sa1.WriteRegister(0x2200, 0);
    sa1.GetCpu()->Regs.rP |= SNCPU_FLAG_M | SNCPU_FLAG_Z;
    SNCpuT host;
    SNCPUNew(&host);
    SNCPUSA1BusSetHost(&host);
    auto start = std::chrono::steady_clock::now();
    for (unsigned f=0; f<frames; ++f)
        for (unsigned line=0; line<262; ++line)
        {
            SNCPUSA1BusBeginLine();
            SNCPUResetCounter(sa1.GetCpu(), SNCPU_COUNTER_LINE);
            Uint32 prev = 0;
            for (Uint32 clock=56; clock<=1344; clock+=56)
            {
                host.Counter[SNCPU_COUNTER_LINE] = (Int32)clock;
                SNCPUSA1BusRecord(&host, 0x7E1000, SNCPU_CYCLE_SLOW, 1);
                SNCPUSA1BusRecord(&host, 0xC0816F, SNCPU_CYCLE_FAST, 2);
                SNCPUSA1BusRecord(&host, 0x400000, SNCPU_CYCLE_SLOW, 1);
                SNCPUSA1BusFinalize(clock);
                sa1.StepMasterCycles((Int32)(clock-prev));
                prev=clock;
            }
            SNCPUSA1BusFinalize(SNES_CYCLESPERLINE);
            sa1.StepMasterCycles((Int32)(SNES_CYCLESPERLINE-prev));
        }
    auto end = std::chrono::steady_clock::now();
    const auto *s = sa1.GetState();
    std::printf("host synthetic ms=%.3f frames=%u master=%llu sched=%llu exec=%llu slices=%u idle=%llu drops=%u timer=%u/%u PC=%06X\n",
        std::chrono::duration<double, std::milli>(end-start).count(),frames,
        (unsigned long long)s->MasterCycles,(unsigned long long)s->ScheduledCycles,
        (unsigned long long)s->ExecutedCycles,(unsigned)s->ExecutionSlices,
        (unsigned long long)sa1.GetIdleSleepSlices(),(unsigned)SNCPUSA1BusGetDroppedEvents(),
        (unsigned)s->HCounter,(unsigned)s->VCounter,(unsigned)sa1.GetCpu()->Regs.rPC);
    SNCPUSA1BusSetHost(NULL);
    SNCPUDelete(&host);
    return 0;
}
