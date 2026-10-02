#include <string.h>
#include <stddef.h>
#include "types.h"
#include "sncpu.h"
#include "sncpu_c.h"
static SNCpuT cpu;
static unsigned char memory[SNCPU_MEM_SIZE];
unsigned char *FixtureMemory(void) { return memory; }
void FixtureRun(unsigned char *state) {
 memset(&cpu,0,sizeof cpu); memcpy(&cpu.Regs,state,20);
 memcpy(&cpu.Cycles,state+20,4);memcpy(cpu.Counter,state+24,16);
 cpu.uSignal=state[49];cpu.uOpenBus=state[52];
 memcpy(&cpu.uCpuCycleCount,state+56,4);
 SNCPUSetBank(&cpu,0,SNCPU_MEM_SIZE,memory,TRUE);
 SNCPUSetMemSpeed(&cpu,0,SNCPU_MEM_SIZE,8);
 SNCPUExecute_C(&cpu);
 memcpy(state,&cpu.Regs,20);memcpy(state+20,&cpu.Cycles,4);
 memcpy(state+24,cpu.Counter,16);state[49]=cpu.uSignal;state[52]=cpu.uOpenBus;
 memcpy(state+56,&cpu.uCpuCycleCount,4);
}
