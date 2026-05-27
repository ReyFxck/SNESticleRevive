/* nesrom.h
 *
 * Emu::Rom wrappers for NES content. There are three subclasses because
 * the iaddis mainloop dispatches ROM types by class identity (it asks
 * each wrapper for its extensions and registers them with the browser
 * separately):
 *
 *  - NesRom       (".nes") - a standard iNES cartridge image. The
 *                  iNES header is the canonical NES dump format and
 *                  encodes mapper number, PRG/CHR bank counts,
 *                  mirroring, battery-backed SRAM, trainer presence.
 *
 *  - NesDisk      (".fds") - a Famicom Disk System ramdisk dump. The
 *                  FDS shipped its games as floppy disks rather than
 *                  cartridges and needs a separate BIOS to boot
 *                  (disksys.rom); this wrapper holds the disk image
 *                  itself. The user can swap "sides" / disks at
 *                  runtime via the mainloop input handler.
 *
 *  - NesFDSBios   ("rom" - matches the "disksys.rom" filename) - the
 *                  Nintendo-supplied 8 KB FDS BIOS. Loaded once and
 *                  reused for every FDS disk. The mainloop tries to
 *                  pair a .fds with a disksys.rom in the same dir.
 *
 * All three derive from Emu::Rom so they slot into the same browser
 * pipeline, _MainLoopExecuteFile() switch, and load/unload glue that
 * SnesRom uses.
 *
 * Phase 2: NesRom does a real iNES header parse and stores the raw
 * data; NesDisk / NesFDSBios are skeletal (extension + name only).
 * The InfoNES core is NOT yet pointed at any of this data - that
 * binding happens in NesSystem::SetRom() in Phase 3.
 */

#ifndef _NESROM_H
#define _NESROM_H

#include "types.h"
#include "emurom.h"

class CDataIO;

class NesRom : public Emu::Rom
{
public:
    NesRom();
    ~NesRom();

    /* iNES file. Returns LOADERROR_NONE on a successful header parse +
       data copy, or one of the LOADERROR_* codes on failure. The data
       buffer hands the .nes bytes over to the wrapper - if pBuffer is
       NULL the wrapper allocates its own. */
    virtual LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0);
    virtual void        Unload();

    /* Exposed for NesSystem::SetRom() (Phase 3) and SRAM bookkeeping. */
    Uint8           *GetData()              {return m_pRomData;}
    Uint32           GetBytes()             {return m_uRomBytes;}
    Uint32           GetMapperNumber()      {return m_uMapperNo;}
    Bool             HasBatterySRAM()       {return (m_uFlags6 & 0x02) != 0;}
    Bool             HasTrainer()           {return (m_uFlags6 & 0x04) != 0;}

    virtual Uint32   GetNumExts()                    {return 1;}
    virtual Char    *GetExtName(Uint32 uExt)         {(void)uExt; return (Char *)"nes";}
    virtual Uint32   GetNumRomRegions()              {return 1;}
    virtual Char    *GetRomRegionName(Uint32 uReg)   {(void)uReg; return (Char *)"PRG";}
    virtual Uint32   GetRomRegionSize(Uint32 uReg)   {(void)uReg; return m_uRomBytes;}
    virtual Char    *GetMapperName();
    virtual Char    *GetRomTitle()                   {return (Char *)m_szTitle;}

private:
    Uint8   *m_pRomMem;     /* base of allocation (may differ from data ptr) */
    Uint8   *m_pRomData;    /* .nes bytes (header + PRG + CHR) */
    Uint32   m_uRomBytes;
    Uint32   m_uMapperNo;
    Uint8    m_uFlags6;
    Uint8    m_uFlags7;
    Uint8    m_uPrgRomBanks; /* 16K each */
    Uint8    m_uChrRomBanks; /* 8K each */
    Char     m_szTitle[256];
    Char     m_szMapper[32];
};

class NesDisk : public Emu::Rom
{
public:
    NesDisk();
    ~NesDisk();

    virtual LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0);
    virtual void        Unload();

    Uint8           *GetData()              {return m_pDiskData;}
    Uint32           GetBytes()             {return m_uDiskBytes;}
    Uint32           GetNumDisks()          {return m_uNumDisks;}

    virtual Uint32   GetNumExts()                    {return 1;}
    virtual Char    *GetExtName(Uint32 uExt)         {(void)uExt; return (Char *)"fds";}
    virtual Uint32   GetNumRomRegions()              {return 1;}
    virtual Char    *GetRomRegionName(Uint32 uReg)   {(void)uReg; return (Char *)"DISK";}
    virtual Uint32   GetRomRegionSize(Uint32 uReg)   {(void)uReg; return m_uDiskBytes;}
    virtual Char    *GetMapperName()                 {return (Char *)"FDS";}
    virtual Char    *GetRomTitle()                   {return (Char *)"Famicom Disk System";}

private:
    Uint8   *m_pDiskMem;
    Uint8   *m_pDiskData;
    Uint32   m_uDiskBytes;
    Uint32   m_uNumDisks;
};

class NesFDSBios : public Emu::Rom
{
public:
    NesFDSBios();
    ~NesFDSBios();

    virtual LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0);
    virtual void        Unload();

    Uint8           *GetData()              {return m_pBiosData;}
    Uint32           GetBytes()             {return m_uBiosBytes;}

    virtual Uint32   GetNumExts()                    {return 1;}
    /* The mainloop pairs any disksys.rom alongside a .fds with this
       extension. "rom" is matched against the full filename via the
       browser's extension table. */
    virtual Char    *GetExtName(Uint32 uExt)         {(void)uExt; return (Char *)"rom";}
    virtual Uint32   GetNumRomRegions()              {return 1;}
    virtual Char    *GetRomRegionName(Uint32 uReg)   {(void)uReg; return (Char *)"BIOS";}
    virtual Uint32   GetRomRegionSize(Uint32 uReg)   {(void)uReg; return m_uBiosBytes;}
    virtual Char    *GetMapperName()                 {return (Char *)"FDS-BIOS";}
    virtual Char    *GetRomTitle()                   {return (Char *)"FDS BIOS";}

private:
    Uint8   *m_pBiosMem;
    Uint8   *m_pBiosData;
    Uint32   m_uBiosBytes;
};

#endif
