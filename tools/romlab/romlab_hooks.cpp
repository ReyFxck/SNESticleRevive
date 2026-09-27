#include "romlab_hooks.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string.h>
#include <utility>
#include <vector>

#include "types.h"
#include "sncpu.h"

namespace {

enum EventKind
{
    EVENT_CPU,
    EVENT_SA1,
    EVENT_CPU_UNIMPLEMENTED,
    EVENT_SA1_UNIMPLEMENTED,
    EVENT_SPC,
    EVENT_SPC_UNIMPLEMENTED,
    EVENT_UNMAPPED_READ,
    EVENT_UNMAPPED_WRITE,
    EVENT_IO_READ,
    EVENT_IO_WRITE
};

struct TraceEvent
{
    uint32_t pc;
    uint8_t value;
    uint8_t kind;
    uint8_t sa1;
};

static const size_t kRingSize = 512;
static SNCpu_t *gMainCpu = NULL;
static RomLabHooks::FrameStats gFrame;
static uint64_t gUnimplementedCpu[2][256];
static uint64_t gUnimplemented[256];
static std::map<uint32_t, uint64_t> gUnhandled;
static TraceEvent gRing[kRingSize];
static size_t gRingNext = 0;
static size_t gRingCount = 0;

static void Push(uint32_t pc, uint8_t value, EventKind kind, bool sa1 = false)
{
    gRing[gRingNext].pc = pc;
    gRing[gRingNext].value = value;
    gRing[gRingNext].kind = (uint8_t)kind;
    gRing[gRingNext].sa1 = sa1 ? 1 : 0;
    gRingNext = (gRingNext + 1) % kRingSize;
    if (gRingCount < kRingSize)
        ++gRingCount;
}

} // namespace

extern "C" void SnesRomLabTraceCpuOpcode(SNCpu_t *cpu, Uint32 pc,
                                           Uint8 opcode)
{
    if (cpu == gMainCpu)
    {
        ++gFrame.CpuInstructions;
        Push(pc & 0xFFFFFFu, opcode, EVENT_CPU);
    }
    else
    {
        ++gFrame.Sa1Instructions;
        Push(pc & 0xFFFFFFu, opcode, EVENT_SA1);
    }
}

extern "C" void SnesRomLabTraceSpcOpcode(Uint32 pc, Uint8 opcode)
{
    ++gFrame.SpcInstructions;
    Push(pc & 0xFFFFu, opcode, EVENT_SPC);
}

extern "C" void SnesRomLabUnimplementedCpuOpcode(SNCpu_t *cpu, Uint32 pc,
                                                    Uint8 opcode)
{
    bool sa1 = cpu != gMainCpu;
    if (sa1) ++gFrame.UnimplementedSa1Instructions;
    else ++gFrame.UnimplementedCpuInstructions;
    gFrame.LastUnimplementedCpuPC = pc & 0xFFFFFFu;
    gFrame.LastUnimplementedCpuOpcode = opcode;
    gFrame.LastUnimplementedCpuWasSa1 = sa1 ? 1 : 0;
    ++gUnimplementedCpu[sa1 ? 1 : 0][opcode];
    Push(pc & 0xFFFFFFu, opcode,
         sa1 ? EVENT_SA1_UNIMPLEMENTED : EVENT_CPU_UNIMPLEMENTED, sa1);
}

extern "C" void SnesRomLabUnimplementedSpcOpcode(Uint32 pc, Uint8 opcode)
{
    ++gFrame.UnimplementedSpcInstructions;
    gFrame.LastUnimplementedSpcPC = (uint16_t)pc;
    gFrame.LastUnimplementedSpcOpcode = opcode;
    ++gUnimplemented[opcode];
    Push(pc & 0xFFFFu, opcode, EVENT_SPC_UNIMPLEMENTED);
}

/* flags: bit 0 = write, bit 1 = unhandled I/O (otherwise unmapped bus). */
extern "C" void SnesRomLabTraceUnhandledAccess(SNCpu_t *cpu, Uint32 address,
                                                  Uint8 value, Uint8 flags)
{
    bool write = (flags & 1u) != 0;
    bool io = (flags & 2u) != 0;
    bool sa1 = cpu != gMainCpu;
    EventKind kind;

    if (io)
    {
        if (write) ++gFrame.UnhandledIoWrites;
        else ++gFrame.UnhandledIoReads;
        kind = write ? EVENT_IO_WRITE : EVENT_IO_READ;
    }
    else
    {
        if (write) ++gFrame.UnmappedWrites;
        else ++gFrame.UnmappedReads;
        kind = write ? EVENT_UNMAPPED_WRITE : EVENT_UNMAPPED_READ;
    }

    address &= 0xFFFFFFu;
    gFrame.LastUnhandledAddress = address;
    gFrame.LastUnhandledValue = value;
    gFrame.LastUnhandledFlags = (uint8_t)(flags | (sa1 ? 4u : 0u));
    uint32_t key = address | ((uint32_t)(flags & 3u) << 24) |
                   (sa1 ? (1u << 26) : 0u);
    ++gUnhandled[key];
    Push(address, value, kind, sa1);
}

namespace RomLabHooks {

void Reset()
{
    gMainCpu = NULL;
    memset(&gFrame, 0, sizeof(gFrame));
    memset(gUnimplementedCpu, 0, sizeof(gUnimplementedCpu));
    memset(gUnimplemented, 0, sizeof(gUnimplemented));
    gUnhandled.clear();
    memset(gRing, 0, sizeof(gRing));
    gRingNext = 0;
    gRingCount = 0;
}

void SetMainCpu(SNCpu_t *cpu)
{
    gMainCpu = cpu;
}

void BeginFrame()
{
    memset(&gFrame, 0, sizeof(gFrame));
}

FrameStats GetFrameStats()
{
    return gFrame;
}

uint64_t GetUnimplementedSpcCount()
{
    uint64_t total = 0;
    for (size_t i = 0; i < 256; ++i)
        total += gUnimplemented[i];
    return total;
}

std::string GetUnimplementedSpcSummary()
{
    char item[64];
    std::string result;
    for (size_t i = 0; i < 256; ++i)
    {
        if (!gUnimplemented[i])
            continue;
        snprintf(item, sizeof(item), "%s%02X:%llu",
                 result.empty() ? "" : ",",
                 (unsigned)i,
                 (unsigned long long)gUnimplemented[i]);
        result += item;
    }
    return result;
}

uint64_t GetUnimplementedCpuCount()
{
    uint64_t total = 0;
    for (size_t cpu = 0; cpu < 2; ++cpu)
        for (size_t opcode = 0; opcode < 256; ++opcode)
            total += gUnimplementedCpu[cpu][opcode];
    return total;
}

std::string GetUnimplementedCpuSummary()
{
    char item[80];
    std::string result;
    for (size_t cpu = 0; cpu < 2; ++cpu)
    {
        for (size_t opcode = 0; opcode < 256; ++opcode)
        {
            if (!gUnimplementedCpu[cpu][opcode])
                continue;
            snprintf(item, sizeof(item), "%s%s-%02X:%llu",
                     result.empty() ? "" : ",",
                     cpu ? "sa1" : "cpu", (unsigned)opcode,
                     (unsigned long long)gUnimplementedCpu[cpu][opcode]);
            result += item;
        }
    }
    return result;
}

uint64_t GetUnhandledAccessCount()
{
    uint64_t total = 0;
    for (std::map<uint32_t, uint64_t>::const_iterator it = gUnhandled.begin();
         it != gUnhandled.end(); ++it)
        total += it->second;
    return total;
}

std::string GetUnhandledAccessSummary(size_t limit)
{
    std::vector<std::pair<uint32_t, uint64_t> > accesses(
        gUnhandled.begin(), gUnhandled.end());
    std::sort(accesses.begin(), accesses.end(),
        [](const std::pair<uint32_t, uint64_t> &left,
           const std::pair<uint32_t, uint64_t> &right) {
            if (left.second != right.second)
                return left.second > right.second;
            return left.first < right.first;
        });

    std::ostringstream result;
    size_t count = std::min(limit, accesses.size());
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t key = accesses[i].first;
        uint8_t flags = (uint8_t)((key >> 24) & 3u);
        bool sa1 = (key & (1u << 26)) != 0;
        if (i) result << ',';
        result << (sa1 ? "sa1-" : "cpu-")
               << ((flags & 2u) ? "io-" : "map-")
               << ((flags & 1u) ? 'w' : 'r') << '@';
        result.width(6);
        result.fill('0');
        result << std::hex << std::uppercase << (key & 0xFFFFFFu)
               << std::dec << ':' << accesses[i].second;
    }
    return result.str();
}

void DumpRecent(FILE *file)
{
    if (!file)
        return;

    size_t first = (gRingNext + kRingSize - gRingCount) % kRingSize;
    for (size_t i = 0; i < gRingCount; ++i)
    {
        const TraceEvent &event = gRing[(first + i) % kRingSize];
        const char *kind = event.kind == EVENT_CPU ? "cpu" :
            event.kind == EVENT_SA1 ? "sa1" :
            event.kind == EVENT_CPU_UNIMPLEMENTED ? "cpu-unimplemented" :
            event.kind == EVENT_SA1_UNIMPLEMENTED ? "sa1-unimplemented" :
            event.kind == EVENT_SPC ? "spc" :
            event.kind == EVENT_SPC_UNIMPLEMENTED ? "spc-unimplemented" :
            event.kind == EVENT_UNMAPPED_READ ? "unmapped-read" :
            event.kind == EVENT_UNMAPPED_WRITE ? "unmapped-write" :
            event.kind == EVENT_IO_READ ? "unhandled-io-read" :
            "unhandled-io-write";
        if (event.kind <= EVENT_SPC_UNIMPLEMENTED)
            fprintf(file, "%s pc=%06X opcode=%02X\n", kind,
                    (unsigned)event.pc, (unsigned)event.value);
        else
            fprintf(file, "%s%s address=%06X value=%02X\n",
                    event.sa1 ? "sa1-" : "cpu-", kind,
                    (unsigned)event.pc, (unsigned)event.value);
    }
}

} // namespace RomLabHooks
