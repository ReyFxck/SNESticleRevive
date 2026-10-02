#include "pch.h"
#ifdef PS2_PORT

#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/NotificationManager.h"
#include "Shared/BatteryManager.h"
#include "Shared/CheatManager.h"
#include "Shared/SystemActionManager.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/BaseControlManager.h"
#include "Shared/RomInfo.h"
#include "Shared/TimingInfo.h"
#include "Shared/EmulatorLock.h"
#include "Shared/DebuggerRequest.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "Shared/Interfaces/IInputRecorder.h"
#include "NES/NesConsole.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"

#include "Debugger/Debugger.h"

Emulator::Emulator() :
    _settings(new EmuSettings(this)),
    _debugHud(nullptr),
    _scriptHud(nullptr),
    _notificationManager(new NotificationManager()),
    _batteryManager(new BatteryManager()),
    _soundMixer(new SoundMixer(this)),
    _videoRenderer(nullptr),
    _videoDecoder(nullptr),
    _saveStateManager(nullptr),
    _cheatManager(new CheatManager(this)),
    _movieManager(nullptr),
    _historyViewer(nullptr),
    _gameServer(nullptr),
    _gameClient(nullptr),
    _rewindManager(nullptr)
{
    _paused = false;
    _pauseOnNextFrame = false;
    _stopFlag = false;
    _isRunAheadFrame = false;
    _lockCounter = 0;
    _threadPaused = false;
    _debugRequestCount = 0;
    _blockDebuggerRequestCount = 0;
    _systemActionManager.reset(new SystemActionManager(this));

    // PS2 frontend owns video/audio/input. Keep Mesen's accurate NES core,
    // but turn off desktop-only features and choose sane defaults.
    NesConfig &nes = _settings->GetNesConfig();
    nes.EnableHdPacks = false;
    nes.DisableGameDatabase = true;
    // Desktop settings normally populate these; the standalone defaults mute every channel.
    for(auto &volume : nes.ChannelVolumes) volume = 100;
    nes.Port1.Type = ControllerType::NesController;
    nes.Port2.Type = ControllerType::NesController;

    AudioConfig &audio = _settings->GetAudioConfig();
    audio.EnableAudio = true;
    audio.SampleRate = 48000;
    audio.DisableDynamicSampleRate = true;
}

Emulator::~Emulator()
{
    if(_console) {
        _console->SaveBattery();
        _console.reset();
    }
}

void Emulator::Initialize(bool) {}

void Emulator::Release()
{
    if(_console) {
        _console->SaveBattery();
        _console.reset();
    }
}

void Emulator::Run()
{
    if(_console && !_paused) {
        _console->RunFrame();
    }
}

void Emulator::Stop(bool, bool, bool saveBattery)
{
    if(_console && saveBattery) {
        _console->SaveBattery();
    }
    _console.reset();
    // Unloading NES must also release its cartridge copy before SNES loads.
    _rom.RomFile = VirtualFile();
    _rom.PatchFile = VirtualFile();
    memset(_consoleMemory, 0, sizeof(_consoleMemory));
}

void Emulator::OnBeforeSendFrame() {}

void Emulator::ProcessEndOfFrame()
{
    if(_console) {
        _console->GetControlManager()->ProcessEndOfFrame();
    }
    _frameRunning = false;
}

void Emulator::Reset()
{
    if(!_console) return;
    _console->Reset();
    if(_systemActionManager) _systemActionManager->ResetState();
    _console->GetControlManager()->UpdateInputState();
    _console->GetControlManager()->ResetLagCounter();
}

void Emulator::ReloadRom(bool)
{
    if(!_rom.RomFile.IsValid()) return;
    VirtualFile rom = _rom.RomFile;
    VirtualFile patch = _rom.PatchFile;
    LoadRom(rom, patch, true, false);
}

void Emulator::PowerCycle() { ReloadRom(true); }
void Emulator::PauseOnNextFrame() { _pauseOnNextFrame = true; }
void Emulator::Pause() { _paused = true; }
void Emulator::Resume() { _paused = false; }
bool Emulator::IsPaused() { return _paused; }
void Emulator::OnBeforePause(bool clearAudioBuffer) { if(clearAudioBuffer) _soundMixer->StopAudio(true); }

bool Emulator::LoadRom(VirtualFile romFile, VirtualFile patchFile, bool, bool)
{
    if(!romFile.IsValid()) return false;

    if(patchFile.IsValid()) {
        romFile.ApplyPatch(patchFile);
    }

    if(_console) {
        _console->SaveBattery();
        _console.reset();
    }
    memset(_consoleMemory, 0, sizeof(_consoleMemory));

    string baseName = FolderUtilities::GetFilename(romFile.GetFileName(), false);
    _batteryManager->Initialize(""); // SNESticle owns battery file I/O.

    unique_ptr<IConsole> console(new NesConsole(this));
    LoadRomResult result = console->LoadRom(romFile);
    if(result != LoadRomResult::Success) {
        return false;
    }

    _rom.RomFile = romFile;
    _rom.PatchFile = patchFile;
    _console.reset(console);
    _consoleType = ConsoleType::Nes;
    _rom.Format = _console->GetRomFormat();
    _notificationManager->RegisterNotificationListener(_console.lock());
    _console->GetControlManager()->UpdateInputState();
    return true;
}

string Emulator::GetHash(HashType type)
{
    shared_ptr<IConsole> console = GetConsole();
    if(console) {
        string h = console->GetHash(type);
        if(!h.empty()) return h;
    }
    return _rom.RomFile.IsValid() ? _rom.RomFile.GetSha1Hash() : "";
}

uint32_t Emulator::GetCrc32() { return _rom.RomFile.IsValid() ? _rom.RomFile.GetCrc32() : 0; }
PpuFrameInfo Emulator::GetPpuFrame() { auto c=GetConsole(); return c ? c->GetPpuFrame() : PpuFrameInfo{}; }
ConsoleRegion Emulator::GetRegion() { auto c=GetConsole(); return c ? c->GetRegion() : ConsoleRegion::Ntsc; }
shared_ptr<IConsole> Emulator::GetConsole() { return _console.lock(); }
IConsole* Emulator::GetConsoleUnsafe() { return _console.get(); }
ConsoleType Emulator::GetConsoleType() { return _consoleType; }
vector<CpuType> Emulator::GetCpuTypes() { auto c=GetConsole(); return c ? c->GetCpuTypes() : vector<CpuType>{}; }
TimingInfo Emulator::GetTimingInfo(CpuType cpuType) { auto c=GetConsole(); return c ? c->GetTimingInfo(cpuType) : TimingInfo{}; }
uint64_t Emulator::GetMasterClock() { return _console ? _console->GetMasterClock() : 0; }
uint32_t Emulator::GetMasterClockRate() { return _console ? _console->GetMasterClockRate() : 0; }
uint32_t Emulator::GetFrameCount() { return GetPpuFrame().FrameCount; }
uint32_t Emulator::GetLagCounter() { auto c=GetConsole(); return c ? c->GetControlManager()->GetLagCounter() : 0; }
void Emulator::ResetLagCounter() { auto c=GetConsole(); if(c) c->GetControlManager()->ResetLagCounter(); }
bool Emulator::HasControlDevice(ControllerType type) { auto c=GetConsole(); return c ? c->GetControlManager()->HasControlDevice(type) : false; }
void Emulator::RegisterInputRecorder(IInputRecorder* r) { auto c=GetConsole(); if(c) c->GetControlManager()->RegisterInputRecorder(r); }
void Emulator::UnregisterInputRecorder(IInputRecorder* r) { auto c=GetConsole(); if(c) c->GetControlManager()->UnregisterInputRecorder(r); }
void Emulator::RegisterInputProvider(IInputProvider* p) { auto c=GetConsole(); if(c) c->GetControlManager()->RegisterInputProvider(p); }
void Emulator::UnregisterInputProvider(IInputProvider* p) { auto c=GetConsole(); if(c) c->GetControlManager()->UnregisterInputProvider(p); }
double Emulator::GetFps() { auto c=GetConsole(); return c ? c->GetFps() : 60.0; }

EmulatorLock Emulator::AcquireLock(bool allowDebuggerLock) { return EmulatorLock(this, allowDebuggerLock); }
void Emulator::Lock() { _lockCounter++; _runLock.Acquire(); }
void Emulator::Unlock() { if(_runLock.IsLockedByCurrentThread()) _runLock.Release(); if(_lockCounter) _lockCounter--; }
bool Emulator::IsThreadPaused() { return true; }
void Emulator::SuspendDebugger(bool) {}
bool Emulator::IsEmulationThread() { return true; }

void Emulator::InitDebugger() {}
void Emulator::StopDebugger() {}
DebuggerRequest Emulator::GetDebugger(bool) { return DebuggerRequest(this); }

void Emulator::SetStopCode(int32_t code) { if(_stopCode == 0) _stopCode = code; }
void Emulator::RegisterMemory(MemoryType type, void* memory, uint32_t size) { _consoleMemory[(int)type] = { memory, size }; }
ConsoleMemoryInfo Emulator::GetMemory(MemoryType type) { return _consoleMemory[(int)type]; }

AudioTrackInfo Emulator::GetAudioTrackInfo() { auto c=GetConsole(); return c ? c->GetAudioTrackInfo() : AudioTrackInfo{}; }
void Emulator::ProcessAudioPlayerAction(AudioPlayerActionParams p) { auto c=GetConsole(); if(c) c->ProcessAudioPlayerAction(p); }

void Emulator::ProcessEvent(EventType, std::optional<CpuType>) {}
template<CpuType cpuType> void Emulator::AddDebugEvent(DebugEventType) {}
template void Emulator::AddDebugEvent<CpuType::Nes>(DebugEventType);
void Emulator::BreakIfDebugging(CpuType, BreakSource) {}

BaseVideoFilter* Emulator::GetVideoFilter(bool) { return nullptr; }
void Emulator::GetScreenRotationOverride(uint32_t& rotation) { rotation = 0; }
void Emulator::InputBarcode(uint64_t, uint32_t) {}
void Emulator::ProcessTapeRecorderAction(TapeRecorderAction, string) {}
ShortcutState Emulator::IsShortcutAllowed(EmulatorShortcut shortcut, uint32_t param) { auto c=GetConsole(); return c ? c->IsShortcutAllowed(shortcut,param) : ShortcutState::Disabled; }
bool Emulator::IsKeyboardConnected() { return false; }

void Emulator::Serialize(ostream&, bool, int) {}
DeserializeResult Emulator::Deserialize(istream&, uint32_t, bool, optional<ConsoleType>, bool) { return {}; }

#endif // PS2_PORT
