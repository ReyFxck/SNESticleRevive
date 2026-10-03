#pragma once
#include "pch.h"
#include "Core/Debugger/DebugTypes.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Debugger/DebugUtilities.h"
#include "Core/Shared/EmulatorLock.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/Interfaces/IConsole.h"
#include "Core/Shared/Audio/AudioPlayerTypes.h"
#include "Utilities/Timer.h"
#include "Utilities/safe_ptr.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/VirtualFile.h"

class Debugger;
class DebugHud;
class SoundMixer;
class VideoRenderer;
class VideoDecoder;
class NotificationManager;
class EmuSettings;
class SaveStateManager;
class RewindManager;
class BatteryManager;
class CheatManager;
class MovieManager;
class HistoryViewer;
class FrameLimiter;
class DebugStats;
class BaseControlManager;
class VirtualFile;
class BaseVideoFilter;
class ShortcutKeyHandler;
class SystemActionManager;
class AudioPlayerHud;
class GameServer;
class GameClient;

class IInputRecorder;
class IInputProvider;

struct RomInfo;
struct TimingInfo;

enum class MemoryOperationType;
enum class MemoryType;
enum class EventType;
enum class ConsoleRegion;
enum class ConsoleType;
enum class HashType;
enum class TapeRecorderAction;

struct ConsoleMemoryInfo
{
	void* Memory;
	uint32_t Size;
};

class Emulator
{
private:
	friend class DebuggerRequest;
	friend class EmulatorLock;

#ifdef PS2_PORT
	thread* _emuThread = nullptr;
	AudioPlayerHud* _audioPlayerHud = nullptr;
#else
	unique_ptr<thread> _emuThread;
	unique_ptr<AudioPlayerHud> _audioPlayerHud;
#endif
	safe_ptr<IConsole> _console;

	shared_ptr<ShortcutKeyHandler> _shortcutKeyHandler;
	safe_ptr<Debugger> _debugger;
	shared_ptr<SystemActionManager> _systemActionManager;

	const unique_ptr<EmuSettings> _settings;
#ifdef PS2_PORT
	DebugHud* const _debugHud;
	DebugHud* const _scriptHud;
#else
	const unique_ptr<DebugHud> _debugHud;
	const unique_ptr<DebugHud> _scriptHud;
#endif
	const unique_ptr<NotificationManager> _notificationManager;
	const unique_ptr<BatteryManager> _batteryManager;
	const unique_ptr<SoundMixer> _soundMixer;
#ifdef PS2_PORT
	VideoRenderer* const _videoRenderer;
	VideoDecoder* const _videoDecoder;
	SaveStateManager* const _saveStateManager;
#else
	const unique_ptr<VideoRenderer> _videoRenderer;
	const unique_ptr<VideoDecoder> _videoDecoder;
	const unique_ptr<SaveStateManager> _saveStateManager;
#endif
	const unique_ptr<CheatManager> _cheatManager;
#ifdef PS2_PORT
	MovieManager* const _movieManager;
	HistoryViewer* const _historyViewer;
#else
	const unique_ptr<MovieManager> _movieManager;
	const unique_ptr<HistoryViewer> _historyViewer;
#endif
	
	const shared_ptr<GameServer> _gameServer;
	const shared_ptr<GameClient> _gameClient;
	const shared_ptr<RewindManager> _rewindManager;

#ifndef PS2_PORT
	thread_local static thread::id _currentThreadId;
	thread::id _emulationThreadId;
#endif

	atomic<uint32_t> _lockCounter;
	SimpleLock _runLock;
	SimpleLock _loadLock;

	SimpleLock _debuggerLock;
	atomic<bool> _stopFlag;
	atomic<bool> _paused;
	atomic<bool> _pauseOnNextFrame;
	atomic<bool> _threadPaused;

	atomic<int> _debugRequestCount;
	atomic<int> _blockDebuggerRequestCount;

	atomic<bool> _isRunAheadFrame;
	bool _frameRunning = false;

	RomInfo _rom;
	ConsoleType _consoleType = {};

	ConsoleMemoryInfo _consoleMemory[DebugUtilities::GetMemoryTypeCount()] = {};

#ifdef PS2_PORT
	DebugStats* _stats = nullptr;
	FrameLimiter* _frameLimiter = nullptr;
#else
	unique_ptr<DebugStats> _stats;
	unique_ptr<FrameLimiter> _frameLimiter;
#endif
	Timer _lastFrameTimer;
	double _frameDelay = 0;
	
	uint32_t _autoSaveStateFrameCounter = 0;
	int32_t _stopCode = 0;
	bool _stopRequested = false;

	void WaitForLock();
	void WaitForPauseEnd();

	void ProcessAutoSaveState();
	bool ProcessSystemActions();
	void RunFrameWithRunAhead();

	void BlockDebuggerRequests();
	void ResetDebugger(bool startDebugger = false);

	double GetFrameDelay();

	void TryLoadRom(VirtualFile& romFile, LoadRomResult& result, unique_ptr<IConsole>& console, bool useFileSignature);
	template<typename T> void TryLoadRom(VirtualFile& romFile, LoadRomResult& result, unique_ptr<IConsole>& console, bool useFileSignature);

	void InitConsole(unique_ptr<IConsole>& newConsole, ConsoleMemoryInfo originalConsoleMemory[], bool preserveRom);

	bool InternalLoadRom(VirtualFile romFile, VirtualFile patchFile, bool stopRom = true, bool forPowerCycle = false);

public:
	Emulator();
	~Emulator();

	void Initialize(bool enableShortcuts = true);
	void Release();

	void Run();
	void Stop(bool sendNotification, bool preventRecentGameSave = false, bool saveBattery = true);

	void OnBeforeSendFrame();
	void ProcessEndOfFrame();

	void Reset();
	void ReloadRom(bool forPowerCycle);
	void PowerCycle();

	void PauseOnNextFrame();

	void Pause();
	void Resume();
	bool IsPaused();

	void OnBeforePause(bool clearAudioBuffer);

	bool LoadRom(VirtualFile romFile, VirtualFile patchFile, bool stopRom = true, bool forPowerCycle = false);
	RomInfo& GetRomInfo() { return _rom; }
	string GetHash(HashType type);
	uint32_t GetCrc32();
	PpuFrameInfo GetPpuFrame();
	ConsoleRegion GetRegion();
	shared_ptr<IConsole> GetConsole();
	IConsole* GetConsoleUnsafe();
	ConsoleType GetConsoleType();
	vector<CpuType> GetCpuTypes();
	uint64_t GetMasterClock();
	uint32_t GetMasterClockRate();

	EmulatorLock AcquireLock(bool allowDebuggerLock = true);
	void Lock();
	void Unlock();
	bool IsThreadPaused();

	bool IsDebuggerBlocked() { return _blockDebuggerRequestCount > 0; }
	void SuspendDebugger(bool release);

	void Serialize(ostream& out, bool includeSettings, int compressionLevel = 1);
	DeserializeResult Deserialize(istream& in, uint32_t fileFormatVersion, bool includeSettings, optional<ConsoleType> consoleType = std::nullopt, bool sendNotification = true);

	SoundMixer* GetSoundMixer() { return _soundMixer.get(); }
#ifdef PS2_PORT
	VideoRenderer* GetVideoRenderer() { return _videoRenderer; }
	VideoDecoder* GetVideoDecoder() { return _videoDecoder; }
	SaveStateManager* GetSaveStateManager() { return _saveStateManager; }
	DebugHud* GetDebugHud() { return _debugHud; }
	DebugHud* GetScriptHud() { return _scriptHud; }
	MovieManager* GetMovieManager() { return _movieManager; }
	HistoryViewer* GetHistoryViewer() { return _historyViewer; }
#else
	VideoRenderer* GetVideoRenderer() { return _videoRenderer.get(); }
	VideoDecoder* GetVideoDecoder() { return _videoDecoder.get(); }
	SaveStateManager* GetSaveStateManager() { return _saveStateManager.get(); }
	DebugHud* GetDebugHud() { return _debugHud.get(); }
	DebugHud* GetScriptHud() { return _scriptHud.get(); }
	MovieManager* GetMovieManager() { return _movieManager.get(); }
	HistoryViewer* GetHistoryViewer() { return _historyViewer.get(); }
#endif
	ShortcutKeyHandler* GetShortcutKeyHandler() { return _shortcutKeyHandler.get(); }
	NotificationManager* GetNotificationManager() { return _notificationManager.get(); }
	EmuSettings* GetSettings() { return _settings.get(); }
	RewindManager* GetRewindManager() { return _rewindManager.get(); }
	BatteryManager* GetBatteryManager() { return _batteryManager.get(); }
	CheatManager* GetCheatManager() { return _cheatManager.get(); }
	GameServer* GetGameServer() { return _gameServer.get(); }
	GameClient* GetGameClient() { return _gameClient.get(); }
	shared_ptr<SystemActionManager> GetSystemActionManager() { return _systemActionManager; }

	BaseVideoFilter* GetVideoFilter(bool getDefaultFilter = false);
	void GetScreenRotationOverride(uint32_t& rotation);

	void InputBarcode(uint64_t barcode, uint32_t digitCount);
	void ProcessTapeRecorderAction(TapeRecorderAction action, string filename);

	ShortcutState IsShortcutAllowed(EmulatorShortcut shortcut, uint32_t shortcutParam);
	bool IsKeyboardConnected();

	void InitDebugger();
	void StopDebugger();
	DebuggerRequest GetDebugger(bool autoInit = false);
	bool IsDebugging() { return !!_debugger; }
	Debugger* InternalGetDebugger() { return _debugger.get(); }

#ifndef PS2_PORT
	thread::id GetEmulationThreadId() { return _emulationThreadId; }
#endif
	bool IsEmulationThread();

	int32_t GetStopCode() { return _stopCode; }
	void SetStopCode(int32_t stopCode);

	void RegisterMemory(MemoryType type, void* memory, uint32_t size);
	ConsoleMemoryInfo GetMemory(MemoryType type);

	AudioTrackInfo GetAudioTrackInfo();
	void ProcessAudioPlayerAction(AudioPlayerActionParams p);
	#ifdef PS2_PORT
	AudioPlayerHud* GetAudioPlayerHud() { return _audioPlayerHud; }
#else
	AudioPlayerHud* GetAudioPlayerHud() { return _audioPlayerHud.get(); }
#endif

	bool IsRunning() { return _console != nullptr; }
	bool IsRunAheadFrame() { return _isRunAheadFrame; }

	TimingInfo GetTimingInfo(CpuType cpuType);
	uint32_t GetFrameCount();

	uint32_t GetLagCounter();
	void ResetLagCounter();	
	bool HasControlDevice(ControllerType type);
	void RegisterInputRecorder(IInputRecorder* recorder);
	void UnregisterInputRecorder(IInputRecorder* recorder);
	void RegisterInputProvider(IInputProvider* provider);
	void UnregisterInputProvider(IInputProvider* provider);

	double GetFps();
	
#ifdef PS2_PORT
	template<CpuType type> __forceinline void ProcessInstruction() {}

	template<CpuType type, uint8_t accessWidth = 1, MemoryAccessFlags flags = MemoryAccessFlags::None, typename T> __forceinline void ProcessMemoryRead(uint32_t, T&, MemoryOperationType) {}

	template<CpuType type, uint8_t accessWidth = 1, MemoryAccessFlags flags = MemoryAccessFlags::None, typename T> __forceinline bool ProcessMemoryWrite(uint32_t, T&, MemoryOperationType) { return true; }

	template<CpuType cpuType, MemoryType memType, MemoryOperationType opType, typename T> __forceinline void ProcessMemoryAccess(uint32_t, T) {}

	template<CpuType type> __forceinline void ProcessIdleCycle() {}
	template<CpuType type> __forceinline void ProcessHaltedCpu() {}

	template<CpuType type, typename T> __forceinline void ProcessPpuRead(uint32_t, T&, MemoryType, MemoryOperationType = MemoryOperationType::Read) {}
	template<CpuType type, typename T> __forceinline void ProcessPpuWrite(uint32_t, T&, MemoryType) {}
	template<CpuType type> __forceinline void ProcessPpuCycle() {}
	template<CpuType type> void ProcessInterrupt(uint32_t, uint32_t, bool) {}
	__forceinline void DebugLog(string) {}
#else
	template<CpuType type> __forceinline void ProcessInstruction()
	{
		if(_debugger) {
			_debugger->ProcessInstruction<type>();
		}
	}

	template<CpuType type, uint8_t accessWidth = 1, MemoryAccessFlags flags = MemoryAccessFlags::None, typename T> __forceinline void ProcessMemoryRead(uint32_t addr, T& value, MemoryOperationType opType)
	{
		if(_debugger) {
			_debugger->ProcessMemoryRead<type, accessWidth, flags>(addr, value, opType);
		}
	}

	template<CpuType type, uint8_t accessWidth = 1, MemoryAccessFlags flags = MemoryAccessFlags::None, typename T> __forceinline bool ProcessMemoryWrite(uint32_t addr, T& value, MemoryOperationType opType)
	{
		if(_debugger) {
			return _debugger->ProcessMemoryWrite<type, accessWidth, flags>(addr, value, opType);
		}
		return true;
	}

	template<CpuType cpuType, MemoryType memType, MemoryOperationType opType, typename T> __forceinline void ProcessMemoryAccess(uint32_t addr, T value)
	{
		if(_debugger) {
			_debugger->ProcessMemoryAccess<cpuType, memType, opType, T>(addr, value);
		}
	}

	template<CpuType type> __forceinline void ProcessIdleCycle()
	{
		if(_debugger) {
			_debugger->ProcessIdleCycle<type>();
		}
	}

	template<CpuType type> __forceinline void ProcessHaltedCpu()
	{
		if(_debugger) {
			_debugger->ProcessHaltedCpu<type>();
		}
	}

	template<CpuType type, typename T> __forceinline void ProcessPpuRead(uint32_t addr, T& value, MemoryType memoryType, MemoryOperationType opType = MemoryOperationType::Read)
	{
		if(_debugger) {
			_debugger->ProcessPpuRead<type>(addr, value, memoryType, opType);
		}
	}

	template<CpuType type, typename T> __forceinline void ProcessPpuWrite(uint32_t addr, T& value, MemoryType memoryType)
	{
		if(_debugger) {
			_debugger->ProcessPpuWrite<type>(addr, value, memoryType);
		}
	}

	template<CpuType type> __forceinline void ProcessPpuCycle()
	{
		if(_debugger) {
			_debugger->ProcessPpuCycle<type>();
		}
	}

	template<CpuType type> void ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi)
	{
		if(_debugger) {
			_debugger->ProcessInterrupt<type>(originalPc, currentPc, forNmi);
		}
	}

	__forceinline void DebugLog(string log)
	{
		if(_debugger) {
			_debugger->Log(log);
		}
	}
#endif

	void ProcessEvent(EventType type, std::optional<CpuType> cpuType = std::nullopt);
	template<CpuType cpuType> void AddDebugEvent(DebugEventType evtType);
	void BreakIfDebugging(CpuType sourceCpu, BreakSource source);
};

enum class HashType
{
	Sha1,
	Sha1Cheat
};