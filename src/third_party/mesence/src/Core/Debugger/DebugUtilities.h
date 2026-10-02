#pragma once
#include "DebugTypes.h"

class DebugUtilities {
public:
    static constexpr MemoryType GetCpuMemoryType(CpuType) {
        return MemoryType::None;
    }

    static constexpr int GetMemoryTypeCount() {
        // This count sizes live console memory, even with the debugger off.
        return static_cast<int>(MemoryType::None) + 1;
    }

    static constexpr bool IsRom(MemoryType) {
        return false;
    }

    static constexpr int GetProgramCounterSize(CpuType) {
        return 0;
    }
};
