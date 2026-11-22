/*===================================================================*/
/*                                                                   */
/*  InfoNES_pAPU.cpp : cycle-timed NES 2A03 APU bridge               */
/*                                                                   */
/*  SNESticleRevive uses Shay Green's Nes_Snd_Emu + Blip_Buffer      */
/*  (LGPL-2.1) for the five base channels.  Register writes remain   */
/*  wired through InfoNES, but are applied at their CPU-cycle time    */
/*  instead of being approximated in five frame-sized PCM passes.     */
/*                                                                   */
/*===================================================================*/

#include "K6502.h"
#include "K6502_rw.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "third_party/nes_snd_emu/Nes_Apu.h"
#include "third_party/nes_snd_emu/Blip_Buffer.h"
#include <new>

/* NTSC 2A03 CPU clock.  Blip_Buffer uses this clock directly, so timer
   periods produce the correct pitch without a frame-local PCM resampler. */
#define NES_APU_CLOCK_HZ       1789773u
#define NES_APU_OUTPUT_HZ        32000u
#define NES_APU_FRAME_CYCLES      29830u
#define INFONES_FRAME_SCANLINES     263u
#define INFONES_RAW_FRAME_CYCLES \
  ((unsigned int)STEP_PER_SCANLINE * INFONES_FRAME_SCANLINES)

#define INFONES_BLIP_STATE_MAGIC   0x4E424C50u /* "NBLP" */
#define INFONES_BLIP_STATE_VERSION 1u
#define INFONES_LEGACY_APU_MAGIC   0x41505553u /* "APUS" */

struct InfoNESBlipStateT
{
  unsigned int uMagic;
  unsigned int uVersion;
  apu_state_t Apu;
};

typedef char InfoNESBlipStateFits[
  sizeof(InfoNESBlipStateT) <= INFONES_APU_STATE_MAX ? 1 : -1
];

/* The PS2SDK startup used by this project walks .ctors, while current GCC
   emits these non-trivial constructors into .init_array.  Keeping Nes_Apu
   and Blip_Buffer as ordinary global objects therefore leaves them merely
   zero-filled on real PS2 startup; the first output() call then dereferences
   null oscillator pointers.  Give both objects static lifetime storage but
   construct them explicitly before their first use. */
static unsigned char s_NesApuStorage[sizeof(Nes_Apu)]
  __attribute__((aligned(16)));
static unsigned char s_NesBlipStorage[sizeof(Blip_Buffer)]
  __attribute__((aligned(16)));
static Nes_Apu     *s_pNesApu;
static Blip_Buffer *s_pNesBlip;
static int          s_ApuObjectsConstructed;
static WORD        s_EnterClock;
static int         s_LastApuTime;
static int         s_FrameEndTime;
static int         s_ApuReady;

static void InfoNES_ApuConstructObjects( void )
{
  if ( s_ApuObjectsConstructed )
    return;

  s_pNesBlip = ::new (s_NesBlipStorage) Blip_Buffer;
  s_pNesApu = ::new (s_NesApuStorage) Nes_Apu;
  s_ApuObjectsConstructed = 1;
}

/* Convert InfoNES's integer scanline CPU budget to the canonical NTSC APU
   timeline.  InfoNES executes 263 * 113 cycles between normal VSync calls;
   scaling that interval to 29,830 cycles keeps both note pitch and the
   32-kHz sample cadence stable without changing CPU/PPU gameplay timing. */
static int InfoNES_ApuScaleTime( unsigned int rawCycles )
{
  unsigned long long scaled =
    (unsigned long long)rawCycles * NES_APU_FRAME_CYCLES +
    INFONES_RAW_FRAME_CYCLES / 2;
  scaled /= INFONES_RAW_FRAME_CYCLES;
  if ( scaled > (unsigned int)s_FrameEndTime )
    scaled = (unsigned int)s_FrameEndTime;
  return (int)scaled;
}

static int InfoNES_ApuCurrentTime( void )
{
  unsigned int raw = (WORD)( K6502_GetPassedClocks() - s_EnterClock );
  int time = InfoNES_ApuScaleTime( raw );

  /* CPU memory callbacks occur before the instruction's final CLK() in the
     legacy 6502 interpreter.  Clamping also protects Nes_Apu's monotonic
     time contract if an instruction straddles the end of a video frame. */
  if ( time < s_LastApuTime )
    time = s_LastApuTime;
  if ( time > s_FrameEndTime )
    time = s_FrameEndTime;
  return time;
}

static int InfoNES_ApuCyclesToNextVsync( void )
{
  int scanline = (int)PPU_Scanline;
  /* InfoNES calls pAPUVsync from HSync after the CPU budget for
     SCAN_VBLANK_START has already run, so the interval includes that
     scanline too. This matters on ROM/reset and immediately after loading a
     state; steady-state intervals are always the full 263 scanlines. */
  int scans = SCAN_VBLANK_START - scanline + 1;
  if ( scans <= 0 )
    scans += (int)INFONES_FRAME_SCANLINES;
  return InfoNES_ApuScaleTime(
    (unsigned int)scans * (unsigned int)STEP_PER_SCANLINE );
}

static int InfoNES_DmcRead( void *, nes_addr_t addr )
{
  return K6502_Read( (WORD)addr );
}

static void InfoNES_ApuIrqChanged( void * )
{
  if ( s_ApuReady &&
       s_pNesApu->earliest_irq( s_LastApuTime ) == Nes_Apu::irq_waiting )
  {
    IRQ_REQ;
  }
}

static void InfoNES_ApuWriteRegister( WORD addr, BYTE value )
{
  if ( !s_ApuReady )
    return;

  int time = InfoNES_ApuCurrentTime();
  s_pNesApu->write_register( time, addr, value );
  s_LastApuTime = time;
}

/* InfoNES dispatches $4000-$4013 through this table. */
ApuWritefunc pAPUSoundRegs[20] =
{
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister,
  InfoNES_ApuWriteRegister, InfoNES_ApuWriteRegister
};

void InfoNES_pAPUWriteControl( WORD addr, BYTE value )
{
  InfoNES_ApuWriteRegister( addr, value );
}

void InfoNES_pAPUWriteFrameCounter( WORD addr, BYTE value )
{
  InfoNES_ApuWriteRegister( addr, value );
}

BYTE InfoNES_pAPUReadStatus( void )
{
  if ( !s_ApuReady )
    return 0;

  int time = InfoNES_ApuCurrentTime();
  int status = s_pNesApu->read_status( time );
  s_LastApuTime = time;
  return (BYTE)status;
}

static void InfoNES_ApuDrainSamples( void )
{
  static short Samples[1024];
  long available;

  while ( (available = s_pNesBlip->samples_avail()) > 0 )
  {
    if ( available > (long)(sizeof Samples / sizeof Samples[0]) )
      available = (long)(sizeof Samples / sizeof Samples[0]);
    long count = s_pNesBlip->read_samples( Samples, available );
    if ( count <= 0 )
      break;
    InfoNES_SoundOutputSamples( Samples, (int)count );
  }
}

void InfoNES_pAPUVsync( void )
{
  if ( !s_ApuReady )
    return;

  int endTime = s_FrameEndTime;
  if ( endTime < s_LastApuTime )
    endTime = s_LastApuTime;

  s_pNesApu->end_frame( endTime );
  s_pNesBlip->end_frame( endTime );
  InfoNES_ApuDrainSamples();

  s_EnterClock = K6502_GetPassedClocks();
  s_LastApuTime = 0;
  s_FrameEndTime = NES_APU_FRAME_CYCLES;
}

int InfoNES_pAPUSaveState( void *pState, int nStateBytes )
{
  InfoNESBlipStateT State;

  if ( !pState || !s_ApuReady || nStateBytes < (int)sizeof State )
    return 0;

  int time = InfoNES_ApuCurrentTime();
  s_pNesApu->synchronize( time );
  s_LastApuTime = time;

  InfoNES_MemorySet( &State, 0, sizeof State );
  State.uMagic = INFONES_BLIP_STATE_MAGIC;
  State.uVersion = INFONES_BLIP_STATE_VERSION;
  s_pNesApu->save_state( &State.Apu );

  InfoNES_MemorySet( pState, 0, nStateBytes );
  InfoNES_MemoryCopy( pState, &State, sizeof State );
  return (int)sizeof State;
}

static void InfoNES_ApuReplayRegisters( void )
{
  int addr;

  s_pNesApu->reset( false, APU_Reg[0x11] & 0x7f );
  for ( addr = 0x4000; addr <= 0x4013; addr++ )
    s_pNesApu->write_register( 0, (nes_addr_t)addr, APU_Reg[addr & 0x1f] );
  s_pNesApu->write_register( 0, 0x4015, APU_Reg[0x15] & 0x1f );
  s_pNesApu->write_register( 0, 0x4017, APU_Reg[0x17] );
}

int InfoNES_pAPULoadState( const void *pState, int nStateBytes )
{
  InfoNESBlipStateT Image;
  unsigned int magic = 0;

  if ( !pState || !s_ApuReady || nStateBytes < (int)(2 * sizeof magic) )
    return 0;

  /* NesStateT stores this blob in a byte array whose address is only
     four-byte aligned. Copy into an aligned local before touching the
     snapshot's double/32-bit fields; direct casts can trap on the EE. */
  InfoNES_MemoryCopy( &magic, pState, sizeof magic );
  InfoNES_MemorySet( &Image, 0, sizeof Image );
  if ( nStateBytes == (int)sizeof Image )
    InfoNES_MemoryCopy( &Image, pState, sizeof Image );

  s_pNesBlip->clear();

  if ( nStateBytes == (int)sizeof Image &&
       Image.uMagic == INFONES_BLIP_STATE_MAGIC &&
       Image.uVersion == INFONES_BLIP_STATE_VERSION &&
       Image.Apu.magic == NES_APU_STATE_MAGIC &&
       Image.Apu.version == NES_APU_STATE_VERSION )
  {
    /* Reset establishes synth gains and callback-owned pointers; load_state
       restores only pointer-free oscillator/frame/DMC data. */
    s_pNesApu->reset( false, 0 );
    s_pNesApu->load_state( Image.Apu );
  }
  else if ( magic == INFONES_LEGACY_APU_MAGIC )
  {
    /* r5/r6 states used the old InfoNES event renderer.  Its private phase
       layout cannot be translated exactly, but replaying the restored APU
       registers preserves compatibility and resumes every enabled channel. */
    InfoNES_ApuReplayRegisters();
  }
  else
  {
    return 0;
  }

  InfoNES_SoundReset();
  s_EnterClock = K6502_GetPassedClocks();
  s_LastApuTime = 0;
  s_FrameEndTime = NES_APU_FRAME_CYCLES;
  s_FrameEndTime = InfoNES_ApuCyclesToNextVsync();
  return 1;
}

void InfoNES_pAPUInit( void )
{
  InfoNES_SoundInit();
  s_ApuReady = 0;
  InfoNES_ApuConstructObjects();

  /* 25 ms holds one 60-Hz frame plus Blip's impulse tail and allows the
     higher clock/sample fixed-point precision selected in blargg_config.h. */
  if ( s_pNesBlip->set_sample_rate( NES_APU_OUTPUT_HZ, 25 ) )
  {
    InfoNES_MessageBox( "Unable to allocate NES audio buffer" );
    return;
  }

  s_pNesBlip->clock_rate( NES_APU_CLOCK_HZ );
  s_pNesBlip->bass_freq( 20 );
  s_pNesBlip->clear();

  s_pNesApu->output( s_pNesBlip );
  s_pNesApu->dmc_reader( InfoNES_DmcRead, 0 );
  s_pNesApu->irq_notifier( InfoNES_ApuIrqChanged, 0 );
  s_pNesApu->volume( 1.0 );
  s_pNesApu->reset( false, 0 );

  InfoNES_SoundOpen( 0, NES_APU_OUTPUT_HZ );
  s_EnterClock = 0; /* K6502_Reset follows InfoNES_pAPUInit. */
  s_LastApuTime = 0;
  s_FrameEndTime = NES_APU_FRAME_CYCLES;
  s_FrameEndTime = InfoNES_ApuCyclesToNextVsync();
  s_ApuReady = 1;
}

void InfoNES_pAPUDone( void )
{
  s_ApuReady = 0;
  if ( s_ApuObjectsConstructed )
  {
    s_pNesApu->output( 0 );
    s_pNesBlip->clear();
  }
  InfoNES_SoundClose();
}
