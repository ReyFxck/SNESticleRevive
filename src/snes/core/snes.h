
#ifndef _SNES_H
#define _SNES_H

#include "emusys.h"

extern "C" {
#include "sndisasm.h"
#include "sncpu.h"
#include "snspcdisasm.h"
#include "snspc.h"
}
#include "snspcio.h"
#include "snspcdsp.h"
#include "snspcmix.h"
#include "snio.h"
#include "snppu.h"
#include "sndma.h"
#include "snrom.h"
#include "snppurender.h"
#include "sndebug.h"

#include "sndsp1.h"
#include "sndsp2.h"
#include "sndsp4.h"
#include "sndsp1_lle.h"
#include "snobc1.h"
#include "sncx4.h"
#include "snsdd1.h"
#include "snsrtc.h"

#define SNES_RAMSIZE  0x20000
#define SNES_SRAMSIZE (256 * 1024)

#define SNES_DSP1 1

class SnesSystem : public Emu::System
{
public:
    SnesSystem();
    ~SnesSystem();
    SNCpuT *GetCpu() {return &m_Cpu;}
    SNSpcT *GetSpc() {return &m_Spc;}
    SnesPPU *GetPPU() {return &m_PPU;}

    Uint32	GetFrame() {return m_uFrame;}
    Uint8	*GetSRAM() {return m_SRam;}

    // Nome do firmware de DSP (ex.: "dsp4") que o jogo selecionado
    // precisa mas NAO foi encontrado.  NULL = nada faltando.  A UI
    // consulta isto apos carregar a ROM para avisar o usuario na tela
    // (em vez de simplesmente travar sem o chip).
    const char *GetMissingDspFirmware() { return m_pMissingDspFw; }

    void 	SetRom(class Emu::Rom *pRom);
    void	SetSnesRom(SnesRom *pRom);
    void	Reset();
    void	SoftReset();
	void	ExecuteFrame(Emu::SysInputT *pInput, class CRenderSurface *pTarget, class CMixBuffer *pSound, ModeE eMode);

    void	SaveState(struct SnesStateT *pState);
    Bool	RestoreState(struct SnesStateT *pState);

    void    SaveState(void *pState, Int32 nStateBytes);
    void    RestoreState(void *pState, Int32 nStateBytes);
    Int32   GetStateSize();

    Int32   GetSRAMBytes();
    Uint8   *GetSRAMData();

	virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate() {return 32000;}

    static const Char *GetRegName(Uint32 uAddr);


private:
	SNCpuT		m_Cpu;
	SnesPPU		m_PPU;
	SnesDMAC	m_DMAC;
	SnesIO		m_IO;
	SNSpcIO		m_SpcIO;
	SNSpcT		m_Spc;
	SNSpcDsp	m_SpcDsp;

	// extra hardware
	ISNDSP		*m_pDsp;

	// nome do firmware DSP que o jogo precisa mas faltou carregar
	// (ex.: "dsp4").  NULL = nada faltando.  Setado em MapMem.
	const char	*m_pMissingDspFw;

#if SNES_DSP1
	SNDSP1		m_DSP1;
	SNDSP2		m_DSP2;
	// DSP-4 (Top Gear 3000): HLE self-contained, sem firmware.
	SNDSP4		m_DSP4;
	// Nucleo uPD7725 LLE compartilhado, usado para DSP-3 (SD Gundam GX):
	// so' muda o firmware carregado nele.
	SNDSP1_LLE	m_DSP_LLE;
#endif

	SNOBC1		m_OBC1;

	SNCX4		m_CX4;

	SNSDD1		m_SDD1;
	Bool		m_bSDD1;

	SNSRTC		m_SRTC;
	Bool		m_bSRTC;

	SnesRom		*m_pRom;

	SnesPPURender	m_PPURender;
	SNSpcDspMixFull		m_SpcDspMixer;		    // non-deterministic mixer
	SNSpcDspMixSilent	m_SpcDspSilentMixer;    // deterministic mixer

	Uint32		m_uSramSize;
	Uint8		m_Ram[SNES_RAMSIZE] _ALIGN(16);
	Uint8		m_SRam[SNES_SRAMSIZE] _ALIGN(16);


private:
	static Uint8 SNCPU_TRAPFUNC ReadMem(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteMem(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC Read2000(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  Write2000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC Read4000(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  Write4000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadSRAM(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteSRAM(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadDSP1(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteDSP1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadOBC1(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteOBC1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadCX4(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteCX4(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 CX4ReadMem(void *pCtx, Uint32 uAddr);

    static Uint8 SNCPU_TRAPFUNC Read2000Debug(SNCpuT *pCpu, Uint32 uAddr);
    static Uint8 SNCPU_TRAPFUNC Read4000Debug(SNCpuT *pCpu, Uint32 uAddr);
    static void SNCPU_TRAPFUNC  Write2000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
    static void SNCPU_TRAPFUNC  Write4000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);



	void	MapLoRom();
	void	MapHiRom();
	void	MapMem(struct SnesMemMapT *pMemMap);
	void	MapMem(SNRomMappingE eRomMapping, Uint32 uFlags);
	void	MapMemExLoRom(void);
	// Carrega o firmware combinado (program+data) do uPD7725 para o
	// nucleo LLE compartilhado, procurando o arquivo <name>.rom em
	// pastas conhecidas.  Devolve TRUE se conseguiu carregar.
	Bool	LoadDspFirmware(const char *name);
	void	RemapSDD1(void);   // (re)mapeia $C0-$FF conforme $4804-$4807
	void	DumpMemMap();

	void	SetFastRom();
	void	SetSlowRom();

	void	SyncSPC(Int32 uExtra = 0);
	void	SyncPPU();
	void	ExecuteLine();
    void    ExecuteWithIRQ(Int32 nCycles, Int32 &nIRQCycles);
    void    ExecuteCPU(Int32 nExecCycles);
};

void SnesDebugBegin(SnesSystem *pSnes, const char *pFileName);
void SnesDebugEnd();

#endif

