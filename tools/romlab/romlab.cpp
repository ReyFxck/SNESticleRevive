/*
 * SNESticle ROM Lab - deterministic whole-core host runner.
 *
 * ROM Lab executes the same portable SNES core used by the PS2 build,
 * snapshots every subsystem at frame boundaries and can compare the result
 * against another run. ROM images are never bundled with the tool.
 */

#include <algorithm>
#include <chrono>
#include <errno.h>
#include <filesystem>
#include <memory>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "types.h"
#include "dataio.h"
#include "pixelformat.h"
#include "rendersurface.h"
#include "snes.h"
extern "C" {
#include "sncpu_c.h"
#include "snspc_c.h"
}
#include "snppucolor.h"
#include "snstate.h"

#include "input_movie.h"
#include "romlab_hooks.h"
#include "state_io.h"
#include "trace.h"

extern "C" void DLog(const char *format, ...)
{
#if SNDBG_LOG
	va_list args;
	va_start(args, format);
	std::vfprintf(stderr, format, args);
	std::fputc('\n', stderr);
	va_end(args);
#else
    (void)format;
#endif
}

namespace {

static const char *kVersion = "1";
static const uint32_t kVideoWidth = 256;
static const uint32_t kVideoHeight = SNESPPU_VISIBLE_LINES_OVERSCAN;

struct Options
{
    std::string RomPath;
    std::string StatePath;
    std::string SramPath;
    std::string InputPath;
    std::string TracePath;
    std::string ReferencePath;
    std::string CheckpointPath;
    std::string DumpDirectory;
    uint64_t Frames = 600;
    uint64_t Warmup = 0;
    size_t StallWindow = 120;
    bool Video = true;
    bool VerifyDeterminism = false;
    bool ForceState = false;
    bool ContinueOnDivergence = false;
    bool Quiet = false;
};

struct RunResult
{
    std::vector<RomLabFrameRecord> Records;
    std::unique_ptr<SnesStateT> FinalState;
    std::vector<uint8_t> LastVideo;
    uint64_t CoreNanoseconds = 0;
    bool Diverged = false;
    uint64_t DivergenceIndex = 0;
    std::string Difference;
};

static void Usage(const char *program)
{
    fprintf(stderr,
        "SNESticle ROM Lab v%s\n"
        "\n"
        "Usage:\n"
        "  %s run --rom GAME.sfc [options]\n"
        "  %s diff TRACE_A.jsonl TRACE_B.jsonl\n"
        "  %s self-test\n"
        "\n"
        "Run options:\n"
        "  --state FILE          SNESticle raw or SNRSTATE save state\n"
        "  --sram FILE           raw cartridge SRAM (ignored when state is used)\n"
        "  --input FILE          sparse deterministic controller movie\n"
        "  --frames N            measured frames (default: 600)\n"
        "  --warmup N            unmeasured frames before capture\n"
        "  --trace FILE          write per-frame JSONL trace\n"
        "  --reference FILE      compare against a ROM Lab JSONL trace\n"
        "  --verify-determinism  replay twice and require exact hashes\n"
        "  --checkpoint FILE     write final PS2-compatible SNRSTATE file\n"
        "  --dump-dir DIR        dump final or first-divergence artifacts\n"
        "  --stall-window N      deadlock analysis window (default: 120)\n"
        "  --no-video            execute PPU state without framebuffer output\n"
        "  --force-state         allow ROM identity mismatch for a state\n"
        "  --continue            continue after a trace divergence\n"
        "  --quiet               suppress normal progress and summary output\n",
        kVersion, program, program, program);
}

static bool ParseUnsigned(const char *text, uint64_t *value)
{
    if (!text || !*text || *text == '-')
        return false;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (!end || *end || end == text)
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool NeedValue(int argc, char **argv, int *index,
                      const char *option, std::string *value)
{
    if (*index + 1 >= argc)
    {
        fprintf(stderr, "%s requires a value\n", option);
        return false;
    }
    *value = argv[++*index];
    return true;
}

static bool ParseRunOptions(int argc, char **argv, int start,
                            Options *options)
{
    for (int i = start; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--rom")
        {
            if (!NeedValue(argc, argv, &i, "--rom", &options->RomPath))
                return false;
        }
        else if (arg == "--state")
        {
            if (!NeedValue(argc, argv, &i, "--state", &options->StatePath))
                return false;
        }
        else if (arg == "--sram")
        {
            if (!NeedValue(argc, argv, &i, "--sram", &options->SramPath))
                return false;
        }
        else if (arg == "--input")
        {
            if (!NeedValue(argc, argv, &i, "--input", &options->InputPath))
                return false;
        }
        else if (arg == "--trace")
        {
            if (!NeedValue(argc, argv, &i, "--trace", &options->TracePath))
                return false;
        }
        else if (arg == "--reference")
        {
            if (!NeedValue(argc, argv, &i, "--reference",
                           &options->ReferencePath))
                return false;
        }
        else if (arg == "--checkpoint")
        {
            if (!NeedValue(argc, argv, &i, "--checkpoint",
                           &options->CheckpointPath))
                return false;
        }
        else if (arg == "--dump-dir")
        {
            if (!NeedValue(argc, argv, &i, "--dump-dir",
                           &options->DumpDirectory))
                return false;
        }
        else if (arg == "--frames" || arg == "--warmup" ||
                 arg == "--stall-window")
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "%s requires a value\n", arg.c_str());
                return false;
            }
            uint64_t value = 0;
            if (!ParseUnsigned(argv[++i], &value))
            {
                fprintf(stderr, "invalid value for %s\n", arg.c_str());
                return false;
            }
            if (arg == "--frames") options->Frames = value;
            else if (arg == "--warmup") options->Warmup = value;
            else options->StallWindow = (size_t)value;
        }
        else if (arg == "--no-video") options->Video = false;
        else if (arg == "--verify-determinism")
            options->VerifyDeterminism = true;
        else if (arg == "--force-state") options->ForceState = true;
        else if (arg == "--continue") options->ContinueOnDivergence = true;
        else if (arg == "--quiet") options->Quiet = true;
        else
        {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    if (options->RomPath.empty())
    {
        fprintf(stderr, "--rom is required\n");
        return false;
    }
    if (!options->Frames)
    {
        fprintf(stderr, "--frames must be greater than zero\n");
        return false;
    }
    return true;
}

static bool OpenRom(const std::string &path, SnesRom *rom,
                    std::string *error)
{
    CFileIO file;
    if (!file.Open(path.c_str(), "rb"))
    {
        *error = "cannot open ROM: " + path;
        return false;
    }
    Emu::Rom::LoadErrorE result = rom->LoadRom(&file);
    if (result != Emu::Rom::LOADERROR_NONE)
    {
        char message[128];
        snprintf(message, sizeof(message), "cannot load ROM (error %d): ",
                 (int)result);
        *error = message + path;
        return false;
    }
    return true;
}

static bool WritePPM(const std::filesystem::path &path,
                     const std::vector<uint8_t> &rgba, std::string *error)
{
    FILE *file = fopen(path.string().c_str(), "wb");
    if (!file)
    {
        *error = "cannot create image '" + path.string() + "': " +
                 strerror(errno);
        return false;
    }
    fprintf(file, "P6\n%u %u\n255\n", kVideoWidth, kVideoHeight);
    bool ok = true;
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        if (fwrite(&rgba[i], 1, 3, file) != 3)
        {
            ok = false;
            break;
        }
    }
    if (fclose(file)) ok = false;
    if (!ok) *error = "cannot write image: " + path.string();
    return ok;
}

static bool DumpArtifacts(const Options &options, const char *label,
                          uint64_t index, const SnesStateT &state,
                          const std::vector<uint8_t> &video,
                          std::string *error)
{
    if (options.DumpDirectory.empty())
        return true;
    std::error_code fsError;
    std::filesystem::create_directories(options.DumpDirectory, fsError);
    if (fsError)
    {
        *error = "cannot create dump directory: " + fsError.message();
        return false;
    }

    char base[64];
    snprintf(base, sizeof(base), "%s-%06llu", label,
             (unsigned long long)index);
    std::filesystem::path directory(options.DumpDirectory);
    if (!RomLabStateIO::SaveRawState(
            (directory / (std::string(base) + ".state")).string(),
            state, error))
        return false;
    if (!video.empty() && !WritePPM(
            directory / (std::string(base) + ".ppm"), video, error))
        return false;

    std::filesystem::path opcodes = directory /
        (std::string(base) + "-opcodes.txt");
    FILE *file = fopen(opcodes.string().c_str(), "wb");
    if (!file)
    {
        *error = "cannot create opcode dump: " + opcodes.string();
        return false;
    }
    RomLabHooks::DumpRecent(file);
    bool ok = !fclose(file);
    if (!ok) *error = "cannot write opcode dump: " + opcodes.string();
    return ok;
}

static bool PrepareInitialState(SnesRom &rom, const Options &options,
                                SnesStateT *initial, std::string *error)
{
    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();

    RomLabStateIO::RomIdentity identity = RomLabStateIO::Identify(rom);
    if (!options.StatePath.empty())
    {
        SnesStateT loaded;
        if (!RomLabStateIO::LoadState(options.StatePath, identity,
                                      options.ForceState, &loaded, error))
            return false;
        if (!system.RestoreState(&loaded))
        {
            *error = "core rejected the decoded SNESticle state";
            return false;
        }
    }
    else if (!options.SramPath.empty())
    {
        size_t bytes = (size_t)system.GetSRAMBytes();
        if (!bytes)
        {
            *error = "this cartridge does not expose battery SRAM";
            return false;
        }
        if (!RomLabStateIO::LoadSRAM(options.SramPath,
                                     system.GetSRAMData(), bytes, error))
            return false;
    }

    system.SaveState(initial);
    return true;
}

static RunResult RunOnce(SnesRom &rom, const Options &options,
                         const SnesStateT &initial, InputMovie movie,
                         const std::vector<RomLabFrameRecord> *reference,
                         FILE *trace, std::string *error)
{
    RunResult result;
    result.FinalState.reset(new SnesStateT);
    result.Records.reserve((size_t)options.Frames);

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    if (!system.RestoreState(const_cast<SnesStateT *>(&initial)))
    {
        *error = "cannot restore initial state";
        return result;
    }

    std::vector<uint8_t> video;
    CRenderSurface surface;
    CRenderSurface *target = NULL;
    if (options.Video)
    {
        video.resize((size_t)kVideoWidth * kVideoHeight * 4);
        surface.Set(video.data(), kVideoWidth, kVideoHeight,
                    kVideoWidth * 4,
                    PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
        target = &surface;
    }

    RomLabHooks::Reset();
    RomLabHooks::SetMainCpu(system.GetCpu());
    movie.Reset();

    const uint64_t totalFrames = options.Warmup + options.Frames;
    for (uint64_t relativeFrame = 0; relativeFrame < totalFrames;
         ++relativeFrame)
    {
        if (!video.empty())
            memset(video.data(), 0, video.size());
        const Emu::SysInputT &input = movie.Get(relativeFrame);
        RomLabHooks::BeginFrame();
        std::chrono::steady_clock::time_point begin =
            std::chrono::steady_clock::now();
        system.ExecuteFrame(const_cast<Emu::SysInputT *>(&input), target, NULL,
                            Emu::System::MODE_ACCURATEDETERMINISTIC);
        std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();

        if (relativeFrame < options.Warmup)
            continue;

        uint64_t index = relativeFrame - options.Warmup;
        uint64_t nanoseconds = (uint64_t)
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
                .count();
        result.CoreNanoseconds += nanoseconds;
        system.SaveState(result.FinalState.get());
        uint32_t videoHash = video.empty() ? 0 :
            RomLabStateIO::Hash32(video.data(), video.size());
        RomLabFrameRecord record = RomLabBuildFrameRecord(
            index, nanoseconds, *result.FinalState, videoHash,
            (size_t)std::max(0, system.GetSRAMBytes()),
            RomLabHooks::GetFrameStats());
        result.Records.push_back(record);
        result.LastVideo = video;

        if (trace && !RomLabWriteTraceFrame(trace, record, error))
            return result;

        if (reference)
        {
            bool frameDiverged = false;
            std::string frameDifference;
            if (index >= reference->size())
            {
                frameDiverged = true;
                frameDifference = "reference-ended";
            }
            else
            {
                frameDifference = RomLabCompareFrames(
                    (*reference)[(size_t)index], record);
                frameDiverged = !frameDifference.empty();
            }
            if (frameDiverged)
            {
                bool firstDivergence = !result.Diverged;
                if (!result.Diverged)
                {
                    result.Diverged = true;
                    result.DivergenceIndex = index;
                    result.Difference = frameDifference;
                }
                if (firstDivergence &&
                    !DumpArtifacts(options, "divergence", index,
                                   *result.FinalState, video, error))
                    return result;
                if (!options.ContinueOnDivergence)
                    break;
            }
        }
    }
    return result;
}

static double Percentile(std::vector<uint64_t> values, double percentile)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = (size_t)((values.size() - 1) * percentile);
    return (double)values[index] / 1000000.0;
}

static void PrintRunSummary(const Options &options, const RunResult &result,
                            const RomLabStallResult &stall)
{
    if (result.Records.empty())
        return;
    std::vector<uint64_t> durations;
    durations.reserve(result.Records.size());
    for (size_t i = 0; i < result.Records.size(); ++i)
        durations.push_back(result.Records[i].CoreNanoseconds);
    double fps = result.CoreNanoseconds
        ? (double)result.Records.size() * 1000000000.0 /
          (double)result.CoreNanoseconds : 0.0;
    const RomLabFrameRecord &last = result.Records.back();

    printf("Result: frames=%zu host-core=%.2f fps frame-ms p50/p95/max="
           "%.3f/%.3f/%.3f\n",
           result.Records.size(), fps,
           Percentile(durations, 0.50), Percentile(durations, 0.95),
           Percentile(durations, 1.00));
    printf("Last: frame=%u CPU=%06X SPC=%04X APU cpu->spc="
           "%02X/%02X/%02X/%02X spc->cpu=%02X/%02X/%02X/%02X\n",
           last.EmulatedFrame, last.CpuPC, last.SpcPC,
           last.CpuToSpc[0], last.CpuToSpc[1], last.CpuToSpc[2],
           last.CpuToSpc[3], last.SpcToCpu[0], last.SpcToCpu[1],
           last.SpcToCpu[2], last.SpcToCpu[3]);
    if (stall.Possible)
    {
        printf("Warning: possible deadlock from capture index %llu to %llu "
               "(%zu CPU frame-boundary PCs, stable video/PPU and APU ports).\n",
               (unsigned long long)stall.FirstIndex,
               (unsigned long long)stall.LastIndex,
               stall.DistinctCpuPCs);
    }
    uint64_t missing = RomLabHooks::GetUnimplementedSpcCount();
    if (missing)
    {
        printf("ERROR: SPC700 executed %llu unimplemented instruction(s): %s\n",
               (unsigned long long)missing,
               RomLabHooks::GetUnimplementedSpcSummary().c_str());
    }
    uint64_t missingCpu = RomLabHooks::GetUnimplementedCpuCount();
    if (missingCpu)
    {
        printf("ERROR: 65C816/SA-1 executed %llu unimplemented "
               "instruction(s): %s\n",
               (unsigned long long)missingCpu,
               RomLabHooks::GetUnimplementedCpuSummary().c_str());
    }
    uint64_t unhandled = RomLabHooks::GetUnhandledAccessCount();
    if (unhandled)
    {
        printf("Candidates: %llu unhandled/open-bus access(es); hottest: %s\n",
               (unsigned long long)unhandled,
               RomLabHooks::GetUnhandledAccessSummary().c_str());
    }
    if (!options.TracePath.empty())
        printf("Trace: %s\n", options.TracePath.c_str());
}

static int RunCommand(const Options &options)
{
    std::string error;
    SnesRom rom;
    if (!OpenRom(options.RomPath, &rom, &error))
    {
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }
    RomLabStateIO::RomIdentity identity = RomLabStateIO::Identify(rom);

    InputMovie movie;
    if (!options.InputPath.empty() && !movie.Load(options.InputPath, &error))
    {
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<SnesStateT> initial(new SnesStateT);
    if (!PrepareInitialState(rom, options, initial.get(), &error))
    {
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }

    std::vector<RomLabFrameRecord> reference;
    if (!options.ReferencePath.empty())
    {
        uint32_t referenceCRC = 0;
        if (!RomLabLoadTrace(options.ReferencePath, &reference,
                             &referenceCRC, &error))
        {
            fprintf(stderr, "ROM Lab: %s\n", error.c_str());
            return 1;
        }
        if (referenceCRC && referenceCRC != identity.CRC32)
        {
            fprintf(stderr,
                "ROM Lab: reference trace is for ROM %08X, current ROM is %08X\n",
                referenceCRC, identity.CRC32);
            return 1;
        }
    }

    FILE *trace = NULL;
    if (!options.TracePath.empty())
    {
        trace = fopen(options.TracePath.c_str(), "wb");
        if (!trace)
        {
            fprintf(stderr, "ROM Lab: cannot create trace '%s': %s\n",
                    options.TracePath.c_str(), strerror(errno));
            return 1;
        }
        if (!RomLabWriteTraceHeader(trace,
                rom.GetRomTitle() ? rom.GetRomTitle() : "",
                rom.GetMapperName() ? rom.GetMapperName() : "",
                rom.m_eVideoType == SNROM_VIDEO_PAL ? "PAL" : "NTSC",
                identity.CRC32, identity.Bytes, identity.Flags,
                (uint32_t)sizeof(SnesStateT), options.Video, &error))
        {
            fclose(trace);
            fprintf(stderr, "ROM Lab: %s\n", error.c_str());
            return 1;
        }
    }

    if (!options.Quiet)
    {
        printf("SNESticle ROM Lab v%s\n", kVersion);
        printf("ROM: %s | %s | %s | CRC32 %08X | %u bytes\n",
               rom.GetRomTitle() ? rom.GetRomTitle() : "",
               rom.GetMapperName() ? rom.GetMapperName() : "",
               rom.m_eVideoType == SNROM_VIDEO_PAL ? "PAL" : "NTSC",
               identity.CRC32, identity.Bytes);
        printf("Start: frame=%u | capture=%llu | warmup=%llu | video=%s | "
               "state-bytes=%zu\n",
               initial->uFrame, (unsigned long long)options.Frames,
               (unsigned long long)options.Warmup,
               options.Video ? "on" : "off", sizeof(SnesStateT));
    }

    RunResult result = RunOnce(rom, options, *initial, movie,
        reference.empty() ? NULL : &reference, trace, &error);
    if (!error.empty())
    {
        if (trace) fclose(trace);
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }

    RomLabStallResult stall = RomLabDetectStall(result.Records,
                                                options.StallWindow);
    if (!result.Diverged && !options.DumpDirectory.empty() &&
        result.FinalState && !result.Records.empty() &&
        !DumpArtifacts(options, "final", result.Records.back().Index,
                       *result.FinalState, result.LastVideo, &error))
    {
        if (trace) fclose(trace);
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }
    if (trace)
    {
        RomLabWriteTraceSummary(trace, result.Records.size(),
            result.CoreNanoseconds, result.Diverged,
            result.DivergenceIndex, stall.Possible, &error);
        if (fclose(trace) && error.empty()) error = "cannot close trace";
        if (!error.empty())
        {
            fprintf(stderr, "ROM Lab: %s\n", error.c_str());
            return 1;
        }
    }

    if (result.Diverged)
    {
        fprintf(stderr,
            "DIVERGENCE at capture index %llu: %s\n",
            (unsigned long long)result.DivergenceIndex,
            result.Difference.c_str());
    }

    if (options.VerifyDeterminism && !result.Diverged)
    {
        Options verifyOptions = options;
        verifyOptions.TracePath.clear();
        verifyOptions.ReferencePath.clear();
        RunResult verification = RunOnce(rom, verifyOptions, *initial, movie,
                                          &result.Records, NULL, &error);
        if (!error.empty())
        {
            fprintf(stderr, "ROM Lab: %s\n", error.c_str());
            return 1;
        }
        if (verification.Diverged ||
            verification.Records.size() != result.Records.size())
        {
            fprintf(stderr,
                "NONDETERMINISTIC at capture index %llu: %s\n",
                (unsigned long long)verification.DivergenceIndex,
                verification.Difference.c_str());
            return 3;
        }
        if (!options.Quiet)
            printf("Determinism: exact match across %zu frames.\n",
                   result.Records.size());
    }

    if (!options.CheckpointPath.empty())
    {
        if (!result.FinalState || !RomLabStateIO::SaveStateFile(
                options.CheckpointPath, identity, *result.FinalState,
                0, 1, &error))
        {
            fprintf(stderr, "ROM Lab: %s\n", error.c_str());
            return 1;
        }
        if (!options.Quiet)
            printf("Checkpoint: %s\n", options.CheckpointPath.c_str());
    }

    if (!options.Quiet)
        PrintRunSummary(options, result, stall);

    if (result.Diverged)
        return 2;
    if (RomLabHooks::GetUnimplementedSpcCount() ||
        RomLabHooks::GetUnimplementedCpuCount())
        return 4;
    return 0;
}

static int DiffCommand(const std::string &leftPath,
                       const std::string &rightPath)
{
    std::vector<RomLabFrameRecord> left;
    std::vector<RomLabFrameRecord> right;
    uint32_t leftCRC = 0, rightCRC = 0;
    std::string error;
    if (!RomLabLoadTrace(leftPath, &left, &leftCRC, &error) ||
        !RomLabLoadTrace(rightPath, &right, &rightCRC, &error))
    {
        fprintf(stderr, "ROM Lab: %s\n", error.c_str());
        return 1;
    }
    if (leftCRC && rightCRC && leftCRC != rightCRC)
    {
        fprintf(stderr, "ROM Lab: traces use different ROMs (%08X != %08X)\n",
                leftCRC, rightCRC);
        return 1;
    }
    size_t common = std::min(left.size(), right.size());
    for (size_t i = 0; i < common; ++i)
    {
        std::string difference = RomLabCompareFrames(left[i], right[i]);
        if (!difference.empty())
        {
            printf("DIVERGENCE index=%zu frame=%u: %s\n", i,
                   right[i].EmulatedFrame, difference.c_str());
            return 2;
        }
    }
    if (left.size() != right.size())
    {
        printf("DIVERGENCE index=%zu: trace lengths differ (%zu != %zu)\n",
               common, left.size(), right.size());
        return 2;
    }
    printf("MATCH: %zu frame checkpoints are identical.\n", common);
    return 0;
}

static std::vector<uint8_t> BuildSelfTestRom()
{
    std::vector<uint8_t> rom(0x8000, 0xFF);
    rom[0x0000] = 0x78; // SEI
    rom[0x0001] = 0x80; // BRA -2
    rom[0x0002] = 0xFE;

    SNRomInfoT *header = (SNRomInfoT *)&rom[0x7FC0];
    memset(header, 0, sizeof(*header));
    memset(header->Title, ' ', sizeof(header->Title));
    memcpy(header->Title, "ROMLAB SELF TEST", 16);
    header->RomMakeup = 0x20;
    header->RomType = 0;
    header->RomSize = 5;
    header->SRAMSize = 0;
    header->Country = 1;
    header->License = 0x33;
    header->Checksum = 0x1234;
    header->InverseChecksum = (uint16_t)~header->Checksum;
    for (size_t vector = 0x7FFA; vector <= 0x7FFE; vector += 2)
    {
        rom[vector] = 0x00;
        rom[vector + 1] = 0x80;
    }
    return rom;
}

static bool CheckExHiRomSramMirrors()
{
    std::vector<uint8_t> image(0x600000, 0xFF);
    SNRomInfoT *header = (SNRomInfoT *)&image[0x40FFC0];
    memset(header, 0, sizeof(*header));
    memset(header->Title, ' ', sizeof(header->Title));
    memcpy(header->Title, "ROMLAB EXHI SRAM", 17);
    header->RomMakeup = 0x35;
    header->RomType = 0x02;
    header->RomSize = 0x0D;
    header->SRAMSize = 0x03;
    header->Country = 1;
    header->License = 0x33;
    header->Checksum = 0x3456;
    header->InverseChecksum = (uint16_t)~header->Checksum;

    /* CPU $00:FF00 and its reset vector both live in the extended half. */
    image[0x40FF00] = 0x78; // SEI
    image[0x40FF01] = 0x80; // BRA -2
    image[0x40FF02] = 0xFE;
    image[0x40FFFC] = 0x00;
    image[0x40FFFD] = 0xFF;

    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE ||
        rom.m_eMapping != SNROM_MAPPING_EXHIROM)
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    if (system.GetSRAMBytes() != 8192)
        return false;

    /* Mesen's compatibility map exposes the strict $80-$BF window and the
       $20-$3F mirror used by expanded translations. Both must reach the
       same physical SRAM byte. */
    SNCPUWrite8(system.GetCpu(), 0x307808, 0x5A);
    if (SNCPURead8(system.GetCpu(), 0xB07808) != 0x5A ||
        system.GetSRAMData()[0x1808] != 0x5A)
        return false;
    SNCPUWrite8(system.GetCpu(), 0xB07FFF, 0xA5);
    return SNCPURead8(system.GetCpu(), 0x307FFF) == 0xA5;
}

static bool CheckSRAM128KBoardMap()
{
    std::vector<uint8_t> image(0x400000, 0xFF);
    image[0x0000] = 0x78; // SEI

    SNRomInfoT *header = (SNRomInfoT *)&image[0x7FC0];
    memset(header, 0, sizeof(*header));
    memset(header->Title, ' ', sizeof(header->Title));
    memcpy(header->Title, "RPG-TCOOL 2", 11);
    header->RomMakeup = 0x20;
    header->RomType = 0x02;
    header->RomSize = 0x0C;
    header->SRAMSize = 7; // 1 Mbit = 128 KiB
    header->Country = 1;
    header->License = 0x33;
    header->Checksum = 0x2468;
    header->InverseChecksum = (uint16_t)~header->Checksum;
    image[0x7FFC] = 0x00;
    image[0x7FFD] = 0x80;

    /* These low-half addresses must remain ROM on the special board. */
    image[0x3A0123] = 0xC7; // CPU $74:0123
    image[0x380456] = 0xD9; // CPU $F0:0456

    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE ||
        !(rom.m_Flags & SNROM_FLAG_SRAM128K_SPECIAL))
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();

    if (system.GetSRAMBytes() != 0x20000)
        return false;

    memset(system.GetSRAMData(), 0, SNES_SRAMSIZE);

    SNCPUWrite8(system.GetCpu(), 0x700123, 0x11);
    SNCPUWrite8(system.GetCpu(), 0x710123, 0x22);
    SNCPUWrite8(system.GetCpu(), 0x720123, 0x33);
    SNCPUWrite8(system.GetCpu(), 0x730123, 0x44);

    if (system.GetSRAMData()[0x00123] != 0x11 ||
        system.GetSRAMData()[0x08123] != 0x22 ||
        system.GetSRAMData()[0x10123] != 0x33 ||
        system.GetSRAMData()[0x18123] != 0x44)
        return false;

    /* Adjacent 64-KiB windows overlap by 32 KiB. */
    SNCPUWrite8(system.GetCpu(), 0x709000, 0x5A);
    if (SNCPURead8(system.GetCpu(), 0x711000) != 0x5A)
        return false;

    /* Unlike generic LoROM SRAM decode, these regions are ROM. */
    if (SNCPURead8(system.GetCpu(), 0x740123) != 0xC7 ||
        SNCPURead8(system.GetCpu(), 0xF00456) != 0xD9)
        return false;

    return true;
}

static bool CheckDezaemonStandardSramMap()
{
    std::vector<uint8_t> image(0x200000, 0xFF);

    SNRomInfoT *header = (SNRomInfoT *)&image[0x7FC0];
    memset(header, 0, sizeof(*header));
    memset(header->Title, ' ', sizeof(header->Title));
    memcpy(header->Title, "DEZAEMON  ", 10);
    header->RomMakeup = 0x20;
    header->RomType = 0x02;
    header->RomSize = 0x0B;
    header->SRAMSize = 7; // 128 KiB
    header->Country = 1;
    header->License = 0x33;
    header->Checksum = 0x2D5A;
    header->InverseChecksum = (uint16_t)~header->Checksum;
    image[0x7FFC] = 0x00;
    image[0x7FFD] = 0x80;

    /* Standard LoROM keeps the upper half as ROM even on 128-KiB SRAM carts. */
    image[0x180123] = 0xC3;

    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE ||
        rom.m_eMapping != SNROM_MAPPING_LOROM ||
        (rom.m_Flags & SNROM_FLAG_SRAM128K_SPECIAL))
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    if (system.GetSRAMBytes() != 0x20000)
        return false;

    memset(system.GetSRAMData(), 0, SNES_SRAMSIZE);

    /* Four 32-KiB slices span the physical 128 KiB, then repeat. */
    SNCPUWrite8(system.GetCpu(), 0x700123, 0x11);
    SNCPUWrite8(system.GetCpu(), 0x710123, 0x22);
    SNCPUWrite8(system.GetCpu(), 0x720123, 0x33);
    SNCPUWrite8(system.GetCpu(), 0x730123, 0x44);
    if (system.GetSRAMData()[0x00123] != 0x11 ||
        system.GetSRAMData()[0x08123] != 0x22 ||
        system.GetSRAMData()[0x10123] != 0x33 ||
        system.GetSRAMData()[0x18123] != 0x44)
        return false;

    /* $74 repeats the first 32-KiB slice, and the $F0 mirror starts there too. */
    if (SNCPURead8(system.GetCpu(), 0x740123) != 0x11 ||
        SNCPURead8(system.GetCpu(), 0xF00123) != 0x11)
        return false;

    /* Upper halves remain cartridge ROM, unlike the old dead Dezaemon hack. */
    if (SNCPURead8(system.GetCpu(), 0x708123) != 0xC3 ||
        SNCPURead8(system.GetCpu(), 0xF08123) != 0xC3)
        return false;

    return true;
}

static bool CheckNoMAD1Map()
{
    std::vector<uint8_t> image(0x200000, 0xFF);
    SNRomInfoT *header = (SNRomInfoT *)&image[0x7FC0];
    memset(header, 0, sizeof(*header));
    memset(header->Title, ' ', sizeof(header->Title));
    memcpy(header->Title, "WANDERERS FROM YS", 17);
    header->RomMakeup = 0x20;
    header->RomType = 0x02;
    header->RomSize = 0x0B;
    header->SRAMSize = 3; // 8 KiB
    header->Country = 1;
    header->License = 0x33;
    header->Checksum = 0x7953;
    header->InverseChecksum = (uint16_t)~header->Checksum;
    image[0x7FFC] = 0x00;
    image[0x7FFD] = 0x80;

    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE ||
        !(rom.m_Flags & SNROM_FLAG_NOMAD1))
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    if (system.GetSRAMBytes() != 0x2000)
        return false;

    memset(system.GetSRAMData(), 0, SNES_SRAMSIZE);

    /* Exactly 2 MiB deliberately bypasses Revive's generic small-ROM
       full-bank SRAM compatibility path. This write succeeds only because
       the explicit NoMAD1 board overlay owns the upper half of bank $70. */
    SNCPUWrite8(system.GetCpu(), 0x708123, 0xA5);
    if (system.GetSRAMData()[0x0123] != 0xA5 ||
        SNCPURead8(system.GetCpu(), 0xF08123) != 0xA5)
        return false;

    /* $7E/$7F remain WRAM because the system map overrides cartridge decode. */
    SNCPUWrite8(system.GetCpu(), 0x7E8123, 0x5C);
    if (system.GetSRAMData()[0x0123] != 0xA5 ||
        SNCPURead8(system.GetCpu(), 0x7E8123) != 0x5C)
        return false;

    return true;
}

static bool CheckSDD1MapAndSpeed()
{
    SNSDD1 chip;
    chip.ClearMapDirty();
    chip.WriteReg(0x4804, chip.BankSegment(0));
    if (chip.MapDirty()) return false;
    chip.WriteReg(0x4804, 5);
    chip.WriteReg(0x4804, 5);
    if (!chip.MapDirty()) return false; // repeated write cannot cancel a change
    chip.ClearMapDirty();
    chip.WriteReg(0x4800, 0xFF);
    chip.WriteReg(0x4801, 0xA5);
    if (chip.MapDirty() || !chip.DmaEnabled(0) || chip.DmaEnabled(1))
        return false;

    std::vector<uint8_t> image = BuildSelfTestRom();
    image.resize(0x600000, 0xFF);
    SNRomInfoT *header = (SNRomInfoT *)&image[0x7FC0];
    header->RomMakeup = 0x32;
    header->RomType = 0x45;
    header->RomSize = 0x0D;
    for (Uint32 segment = 0; segment < 6; segment++)
        for (Uint32 page = 0; page < 128; page++)
            image[segment * 0x100000 + page * SNCPU_BANK_SIZE + 0x100] =
                (Uint8)(segment * 37 + page);

    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE ||
        !(rom.m_Flags & SNROM_FLAG_SDD1)) return false;
    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    SNCpuT *cpu = system.GetCpu();
    Uint8 expectedSegments[4] = { 0, 1, 2, 3 };
    for (Uint32 fast = 0; fast < 2; fast++)
    {
        SNCPUWrite8(cpu, 0x420D, (Uint8)fast);
        for (Uint32 group = 0; group < 4; group++)
        {
            Uint8 segment = (Uint8)(fast ? group : 5 - group);
            SNCPUWrite8(cpu, 0x4804 + group, segment);
            SNCPUWrite8(cpu, 0x4804 + group, segment);
            expectedSegments[group] = segment;
            for (Uint32 checkGroup = 0; checkGroup < 4; checkGroup++)
            {
                for (Uint32 page = 0; page < 128; page++)
                {
                    Uint32 address = 0xC00000 + checkGroup * 0x100000 +
                                     page * SNCPU_BANK_SIZE + 0x100;
                    if (SNCPURead8(cpu, address) !=
                            (Uint8)(expectedSegments[checkGroup] * 37 + page) ||
                        cpu->Bank[address >> SNCPU_BANK_SHIFT].uBankCycle !=
                            (fast ? SNCPU_CYCLE_FAST : SNCPU_CYCLE_SLOW))
                        return false;
                }
            }
        }
    }
    SNCPUWrite8(cpu, 0x4804, 5);
    system.Reset();
    for (Uint32 group = 0; group < 4; group++)
    {
        SNCPUWrite8(cpu, 0x4804 + group, (Uint8)group);
        for (Uint32 page = 0; page < 128; page++)
        {
            Uint32 address = 0xC00000 + group * 0x100000 +
                             page * SNCPU_BANK_SIZE + 0x100;
            if (SNCPURead8(cpu, address) != (Uint8)(group * 37 + page) ||
                cpu->Bank[address >> SNCPU_BANK_SHIFT].uBankCycle != SNCPU_CYCLE_SLOW)
                return false;
        }
    }
    return true;
}

static bool CheckSuperScopeBeamLatch()
{
    std::vector<uint8_t> image = BuildSelfTestRom();
    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());

    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE)
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();

    Emu::SysInputT input;
    for (size_t i = 0; i < EMUSYS_DEVICE_NUM; ++i)
        input.uPad[i] = EMUSYS_DEVICE_DISCONNECTED;
    input.uPad[4] = EMUSYS_SNES_SPECIAL_SUPERSCOPE;
    /* X=255 fires at (255+24)*4 = 1116 master clocks, inside HBlank.
       This validates that the optical event survives the HDMA/CPU boundary. */
    input.uPad[2] = 255u | (77u << 8);
    input.uPad[3] = 0;

    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);

    SnesPPU *ppu = system.GetPPU();
    Uint8 stat = ppu->Read8(0x213F, 0, FALSE);
    if (!(stat & 0x40))
        return false;

    Uint16 h = ppu->Read8(0x213C);
    h |= (Uint16)(ppu->Read8(0x213C) & 1u) << 8;
    Uint16 v = ppu->Read8(0x213D);
    v |= (Uint16)(ppu->Read8(0x213D) & 1u) << 8;

    return h == 255u && v == 77u;
}

static bool CheckJustifierBeamLatch()
{
    std::vector<uint8_t> image = BuildSelfTestRom();
    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());

    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE)
        return false;

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();

    Emu::SysInputT input;
    for (size_t i = 0; i < EMUSYS_DEVICE_NUM; ++i)
        input.uPad[i] = EMUSYS_DEVICE_DISCONNECTED;
    input.uPad[4] = EMUSYS_SNES_SPECIAL_JUSTIFIER;
    input.uPad[2] = 200u | (91u << 8);
    input.uPad[1] = (Uint16)(0xFFF0u | EMUSYS_SNES_JUSTIFIER1_TRIGGER);

    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);

    SnesPPU *ppu = system.GetPPU();
    Uint8 stat = ppu->Read8(0x213F, 0, FALSE);
    if (!(stat & 0x40))
        return false;

    Uint16 h = ppu->Read8(0x213C);
    h |= (Uint16)(ppu->Read8(0x213C) & 1u) << 8;
    Uint16 v = ppu->Read8(0x213D);
    v |= (Uint16)(ppu->Read8(0x213D) & 1u) << 8;

    return h == 200u && v == 91u;
}

static int SelfTestCommand()
{
    if (!CheckSDD1MapAndSpeed())
    {
        fprintf(stderr, "ROM Lab self-test: S-DD1 map/FastROM timing failed\n");
        return 1;
    }

    if (!CheckExHiRomSramMirrors())
    {
        fprintf(stderr, "ROM Lab self-test: ExHiROM SRAM mirrors failed\n");
        return 1;
    }

    if (!CheckSRAM128KBoardMap())
    {
        fprintf(stderr, "ROM Lab self-test: 128-KiB SRAM board map failed\n");
        return 1;
    }

    if (!CheckDezaemonStandardSramMap())
    {
        fprintf(stderr, "ROM Lab self-test: Dezaemon standard LoROM SRAM map failed\n");
        return 1;
    }

    if (!CheckNoMAD1Map())
    {
        fprintf(stderr, "ROM Lab self-test: NoMAD1 full-bank SRAM map failed\n");
        return 1;
    }

    if (!CheckSuperScopeBeamLatch())
    {
        fprintf(stderr, "ROM Lab self-test: Super Scope beam latch failed\n");
        return 1;
    }

    if (!CheckJustifierBeamLatch())
    {
        fprintf(stderr, "ROM Lab self-test: Justifier beam latch failed\n");
        return 1;
    }

    std::vector<uint8_t> image = BuildSelfTestRom();
    CMemFileIO file;
    file.Open(image.data(), (Uint32)image.size());
    SnesRom rom;
    if (rom.LoadRom(&file) != Emu::Rom::LOADERROR_NONE)
    {
        fprintf(stderr, "ROM Lab self-test: synthetic ROM did not load\n");
        return 1;
    }

    SnesSystem system;
    system.SetSnesRom(&rom);
    system.Reset();
    Emu::SysInputT input;
    input.uPad[0] = 0;
    for (size_t i = 1; i < EMUSYS_DEVICE_NUM; ++i)
        input.uPad[i] = EMUSYS_DEVICE_DISCONNECTED;

    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    std::unique_ptr<SnesStateT> checkpoint(new SnesStateT);
    std::unique_ptr<SnesStateT> expected(new SnesStateT);
    std::unique_ptr<SnesStateT> actual(new SnesStateT);
    system.SaveState(checkpoint.get());
    if (checkpoint->CPU.Regs.rPC == 0x008000u ||
        checkpoint->SPC.Regs.rPC == 0xFFC0u)
    {
        fprintf(stderr,
                "ROM Lab self-test: CPU executor did not advance the core\n");
        return 1;
    }
    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    system.SaveState(expected.get());
    if (!system.RestoreState(checkpoint.get()))
    {
        fprintf(stderr, "ROM Lab self-test: state restore failed\n");
        return 1;
    }
    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    system.ExecuteFrame(&input, NULL, NULL,
                        Emu::System::MODE_ACCURATEDETERMINISTIC);
    system.SaveState(actual.get());
    if (memcmp(expected.get(), actual.get(), sizeof(SnesStateT)))
    {
        fprintf(stderr, "ROM Lab self-test: replay state diverged\n");
        return 1;
    }

    const uint64_t stamp = (uint64_t)
        std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base = std::filesystem::temp_directory_path() /
        ("snesticle-romlab-selftest-" + std::to_string(stamp));
    std::filesystem::path romPath = base;
    std::filesystem::path moviePath = base;
    std::filesystem::path tracePath = base;
    std::filesystem::path statePath = base;
    std::filesystem::path rawStatePath = base;
    romPath += ".sfc";
    moviePath += ".input";
    tracePath += ".jsonl";
    statePath += ".state";
    rawStatePath += ".raw.state";
    std::vector<std::filesystem::path> temporary = {
        romPath, moviePath, tracePath, statePath, rawStatePath
    };
    auto cleanup = [&temporary]() {
        std::error_code ignored;
        for (size_t i = 0; i < temporary.size(); ++i)
            std::filesystem::remove(temporary[i], ignored);
    };
    auto fail = [&cleanup](const std::string &message) {
        cleanup();
        fprintf(stderr, "ROM Lab self-test: %s\n", message.c_str());
        return 1;
    };

    FILE *romFile = fopen(romPath.string().c_str(), "wb");
    bool romWritten = romFile &&
        fwrite(image.data(), 1, image.size(), romFile) == image.size();
    if (romFile && fclose(romFile)) romWritten = false;
    if (!romWritten)
    {
        return fail("cannot create temporary synthetic ROM");
    }
    FILE *movieFile = fopen(moviePath.string().c_str(), "wb");
    bool movieWritten = movieFile &&
        fputs("0 A+B\n2 NONE\n3 A\n4 B\n5 0x8000\n", movieFile) >= 0;
    if (movieFile && fclose(movieFile)) movieWritten = false;
    if (!movieWritten)
    {
        return fail("cannot create temporary input movie");
    }

    std::string error;
    SnesRom diskRom;
    if (!OpenRom(romPath.string(), &diskRom, &error))
        return fail(error);
    InputMovie diskMovie;
    if (!diskMovie.Load(moviePath.string(), &error))
        return fail(error);
    if (diskMovie.Get(0).uPad[0] != (SNESIO_JOY_A | SNESIO_JOY_B) ||
        diskMovie.Get(1).uPad[0] != (SNESIO_JOY_A | SNESIO_JOY_B) ||
        diskMovie.Get(2).uPad[0] != 0 ||
        diskMovie.Get(3).uPad[0] != SNESIO_JOY_A ||
        diskMovie.Get(4).uPad[0] != SNESIO_JOY_B ||
        diskMovie.Get(5).uPad[0] != SNESIO_JOY_B)
        return fail("input movie did not preserve sparse controller state");

    Options options;
    options.RomPath = romPath.string();
    options.InputPath = moviePath.string();
    options.Frames = 5;
    options.StallWindow = 0;
    std::unique_ptr<SnesStateT> initial(new SnesStateT);
    if (!PrepareInitialState(diskRom, options, initial.get(), &error))
        return fail(error);

    FILE *traceFile = fopen(tracePath.string().c_str(), "wb");
    RomLabStateIO::RomIdentity identity = RomLabStateIO::Identify(diskRom);
    if (!traceFile || !RomLabWriteTraceHeader(traceFile,
            diskRom.GetRomTitle() ? diskRom.GetRomTitle() : "",
            diskRom.GetMapperName() ? diskRom.GetMapperName() : "",
            "NTSC", identity.CRC32, identity.Bytes, identity.Flags,
            (uint32_t)sizeof(SnesStateT), true, &error))
    {
        if (traceFile) fclose(traceFile);
        return fail(error.empty() ? "cannot create trace" : error);
    }
    RunResult first = RunOnce(diskRom, options, *initial, diskMovie,
                              NULL, traceFile, &error);
    if (error.empty())
        RomLabWriteTraceSummary(traceFile, first.Records.size(),
            first.CoreNanoseconds, false, 0, false, &error);
    if (fclose(traceFile) && error.empty())
        error = "cannot close trace";
    if (!error.empty() || first.Records.size() != options.Frames)
        return fail(error.empty() ? "whole-core run returned too few frames" : error);
    if (!first.Records[0].Instructions.CpuInstructions ||
        !first.Records[0].Instructions.SpcInstructions)
        return fail("whole-core trace recorded zero executed instructions");

    std::vector<RomLabFrameRecord> loadedTrace;
    uint32_t loadedCRC = 0;
    if (!RomLabLoadTrace(tracePath.string(), &loadedTrace, &loadedCRC, &error) ||
        loadedCRC != identity.CRC32 || loadedTrace.size() != first.Records.size() ||
        !RomLabCompareFrames(first.Records[0], loadedTrace[0]).empty())
        return fail(error.empty() ? "trace round-trip mismatch" : error);

    if (!RomLabStateIO::SaveRawState(rawStatePath.string(), *first.FinalState,
                                     &error) ||
        !RomLabStateIO::SaveStateFile(statePath.string(), identity,
                                      *first.FinalState, 0, 1, &error))
        return fail(error);
    std::unique_ptr<SnesStateT> loadedState(new SnesStateT);
    if (!RomLabStateIO::LoadState(statePath.string(), identity, false,
                                  loadedState.get(), &error) ||
        memcmp(first.FinalState.get(), loadedState.get(), sizeof(SnesStateT)))
        return fail(error.empty() ? "state container round-trip mismatch" : error);
    if (!RomLabStateIO::LoadState(rawStatePath.string(), identity, false,
                                  loadedState.get(), &error) ||
        memcmp(first.FinalState.get(), loadedState.get(), sizeof(SnesStateT)))
        return fail(error.empty() ? "raw state round-trip mismatch" : error);

    RunResult second = RunOnce(diskRom, options, *initial, diskMovie,
                               &loadedTrace, NULL, &error);
    if (!error.empty() || second.Diverged ||
        second.Records.size() != first.Records.size())
        return fail(error.empty() ? "deterministic trace replay diverged" : error);

    cleanup();
    printf("ROM Lab self-test: PASS (state-bytes=%zu frame=%u CPU=%06X SPC=%04X)\n",
           sizeof(SnesStateT), actual->uFrame,
           actual->CPU.Regs.rPC & 0xFFFFFFu, actual->SPC.Regs.rPC);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
	/* The PS2 frontend selects its CPU backends before entering the main
	   loop. ROM Lab has no frontend, so select the same portable executors
	   explicitly; otherwise the library defaults merely consume each slice
	   without executing a single instruction. */
	SNCPUSetExecuteFunc(SNCPUExecute_C);
	SNSPCSetExecuteFunc(SNSPCExecute_C);

	/* The PS2 frontend builds this 15-bit-to-RGB table during boot. ROM Lab
	   uses the same portable renderer directly, so initialise the table here
	   before any frame can be captured. The original profile preserves the
	   emulator's established RGB output. */
	SNPPUColorCalibT calib = { 0.9f, 20.0f, 0.2f };
	SNPPUColorSetProfile(SNPPU_COLOR_PROFILE_ORIGINAL);
	SNPPUColorCalibrate(&calib);

    if (argc < 2)
    {
        Usage(argv[0]);
        return 2;
    }
    std::string command = argv[1];
    if (command == "self-test")
        return SelfTestCommand();
    if (command == "diff")
    {
        if (argc != 4)
        {
            Usage(argv[0]);
            return 2;
        }
        return DiffCommand(argv[2], argv[3]);
    }
    if (command == "run")
    {
        Options options;
        if (!ParseRunOptions(argc, argv, 2, &options))
        {
            Usage(argv[0]);
            return 2;
        }
        return RunCommand(options);
    }
    if (command == "--help" || command == "-h" || command == "help")
    {
        Usage(argv[0]);
        return 0;
    }

    /* Compact compatibility form: romlab GAME.sfc [FRAMES]. */
    Options options;
    options.RomPath = command;
    if (argc >= 3 && !ParseUnsigned(argv[2], &options.Frames))
    {
        Usage(argv[0]);
        return 2;
    }
    if (argc > 3)
    {
        Usage(argv[0]);
        return 2;
    }
    return RunCommand(options);
}
