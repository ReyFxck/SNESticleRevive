#include <cstdio>
#include <cstdlib>
#include <climits>
#include "snppuvu0.h"

static Uint32 seed = 0x98275abc;
static Uint32 next() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static void check(bool ok) { if (!ok) { std::fprintf(stderr,"VU0 shadow test failed\n"); std::exit(1); } }
int main()
{
    static Uint8 vram[65536];
    for (unsigned i=0;i<sizeof vram;++i) vram[i]=(Uint8)next();
    SnesVU0CoordGroup coords[64];
    Uint8 a[256], b[256];
    unsigned cases=0, rejected=0;
    for (unsigned n=0;n<25000;++n) {
        SnesPPUMode7LineT l = {(Int32)(next()%16777217u)-8388608,
            (Int32)(next()%16777217u)-8388608,
            (Int32)(next()%65537u)-32768, (Int32)(next()%65537u)-32768};
        if (n%4==0) { l.x=(Int32)(next()%524288u)-131072; l.y=(Int32)(next()%524288u)-131072; l.dx=(Int32)(next()%4097u)-2048; l.dy=(Int32)(next()%4097u)-2048; }
        if (!SnesVU0Mode7Eligible(l)) { ++rejected; continue; }
        SnesVU0Mode7ModelCoords(coords,l);
        for (unsigned i=0;i<256;++i) {
            check(coords[i/4].x[i%4]==l.x+l.dx*(Int32)i);
            check(coords[i/4].y[i%4]==l.y+l.dy*(Int32)i);
        }
        for (unsigned mode=0;mode<4;++mode) {
            SnesVU0Mode7Reference(a,vram,l,mode);
            SnesVU0Mode7FetchCoords(b,vram,coords,mode);
            bool disabled=false;
            check(SnesVU0Mode7Compare(a,b,disabled) && !disabled);
            b[next()%256]^=0x80;
            check(!SnesVU0Mode7Compare(a,b,disabled) && disabled);
            check(!SnesVU0Mode7Compare(a,a,disabled));
            ++cases;
        }
    }
    SnesPPUMode7LineT edge={8388608,-8388608,0,0};
    check(SnesVU0Mode7Eligible(edge)); edge.dx=1; check(!SnesVU0Mode7Eligible(edge));
    edge.x=INT_MAX; edge.dx=INT_MAX; check(!SnesVU0Mode7Eligible(edge));
    check(cases>10000 && rejected>0);
    std::printf("PASS: %u Mode7 pixel/priority-bit comparisons + injected faults; %u ineligible lines; portable model only\n", cases,rejected);
}
