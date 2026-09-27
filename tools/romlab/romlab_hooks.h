#ifndef SNESTICLE_ROMLAB_HOOKS_H
#define SNESTICLE_ROMLAB_HOOKS_H

#include <stdint.h>
#include <stdio.h>
#include <string>

struct SNCpu_t;

namespace RomLabHooks {

struct FrameStats
{
    uint64_t CpuInstructions;
    uint64_t Sa1Instructions;
    uint64_t SpcInstructions;
    uint32_t UnimplementedCpuInstructions;
    uint32_t UnimplementedSa1Instructions;
    uint32_t UnimplementedSpcInstructions;
    uint32_t UnmappedReads;
    uint32_t UnmappedWrites;
    uint32_t UnhandledIoReads;
    uint32_t UnhandledIoWrites;
    uint32_t LastUnhandledAddress;
    uint32_t LastUnimplementedCpuPC;
    uint16_t LastUnimplementedSpcPC;
    uint8_t LastUnimplementedCpuOpcode;
    uint8_t LastUnimplementedCpuWasSa1;
    uint8_t LastUnimplementedSpcOpcode;
    uint8_t LastUnhandledValue;
    uint8_t LastUnhandledFlags;
};

void Reset();
void SetMainCpu(SNCpu_t *cpu);
void BeginFrame();
FrameStats GetFrameStats();
uint64_t GetUnimplementedSpcCount();
std::string GetUnimplementedSpcSummary();
uint64_t GetUnimplementedCpuCount();
std::string GetUnimplementedCpuSummary();
uint64_t GetUnhandledAccessCount();
std::string GetUnhandledAccessSummary(size_t limit = 12);
void DumpRecent(FILE *file);

} // namespace RomLabHooks

#endif
