/* Differential SPC timing and BRR decoder tests. No target speed claims. */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include "types.h"
#include "snspc.h"
#include "snspc_c.h"
extern "C" {
#include "snspcbrr.h"
}
extern Int32 SNSPCExecute_Reference(SNSpcT *);
static uint32_t rng = 0x6928317u;
static uint32_t Next() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
struct Trace { uint32_t hash; unsigned count; };
static void Event(SNSpcT *s, unsigned addr, unsigned value) {
    Trace *t = static_cast<Trace*>(s->pUserData);
    t->hash = (t->hash ^ addr) * 16777619u;
    t->hash = (t->hash ^ value) * 16777619u;
    t->hash = (t->hash ^ (uint32_t)s->Cycles) * 16777619u;
    ++t->count;
    // Ensure two-byte accesses do not reset the clock between callbacks.
    s->Cycles -= 3;
}
static Uint8 Read(SNSpcT *s, Uint32 addr) {
    Uint8 val = (Uint8)(s->Mem[addr] ^ (Uint8)s->Cycles);
    Event(s, addr, val); return val;
}
static void Write(SNSpcT *s, Uint32 addr, Uint8 val) {
    Event(s, addr, 256u | val);
    s->Mem[addr] ^= 0x55;
    // Exercise the IPL ROM mapping change between bytes.
    if (addr == 0xF1) s->bRomEnable = (val & 0x80) != 0;
}
static void Require(bool good, const char *name, unsigned n) {
    if (!good) { std::fprintf(stderr, "%s failure at %u\n", name, n); std::exit(1); }
}
static unsigned SpcTest() {
    static SNSpcT a, b;
    static Uint8 initial[65536];
    for (auto &v: initial) v = (Uint8)Next();
    unsigned n = 0, ioEvents = 0;
    const Uint16 pcs[] = {0x0200, 0x7FFE, 0xFFFE, 0xFFFF};
    const Uint16 operands[] = {0x00EF,0x00F0,0x00F1,0x00FE,0x00FF,0x01FF,0xFFC0,0xFFFF,0x1234};
    for (unsigned op=0; op<256; ++op) for (unsigned k=0;k<72;++k) {
        std::memset(&a,0,sizeof(a));
        std::memcpy(a.Mem,initial,sizeof(initial));
        a.Regs.rPC = pcs[k%4];
        a.Regs.rA = (Uint8)Next(); a.Regs.rX = (Uint8)Next(); a.Regs.rY = (Uint8)Next();
        a.Regs.rSP = (Uint8)Next(); a.Regs.rPSW = (Uint8)Next();
        a.bRomEnable = k & 1; a.Cycles = (k%12)*SNSPC_CYCLE+(k%3);
        a.Counter[0] = 0x123456; a.Counter[1] = 0x8765;
        a.pReadTrapFunc=Read; a.pWriteTrapFunc=Write;
        Uint16 pc=a.Regs.rPC, operand=operands[(k/4)%9];
        a.Mem[pc]=(Uint8)op; a.Mem[(Uint16)(pc+1)]=(Uint8)operand;
        a.Mem[(Uint16)(pc+2)]=(Uint8)(operand>>8);
        // Random subsequent code is bounded by the small cycle budget.
        std::memcpy(&b,&a,sizeof(a));
        Trace ta={2166136261u,0}, tb=ta; a.pUserData=&ta; b.pUserData=&tb;
        SNSPCExecute_C(&a); SNSPCExecute_Reference(&b);
        a.pUserData=b.pUserData=nullptr;
        Require(std::memcmp(&a,&b,sizeof(a))==0 && ta.hash==tb.hash && ta.count==tb.count,"SPC",n);
        ++n; ioEvents+=ta.count;
    }
    Require(ioEvents>100,"SPC I/O coverage",ioEvents);
    std::printf("SPC differential: %u cases, %u I/O events, PASS\n",n,ioEvents);
    return n;
}
static Uint8 ReferenceBRR(const Uint8 *data, Int16 *out, Int16 p0, Int16 p1) {
    const int coeff[4][2]={{0,0},{240,0},{488,-240},{460,-208}};
    unsigned h=data[0],r=h>>4; if(r>12)r-=4;
    Int16 decode[16];
    for (unsigned j=0;j<16;++j) {
        int nibble=(data[1+j/2] >> (j%2?0:4))&15;
        if(nibble>=8)nibble-=16;
        decode[j]=(Int16)(nibble*(1<<r));
    }
    for(unsigned j=0;j<16;++j) {
        int f=(h>>2)&3;
        int v=(decode[j]>>1)+((p0*coeff[f][0])>>9)+((p1*coeff[f][1])>>9);
        if(v>32767)v=32767;
        if(v<-32768)v=-32768;
        out[j]=(Int16)(v*2); p1=p0; p0=out[j];
    }
    return h&3;
}
static unsigned BrrTest() {
    unsigned count=0;
    for(unsigned h=0;h<256;++h) for(unsigned k=0;k<512;++k) {
        Uint8 data[9]; data[0]=(Uint8)h;
        for(unsigned j=1;j<9;++j) data[j]=k<16?(Uint8)(17*k):(Uint8)Next();
        Int16 p0=(Int16)Next(),p1=(Int16)Next(), a[16],b[16];
        if(k<16) { p0=(k&1)?32767:-32768; p1=(k&2)?32767:-32768; }
        Uint8 f=ReferenceBRR(data,a,p0,p1);
        Require(SNSpcBRRDecode(data,b,p0,p1)==f && std::memcmp(a,b,sizeof(a))==0,"BRR",count);
        // Exact alias case: old decoder consumed the entire block first.
        Int16 alias[16]; std::memcpy(alias,data,sizeof(data));
        Require(SNSpcBRRDecode((Uint8*)alias,alias,p0,p1)==f && std::memcmp(a,alias,sizeof(a))==0,"BRR alias",count);
        ++count;
    }
    std::printf("BRR differential: %u blocks + %u overlapping blocks, PASS\n",count,count);
    return count;
}
int main() { SpcTest(); BrrTest(); }
