#include "trace.h"

#include <algorithm>
#include <errno.h>
#include <stdlib.h>
#include <set>
#include <sstream>
#include <string.h>

#include "state_io.h"

namespace {

static std::string JsonEscape(const std::string &value)
{
    std::string escaped;
    for (size_t i = 0; i < value.size(); ++i)
    {
        unsigned char c = (unsigned char)value[i];
        switch (c)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c >= 0x20)
                escaped += (char)c;
            break;
        }
    }
    return escaped;
}

static bool WriteOK(FILE *file, std::string *error)
{
    if (!file || ferror(file))
    {
        if (error) *error = "cannot write trace";
        return false;
    }
    return true;
}

static bool ExtractUnsigned(const std::string &line, const char *name,
                            uint64_t *value, int base = 10)
{
    std::string key = std::string("\"") + name + "\":";
    size_t pos = line.find(key);
    if (pos == std::string::npos)
        return false;
    pos += key.size();
    bool quoted = pos < line.size() && line[pos] == '"';
    if (quoted) ++pos;
    char *end = NULL;
    unsigned long long parsed = strtoull(line.c_str() + pos, &end, base);
    if (!end || end == line.c_str() + pos)
        return false;
    if (quoted && *end != '"')
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool ParsePorts(const std::string &line, const char *name,
                       uint8_t ports[4])
{
    std::string key = std::string("\"") + name + "\":\"";
    size_t pos = line.find(key);
    if (pos == std::string::npos)
        return false;
    pos += key.size();
    unsigned values[4];
    if (sscanf(line.c_str() + pos, "%2x/%2x/%2x/%2x",
               &values[0], &values[1], &values[2], &values[3]) != 4)
        return false;
    for (size_t i = 0; i < 4; ++i)
        ports[i] = (uint8_t)values[i];
    return true;
}

static void AddDifference(std::ostringstream &out, const char *name,
                          uint32_t expected, uint32_t actual)
{
    if (expected == actual)
        return;
    if (out.tellp() > 0)
        out << ',';
    out << name << '(';
    out.width(8); out.fill('0'); out << std::hex << std::uppercase << expected;
    out << "!=";
    out.width(8); out.fill('0'); out << std::hex << std::uppercase << actual;
    out << ')';
}

} // namespace

RomLabFrameRecord RomLabBuildFrameRecord(uint64_t index,
    uint64_t coreNanoseconds, const SnesStateT &state,
    uint32_t videoHash, size_t activeSramBytes,
    const RomLabHooks::FrameStats &instructions)
{
    RomLabFrameRecord record;
    memset(&record, 0, sizeof(record));
    record.Index = index;
    record.EmulatedFrame = state.uFrame;
    record.CoreNanoseconds = coreNanoseconds;
    record.StateHash = RomLabStateIO::Hash32(&state, sizeof(state));
    record.CpuHash = RomLabStateIO::Hash32(&state.CPU, sizeof(state.CPU));
    record.WRamHash = RomLabStateIO::Hash32(state.Ram, sizeof(state.Ram));
    record.SramHash = RomLabStateIO::Hash32(state.SRam,
        std::min(activeSramBytes, sizeof(state.SRam)));
    record.IoHash = RomLabStateIO::Hash32(&state.IO, sizeof(state.IO));
    record.DmaHash = RomLabStateIO::Hash32(&state.DMAC, sizeof(state.DMAC));
    record.PpuHash = RomLabStateIO::Hash32(&state.PPU, sizeof(state.PPU));
    record.PpuRegsHash = RomLabStateIO::Hash32(&state.PPU.Regs,
                                               sizeof(state.PPU.Regs));
    record.VramHash = RomLabStateIO::Hash32(state.PPU.m_VRAM,
                                            sizeof(state.PPU.m_VRAM));
    record.CgramHash = RomLabStateIO::Hash32(state.PPU.m_CGRAM,
                                             sizeof(state.PPU.m_CGRAM));
    record.OamHash = RomLabStateIO::Hash32(&state.PPU.m_OAM,
                                           sizeof(state.PPU.m_OAM));
    record.SpcHash = RomLabStateIO::Hash32(&state.SPC, sizeof(state.SPC));
    record.SpcRamHash = RomLabStateIO::Hash32(state.SpcRam,
                                              sizeof(state.SpcRam));
    record.SpcIoHash = RomLabStateIO::Hash32(&state.SPCIO,
                                             sizeof(state.SPCIO));
    record.DspHash = RomLabStateIO::Hash32(&state.SPCDSP,
                                           sizeof(state.SPCDSP));
    record.Sa1Hash = RomLabStateIO::Hash32(&state.SA1, sizeof(state.SA1));
    record.VideoHash = videoHash;
    record.CpuPC = state.CPU.Regs.rPC & 0xFFFFFFu;
    record.SpcPC = state.SPC.Regs.rPC;
    for (size_t i = 0; i < 4; ++i)
    {
        record.CpuToSpc[i] = state.SPCIO.Regs.apu_w[i];
        record.SpcToCpu[i] = state.SPCIO.Regs.apu_r[i];
    }
    record.PpuMode = (uint8_t)state.PPU.Regs.bgmode;
    record.MainLayers = (uint8_t)state.PPU.Regs.tm;
    record.SubLayers = (uint8_t)state.PPU.Regs.ts;
    record.Brightness = (uint8_t)state.PPU.Regs.inidisp & 0x0Fu;
    record.Instructions = instructions;
    return record;
}

std::string RomLabCompareFrames(const RomLabFrameRecord &expected,
                                const RomLabFrameRecord &actual)
{
    std::ostringstream differences;
    AddDifference(differences, "state", expected.StateHash, actual.StateHash);
    AddDifference(differences, "cpu", expected.CpuHash, actual.CpuHash);
    AddDifference(differences, "wram", expected.WRamHash, actual.WRamHash);
    AddDifference(differences, "sram", expected.SramHash, actual.SramHash);
    AddDifference(differences, "io", expected.IoHash, actual.IoHash);
    AddDifference(differences, "dma", expected.DmaHash, actual.DmaHash);
    AddDifference(differences, "ppu", expected.PpuHash, actual.PpuHash);
    AddDifference(differences, "ppu-regs", expected.PpuRegsHash,
                  actual.PpuRegsHash);
    AddDifference(differences, "vram", expected.VramHash, actual.VramHash);
    AddDifference(differences, "cgram", expected.CgramHash, actual.CgramHash);
    AddDifference(differences, "oam", expected.OamHash, actual.OamHash);
    AddDifference(differences, "spc", expected.SpcHash, actual.SpcHash);
    AddDifference(differences, "spc-ram", expected.SpcRamHash,
                  actual.SpcRamHash);
    AddDifference(differences, "spc-io", expected.SpcIoHash,
                  actual.SpcIoHash);
    AddDifference(differences, "dsp", expected.DspHash, actual.DspHash);
    AddDifference(differences, "sa1", expected.Sa1Hash, actual.Sa1Hash);
    if (expected.VideoHash && actual.VideoHash)
        AddDifference(differences, "video", expected.VideoHash,
                      actual.VideoHash);
    return differences.str();
}

bool RomLabWriteTraceHeader(FILE *file, const std::string &title,
    const std::string &mapper, const std::string &region,
    uint32_t romCRC, uint32_t romBytes, uint32_t romFlags,
    uint32_t stateBytes, bool video, std::string *error)
{
    fprintf(file,
        "{\"type\":\"header\",\"schema\":\"snesticle-romlab-v1\","
        "\"title\":\"%s\",\"mapper\":\"%s\",\"region\":\"%s\","
        "\"rom_crc\":\"%08X\",\"rom_bytes\":%u,\"rom_flags\":\"%08X\","
        "\"state_bytes\":%u,\"video\":%s}\n",
        JsonEscape(title).c_str(), JsonEscape(mapper).c_str(),
        JsonEscape(region).c_str(), romCRC, romBytes, romFlags,
        stateBytes, video ? "true" : "false");
    return WriteOK(file, error);
}

bool RomLabWriteTraceFrame(FILE *file, const RomLabFrameRecord &r,
                           std::string *error)
{
    fprintf(file,
        "{\"type\":\"frame\",\"index\":%llu,\"frame\":%u,\"core_ns\":%llu,"
        "\"state\":\"%08X\",\"cpu\":\"%08X\",\"wram\":\"%08X\","
        "\"sram\":\"%08X\",\"io\":\"%08X\",\"dma\":\"%08X\","
        "\"ppu\":\"%08X\",\"ppu_regs\":\"%08X\",\"vram\":\"%08X\","
        "\"cgram\":\"%08X\",\"oam\":\"%08X\",\"spc\":\"%08X\","
        "\"spc_ram\":\"%08X\",\"spc_io\":\"%08X\",\"dsp\":\"%08X\","
        "\"sa1\":\"%08X\",\"video_hash\":\"%08X\","
        "\"cpu_pc\":\"%06X\",\"spc_pc\":\"%04X\","
        "\"cpu_to_spc\":\"%02X/%02X/%02X/%02X\","
        "\"spc_to_cpu\":\"%02X/%02X/%02X/%02X\","
        "\"ppu_mode\":%u,\"tm\":%u,\"ts\":%u,\"brightness\":%u,"
        "\"cpu_ins\":%llu,\"sa1_ins\":%llu,\"spc_ins\":%llu,"
        "\"cpu_unimplemented\":%u,\"sa1_unimplemented\":%u,"
        "\"cpu_bad_pc\":\"%06X\",\"cpu_bad_op\":\"%02X\","
        "\"cpu_bad_is_sa1\":%u,"
        "\"spc_unimplemented\":%u,\"spc_bad_pc\":\"%04X\","
        "\"spc_bad_op\":\"%02X\",\"unmapped_r\":%u,"
        "\"unmapped_w\":%u,\"unhandled_io_r\":%u,"
        "\"unhandled_io_w\":%u,\"unhandled_addr\":\"%06X\","
        "\"unhandled_value\":\"%02X\",\"unhandled_flags\":%u}\n",
        (unsigned long long)r.Index, r.EmulatedFrame,
        (unsigned long long)r.CoreNanoseconds,
        r.StateHash, r.CpuHash, r.WRamHash, r.SramHash, r.IoHash,
        r.DmaHash, r.PpuHash, r.PpuRegsHash, r.VramHash, r.CgramHash,
        r.OamHash, r.SpcHash, r.SpcRamHash, r.SpcIoHash, r.DspHash,
        r.Sa1Hash, r.VideoHash, r.CpuPC, r.SpcPC,
        r.CpuToSpc[0], r.CpuToSpc[1], r.CpuToSpc[2], r.CpuToSpc[3],
        r.SpcToCpu[0], r.SpcToCpu[1], r.SpcToCpu[2], r.SpcToCpu[3],
        r.PpuMode, r.MainLayers, r.SubLayers, r.Brightness,
        (unsigned long long)r.Instructions.CpuInstructions,
        (unsigned long long)r.Instructions.Sa1Instructions,
        (unsigned long long)r.Instructions.SpcInstructions,
        r.Instructions.UnimplementedCpuInstructions,
        r.Instructions.UnimplementedSa1Instructions,
        r.Instructions.LastUnimplementedCpuPC,
        r.Instructions.LastUnimplementedCpuOpcode,
        r.Instructions.LastUnimplementedCpuWasSa1,
        r.Instructions.UnimplementedSpcInstructions,
        r.Instructions.LastUnimplementedSpcPC,
        r.Instructions.LastUnimplementedSpcOpcode,
        r.Instructions.UnmappedReads, r.Instructions.UnmappedWrites,
        r.Instructions.UnhandledIoReads,
        r.Instructions.UnhandledIoWrites,
        r.Instructions.LastUnhandledAddress,
        r.Instructions.LastUnhandledValue,
        r.Instructions.LastUnhandledFlags);
    return WriteOK(file, error);
}

bool RomLabWriteTraceSummary(FILE *file, uint64_t frames,
    uint64_t coreNanoseconds, bool divergence, uint64_t divergenceFrame,
    bool possibleStall, std::string *error)
{
    double fps = coreNanoseconds
        ? (double)frames * 1000000000.0 / (double)coreNanoseconds : 0.0;
    fprintf(file,
        "{\"type\":\"summary\",\"frames\":%llu,\"core_ns\":%llu,"
        "\"host_core_fps\":%.3f,\"divergence\":%s,"
        "\"divergence_index\":%llu,\"possible_stall\":%s}\n",
        (unsigned long long)frames, (unsigned long long)coreNanoseconds,
        fps, divergence ? "true" : "false",
        (unsigned long long)divergenceFrame,
        possibleStall ? "true" : "false");
    return WriteOK(file, error);
}

bool RomLabLoadTrace(const std::string &path,
    std::vector<RomLabFrameRecord> *records, uint32_t *romCRC,
    std::string *error)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
    {
        if (error) *error = "cannot open trace '" + path + "': " + strerror(errno);
        return false;
    }
    records->clear();
    if (romCRC) *romCRC = 0;
    char buffer[8192];
    size_t lineNumber = 0;
    while (fgets(buffer, sizeof(buffer), file))
    {
        ++lineNumber;
        std::string line(buffer);
        if (line.find("\"type\":\"header\"") != std::string::npos)
        {
            uint64_t value = 0;
            if (romCRC && ExtractUnsigned(line, "rom_crc", &value, 16))
                *romCRC = (uint32_t)value;
            continue;
        }
        if (line.find("\"type\":\"frame\"") == std::string::npos)
            continue;

        RomLabFrameRecord r;
        memset(&r, 0, sizeof(r));
        uint64_t value = 0;
#define GET_DEC(field, member) \
        if (!ExtractUnsigned(line, field, &value, 10)) goto bad_line; \
        r.member = value
#define GET_HEX(field, member) \
        if (!ExtractUnsigned(line, field, &value, 16)) goto bad_line; \
        r.member = (uint32_t)value
        GET_DEC("index", Index);
        GET_DEC("frame", EmulatedFrame);
        ExtractUnsigned(line, "core_ns", &r.CoreNanoseconds, 10);
        GET_HEX("state", StateHash);
        GET_HEX("cpu", CpuHash);
        GET_HEX("wram", WRamHash);
        GET_HEX("sram", SramHash);
        GET_HEX("io", IoHash);
        GET_HEX("dma", DmaHash);
        GET_HEX("ppu", PpuHash);
        GET_HEX("ppu_regs", PpuRegsHash);
        GET_HEX("vram", VramHash);
        GET_HEX("cgram", CgramHash);
        GET_HEX("oam", OamHash);
        GET_HEX("spc", SpcHash);
        GET_HEX("spc_ram", SpcRamHash);
        GET_HEX("spc_io", SpcIoHash);
        GET_HEX("dsp", DspHash);
        GET_HEX("sa1", Sa1Hash);
        GET_HEX("video_hash", VideoHash);
        GET_HEX("cpu_pc", CpuPC);
        if (!ExtractUnsigned(line, "spc_pc", &value, 16)) goto bad_line;
        r.SpcPC = (uint16_t)value;
        ParsePorts(line, "cpu_to_spc", r.CpuToSpc);
        ParsePorts(line, "spc_to_cpu", r.SpcToCpu);
        GET_DEC("ppu_mode", PpuMode);
        GET_DEC("tm", MainLayers);
        GET_DEC("ts", SubLayers);
        GET_DEC("brightness", Brightness);
        records->push_back(r);
        continue;
bad_line:
        if (error)
        {
            std::ostringstream message;
            message << path << ':' << lineNumber
                    << ": malformed ROM Lab frame";
            *error = message.str();
        }
        fclose(file);
        return false;
#undef GET_DEC
#undef GET_HEX
    }
    bool readOK = !ferror(file);
    fclose(file);
    if (!readOK)
    {
        if (error) *error = "cannot read trace: " + path;
        return false;
    }
    if (records->empty())
    {
        if (error) *error = "trace contains no frame records: " + path;
        return false;
    }
    return true;
}

RomLabStallResult RomLabDetectStall(
    const std::vector<RomLabFrameRecord> &records, size_t window)
{
    RomLabStallResult result = { false, 0, 0, 0 };
    if (!window || records.size() < window)
        return result;

    size_t first = records.size() - window;
    const RomLabFrameRecord &base = records[first];
    std::set<uint32_t> pcs;
    bool videoStable = true;
    bool portsStable = true;
    bool pictureStateStable = true;
    for (size_t i = first; i < records.size(); ++i)
    {
        const RomLabFrameRecord &r = records[i];
        pcs.insert(r.CpuPC);
        videoStable = videoStable && r.VideoHash == base.VideoHash;
        portsStable = portsStable &&
            !memcmp(r.CpuToSpc, base.CpuToSpc, sizeof(r.CpuToSpc)) &&
            !memcmp(r.SpcToCpu, base.SpcToCpu, sizeof(r.SpcToCpu));
        pictureStateStable = pictureStateStable &&
            r.VramHash == base.VramHash && r.CgramHash == base.CgramHash &&
            r.OamHash == base.OamHash && r.PpuRegsHash == base.PpuRegsHash;
    }
    result.DistinctCpuPCs = pcs.size();
    result.FirstIndex = base.Index;
    result.LastIndex = records.back().Index;
    result.Possible = pcs.size() <= 4 && portsStable &&
        ((base.VideoHash && videoStable) || pictureStateStable);
    return result;
}
