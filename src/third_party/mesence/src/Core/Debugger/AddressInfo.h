#pragma once
#include <cstdint>

enum class MemoryType;

struct AddressInfo {
    int32_t Address;
    MemoryType Type;
};
