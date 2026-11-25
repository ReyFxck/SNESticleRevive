/* Differential tests of the actual normal-resolution BG hot paths. */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "../../src/snes/ppu/snppurender8.cpp"
#include "../../src/snes/ppu/snppubg.cpp"

SnesChrLookupT _SnesPPU_PlaneLookup[2] _ALIGN(32);
Uint8 _SnesPPU_HFlipLookup[2][256] _ALIGN(32);
static Uint32 rng = 0x8913a72bu;
static Uint32 Next() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void Check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", why); std::exit(1); } }
static void InitLookup()
{
    for (unsigned v=0; v<256; ++v) {
        unsigned flip=0;
        for (unsigned x=0; x<8; ++x) {
            ((Uint8*)_SnesPPU_PlaneLookup[0][v])[x]=(v>>(7-x))&1;
            ((Uint8*)_SnesPPU_PlaneLookup[1][v])[x]=(v>>x)&1;
            flip |= ((v>>x)&1)<<(7-x);
        }
        _SnesPPU_HFlipLookup[0][v]=v;
        _SnesPPU_HFlipLookup[1][v]=flip;
    }
}
static void TestCHR()
{
    static Uint16 vram[0x8000];
    for (unsigned i=0;i<0x8000;++i) vram[i]=(Next()%4==0)?0:(Uint16)Next();
    unsigned cases=0;
    for (unsigned depth=2; depth<=4; depth+=2)
    for (unsigned run=0; run<8192; ++run) {
        const unsigned base=(Next()&7)*0x1000u;
        const unsigned scroll=Next()&7;
        const unsigned palBase=Next()&3;
        const unsigned count=1+Next()%33;
        SnesRenderTileT tiles[33]={};
        Uint8 data[272] _ALIGN(16), masks[96] _ALIGN(8);
        Uint8 expected[272], expectedMask[96];
        std::memset(data,0xA5,sizeof(data)); std::memset(expected,0xA5,sizeof(expected));
        std::memset(masks,0xA5,sizeof(masks)); std::memset(expectedMask,0xA5,sizeof(expectedMask));
        if ((run&63)==0) {
#if SNPPU_BG_CHR_CACHE || SNPPU_OBJ_CACHE
            SnesPPUChrCacheInvalidateAll(&_SnesPPU_ChrCache);
#endif
            for (unsigned j=0;j<64;++j) vram[Next()&0x7fff]=(Uint16)Next();
        }
        for (unsigned i=0;i<count;++i) {
            if (i && Next()%3==0) { tiles[i]=tiles[i-1]; if (Next()&1) tiles[i].uPal=Next()&15; }
            else { tiles[i].uTile=Next()%1041; tiles[i].uPal=Next()&15; tiles[i].uFlip=Next()&3;
                tiles[i].uOffsetY=(scroll==0)?(Next()&7):0; }
            const SnesRenderTileT &t=tiles[i];
            const unsigned row=(base+t.uTile*depth*4+((scroll+t.uOffsetY)^((t.uFlip&2)?7:0)))&0x7fff;
            const unsigned planes[4]={unsigned(vram[row]&255),unsigned(vram[row]>>8),unsigned(vram[(row+8)&0x7fff]&255),unsigned(vram[(row+8)&0x7fff]>>8)};
            Uint8 pix[8], mask=0;
            const unsigned pal=(t.uPal&7)*(1u<<depth)+(depth==2?palBase*32:0);
            for (unsigned x=0;x<8;++x) {
                unsigned color=0, bit=(t.uFlip&1)?x:7-x;
                for (unsigned p=0;p<depth;++p) color|=((planes[p]>>bit)&1)<<p;
                if(color) mask|=1u<<x;
                pix[x]=(Uint8)(pal|color);
            }
            expectedMask[i]=mask; expectedMask[48+i]=(t.uPal&8)?mask:0;
            if(mask) std::memcpy(expected+i*8,pix,8);
        }
        if(depth==2) _FetchCHR2_64(vram,base,tiles,count,scroll,data,masks,_SnesPPU_Tile2PalLookup64[palBase]);
        else _FetchCHR4_64(vram,base,tiles,count,scroll,data,masks);
        Check(!std::memcmp(data,expected,sizeof(data)),"CHR palette/pixels/guards");
        Check(!std::memcmp(masks,expectedMask,sizeof(masks)),"CHR opacity/priority/guards");
        ++cases;
    }
    std::printf("CHR: %u randomized scanlines PASS\n",cases);
}
static void TestMap()
{
    static Uint16 map[4][1024]; SnesPPUScreenT *screens[4];
    for(unsigned s=0;s<4;++s) { screens[s]=(SnesPPUScreenT*)map[s]; for(unsigned i=0;i<1024;++i) map[s][i]=(Uint16)Next(); }
    unsigned cases=0;
    for(unsigned start=0;start<4096;++start) for(unsigned count: {1u,2u,17u,32u,33u,34u}) {
        SnesRenderTileT out[35], expected[35];
        std::memset(out,0xA5,sizeof(out)); std::memset(expected,0xA5,sizeof(expected));
        unsigned addr=start;
        for(unsigned i=0;i<count;++i) {
            unsigned word=map[(addr>>10)&3][addr&1023];
            expected[i].uTile=word&1023; expected[i].uPal=(word>>10)&15;
            expected[i].uFlip=(word>>14)&3; expected[i].uOffsetY=0;
            ++addr; if(!(addr&31)) addr=(addr-32)^1024;
        }
        _FetchBG8x8(start,out,count,screens);
        Check(!std::memcmp(out,expected,sizeof(out)),"map decode / row wrap / guards"); ++cases;
    }
    std::printf("Tilemap: %u cases PASS\n",cases);
}
static void TestMasks()
{
    for(unsigned i=0;i<65536;++i) {
        SNMaskT a,b,c,expected,got;
        for(unsigned j=0;j<8;++j) { a.uMask32[j]=Next(); b.uMask32[j]=Next(); c.uMask32[j]=Next(); }
        SNMaskANDN(&expected,&a,&b); SNMaskOR(&expected,&expected,&c);
        SNMaskAndNotOr(&got,&a,&b,&c); Check(!std::memcmp(&got,&expected,32),"fused mask");
        got=a; SNMaskAndNotOr(&got,&got,&b,&c); Check(!std::memcmp(&got,&expected,32),"alias A");
        got=b; SNMaskAndNotOr(&got,&a,&got,&c); Check(!std::memcmp(&got,&expected,32),"alias B");
        got=c; SNMaskAndNotOr(&got,&a,&b,&got); Check(!std::memcmp(&got,&expected,32),"alias C");
    }
    for(unsigned i=0;i<4096;++i) {
        Uint8 src[33],dst[48],expected[48];
        for(unsigned j=0;j<33;++j) src[j]=Next();
        std::memset(dst,0xA5,sizeof(dst)); std::memset(expected,0xA5,sizeof(expected));
        std::memcpy(expected,src,33); std::memset(expected+33,0,7);
        SnesPPUBGLineCacheStoreMask(dst,src);
        Check(!std::memcmp(dst,expected,sizeof(dst)),"cache mask padding and guards");
    }
    std::puts("Masks: 65536 fused cases + 4096 stores PASS");
}
int main() { InitLookup(); TestCHR(); TestMap(); TestMasks(); }
