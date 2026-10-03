#include "nessystem.h"
#include "../mesence/mesence_bridge.h"
#include "rendersurface.h"
#include "mixbuffer.h"
#include "snio.h"
#include <string.h>
#include <stdio.h>
extern NesRom *_pNesRom;

NesSystem::NesSystem() : m_pCore(MesenceCreate()), m_pNesRom(NULL),
 m_pNesDisk(NULL), m_pCHRRam(NULL), m_bInitialized(FALSE), m_bRomReady(FALSE), m_uFrameTick(0)
{ m_uFrame=0; m_uLine=0; }
NesSystem::~NesSystem() { MesenceDestroy(m_pCore); }
void NesSystem::SetRom(Emu::Rom *rom) {
 MesenceUnload(m_pCore); m_pNesRom=NULL; m_bRomReady=FALSE; m_uFrame=0; m_uFrameTick=0;
 if(rom && rom==_pNesRom) {
  m_pNesRom=(NesRom*)rom;
  m_bRomReady=MesenceLoad(m_pCore,m_pNesRom->GetData(),m_pNesRom->GetBytes()) ? TRUE : FALSE;
  if(!m_bRomReady) printf("[MesenCE] Could not initialize this cartridge.\n");
 }
}
void NesSystem::Reset() { if(!MesenceReset(m_pCore,1)) m_bRomReady=FALSE; m_uFrame=MesenceFrameCount(m_pCore); }
void NesSystem::SoftReset() { if(!MesenceReset(m_pCore,0)) m_bRomReady=FALSE; m_uFrame=MesenceFrameCount(m_pCore); }
static Uint8 MapPad(Uint16 pad, Bool turbo) {
 if(pad==EMUSYS_DEVICE_DISCONNECTED) return 0;
 Uint8 result=0;
 if((pad&SNESIO_JOY_B)||((pad&SNESIO_JOY_A)&&turbo)) result|=1;
 if((pad&SNESIO_JOY_Y)||((pad&SNESIO_JOY_X)&&turbo)) result|=2;
 if(pad&SNESIO_JOY_SELECT) result|=4;
 if(pad&SNESIO_JOY_START) result|=8;
 if(pad&SNESIO_JOY_UP) result|=16;
 if(pad&SNESIO_JOY_DOWN) result|=32;
 if(pad&SNESIO_JOY_LEFT) result|=64;
 if(pad&SNESIO_JOY_RIGHT) result|=128;
 return result;
}
static void AudioSink(void *context, const int16_t *left, const int16_t *right, uint32_t frames) {
 ((CMixBuffer*)context)->OutputSamplesStereo((Int16*)left,(Int16*)right,frames);
}
void NesSystem::ExecuteFrame(Emu::SysInputT *input, CRenderSurface *target,
 CMixBuffer *mix, ModeE mode) {
 (void)mode;
 if(!m_bRomReady) { if(target) target->Clear(); return; }
 Uint8 pads[2]={0,0};
 if(input) for(int i=0;i<2;++i) pads[i]=MapPad(input->uPad[i],(m_uFrame&1)==0);
 Uint32 *pixels=target ? (Uint32*)target->GetLinePtr(0) : NULL;
 if(!MesenceFrame(m_pCore,pads,(uint32_t*)pixels,256,mix ? AudioSink : NULL,mix)) {
  m_bRomReady=FALSE;
  if(target) target->Clear();
  printf("[MesenCE] Cartridge execution failed; return to the menu to reload.\n");
 }
 m_uFrame=MesenceFrameCount(m_pCore); m_uFrameTick=m_uFrame;
}
Int32 NesSystem::GetStateSize() { return SnapshotState() ? (Int32)GetSnapshotBytes() : 0; }
void NesSystem::SaveState(void *state, Int32 bytes) {
 if(state && SnapshotState() && bytes>=(Int32)GetSnapshotBytes())
  memcpy(state,GetSnapshotData(),GetSnapshotBytes());
}
void NesSystem::RestoreState(void *state, Int32 bytes) {
 if(state && bytes>0 && AllocateState(bytes)) {
  memcpy(GetSnapshotData(),state,bytes); RestoreSnapshot();
 }
}
Bool NesSystem::SnapshotState() { return MesenceSnapshot(m_pCore) ? TRUE : FALSE; }
Bool NesSystem::AllocateState(Uint32 bytes) { return MesenceAllocateState(m_pCore,bytes) ? TRUE : FALSE; }
Uint8 *NesSystem::GetSnapshotData() { uint32_t bytes; return MesenceStateData(m_pCore,&bytes); }
Uint32 NesSystem::GetSnapshotBytes() { uint32_t bytes; MesenceStateData(m_pCore,&bytes); return bytes; }
Bool NesSystem::RestoreSnapshot() {
 if(!MesenceRestore(m_pCore)) return FALSE;
 m_uFrame=MesenceFrameCount(m_pCore); m_uFrameTick=m_uFrame; return TRUE;
}
Uint8 *NesSystem::GetSRAMData() { uint32_t bytes; return MesenceSram(m_pCore,&bytes); }
Int32 NesSystem::GetSRAMBytes() { uint32_t bytes; MesenceSram(m_pCore,&bytes); return bytes; }
Uint32 NesSystem::GetFrameRate() { return MesenceFrameRate(m_pCore); }
Uint32 NesSystem::GetSampleRate() { return 48000; }
const char *NesSystem::GetString(StringE type) {
 switch(type) {
  case STRING_SHORTNAME: return "NES";
  case STRING_FULLNAME: return "MesenCE NES";
  case STRING_SRAMEXT: return "srm";
  case STRING_STATEEXT: return "nst";
 }
 return "";
}
