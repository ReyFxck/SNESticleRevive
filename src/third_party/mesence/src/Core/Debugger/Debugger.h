#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include "DebugTypes.h"
#include "DebugUtilities.h"
#include "Shared/EventType.h"
#include "Shared/MemoryOperationType.h"

// Forward declarations
class Emulator;
class IConsole;

// Minimal EventManager stub for GetEventManager to return
class EventManager {
public:
    void AddEvent(DebugEventType /*evtType*/) {}
};

class Debugger {
public:
    Debugger() {}
    Debugger(Emulator*, IConsole*) {}
    ~Debugger() {}
    void Initialize() {}
    void Shutdown() {}
    void Reset() {}
    void Execute(uint32_t cycles) {}
    void ResetSuspendCounter() {}
    void PauseOnNextFrame() {}
    void Step(CpuType, int, StepType, BreakSource) {}
    void Run() {}
    bool IsPaused() { return false; }
    void SuspendDebugger(bool) {}
    bool HasBreakRequest() { return false; }
    void BreakImmediately(CpuType, BreakSource) {}
    void ProcessEvent(EventType, std::optional<CpuType>) {}
    void ProcessConfigChange() {}
    void Log(const std::string&) {}
    void Release() {}
    template<CpuType> void ProcessInstruction() {}
    template<CpuType, uint8_t, uint8_t> void ProcessMemoryRead(uint32_t, void*, int) {}
    template<CpuType, uint8_t, uint8_t> bool ProcessMemoryWrite(uint32_t, void*, int) { return false; }
    template<CpuType, uint8_t, uint8_t, typename T> void ProcessMemoryAccess(uint32_t, T) {}
    template<CpuType> void ProcessIdleCycle() {}
    template<CpuType> void ProcessHaltedCpu() {}
    // Updated to match Emulator calls: now take T& for the value
    template<CpuType, typename T> void ProcessPpuRead(uint32_t, T&, MemoryType, MemoryOperationType) {}
    template<CpuType, typename T> void ProcessPpuWrite(uint32_t, T&, MemoryType) {}
    template<CpuType> void ProcessPpuCycle() {}
    template<CpuType> void ProcessInterrupt(uint32_t, uint32_t, bool) {}
    // New method: GetEventManager
    EventManager* GetEventManager(CpuType) { return &_eventManager; }
private:
    EventManager _eventManager;
};
