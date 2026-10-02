#include "pch.h"
#include "Shared/MessageManager.h"

std::unordered_map<string, string> MessageManager::_enResources = {
	{ "Cheats", "Cheats" },
	{ "Debug", "Debug" },
	{ "EmulationSpeed", "Emulation Speed" },
	{ "ClockRate", "Clock Rate" },
	{ "Error", "Error" },
	{ "GameInfo", "Game Info" },
	{ "GameLoaded", "Game loaded" },
	{ "Input", "Input" },
	{ "Patch", "Patch" },
	{ "Movies", "Movies" },
	{ "NetPlay", "Net Play" },
	{ "Overclock", "Overclock" },
	{ "Region", "Region" },
	{ "SaveStates", "Save States" },
	{ "ScreenshotSaved", "Screenshot Saved" },
	{ "SoundRecorder", "Sound Recorder" },
	{ "Test", "Test" },
	{ "VideoRecorder", "Video Recorder" },

	{ "ApplyingPatch", "Applying patch: %1" },
	{ "PatchFailed", "Failed to apply patch: %1" },
	{ "CheatApplied", "1 cheat applied." },
	{ "CheatsApplied", "%1 cheats applied." },
	{ "CheatsDisabled", "All cheats disabled." },
	{ "CoinInsertedSlot", "Coin inserted (slot %1)" },
	{ "ConnectedToServer", "Connected to server." },
	{ "ConnectionLost", "Connection to server lost." },
	{ "CouldNotConnect", "Could not connect to the server." },
	{ "CouldNotInitializeAudioSystem", "Could not initialize audio system" },
	{ "CouldNotFindRom", "Could not find matching game ROM. (%1)" },
	{ "CouldNotWriteToFile", "Could not write to file: %1" },
	{ "CouldNotLoadFile", "Could not load file: %1" },
	{ "EmulationMaximumSpeed", "Maximum speed" },
	{ "EmulationSpeedPercent", "%1%" },
	{ "FdsDiskInserted", "Disk %1 Side %2 inserted." },
	{ "Frame", "Frame" },
	{ "GameCrash", "Game has crashed (%1)" },
	{ "KeyboardModeDisabled", "Keyboard mode disabled." },
	{ "KeyboardModeEnabled", "Keyboard connected - shortcut keys disabled." },
	{ "Lag", "Lag" },
	{ "Mapper", "Mapper: %1, SubMapper: %2" },
	{ "MovieEnded", "Movie ended." },
	{ "MovieStopped", "Movie stopped." },
	{ "MovieInvalid", "Invalid movie file." },
	{ "MovieMissingRom", "Missing ROM required (%1) to play movie." },
	{ "MovieNewerVersion", "Cannot load movies created by a more recent version of Mesen. Please download the latest version." },
	{ "MovieIncompatibleVersion", "This movie is incompatible with this version of Mesen." },
	{ "MovieIncorrectConsole", "This movie was recorded on another console (%1) and can't be loaded." },
	{ "MoviePlaying", "Playing movie: %1" },
	{ "MovieRecordingTo", "Recording to: %1" },
	{ "MovieSaved", "Movie saved to file: %1" },
	{ "NetplayVersionMismatch", "Netplay client is not running the same version of Mesen and has been disconnected." },
	{ "NetplayNotAllowed", "This action is not allowed while connected to a server." },
	{ "OverclockEnabled", "Overclocking enabled." },
	{ "OverclockDisabled", "Overclocking disabled." },
	{ "PrgSizeWarning", "PRG size is smaller than 32kb" },
	{ "SaveStateEmpty", "Slot is empty." },
	{ "SaveStateIncompatibleVersion", "Save state is incompatible with this version of Mesen." },
	{ "SaveStateInvalidFile", "Invalid save state file." },
	{ "SaveStateWrongSystem", "Error: State cannot be loaded (wrong console type)" },
	{ "SaveStateLoaded", "State #%1 loaded." },
	{ "SaveStateLoadedFile", "State loaded: %1" },
	{ "SaveStateSavedFile", "State saved: %1" },
	{ "SaveStateMissingRom", "Missing ROM required (%1) to load save state." },
	{ "SaveStateNewerVersion", "Cannot load save states created by a more recent version of Mesen. Please download the latest version." },
	{ "SaveStateSaved", "State #%1 saved." },
	{ "SaveStateSlotSelected", "Slot #%1 selected." },
	{ "ScanlineTimingWarning", "PPU timing has been changed." },
	{ "ServerStarted", "Server started (Port: %1)" },
	{ "ServerStopped", "Server stopped" },
	{ "SoundRecorderStarted", "Recording to: %1" },
	{ "SoundRecorderStopped", "Recording saved to: %1" },
	{ "TestFileSavedTo", "Test file saved to: %1" },
	{ "UnexpectedError", "Unexpected error: %1" },
	{ "UnsupportedMapper", "Unsupported mapper (%1), cannot load game." },
	{ "VideoRecorderStarted", "Recording to: %1" },
	{ "VideoRecorderStopped", "Recording saved to: %1" },
};

std::list<string> MessageManager::_log;
SimpleLock MessageManager::_logLock;
SimpleLock MessageManager::_messageLock;
bool MessageManager::_osdEnabled = true;
bool MessageManager::_outputToStdout = false;
IMessageManager* MessageManager::_messageManager = nullptr;

void MessageManager::RegisterMessageManager(IMessageManager* messageManager)
{
	auto lock = _messageLock.AcquireSafe();
	if(MessageManager::_messageManager == nullptr) {
		MessageManager::_messageManager = messageManager;
	}
}

void MessageManager::UnregisterMessageManager(IMessageManager* messageManager)
{
	auto lock = _messageLock.AcquireSafe();
	if(MessageManager::_messageManager == messageManager) {
		MessageManager::_messageManager = nullptr;
	}
}

void MessageManager::SetOptions(bool osdEnabled, bool outputToStdout)
{
	_osdEnabled = osdEnabled;
	_outputToStdout = outputToStdout;
}

string MessageManager::Localize(string key)
{
	std::unordered_map<string, string> *resources = &_enResources;
	/*switch(EmulationSettings::GetDisplayLanguage()) {
		case Language::English: resources = &_enResources; break;
		case Language::French: resources = &_frResources; break;
		case Language::Japanese: resources = &_jaResources; break;
		case Language::Russian: resources = &_ruResources; break;
		case Language::Spanish: resources = &_esResources; break;
		case Language::Ukrainian: resources = &_ukResources; break;
		case Language::Portuguese: resources = &_ptResources; break;
		case Language::Catalan: resources = &_caResources; break;
		case Language::Chinese: resources = &_zhResources; break;
	}*/
	if(resources) {
		if(resources->find(key) != resources->end()) {
			return (*resources)[key];
		}/* else if(EmulationSettings::GetDisplayLanguage() != Language::English) {
			//Fallback on English if resource key is missing another language
			resources = &_enResources;
			if(resources->find(key) != resources->end()) {
				return (*resources)[key];
			}
		}*/
	}

	return key;
}

void MessageManager::DisplayMessage(string title, string message, string param1, string param2)
{
	if(MessageManager::_messageManager) {
		auto lock = _messageLock.AcquireSafe();
		if(!MessageManager::_messageManager) {
			return;
		}

		title = Localize(title);
		message = Localize(message);

		size_t startPos = message.find("%1");
		if(startPos != std::string::npos) {
			message.replace(startPos, 2, param1);
		}

		startPos = message.find("%2");
		if(startPos != std::string::npos) {
			message.replace(startPos, 2, param2);
		}

		if(_osdEnabled) {
			MessageManager::_messageManager->DisplayMessage(title, message);
		} else {
			MessageManager::Log("[" + title + "] " + message);
		}
	}
}

void MessageManager::Log(string message)
{
	auto lock = _logLock.AcquireSafe();
	if(message.empty()) {
		message = "------------------------------------------------------";
	}
	if(_log.size() >= 1000) {
		_log.pop_front();
	}
	_log.push_back(message);

	if(_outputToStdout) {
		std::cout << message << std::endl;
	}
}

void MessageManager::ClearLog()
{
	auto lock = _logLock.AcquireSafe();
	_log.clear();
}

string MessageManager::GetLog()
{
	auto lock = _logLock.AcquireSafe();
	stringstream ss;
	for(string &msg : _log) {
		ss << msg << "\n";
	}
	return ss.str();
}
