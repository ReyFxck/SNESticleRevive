#include "mesence_bridge.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "NES/NesConsole.h"
#include "NES/NesCpu.h"
#include "NES/NesMemoryManager.h"
#include "NES/BaseMapper.h"
#include "Utilities/VirtualFile.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

static uint64_t mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static std::vector<uint8_t> cartridge(bool pal)
{
    std::vector<uint8_t> data(16 + 32768 + 8192, 0);
    memcpy(data.data(), "NES\x1a", 4);
    data[4] = 2; data[5] = 1; data[6] = 2; data[9] = pal;
    // Original workload: rendering, OAM DMA and looping DMC at maximum rate.
    const uint8_t code[] = {
        0x78,0xd8,0xa2,0xff,0x9a,0xa9,0,0x8d,0,0x20,0x8d,1,0x20,
        0xa9,0x8f,0x8d,0x10,0x40,0xa9,0,0x8d,0x12,0x40,
        0xa9,0x1f,0x8d,0x13,0x40,0xa9,0x10,0x8d,0x15,0x40,
        0x2c,2,0x20,0x10,0xfb,0xa9,0x18,0x8d,1,0x20,
        0xa9,2,0x8d,0x14,0x40,0xe6,0,
        0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,
        0xa5,0,0x29,0x3f,0x8d,7,0x20,
        0xa9,0,0x8d,5,0x20,0x8d,5,0x20,0x4c,0x21,0x80
    };
    memcpy(data.data()+16, code, sizeof(code));
    for(unsigned i=0x7ffa; i<0x8000; i+=2) {
        data[16+i]=0; data[16+i+1]=0x80;
    }
    for(unsigned i=0; i<8192; ++i) data[16+32768+i]=(uint8_t)(i*37);
    return data;
}

class Bus : public INesMemoryHandler
{
public:
    uint8_t bytes[65536] = {};
    uint64_t hash = 0;
    unsigned reads = 0, writes = 0;
    void GetMemoryRanges(MemoryRanges&) override {}
    uint8_t ReadRam(uint16_t addr) override {
        ++reads; hash=mix(hash, (uint32_t(addr)<<8)|bytes[addr]);
        return bytes[addr];
    }
    uint8_t PeekRam(uint16_t addr) override { return bytes[addr]; }
    void WriteRam(uint16_t addr, uint8_t value) override {
        ++writes; hash=mix(hash, (UINT64_C(1)<<32)|(uint32_t(addr)<<8)|value);
        bytes[addr]=value;
    }
};

class RegisterBus : public Bus
{
public:
    void GetMemoryRanges(MemoryRanges& ranges) override {
        ranges.SetAllowOverride();
        ranges.AddHandler(MemoryOperation::Read,0x50ff,0x5101);
    }
};

static void checkMemory()
{
    Emulator emu; auto data=cartridge(false);
    assert(emu.LoadRom(VirtualFile(data.data(),data.size(),"memory.nes"),VirtualFile(),true,false));
    auto console=static_cast<NesConsole*>(emu.GetConsoleUnsafe());
    auto memory=console->GetMemoryManager();
    memory->Write(0x0011,0x29,MemoryOperationType::Write);
    for(unsigned mirror=0;mirror<4;++mirror) assert(memory->Read(0x11+mirror*0x800)==0x29);
    Bus a; RegisterBus b;
    memset(a.bytes,0x41,sizeof(a.bytes)); memset(b.bytes,0x62,sizeof(b.bytes));
    memory->RegisterReadHandler(&a,0x5000,0x51ff);
    memory->RegisterWriteHandler(&b,0x5080,0x517f);
    memory->RegisterIODevice(&b);
    assert(memory->Read(0x50fe)==0x41 && memory->Read(0x50ff)==0x62);
    assert(memory->Read(0x5101)==0x62 && memory->Read(0x5102)==0x41);
    assert(memory->DebugRead(0x5100)==0x62);
    memory->Write(0x50fe,0x73,MemoryOperationType::Write);
    assert(b.bytes[0x50fe]==0x73 && memory->Read(0x50fe)==0x41);
    memory->UnregisterIODevice(&b);
    assert(memory->Read(0x50ff)==0x41); // Unregister exposes open bus, not the previous owner.
    assert(memory->Read(0x5102)==0x41);
    // A split page becomes uniform again, with independent read/write ownership.
    memory->RegisterReadHandler(&b,0x5000,0x51ff);
    assert(memory->Read(0x50fe)==0x73 && memory->Read(0x51ff)==0x62);
    memory->RegisterReadHandler(&a,0x4015,0x4015);
    a.bytes[0x4015]=0x18;
    assert(memory->Read(0x4015)==0x18 && memory->GetOpenBus()==0x62);
    // RAM fast paths must respect per-address overrides.
    memory->RegisterReadHandler(&b,0x0011,0x0011);
    memory->RegisterWriteHandler(&b,0x0011,0x0011);
    memory->Write(0x0011,0x7c,MemoryOperationType::Write);
    assert(memory->Read(0x0011)==0x7c && memory->Read(0x0811)==0x29);
    memory->RegisterReadHandler(&a,0xffff,0xffff);
    memory->RegisterWriteHandler(&b,0xffff,0xffff);
    memory->Write(0xffff,0x9a,MemoryOperationType::Write);
    assert(memory->Read(0xffff)==0x41 && b.bytes[0xffff]==0x9a);
    memory->RegisterReadHandler(console->GetMapper(),0,65535);
    memory->RegisterWriteHandler(console->GetMapper(),0,65535);
    puts("Memory: mirrors, split registers, independent ownership, overrides, open bus and $FFFF PASS");
}

static uint64_t checkCpu()
{
    uint64_t hash=UINT64_C(1469598103934665603);
    for(auto region : {ConsoleRegion::Ntsc, ConsoleRegion::Pal, ConsoleRegion::Dendy}) {
        Emulator emu;
        emu.GetSettings()->GetNesConfig().Region=region;
        auto data=cartridge(false);
        assert(emu.LoadRom(VirtualFile(data.data(),data.size(),"cpu.nes"),VirtualFile(),true,false));
        auto console=static_cast<NesConsole*>(emu.GetConsoleUnsafe());
        auto memory=console->GetMemoryManager(); auto cpu=console->GetCpu();
        Bus bus;
        memory->RegisterReadHandler(&bus,0,65535);
        memory->RegisterWriteHandler(&bus,0,65535);
        for(unsigned op=0; op<256; ++op) for(unsigned seed=0; seed<32; ++seed) {
            for(unsigned i=0;i<65536;++i) bus.bytes[i]=(uint8_t)(i*13+seed*29);
            bus.bytes[0xfffc]=0; bus.bytes[0xfffd]=0x80;
            console->Reset();
            auto state=cpu->GetState();
            state.PC=(seed&1)?0x80fe:0x8000;
            state.A=seed*7; state.X=seed*17; state.Y=seed*31;
            state.SP=seed*11; state.PS=seed*9;
            state.IrqFlag=(seed&4)?1:0; state.NmiFlag=(seed&8)!=0;
            cpu->SetState(state);
            bus.bytes[state.PC]=op;
            bus.bytes[uint16_t(state.PC+1)]=(seed&2)?0xff:0x80;
            bus.bytes[uint16_t(state.PC+2)]=0x01;
            bus.hash=0; bus.reads=bus.writes=0;
            cpu->Exec();
            const auto &result=cpu->GetState();
            for(uint64_t value : {uint64_t(result.PC),uint64_t(result.SP),uint64_t(result.PS),
                uint64_t(result.A),uint64_t(result.X),uint64_t(result.Y),result.CycleCount,
                uint64_t(result.IrqFlag),uint64_t(result.NmiFlag),console->GetMasterClock(),bus.hash,
                uint64_t(bus.reads),uint64_t(bus.writes)}) hash=mix(hash,value);
        }
        // Remove stack-owned handlers before destroying the console.
        memory->RegisterReadHandler(console->GetMapper(),0,65535);
        memory->RegisterWriteHandler(console->GetMapper(),0,65535);
    }
    return hash;
}

static uint64_t pcmHash=UINT64_C(1469598103934665603);
static uint64_t pcmFrames=0;
static void sink(void*,const int16_t* left,const int16_t* right,uint32_t frames)
{
    pcmFrames+=frames;
    for(unsigned i=0;i<frames;++i) {
        pcmHash=mix(pcmHash,uint16_t(left[i])); pcmHash=mix(pcmHash,uint16_t(right[i]));
    }
}

int main(int argc,char** argv)
{
    if(argc==1) {
        checkMemory();
        uint64_t cpuHash=checkCpu();
        // Recorded from unmodified main 321abdac before changing dispatch.
        assert(cpuHash==UINT64_C(0xeebbbac2e387d8f3));
        printf("CPU: %016llx (24576 opcode/bus/IRQ cases)\n",(unsigned long long)cpuHash);
    }
    unsigned frames=argc>1 ? (unsigned)std::stoul(argv[1]) : 300;
    uint64_t videoHash=UINT64_C(1469598103934665603),stateHash=videoHash;
    auto start=std::chrono::steady_clock::now();
    for(bool pal : {false,true}) {
        auto core=MesenceCreate(); assert(core); auto data=cartridge(pal);
        assert(MesenceLoad(core,data.data(),data.size()));
        std::vector<uint32_t> pixels(256*240); uint8_t pads[2]={};
        for(unsigned f=0;f<frames;++f) {
            assert(MesenceFrame(core,pads,pixels.data(),256,sink,nullptr));
            for(auto pixel:pixels) videoHash=mix(videoHash,pixel);
            if(argc==1 && f%30==0) {
                assert(MesenceSnapshot(core)); uint32_t bytes;
                auto state=MesenceStateData(core,&bytes);
                for(unsigned i=0;i<bytes;++i) stateHash=mix(stateHash,state[i]);
            }
        }
        MesenceDestroy(core);
    }
    printf("DMA frames: video=%016llx state=%016llx pcm=%016llx samples=%llu\n",
        (unsigned long long)videoHash,(unsigned long long)stateHash,
        (unsigned long long)pcmHash,(unsigned long long)pcmFrames);
    if(argc==1) {
        assert(videoHash==UINT64_C(0x12ea3ffd4bc544f3));
        assert(stateHash==UINT64_C(0xe6aa6e8098898889));
        assert(pcmHash==UINT64_C(0xdb0cc8b63703a953) && pcmFrames==527278);
    }
    if(argc>1) printf("Elapsed: %.6f seconds (%u NTSC + %u PAL frames, host only)\n",
        std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),frames,frames);
}
