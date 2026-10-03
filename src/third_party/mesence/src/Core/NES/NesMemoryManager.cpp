#include "pch.h"
#include "NES/NesMemoryManager.h"
#include "NES/BaseMapper.h"
#include "NES/NesConsole.h"
#include "Shared/CheatManager.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Utilities/Serializer.h"
#include "Shared/MemoryOperationType.h"

NesMemoryManager::NesMemoryManager(NesConsole* console, BaseMapper* mapper)
{
	_console = console;
	_emu = console->GetEmulator();
	_cheatManager = _emu->GetCheatManager();
	_mapper = mapper;

	_internalRamSize = mapper->GetInternalRamSize();
	_internalRam = new uint8_t[_internalRamSize];
	_emu->RegisterMemory(MemoryType::NesInternalRam, _internalRam, _internalRamSize);
	if(_internalRamSize == NesMemoryManager::NesInternalRamSize) {
		_internalRamHandler.reset(new InternalRamHandler<0x7FF>());
		((InternalRamHandler<0x7FF>*)_internalRamHandler.get())->SetInternalRam(_internalRam);
	} else if(_internalRamSize == NesMemoryManager::FamicomBoxInternalRamSize) {
		_internalRamHandler.reset(new InternalRamHandler<0x1FFF>());
		((InternalRamHandler<0x1FFF>*)_internalRamHandler.get())->SetInternalRam(_internalRam);
	} else {
		throw std::runtime_error("unsupported memory size");
	}

	for(int i = 0; i < 0x100; i++) {
		_ramReadHandlers[i].handler = &_openBusHandler;
		_ramWriteHandlers[i].handler = &_openBusHandler;
	}

	RegisterIODevice(_internalRamHandler.get());	
}

NesMemoryManager::~NesMemoryManager()
{
	delete[] _internalRam;
}

void NesMemoryManager::Reset(bool softReset)
{
	if(!softReset) {
		_console->InitializeRam(_internalRam, _internalRamSize);
	}

	_mapper->Reset(softReset);
}

void NesMemoryManager::HandlerPage::Set(uint8_t offset, INesMemoryHandler* value)
{
	if(!split) {
		if(value == handler) return;
		unique_ptr<INesMemoryHandler*[]> entries(new INesMemoryHandler*[0x100]);
		std::fill_n(entries.get(), 0x100, handler);
		split = std::move(entries);
		handler = nullptr;
	}
	split[offset] = value;
}

void NesMemoryManager::HandlerPage::Compact()
{
	if(!split) return;
	INesMemoryHandler* value = split[0];
	for(int i = 1; i < 0x100; i++) {
		if(split[i] != value) return;
	}
	handler = value;
	split.reset();
}

void NesMemoryManager::InitializeMemoryHandlers(HandlerPage* memoryHandlers, INesMemoryHandler* handler, vector<uint16_t> *addresses, bool allowOverride)
{
	for(uint16_t address : *addresses) {
		HandlerPage& page = memoryHandlers[address >> 8];
		INesMemoryHandler* current = page.Get((uint8_t)address);
		if(!allowOverride && current != &_openBusHandler && current != handler) {
			throw std::runtime_error("Can't override existing mapping");
		}
		page.Set((uint8_t)address, handler);
	}
	for(int i = 0; i < 0x100; i++) memoryHandlers[i].Compact();
}

void NesMemoryManager::SetHandlerRange(HandlerPage* memoryHandlers, INesMemoryHandler* handler, uint32_t start, uint32_t end)
{
	// Whole pages can change ownership without allocating split tables.
	while(start <= end) {
		HandlerPage& page = memoryHandlers[start >> 8];
		if((start & 0xFF) == 0 && end - start >= 0xFF) {
			page.handler = handler;
			page.split.reset();
			start += 0x100;
		} else {
			uint32_t last = std::min(end, start | 0xFF);
			do { page.Set((uint8_t)start++, handler); } while(start <= last);
			page.Compact();
		}
	}
}

void NesMemoryManager::RegisterIODevice(INesMemoryHandler*handler)
{
	MemoryRanges ranges;
	handler->GetMemoryRanges(ranges);

	InitializeMemoryHandlers(_ramReadHandlers, handler, ranges.GetRAMReadAddresses(), ranges.GetAllowOverride());
	InitializeMemoryHandlers(_ramWriteHandlers, handler, ranges.GetRAMWriteAddresses(), ranges.GetAllowOverride());
}

void NesMemoryManager::RegisterWriteHandler(INesMemoryHandler* handler, uint32_t start, uint32_t end)
{
	SetHandlerRange(_ramWriteHandlers, handler, start, end);
}

void NesMemoryManager::RegisterReadHandler(INesMemoryHandler* handler, uint32_t start, uint32_t end)
{
	SetHandlerRange(_ramReadHandlers, handler, start, end);
}

void NesMemoryManager::UnregisterIODevice(INesMemoryHandler*handler)
{
	MemoryRanges ranges;
	handler->GetMemoryRanges(ranges);

	for(uint16_t address : *ranges.GetRAMReadAddresses()) {
		_ramReadHandlers[address >> 8].Set((uint8_t)address, &_openBusHandler);
	}

	for(uint16_t address : *ranges.GetRAMWriteAddresses()) {
		_ramWriteHandlers[address >> 8].Set((uint8_t)address, &_openBusHandler);
	}
	for(int i = 0; i < 0x100; i++) {
		_ramReadHandlers[i].Compact();
		_ramWriteHandlers[i].Compact();
	}
}

uint8_t* NesMemoryManager::GetInternalRam()
{
	return _internalRam;
}

uint8_t NesMemoryManager::DebugRead(uint16_t addr)
{
	uint8_t value = _ramReadHandlers[addr >> 8].Get((uint8_t)addr)->PeekRam(addr);
	if(_cheatManager->HasCheats<CpuType::Nes>()) {
		_cheatManager->ApplyCheat<CpuType::Nes>(addr, value);
	}
	return value;
}

uint16_t NesMemoryManager::DebugReadWord(uint16_t addr)
{
	return DebugRead(addr) | (DebugRead(addr + 1) << 8);
}

uint8_t NesMemoryManager::Read(uint16_t addr, MemoryOperationType operationType)
{
	INesMemoryHandler* handler = _ramReadHandlers[addr >> 8].Get((uint8_t)addr);
	// Check the actual registered owner, not merely the address: cartridges
	// and peripherals can override internal RAM or individual registers.
	uint8_t value = handler == _internalRamHandler.get()
		? _internalRam[addr & (_internalRamSize - 1)] : handler->ReadRam(addr);
	if(_cheatManager->HasCheats<CpuType::Nes>()) {
		_cheatManager->ApplyCheat<CpuType::Nes>(addr, value);
	}
	_emu->ProcessMemoryRead<CpuType::Nes>(addr, value, operationType);

	_openBusHandler.SetOpenBus(value, addr == 0x4015);

	return value;
}

void NesMemoryManager::Write(uint16_t addr, uint8_t value, MemoryOperationType operationType)
{
	if(_emu->ProcessMemoryWrite<CpuType::Nes>(addr, value, operationType)) {
		INesMemoryHandler* handler = _ramWriteHandlers[addr >> 8].Get((uint8_t)addr);
		if(handler == _internalRamHandler.get()) {
			_internalRam[addr & (_internalRamSize - 1)] = value;
		} else {
			handler->WriteRam(addr, value);
		}
		_openBusHandler.SetOpenBus(value, false);
	}
}

void NesMemoryManager::DebugWrite(uint16_t addr, uint8_t value, bool disableSideEffects)
{
	if(addr <= 0x1FFF) {
		_ramWriteHandlers[addr >> 8].Get((uint8_t)addr)->WriteRam(addr, value);
	} else {
		INesMemoryHandler* handler = _ramReadHandlers[addr >> 8].Get((uint8_t)addr);
		if(handler) {
			if(disableSideEffects) {
				if(handler == _mapper) {
					//Only allow writes to prg/chr ram/rom (e.g not ppu, apu, mapper registers, etc.)
					((BaseMapper*)handler)->DebugWriteRam(addr, value);
				}
			} else {
				handler->WriteRam(addr, value);
			}
		}
	}
}

void NesMemoryManager::Serialize(Serializer &s)
{
	SVArray(_internalRam, _internalRamSize);
	SV(_openBusHandler);
}

uint8_t NesMemoryManager::GetOpenBus(uint8_t mask)
{
	return _openBusHandler.GetOpenBus() & mask;
}

uint8_t NesMemoryManager::GetInternalOpenBus(uint8_t mask)
{
	return _openBusHandler.GetInternalOpenBus() & mask;
}
