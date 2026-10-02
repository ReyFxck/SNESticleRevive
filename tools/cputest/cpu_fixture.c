#include <string.h>
#include <stddef.h>
#include "types.h"
#include "sncpu.h"
#include "sncpu_c.h"
static SNCpuT cpu;
static unsigned char memory[SNCPU_MEM_SIZE];
static int traps;
void FixtureTraps(int enabled) { traps=enabled; }
static Uint8 SNCPU_TRAPFUNC TrapRead(SNCpuT *pCpu, Uint32 addr) {
 pCpu->Cycles-=3;
 return memory[addr] ^ pCpu->uOpenBus;
}
static void SNCPU_TRAPFUNC TrapWrite(SNCpuT *pCpu, Uint32 addr, Uint8 data) {
 pCpu->Cycles-=3;
 memory[addr]=data ^ 0x3c;
}
unsigned char *FixtureMemory(void) { return memory; }
void FixtureRun(unsigned char *state) {
 memset(&cpu,0,sizeof cpu); memcpy(&cpu.Regs,state,20);
 memcpy(&cpu.Cycles,state+20,4);memcpy(cpu.Counter,state+24,16);
 cpu.uSignal=state[49];cpu.uOpenBus=state[52];
 memcpy(&cpu.uCpuCycleCount,state+56,4);
 SNCPUSetBank(&cpu,0,SNCPU_MEM_SIZE,memory,TRUE);
 SNCPUSetMemSpeed(&cpu,0,SNCPU_MEM_SIZE,8);
 if(traps) {
  SNCPUSetTrap(&cpu,0x2000,8192,TrapRead,TrapWrite);
  SNCPUSetTrap(&cpu,0x20000,8192,TrapRead,TrapWrite);
 }
 SNCPUExecute_C(&cpu);
 memcpy(state,&cpu.Regs,20);memcpy(state+20,&cpu.Cycles,4);
 memcpy(state+24,cpu.Counter,16);state[49]=cpu.uSignal;state[52]=cpu.uOpenBus;
 memcpy(state+56,&cpu.uCpuCycleCount,4);
}
