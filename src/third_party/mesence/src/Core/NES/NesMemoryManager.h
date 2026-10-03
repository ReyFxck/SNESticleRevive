#pragma once

#include "pch.h"

#include "NES/INesMemoryHandler.h"
#include "NES/OpenBusHandler.h"
#include "NES/InternalRamHandler.h"
#include "Shared/MemoryOperationType.h"
#include "Utilities/ISerializable.h"

class BaseMapper;
class CheatManager;
class Emulator;
class NesConsole;

class NesMemoryManager : public ISerializable
{
private:
	static const int NesInternalRamSize = 0x800;
	static const int FamicomBoxInternalRamSize = 0x2000;

	Emulator* _emu = nullptr;
	CheatManager* _cheatManager = nullptr;
	NesConsole* _console = nullptr;
	BaseMapper* _mapper = nullptr;

	uint8_t* _internalRam = nullptr;
	uint32_t _internalRamSize = 0;

	OpenBusHandler _openBusHandler = {};
	unique_ptr<INesMemoryHandler> _internalRamHandler;
	// Most 256-byte CPU pages have a single handler. Only pages with split
	// registers (e.g. $40xx) need a per-address table. Keep the hot directory
	// in a few KiB instead of two 65536-pointer arrays on the EE.
	struct HandlerPage {
		INesMemoryHandler* handler = nullptr;
		unique_ptr<INesMemoryHandler*[]> split;
		INesMemoryHandler* Get(uint8_t offset) const {
			return handler ? handler : split[offset];
		}
		void Set(uint8_t offset, INesMemoryHandler* value);
		void Compact();
	};
	HandlerPage _ramReadHandlers[0x100];
	HandlerPage _ramWriteHandlers[0x100];

	void InitializeMemoryHandlers(HandlerPage* memoryHandlers, INesMemoryHandler* handler, vector<uint16_t>* addresses, bool allowOverride);
	void SetHandlerRange(HandlerPage* memoryHandlers, INesMemoryHandler* handler, uint32_t start, uint32_t end);

protected:
	void Serialize(Serializer& s) override;

public:
	NesMemoryManager(NesConsole* console, BaseMapper* mapper);
	virtual ~NesMemoryManager();

	void Reset(bool softReset);
	void RegisterIODevice(INesMemoryHandler* handler);
	void RegisterWriteHandler(INesMemoryHandler* handler, uint32_t start, uint32_t end);
	void RegisterReadHandler(INesMemoryHandler* handler, uint32_t start, uint32_t end);
	void UnregisterIODevice(INesMemoryHandler* handler);

	uint8_t DebugRead(uint16_t addr);
	uint16_t DebugReadWord(uint16_t addr);
	void DebugWrite(uint16_t addr, uint8_t value, bool disableSideEffects = true);

	uint8_t* GetInternalRam();

	uint8_t Read(uint16_t addr, MemoryOperationType operationType = MemoryOperationType::Read);
	void Write(uint16_t addr, uint8_t value, MemoryOperationType operationType);

	uint8_t GetOpenBus(uint8_t mask = 0xFF);
	uint8_t GetInternalOpenBus(uint8_t mask = 0xFF);
};
