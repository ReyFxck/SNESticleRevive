#include "mesence_bridge.h"
#include "mesence_palette.h"
#include "NES/BaseNesPpu.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/BatteryManager.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/BaseControlManager.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/Interfaces/IAudioDevice.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "NES/NesConsole.h"
#include "NES/BaseMapper.h"
#include "NES/Input/NesController.h"
#include "Utilities/Serializer.h"
#include <new>

namespace {
const uint32_t StateMagic = MESENCE_STATE_FORMAT;
const uint32_t SerializerVersion = 4;
struct StateHeader { uint32_t magic, version, bytes, frame; };
class FrontendInput : public IInputProvider {
public:
 uint8_t pads[2] = {};
 bool SetInput(BaseControlDevice *device) override {
  if(!device || device->GetPort() > 1 ||
     device->GetControllerType() != ControllerType::NesController) return false;
  uint8_t b = pads[device->GetPort()];
  /* A, B, Select, Start, Up, Down, Left, Right in the frontend byte. */
  const NesController::Buttons buttons[8] = {NesController::A, NesController::B,
   NesController::Select, NesController::Start, NesController::Up,
   NesController::Down, NesController::Left, NesController::Right};
  for(unsigned i=0; i<8; ++i) device->SetBitValue(buttons[i], (b >> i) & 1);
  return true;
 }
};
class FrontendAudio : public IAudioDevice {
public:
 MesenceAudioSink sink = nullptr;
 void *context = nullptr;
 void PlayBuffer(int16_t *data, uint32_t frames, uint32_t rate, bool stereo) override {
  if(!sink || rate != 48000) return;
  int16_t left[512], right[512];
  while(frames) {
   uint32_t n = std::min<uint32_t>(frames, 512);
   for(uint32_t i=0; i<n; ++i) {
    left[i] = data[stereo ? 2*i : i];
    right[i] = stereo ? data[2*i+1] : left[i];
   }
   sink(context, left, right, n);
   data += stereo ? 2*n : n; frames -= n;
  }
 }
 void Stop() override {}
 void Pause() override {}
 void ProcessEndOfFrame() override {}
 string GetAvailableDevices() override { return {}; }
 void SetAudioDevice(string) override {}
 AudioStatistics GetStatistics() override { return {}; }
};
}
struct MesenceCore {
 /* Declare emulator last: its destructor must precede its registered sinks. */
 FrontendInput input;
 FrontendAudio audio;
 uint32_t palette[512];
 vector<uint8_t> state;
 vector<uint8_t> battery;
 uint32_t saveBytes=0, chrBytes=0;
 unique_ptr<Emulator> emu;
 bool ready = false;
 MesenceCore() : emu(new Emulator()) {
  emu->GetSoundMixer()->RegisterAudioDevice(&audio);
  BuildPalette(0);
 }
 void BuildPalette(unsigned model) {
  if(model>10) model=0;
  for(unsigned e=0; e<8; ++e) for(unsigned c=0; c<64; ++c) {
   uint32_t rgb=MesencePaletteArgb[model][c];
   double r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
   if(model==0 && (c&15)<=13) {
    if(e&1) {g*=0.84; b*=0.84;}
    if(e&2) {r*=0.84; b*=0.84;}
    if(e&4) {r*=0.84; g*=0.84;}
   }
   if(model!=0) {
    if(e&1) r=255; if(e&2) g=255; if(e&4) b=255;
   }
   palette[e*64+c]=(uint32_t)r|((uint32_t)g<<8)|((uint32_t)b<<16)|0x80000000u;
  }
 }
};
namespace {
string SaveConsole(MesenceCore *core) {
 Serializer s(SerializerVersion,true);
 core->emu->GetConsoleUnsafe()->Serialize(s);
 std::ostringstream out(std::ios::binary);
 s.SaveTo(out,0);
 if(s.HasError()) throw std::runtime_error("Incomplete NES state");
 return out.str();
}
bool LoadConsole(MesenceCore *core, const string& data) {
 /* This frontend always stores uncompressed serializer payloads; the outer
    SNESticle container owns compression and its size/CRC checks. */
 if(data.empty() || data[0]!=0) return false;
 std::istringstream in(data,std::ios::binary);
 Serializer s(SerializerVersion,false);
 s.RequireCompleteState();
 if(!s.LoadFrom(in)) return false;
 core->emu->GetConsoleUnsafe()->Serialize(s);
 return !s.HasError();
}
void PushChrBattery(MesenceCore *core) {
 if(!core->chrBytes) return;
 auto mapper=((NesConsole*)core->emu->GetConsoleUnsafe())->GetMapper(); uint32_t n;
 auto save=mapper->FrontendSaveRam(n); auto chr=mapper->FrontendSaveChrRam(n);
 if(core->saveBytes) memcpy(save,core->battery.data(),core->saveBytes);
 memcpy(chr,core->battery.data()+core->saveBytes,core->chrBytes);
}
void PullChrBattery(MesenceCore *core) {
 if(!core->chrBytes) return;
 auto mapper=((NesConsole*)core->emu->GetConsoleUnsafe())->GetMapper(); uint32_t n;
 auto save=mapper->FrontendSaveRam(n); auto chr=mapper->FrontendSaveChrRam(n);
 if(core->saveBytes) memcpy(core->battery.data(),save,core->saveBytes);
 memcpy(core->battery.data()+core->saveBytes,chr,core->chrBytes);
}
}
extern "C" {
MesenceCore *MesenceCreate() {
 try { return new MesenceCore(); } catch(...) { return nullptr; }
}
void MesenceDestroy(MesenceCore *core) { delete core; }
void MesenceUnload(MesenceCore *core) {
 if(!core) return;
 core->audio.sink=nullptr; core->ready=false;
 core->emu->Stop(false,false,false);
 vector<uint8_t>().swap(core->state);
 vector<uint8_t>().swap(core->battery);
 core->saveBytes=core->chrBytes=0;
}
int MesenceLoad(MesenceCore *core, const void *rom, uint32_t bytes) {
 if(!core || !rom || bytes<16) return 0;
 MesenceUnload(core);
 try {
  VirtualFile image(rom,bytes,"cartridge.nes");
  if(!core->emu->LoadRom(image,VirtualFile(),true,false)) return 0;
  core->emu->RegisterInputProvider(&core->input);
  core->ready=true;
  core->BuildPalette((unsigned)((NesConsole*)core->emu->GetConsoleUnsafe())->GetPpu()->GetPpuModel());
  auto mapper=((NesConsole*)core->emu->GetConsoleUnsafe())->GetMapper();
  auto save=mapper->FrontendSaveRam(core->saveBytes);
  auto chr=mapper->FrontendSaveChrRam(core->chrBytes);
  if(core->chrBytes) {
   core->battery.resize(core->saveBytes+core->chrBytes);
   if(core->saveBytes) memcpy(core->battery.data(),save,core->saveBytes);
   memcpy(core->battery.data()+core->saveBytes,chr,core->chrBytes);
  }
  return 1;
 } catch(...) { MesenceUnload(core); return 0; }
}
int MesenceReset(MesenceCore *core, int hard) {
 if(!core || !core->ready) return 0;
 try {
 if(hard) {
  /* PowerCycle reloads the cartridge; retain battery and input provider. */
  auto ram=core->emu->GetMemory(MemoryType::NesSaveRam);
  vector<uint8_t> battery;
  if(ram.Memory && ram.Size) battery.assign((uint8_t*)ram.Memory,(uint8_t*)ram.Memory+ram.Size);
  core->emu->PowerCycle();
  if(!core->emu->GetConsoleUnsafe()) { core->ready=false; return 0; }
  core->emu->RegisterInputProvider(&core->input);
  ram=core->emu->GetMemory(MemoryType::NesSaveRam);
  if(ram.Memory && ram.Size==battery.size()) memcpy(ram.Memory,battery.data(),ram.Size);
  PushChrBattery(core);
 } else core->emu->Reset();
  return 1;
 } catch(...) { core->ready=false; return 0; }
}
int MesenceFrame(MesenceCore *core, const uint8_t pads[2], uint32_t *pixels,
 uint32_t pitch, MesenceAudioSink audio, void *context) {
 if(!core || !core->ready || !pads) return 0;
 try {
 memcpy(core->input.pads,pads,2);
 core->audio.sink=audio; core->audio.context=context;
 PushChrBattery(core);
 core->emu->Run();
 PullChrBattery(core);
 core->audio.sink=nullptr; core->audio.context=nullptr;
 if(pixels) {
  auto f=core->emu->GetPpuFrame();
  auto src=(const uint16_t*)f.FrameBuffer;
  if(src && f.Width==256 && f.Height<=240 && pitch>=256) {
   for(unsigned y=0; y<f.Height; ++y) {
    uint32_t *dst=pixels+y*pitch;
    for(unsigned x=0; x<256; ++x) dst[x]=core->palette[src[y*256+x]&511];
   }
  }
 }
 return 1;
 } catch(...) {
  /* Exceptions from the imported C++ core must not cross the legacy C ABI. */
  core->audio.sink=nullptr; core->audio.context=nullptr;
  core->ready=false;
  return 0;
 }
}
uint8_t *MesenceSram(MesenceCore *core, uint32_t *bytes) {
 *bytes=0;
 if(!core || !core->ready) return nullptr;
 auto console=(NesConsole*)core->emu->GetConsoleUnsafe();
 if(core->chrBytes) { *bytes=core->battery.size(); return core->battery.data(); }
 return console->GetMapper()->FrontendSaveRam(*bytes);
}
uint32_t MesenceFrameCount(MesenceCore *core) {
 return core && core->ready ? core->emu->GetFrameCount() : 0;
}
uint32_t MesenceFrameRate(MesenceCore *core) {
 return core && core->ready && (core->emu->GetRegion()==ConsoleRegion::Pal || core->emu->GetRegion()==ConsoleRegion::Dendy) ? 50 : 60;
}
int MesenceSnapshot(MesenceCore *core) {
 if(!core || !core->ready) return 0;
 try {
  string data=SaveConsole(core);
  if(data.size()>MESENCE_MAX_STATE_BYTES-sizeof(StateHeader)) return 0;
  StateHeader h={StateMagic,SerializerVersion,(uint32_t)(data.size()+sizeof(h)),MesenceFrameCount(core)};
  core->state.resize(h.bytes);
  memcpy(core->state.data(),&h,sizeof(h));
  memcpy(core->state.data()+sizeof(h),data.data(),data.size());
  return 1;
 } catch(...) { return 0; }
}
int MesenceAllocateState(MesenceCore *core, uint32_t bytes) {
 if(!core || bytes<=sizeof(StateHeader) || bytes>MESENCE_MAX_STATE_BYTES) return 0;
 try { core->state.resize(bytes); return 1; } catch(...) { return 0; }
}
uint8_t *MesenceStateData(MesenceCore *core, uint32_t *bytes) {
 *bytes=core ? core->state.size() : 0;
 return *bytes ? core->state.data() : nullptr;
}
int MesenceRestore(MesenceCore *core) {
 if(!core || !core->ready || core->state.size()<=sizeof(StateHeader)) return 0;
 StateHeader h; memcpy(&h,core->state.data(),sizeof(h));
 if(h.magic!=StateMagic || h.version!=SerializerVersion || h.bytes!=core->state.size()) return 0;
 string backup;
 try {
  string data((char*)core->state.data()+sizeof(h),h.bytes-sizeof(h));
  /* Capture before changing any fields. Failed loads restore the live game. */
  backup=SaveConsole(core);
  if(!LoadConsole(core,data) || MesenceFrameCount(core)!=h.frame) {
   if(!LoadConsole(core,backup)) core->ready=false;
   return 0;
  }
  PullChrBattery(core);
  return 1;
 } catch(...) {
  if(!backup.empty()) {
   try { if(!LoadConsole(core,backup)) core->ready=false; }
   catch(...) { core->ready=false; }
  }
  return 0;
 }
}
}
