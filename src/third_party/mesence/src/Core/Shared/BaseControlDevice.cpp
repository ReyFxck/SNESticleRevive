#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/Emulator.h"
#include "Shared/KeyManager.h"
#include "Shared/EmuSettings.h"
#include "Shared/InputHud.h"
#include "Utilities/StringUtilities.h"
#include "Utilities/Serializer.h"

BaseControlDevice::BaseControlDevice(Emulator* emu, ControllerType type, uint8_t port, KeyMappingSet keyMappingSet)
{
	_emu = emu;
	_type = type;
	_port = port;
	_strobe = false;
	_keyMappings = keyMappingSet.GetKeyMappingArray();
}

BaseControlDevice::~BaseControlDevice()
{
}

uint8_t BaseControlDevice::GetPort()
{
	return _port;
}

ControllerType BaseControlDevice::GetControllerType()
{ 
	return _type; 
}

void BaseControlDevice::SetStateFromInput()
{
	ClearState();
	InternalSetStateFromInput();
}

void BaseControlDevice::InternalSetStateFromInput()
{
}

bool BaseControlDevice::IsCurrentPort(uint16_t addr)
{
	return _port == (addr - 0x4016);
}

bool BaseControlDevice::IsExpansionDevice()
{
	return _port == BaseControlDevice::ExpDevicePort;
}

void BaseControlDevice::StrobeProcessRead()
{
	if(_strobe) {
		RefreshStateBuffer();
	}
}

void BaseControlDevice::StrobeProcessWrite(uint8_t value)
{
	bool prevStrobe = _strobe;
	_strobe = (value & 0x01) == 0x01;

	if(prevStrobe && !_strobe) {
		RefreshStateBuffer();
	}
}

void BaseControlDevice::ClearState()
{
	_state = ControlDeviceState();
}

ControlDeviceState BaseControlDevice::GetRawState()
{
	return _state;
}

void BaseControlDevice::DrawController(InputHud& hud)
{
	InputConfig& cfg = _emu->GetSettings()->GetInputConfig();
	if(hud.GetControllerIndex() < 8 && cfg.DisplayInputPort[hud.GetControllerIndex()]) {
		InternalDrawController(hud);
	}
	hud.EndDrawController();
}

void BaseControlDevice::SetRawState(ControlDeviceState state)
{
	_state = state;
}

void BaseControlDevice::SetTextState(string textState)
{
    (void)textState;
    ClearState();
}

string BaseControlDevice::GetTextState()
{
    return string();
}

void BaseControlDevice::EnsureCapacity(int32_t minBitCount)
{
	uint32_t minByteCount = minBitCount / 8 + 1 + (HasCoordinates() ? 32 : 0);
	int32_t gap = minByteCount - (int32_t)_state.State.size();

	if(gap > 0) {
		_state.State.insert(_state.State.end(), gap, 0);
	}
}

bool BaseControlDevice::HasCoordinates()
{
	return false;
}

bool BaseControlDevice::IsRawString()
{
	return false;
}

uint32_t BaseControlDevice::GetByteIndex(uint8_t bit)
{
	return bit / 8 + (HasCoordinates() ? 4 : 0);
}

bool BaseControlDevice::IsPressed(uint8_t bit)
{
	EnsureCapacity(bit);
	uint8_t bitMask = 1 << (bit % 8);
	return (_state.State[GetByteIndex(bit)] & bitMask) != 0;
}

void BaseControlDevice::SetBitValue(uint8_t bit, bool set)
{
	if(set) {
		SetBit(bit);
	} else {
		ClearBit(bit);
	}
}

void BaseControlDevice::SetBit(uint8_t bit)
{
	EnsureCapacity(bit);
	uint8_t bitMask = 1 << (bit % 8);
	_state.State[GetByteIndex(bit)] |= bitMask;
}

void BaseControlDevice::ClearBit(uint8_t bit)
{
	EnsureCapacity(bit);
	uint8_t bitMask = 1 << (bit % 8);
	_state.State[GetByteIndex(bit)] &= ~bitMask;
}

void BaseControlDevice::InvertBit(uint8_t bit)
{
	if(IsPressed(bit)) {
		ClearBit(bit);
	} else {
		SetBit(bit);
	}
}

void BaseControlDevice::SetPressedState(uint8_t bit, uint16_t keyCode)
{
	if(KeyManager::IsKeyPressed(keyCode)) {
		SetBit(bit);
	}
}

void BaseControlDevice::SetPressedState(uint8_t bit, bool enabled)
{
	if(enabled) {
		SetBit(bit);
	}
}

void BaseControlDevice::SetCoordinates(MousePosition pos)
{
	if(!_emu->GetSettings()->IsInputEnabled()) {
		return;
	}

	EnsureCapacity(-1);

	_state.State[0] = pos.X & 0xFF;
	_state.State[1] = (pos.X >> 8) & 0xFF;
	_state.State[2] = pos.Y & 0xFF;
	_state.State[3] = (pos.Y >> 8) & 0xFF;
}

MousePosition BaseControlDevice::GetCoordinates()
{
	EnsureCapacity(-1);

	MousePosition pos;
	pos.X = _state.State[0] | (_state.State[1] << 8);
	pos.Y = _state.State[2] | (_state.State[3] << 8);
	return pos;
}

void BaseControlDevice::Connect()
{
	_connected = true;
}

void BaseControlDevice::Disconnect()
{
	_connected = false;
}

bool BaseControlDevice::IsConnected()
{
	return _connected;
}

void BaseControlDevice::SetMovement(MouseMovement mov)
{
	if(!_emu->GetSettings()->IsInputEnabled()) {
		return;
	}

	MouseMovement prev = GetMovement();
	mov.dx += prev.dx;
	mov.dy += prev.dy;
	SetCoordinates({ mov.dx, mov.dy });
}

MouseMovement BaseControlDevice::GetMovement()
{
	MousePosition pos = GetCoordinates();
	SetCoordinates({ 0, 0 });
	return { pos.X, pos.Y };
}

bool BaseControlDevice::HasControllerType(ControllerType type)
{
	return _type == type;
}

void BaseControlDevice::SwapButtons(shared_ptr<BaseControlDevice> state1, uint8_t button1, shared_ptr<BaseControlDevice> state2, uint8_t button2)
{
	bool pressed1 = state1->IsPressed(button1);
	bool pressed2 = state2->IsPressed(button2);

	state1->ClearBit(button1);
	state2->ClearBit(button2);

	if(pressed1) {
		state2->SetBit(button2);
	}
	if(pressed2) {
		state1->SetBit(button1);
	}
}

void BaseControlDevice::Serialize(Serializer &s)
{
	SV(_strobe);
	SVVector(_state.State);
}
