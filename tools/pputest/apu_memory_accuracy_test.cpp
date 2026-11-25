/* Physical ARAM/BRR address tests, including the CPU-only IPL overlay. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "types.h"
#include "mixbuffer.h"
extern "C" {
#include "snspc.h"
}
#include "snspcdsp.h"
#include "snspcmix.h"
#include "snspcio.h"
static unsigned checks;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL %s\n",what); std::exit(1); }
}
class Capture : public CMixBuffer {
public:
    Int16 pcm[64] = {};
    unsigned count = 0;
    void GetFormat(Uint32 *r, Uint32 *b, Uint32 *c) override { *r=32000; *b=16; *c=2; }
    Int32 GetOutputSamples() override { return 32; }
    void OutputSamplesStereo(Int16 *l, Int16 *r, Int32 n) override {
        for (int i=0;i<n;++i) { pcm[count++]=l[i]; pcm[count++]=r[i]; }
    }
};
class InspectMixer : public SNSpcDspMixFull {
public:
    SNSpcChannelT *Channel() { return GetChannel(0); }
};
static void WritePhysical(SNSpcT &s, Uint16 a, Uint8 v) {
    if (s.bRomEnable && a>=SNSPC_ROM_ADDR) s.ShadowMem[a-SNSPC_ROM_ADDR]=v;
    else s.Mem[a]=v;
}
static void Render(Uint16 addr, bool overlay, Capture &out) {
    SNSpcT spc={}; SNSpcDsp dsp; InspectMixer mix;
    spc.bRomEnable=overlay;
    if (overlay) std::memset(spc.Mem+SNSPC_ROM_ADDR,0xFF,64);
    dsp.SetMem(spc.Mem); dsp.SetIPLOverlay(spc.ShadowMem,&spc.bRomEnable);
    dsp.SetMixer(0,&mix); mix.SetDsp(&dsp); dsp.Reset(); mix.Reset();
    dsp.Write8(SNSPCDSP_REG_DIR,0x10);
    spc.Mem[0x1000]=(Uint8)addr; spc.Mem[0x1001]=(Uint8)(addr>>8);
    spc.Mem[0x1002]=(Uint8)addr; spc.Mem[0x1003]=(Uint8)(addr>>8);
    WritePhysical(spc,addr,0x83); // looping BRR, range 8, filter 0
    for (unsigned i=1;i<9;++i) WritePhysical(spc,(Uint16)(addr+i),(Uint8)(0x12+i*7));
    dsp.Write8(SNSPCDSP_REG_MVOLL,127); dsp.Write8(SNSPCDSP_REG_MVOLR,127);
    dsp.Write8(SNSPCDSP_REG_VOLL,127); dsp.Write8(SNSPCDSP_REG_VOLR,127);
    dsp.Write8(SNSPCDSP_REG_PITCHHI,0x10); dsp.Write8(SNSPCDSP_REG_GAIN,127);
    dsp.Write8(SNSPCDSP_REG_FLG,0x20); dsp.Write8(SNSPCDSP_REG_KON,1);
    check(mix.Channel()->HasBlock(),"key-on at all addresses");
    mix.Mix(&out);
    check(out.count==64,"sample count");
    check(mix.Channel()->HasBlock(),"loop remains active");
    check(mix.Channel()->uBlockAddr==addr,"loop target restored");
}
int main() {
    SNSpcT spc={}; SNSpcDsp dsp;
    dsp.SetMem(spc.Mem); dsp.SetIPLOverlay(spc.ShadowMem,&spc.bRomEnable);
    dsp.Reset(); check(dsp.GetReg(SNSPCDSP_REG_FLG)==0xE0,"internal FLG reset");
    for (unsigned overlay=0;overlay<2;++overlay) {
        spc.bRomEnable=overlay;
        for (unsigned i=0;i<65536;++i) spc.Mem[i]=(Uint8)(i^(i>>8));
        for (unsigned i=0;i<64;++i) spc.ShadowMem[i]=(Uint8)(0xA7+i);
        for (unsigned a=0;a<65536;++a) {
            Uint8 buf[9]; const Uint8 *p=dsp.GetSampleBlock((Uint16)a,buf);
            for (unsigned i=0;i<9;++i) {
                Uint16 addr=(Uint16)(a+i);
                Uint8 expected=(overlay&&addr>=0xFFC0)?spc.ShadowMem[addr-0xFFC0]:spc.Mem[addr];
                check(p[i]==expected,"physical BRR byte");
            }
        }
        dsp.Write8(SNSPCDSP_REG_DIR,0xFF);
        for (unsigned n=0;n<256;++n) for (unsigned o=0;o<4;++o) {
            Uint16 a=(Uint16)(0xFF00+n*4+o), b=(Uint16)(a+1);
            unsigned lo=(overlay&&a>=0xFFC0)?spc.ShadowMem[a-0xFFC0]:spc.Mem[a];
            unsigned hi=(overlay&&b>=0xFFC0)?spc.ShadowMem[b-0xFFC0]:spc.Mem[b];
            check(dsp.GetSampleDir((Uint8)n,o)==(lo|(hi<<8)),"directory wraps");
        }
    }
    Capture ordinary, zero, wrapped, hidden;
    Render(0x2000,false,ordinary); Render(0,false,zero); Render(0xFFFC,false,wrapped); Render(0xFFFC,true,hidden);
    bool nonzero=false; for(auto v:ordinary.pcm) nonzero|=v!=0;
    check(nonzero,"non-silent baseline");
    check(!std::memcmp(ordinary.pcm,zero.pcm,sizeof(zero.pcm)),"zero-address PCM");
    check(!std::memcmp(ordinary.pcm,wrapped.pcm,sizeof(wrapped.pcm)),"wrapped PCM");
    check(!std::memcmp(ordinary.pcm,hidden.pcm,sizeof(hidden.pcm)),"IPL-hidden PCM");
    SNSpcIO io; io.Reset(); io.SetSpc(&spc); io.SetSpcDsp(&dsp);
    spc.pUserData=&io; spc.pReadTrapFunc=SNSpcIO::Read8Trap; spc.pWriteTrapFunc=SNSpcIO::Write8Trap;
    spc.bRomEnable=FALSE; spc.Cycles=0; spc.Counter[0]=0;
    for (unsigned i=0;i<4;++i) { SNSPCWrite8(&spc,0xF4+i,(Uint8)(0x80+i)); io.WriteCpuPort(0,0,i,(Uint8)(0x40+i)); }
    SNSPCWrite8(&spc,0xF1,0x30);
    for (unsigned i=0;i<4;++i) {
        check(SNSPCRead8(&spc,0xF4+i)==0,"input latch cleared");
        check(io.m_Regs.apu_r[i]==0x80+i,"output latch preserved");
        check(spc.Mem[0xF4+i]==0x80+i,"underlying ARAM preserved");
    }
    spc.Regs.rPSW=0;
    SNSPCWrite8(&spc,0xF0,0x00);
    for(unsigned i=0;i<3;++i) check(io.m_Regs.spc_timer[i].uTestGate!=0,"TEST gate closes");
    spc.Regs.rPSW=SNSPC_FLAG_P;
    SNSPCWrite8(&spc,0xF0,0x08);
    for(unsigned i=0;i<3;++i) check(io.m_Regs.spc_timer[i].uTestGate!=0,"PSW.P protects TEST");
    spc.Regs.rPSW=0;
    SNSPCWrite8(&spc,0xF0,0x08);
    for(unsigned i=0;i<3;++i) check(io.m_Regs.spc_timer[i].uTestGate==0,"TEST gate opens");
    std::printf("APU physical memory: %u assertions PASS\n",checks);
}
