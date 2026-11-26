/* Differential tests of the actual normal-resolution BG hot paths. */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "snppupalettedirty.h"
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
        for(unsigned j=0;j<8;++j) expected.uMask32[j]=a.uMask32[j]&~(b.uMask32[j]|c.uMask32[j]);
        SNMaskAndNot2(&got,&a,&b,&c); Check(!std::memcmp(&got,&expected,32),"fused ANDNOT2");
        got=a; SNMaskAndNot2(&got,&got,&b,&c); Check(!std::memcmp(&got,&expected,32),"ANDNOT2 alias A");
        got=b; SNMaskAndNot2(&got,&a,&got,&c); Check(!std::memcmp(&got,&expected,32),"ANDNOT2 alias B");
        got=c; SNMaskAndNot2(&got,&a,&b,&got); Check(!std::memcmp(&got,&expected,32),"ANDNOT2 alias C");
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
static void TestMosaicAndKey()
{
    unsigned cases=0;
    for(unsigned mosaic=0;mosaic<16;++mosaic)
    for(unsigned scroll=0;scroll<8;++scroll)
    for(unsigned pattern=0;pattern<4;++pattern)
    for(unsigned trial=0;trial<32;++trial) {
        Uint8 src[48] _ALIGN(16) = {}, pri[48] _ALIGN(16) = {};
        SNMaskT op, pr;
        for(unsigned j=0;j<33;++j) {
            src[j]=pattern?Next():0;
            pri[j]=pattern==2?src[j]:pattern==3?(src[j]&Next()):0;
        }
        _SnesPPURestoreCachedMasks(&op,&pr,src,pri,scroll,
            _SnesPPUClassifyBGLineMasks(src,pri),mosaic);
        for(unsigned x=0;x<256;++x) {
            unsigned bit=(x/(mosaic+1))*(mosaic+1)+scroll;
            Check(((op.uMask8[x/8]>>(x&7))&1)==((src[bit/8]>>(bit&7))&1),"mosaic opaque");
            Check(((pr.uMask8[x/8]>>(x&7))&1)==((pri[bit/8]>>(bit&7))&1),"mosaic priority");
        }
        ++cases;
    }
    SnesBGInfoT info={}; info.uBitDepth=4;
    SnesPPUBGLineCacheKeyT key;
    for(unsigned m=0;m<16;++m) {
        info.uMosaic=m;
        SnesPPUBGLineCacheSetKey(&key,99,0x10000,0,1,&info);
        Check(SnesPPUBGLineCacheKeyMatches(&key,99,0x10000,0,1,&info),"same dependency/key");
        Check(!SnesPPUBGLineCacheKeyMatches(&key,100,0x10000,0,1,&info),"changed dependency generation");
        Check(!SnesPPUBGLineCacheKeyMatches(&key,99,0x10001,0,1,&info),"changed tile");
        Check(bool(SnesPPUBGLineCacheKeyMatches(&key,99,0x20000,0,1,&info))==(m==0),"mosaic phase key");
    }
    std::printf("Mosaic masks: %u cases; cache key: 64 checks PASS\n",cases);
}
static void TestPaletteGroups()
{
    SnesPPUPaletteGroups groups;
    for (unsigned run=0;run<4096;++run) {
        bool dirty[256]={};
        groups.Clear();
        Check(groups.Bits()==0,"palette group reset");
        for (unsigned j=0;j<256;++j) {
            unsigned entry=(run==0)?j:(Next()&255);
            dirty[entry]=true;
            groups.Mark(entry);
            Uint64 expected=0;
            for (unsigned k=0;k<256;++k) if (dirty[k]) expected |= (Uint64)1 << (k/4);
            Check(groups.Bits()==expected,"palette group bitmap");
        }
        groups.All();
        Check(groups.Bits()==~(Uint64)0,"all palette groups dirty");
    }
    std::puts("Palette groups: 1048576 incremental marks PASS");
}
/* Production fetch-layer finishing and composition; independent pixel oracle.
   Layer code 0 lets OBJ0 through, 1 blocks OBJ0, 2 blocks OBJ0/1. */
static void TestMode7Compose()
{
    unsigned cases=0;
    for (unsigned run=0; run<4096; ++run)
    {
        SNMaskT bg1[SNPPU_BGPLANE_NUM]={}, bg2[SNPPU_BGPLANE_NUM]={};
        SNMaskT output[SNPPU_BGPLANE_NUM]={}, w1={}, w2={}, math={};
        Uint8 raw[256], pixels[256] _ALIGN(16);
        const bool ext=(run&1)!=0, en1=(run&2)!=0, en2=ext&&(run&4)!=0;
        const bool clip1=(run&8)!=0, clip2=(run&16)!=0;
        const bool math1=(run&32)!=0, math2=(run&64)!=0, backdrop=(run&128)!=0;
        const Uint8 mosaic=(Uint8)(Next()&255);
        for (unsigned x=0;x<256;++x) raw[x]=(x&7)==0?0:(x&7)==1?0x80:(Uint8)Next();
        for (unsigned i=0;i<32;++i) {w1.uMask8[i]=(Uint8)Next(); w2.uMask8[i]=(Uint8)Next();}
        memcpy(bg1,raw,256);
        _FinishMode7Layers((Uint8*)bg1,&bg1[SNPPU_BGPLANE_PRI],&bg1[SNPPU_BGPLANE_OPAQUE],en2?bg2:NULL,mosaic);
        if (backdrop) SNMaskSet(&math);
        Bool rendered=FALSE;
        if (en2) _RenderBG8(pixels,output,bg2,clip2?&w2:NULL,7,&math,math2,NULL,NULL,4,rendered,0,0);
        if (en1) _RenderBG8(pixels,output,bg1,clip1?&w1:NULL,8,&math,math1,NULL,NULL,ext?8:7,rendered,0,0);
        if (!rendered) memset(pixels,0,256);
        unsigned size=(mosaic>>4)+1;
        for (unsigned x=0;x<256;++x)
        {
            const unsigned bit=1u<<(x&7);
            unsigned v1=raw[(mosaic&1)?x-x%size:x];
            unsigned v2=raw[(mosaic&2)?x-x%size:x];
            bool a=en1&&v1&&(!clip1||!(w1.uMask8[x/8]&bit));
            bool b=en2&&(v2&127)&&(!clip2||!(w2.uMask8[x/8]&bit));
            bool hi=b&&(v2&128);
            unsigned expected=hi?(v2&127):a?v1:b?(v2&127):0;
            unsigned pri=hi?2:a?1:0;
            bool wantMath=hi?math2:a?math1:b?math2:backdrop;
            unsigned actualPri=!!(output[SNPPU_BGPLANE_LAYER0].uMask8[x/8]&bit) + 2*!!(output[SNPPU_BGPLANE_LAYER1].uMask8[x/8]&bit);
            Check(pixels[x]==expected,"Mode7 composited pixel");
            Check(actualPri==pri,"Mode7 OBJ priority code");
            Check(!!(math.uMask8[x/8]&bit)==wantMath,"Mode7 color math selection");
        }
        /* Exercise actual OBJ renderer on the composed BG masks, with a
           different window per screen. The mask represents each BG rank. */
        for (unsigned pr=0;pr<4;++pr)
        {
            SnesRenderObj8T obj={};
            obj.iPosX=(Int32)(Next()%249); obj.uPri=pr; obj.uPal=4;
            for (unsigned i=0;i<8;++i) obj.uData[i]=0xF1;
            obj.uData[SNPPU_BGPLANE_OPAQUE]=0xFF;
            Uint8 actual[256] _ALIGN(16); memcpy(actual,pixels,256);
            const SNMaskT *window=(run&256)?&w1:NULL;
            _SnesPPURenderOBJ8(actual,output,&obj,1,window,NULL,NULL,FALSE);
            for (unsigned x=0;x<256;++x)
            {
                unsigned bit=1u<<(x&7);
                unsigned rank=!!(output[SNPPU_BGPLANE_LAYER0].uMask8[x/8]&bit)+2*!!(output[SNPPU_BGPLANE_LAYER1].uMask8[x/8]&bit);
                bool visible=x>=(unsigned)obj.iPosX && x<(unsigned)obj.iPosX+8 && pr>=rank && (!window||!(window->uMask8[x/8]&bit));
                Check(actual[x]==(visible?0xF1:pixels[x]),"Mode7 actual OBJ draw/window");
            }
        }
        ++cases;
    }
    std::printf("Mode7 BG1/EXTBG composition: %u randomized lines PASS\n",cases);
}
int main() { InitLookup(); TestCHR(); TestMap(); TestMasks(); TestMosaicAndKey(); TestPaletteGroups(); TestMode7Compose(); }
