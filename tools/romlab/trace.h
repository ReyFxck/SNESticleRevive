#ifndef SNESTICLE_ROMLAB_TRACE_H
#define SNESTICLE_ROMLAB_TRACE_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "types.h"
#include "snes.h"
#include "romlab_hooks.h"
#include "snstate.h"

struct RomLabFrameRecord
{
    uint64_t Index;
    uint32_t EmulatedFrame;
    uint64_t CoreNanoseconds;

    uint32_t StateHash;
    uint32_t CpuHash;
    uint32_t WRamHash;
    uint32_t SramHash;
    uint32_t IoHash;
    uint32_t DmaHash;
    uint32_t PpuHash;
    uint32_t PpuRegsHash;
    uint32_t VramHash;
    uint32_t CgramHash;
    uint32_t OamHash;
    uint32_t SpcHash;
    uint32_t SpcRamHash;
    uint32_t SpcIoHash;
    uint32_t DspHash;
    uint32_t Sa1Hash;
    uint32_t VideoHash;

    uint32_t CpuPC;
    uint16_t SpcPC;
    uint8_t CpuToSpc[4];
    uint8_t SpcToCpu[4];
    uint8_t PpuMode;
    uint8_t MainLayers;
    uint8_t SubLayers;
    uint8_t Brightness;

    RomLabHooks::FrameStats Instructions;
};

RomLabFrameRecord RomLabBuildFrameRecord(uint64_t index,
    uint64_t coreNanoseconds, const SnesStateT &state,
    uint32_t videoHash, size_t activeSramBytes,
    const RomLabHooks::FrameStats &instructions);

std::string RomLabCompareFrames(const RomLabFrameRecord &expected,
                                const RomLabFrameRecord &actual);

bool RomLabWriteTraceHeader(FILE *file, const std::string &title,
    const std::string &mapper, const std::string &region,
    uint32_t romCRC, uint32_t romBytes, uint32_t romFlags,
    uint32_t stateBytes, bool video, std::string *error);
bool RomLabWriteTraceFrame(FILE *file, const RomLabFrameRecord &record,
                           std::string *error);
bool RomLabWriteTraceSummary(FILE *file, uint64_t frames,
    uint64_t coreNanoseconds, bool divergence, uint64_t divergenceFrame,
    bool possibleStall, std::string *error);

bool RomLabLoadTrace(const std::string &path,
    std::vector<RomLabFrameRecord> *records, uint32_t *romCRC,
    std::string *error);

struct RomLabStallResult
{
    bool Possible;
    size_t DistinctCpuPCs;
    uint64_t FirstIndex;
    uint64_t LastIndex;
};

RomLabStallResult RomLabDetectStall(
    const std::vector<RomLabFrameRecord> &records, size_t window);

#endif
