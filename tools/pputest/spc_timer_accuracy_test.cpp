/* Hardware-oriented timer tests, separate from interpreter equivalence tests. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "types.h"
#include "snspctimer.h"
#include "snspc.h"

static unsigned checks;
static void check(const char *name, unsigned got, unsigned expected)
{
    ++checks;
    if(got!=expected){std::fprintf(stderr,"FAIL %s: %u != %u\n",name,got,expected);std::exit(1);}
}

/* Slow independent reference: a tick-by-tick 8-bit target counter and 4-bit output. */
struct RefTimer {
    unsigned tick,phase,stage,target,out; bool enabled;
    RefTimer(unsigned t):tick(t),phase(0),stage(0),target(0),out(0),enabled(false){}
    void advance(unsigned delta){
        unsigned count=(phase+delta)/tick; phase=(phase+delta)%tick;
        while(count--){if(enabled){stage=(stage+1)&255;if(stage==target){stage=0;out=(out+1)&15;}}}
    }
    void enable(bool e){if(e&&!enabled){stage=0;out=0;}enabled=e;}
};
static void randomized(unsigned tick)
{
    SNSpcTimerT timer; SNSpcTimerReset(&timer,tick);
    RefTimer ref(tick); unsigned now=0,seed=0x5a22+tick;
    for(unsigned i=0;i<50000;++i){
        seed=seed*1664525u+1013904223u;
        unsigned dt=(seed>>8)%(tick*31+1); now+=dt; ref.advance(dt);
        SNSpcTimerSync(&timer,(Int32)now);
        switch(seed&7){
        case 0: case 1: ref.enable((seed&0x80)!=0); SNSpcTimerSetEnable(&timer,(Int32)now,(seed&0x80)?4:0); break;
        case 2: case 3: ref.target=(seed>>24)&255; SNSpcTimerSetTimer(&timer,(Uint8)ref.target); break;
        case 4: check("read-and-clear",SNSpcTimerGetCounter(&timer,(Int32)now),ref.out);ref.out=0;break;
        default:break;
        }
        check("phase + stage",timer.nElapsedCycles,ref.stage*tick+ref.phase);
        check("4-bit output",timer.uUpCounter,ref.out);
        check("enabled",timer.bEnabled,ref.enabled);
    }
}
int main()
{
    const unsigned tick=16*21;
    SNSpcTimerT t; SNSpcTimerReset(&t,tick); SNSpcTimerSetTimer(&t,1);
    SNSpcTimerSetEnable(&t,tick-1,TRUE);
    check("free-running prescaler",SNSpcTimerGetCounter(&t,tick),1);
    check("read clears output",SNSpcTimerGetCounter(&t,tick),0);
    SNSpcTimerSetEnable(&t,2*tick,4);
    check("already enabled does not reset",SNSpcTimerGetCounter(&t,2*tick),1);
    SNSpcTimerSetTimer(&t,5); SNSpcTimerSync(&t,5*tick); // stage 3
    SNSpcTimerSetTimer(&t,2);
    check("lower target waits for wrap",SNSpcTimerGetCounter(&t,6*tick),0);
    check("before wrapped equality",SNSpcTimerGetCounter(&t,259*tick),0);
    check("wrapped equality",SNSpcTimerGetCounter(&t,260*tick),1);
    SNSpcTimerSetEnable(&t,261*tick,FALSE);
    SNSpcTimerSetEnable(&t,262*tick-1,TRUE);
    check("enable resets target stage",SNSpcTimerGetCounter(&t,262*tick),0);
    check("enable retains prescaler phase",SNSpcTimerGetCounter(&t,263*tick),1);
    SNSpcTimerReset(&t,tick); SNSpcTimerSetEnable(&t,0,TRUE);
    check("zero target is 256 ticks",SNSpcTimerGetCounter(&t,255*tick),0);
    check("zero target reaches wrap",SNSpcTimerGetCounter(&t,256*tick),1);
    SNSpcTimerSetTimer(&t,1);
    check("output wraps at 16",SNSpcTimerGetCounter(&t,272*tick),0);
    SNSpcTimerReset(&t,tick); t.iCycleSync=(Int32)0xfffffff0u; SNSpcTimerSetTimer(&t,1);
    SNSpcTimerSetEnable(&t,t.iCycleSync,TRUE);
    check("32-bit time wrap",SNSpcTimerGetCounter(&t,(Int32)(0xfffffff0u+tick)),1);
    SNSpcTimerReset(&t,tick); SNSpcTimerSetTimer(&t,1);
    SNSpcTimerSetEnable(&t,0,TRUE);
    SNSpcTimerSetTestGate(&t,tick/2, FALSE);
    check("TEST falling edge",SNSpcTimerGetCounter(&t,tick/2),1);
    check("TEST gate blocks pulses",SNSpcTimerGetCounter(&t,4*tick),0);
    SNSpcTimerSetTestGate(&t,4*tick,TRUE);
    check("TEST resumes prescaler",SNSpcTimerGetCounter(&t,5*tick),1);
    SNSpcTimerSetTestGate(&t,5*tick+1,FALSE);
    check("low phase gate no falling edge",SNSpcTimerGetCounter(&t,6*tick),0);
    randomized(128*21); randomized(tick);
    SNSpcT spc; std::memset(&spc,0,sizeof(spc));
    spc.Mem[0]=0x12; spc.Mem[0xffff]=0x34; spc.Mem[1]=0x56;
    Uint8 buf[3]; SNSPCPeekMem(&spc,0xffff,buf,3);
    check("peek wraps first",buf[0],0x34);check("peek wraps second",buf[1],0x12);check("peek wraps third",buf[2],0x56);
    SNSPCReadMem(&spc,0xffff,buf,3);check("read wraps",buf[1],0x12);
    SNSPCWrite8(&spc,0x10001u,0xAB);check("write wraps",spc.Mem[1],0xAB);
    std::printf("SPC timer and memory accuracy: %u assertions PASS\n",checks);
}
