#pragma once
#include <cstdint>
#include "AddressInfo.h"
#include "Shared/CpuType.h"
#include "Shared/MemoryType.h"
#include "Shared/MemoryOperationType.h"

enum class DebugEventType {
    None = 0
};

enum class BreakSource {
    None = 0,
    Pause = 1
};

enum class StepType {
    None = 0,
    Step = 1
};
