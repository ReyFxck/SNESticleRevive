/* nesrom.cpp - NesRom / NesDisk / NesFDSBios implementations.
 *
 * Phase 2 of the NES integration: enough to read the file through the
 * iaddis CDataIO abstraction, validate the iNES header, and report
 * basic metadata to the rest of the mainloop (so the ROM appears in
 * the browser and the load path doesn't reject it). The InfoNES core
 * itself is NOT yet driven from this data - that wiring lives in
 * NesSystem::SetRom() in Phase 3.
 *
 * The iNES file format (NESDEV reference):
 *   bytes 0..3   "NES\x1A"
 *   byte  4      number of 16 KB PRG ROM banks
 *   byte  5      number of  8 KB CHR ROM banks (0 = CHR RAM)
 *   byte  6      flags6: mirroring, battery SRAM, trainer, four-screen,
 *                lower nibble of mapper number
 *   byte  7      flags7: VS-system, PlayChoice, NES2.0 flag,
 *                upper nibble of mapper number
 *   bytes 8..15  flags8..15 (extended size info, region, etc.)
 *   bytes 16..(16+512 if trainer): optional 512-byte trainer block
 *   then PRG ROM, then CHR ROM.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "nesrom.h"
#include "dataio.h"


/* ============================================================== NesRom */

NesRom::NesRom()
{
    m_bLoaded       = FALSE;
    m_pRomMem       = NULL;
    m_pRomData      = NULL;
    m_uRomBytes     = 0;
    m_uMapperNo     = 0;
    m_uFlags6       = 0;
    m_uFlags7       = 0;
    m_uPrgRomBanks  = 0;
    m_uChrRomBanks  = 0;
    m_szTitle[0]    = 0;
    m_szMapper[0]   = 0;
}

NesRom::~NesRom()
{
    Unload();
}

void NesRom::Unload()
{
    if (m_pRomMem)
    {
        free(m_pRomMem);
        m_pRomMem = NULL;
    }
    m_pRomData      = NULL;
    m_uRomBytes     = 0;
    m_uMapperNo     = 0;
    m_uFlags6       = 0;
    m_uFlags7       = 0;
    m_uPrgRomBanks  = 0;
    m_uChrRomBanks  = 0;
    m_szTitle[0]    = 0;
    m_szMapper[0]   = 0;
    m_bLoaded       = FALSE;
}

Emu::Rom::LoadErrorE NesRom::LoadRom(CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
    Unload();

    if (!pFileIO)
    {
        return LOADERROR_OPENFILE;
    }

    /* Determine size by seeking to the end - CDataIO has no Length(),
       so we follow the SnesRom::LoadRom pattern (snrom.cpp:423). */
    pFileIO->Seek(0, SEEK_END);
    Uint32 uTotal = (Uint32)pFileIO->GetPos();
    pFileIO->Seek(0, SEEK_SET);
    if (uTotal < 16)
    {
        printf("[NesRom] file is smaller than iNES header (%u bytes)\n", uTotal);
        return LOADERROR_BADHEADERSIZE;
    }

    /* Allocate (or accept a caller buffer). We over-allocate a small
       trailer so InfoNES can read one mapper bank past the end
       without a special bounds check. */
    Uint8 *pBuf;
    if (pBuffer && nBufferBytes >= uTotal + 8)
    {
        pBuf      = pBuffer;
        m_pRomMem = NULL; /* caller owns the buffer */
    }
    else
    {
        m_pRomMem = (Uint8 *)malloc(uTotal + 8);
        if (!m_pRomMem)
        {
            return LOADERROR_OUTOFSPACE;
        }
        pBuf = m_pRomMem;
    }

    /* Read the entire file into the buffer in one shot. */
    size_t nRead = pFileIO->Read(pBuf, (Int32)uTotal);
    if (nRead != uTotal)
    {
        printf("[NesRom] short read: got %u / %u\n", (Uint32)nRead, uTotal);
        Unload();
        return LOADERROR_READFILE;
    }

    /* iNES magic check: "NES\x1A". */
    if (pBuf[0] != 'N' || pBuf[1] != 'E' || pBuf[2] != 'S' || pBuf[3] != 0x1A)
    {
        printf("[NesRom] bad iNES magic: %02X %02X %02X %02X\n",
               pBuf[0], pBuf[1], pBuf[2], pBuf[3]);
        Unload();
        return LOADERROR_INVALID;
    }

    /* Parse the rest of the iNES header. */
    m_uPrgRomBanks = pBuf[4];
    m_uChrRomBanks = pBuf[5];
    m_uFlags6      = pBuf[6];
    m_uFlags7      = pBuf[7];
    m_uMapperNo    = (m_uFlags6 >> 4) | (m_uFlags7 & 0xF0);

    /* Sanity-check the file size against what the header advertises.
       trainer (if present) + 16K * PRG banks + 8K * CHR banks. */
    Uint32 uExpected = 16;
    if (m_uFlags6 & 0x04) uExpected += 512;             /* trainer */
    uExpected += (Uint32)m_uPrgRomBanks * 16 * 1024;
    uExpected += (Uint32)m_uChrRomBanks * 8  * 1024;

    if (uTotal < uExpected)
    {
        printf("[NesRom] file (%u) smaller than iNES header expects (%u)\n",
               uTotal, uExpected);
        Unload();
        return LOADERROR_BADROMSIZE;
    }

    m_pRomData  = pBuf;
    m_uRomBytes = uTotal;

    /* The iNES title field (if present in archival dumps) lives in
       bytes 0..F of the "diskdude!" zone at the end of the header.
       It's almost always all zeros, so we just leave m_szTitle empty
       for now and let the browser fall back to the filename. */
    m_szTitle[0] = 0;

    snprintf(m_szMapper, sizeof(m_szMapper), "iNES mapper %u", m_uMapperNo);

    printf("[NesRom] loaded: %u PRG / %u CHR banks, mapper %u, %s%s, %u bytes\n",
           m_uPrgRomBanks, m_uChrRomBanks, m_uMapperNo,
           HasBatterySRAM() ? "battery " : "",
           HasTrainer()     ? "trainer"  : "no-trainer",
           m_uRomBytes);

    m_bLoaded = TRUE;
    return LOADERROR_NONE;
}

Char *NesRom::GetMapperName()
{
    return m_szMapper[0] ? m_szMapper : (Char *)"iNES";
}


/* ============================================================== NesDisk */

NesDisk::NesDisk()
{
    m_bLoaded     = FALSE;
    m_pDiskMem    = NULL;
    m_pDiskData   = NULL;
    m_uDiskBytes  = 0;
    m_uNumDisks   = 0;
}

NesDisk::~NesDisk()
{
    Unload();
}

void NesDisk::Unload()
{
    if (m_pDiskMem)
    {
        free(m_pDiskMem);
        m_pDiskMem = NULL;
    }
    m_pDiskData   = NULL;
    m_uDiskBytes  = 0;
    m_uNumDisks   = 0;
    m_bLoaded     = FALSE;
}

Emu::Rom::LoadErrorE NesDisk::LoadRom(CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
    /* Phase 2: FDS disks not yet routed to InfoNES. We still parse the
       size + accept the file so the browser path doesn't error. */
    Unload();

    if (!pFileIO)
    {
        return LOADERROR_OPENFILE;
    }

    pFileIO->Seek(0, SEEK_END);
    Uint32 uTotal = (Uint32)pFileIO->GetPos();
    pFileIO->Seek(0, SEEK_SET);
    if (uTotal == 0)
    {
        return LOADERROR_BADROMSIZE;
    }

    Uint8 *pBuf;
    if (pBuffer && nBufferBytes >= uTotal)
    {
        pBuf       = pBuffer;
        m_pDiskMem = NULL;
    }
    else
    {
        m_pDiskMem = (Uint8 *)malloc(uTotal);
        if (!m_pDiskMem)
        {
            return LOADERROR_OUTOFSPACE;
        }
        pBuf = m_pDiskMem;
    }

    size_t nRead = pFileIO->Read(pBuf, (Int32)uTotal);
    if (nRead != uTotal)
    {
        Unload();
        return LOADERROR_READFILE;
    }

    /* FDS dumps come in two flavours: raw (each side = 65500 bytes) or
       fwNES-headered (16-byte "FDS\x1A" prefix + raw). Either way,
       NumDisks = floor(size / 65500). */
    Uint8 *pData     = pBuf;
    Uint32 uDataLen  = uTotal;
    if (uTotal >= 16 && pBuf[0] == 'F' && pBuf[1] == 'D' && pBuf[2] == 'S' && pBuf[3] == 0x1A)
    {
        pData    = pBuf + 16;
        uDataLen = uTotal - 16;
    }

    m_pDiskData  = pData;
    m_uDiskBytes = uDataLen;
    m_uNumDisks  = uDataLen / 65500;
    if (m_uNumDisks < 1) m_uNumDisks = 1;

    printf("[NesDisk] loaded: %u bytes, %u disk side(s)\n", m_uDiskBytes, m_uNumDisks);

    m_bLoaded = TRUE;
    return LOADERROR_NONE;
}


/* ============================================================== NesFDSBios */

NesFDSBios::NesFDSBios()
{
    m_bLoaded     = FALSE;
    m_pBiosMem    = NULL;
    m_pBiosData   = NULL;
    m_uBiosBytes  = 0;
}

NesFDSBios::~NesFDSBios()
{
    Unload();
}

void NesFDSBios::Unload()
{
    if (m_pBiosMem)
    {
        free(m_pBiosMem);
        m_pBiosMem = NULL;
    }
    m_pBiosData   = NULL;
    m_uBiosBytes  = 0;
    m_bLoaded     = FALSE;
}

Emu::Rom::LoadErrorE NesFDSBios::LoadRom(CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
    Unload();

    if (!pFileIO)
    {
        return LOADERROR_OPENFILE;
    }

    pFileIO->Seek(0, SEEK_END);
    Uint32 uTotal = (Uint32)pFileIO->GetPos();
    pFileIO->Seek(0, SEEK_SET);
    /* The FDS BIOS is exactly 8 KB. Some dumps include a 16-byte header
       so allow up to 8 KB + 16 bytes. */
    if (uTotal < 8 * 1024 || uTotal > 8 * 1024 + 64)
    {
        printf("[NesFDSBios] unexpected size %u (want 8192)\n", uTotal);
        return LOADERROR_BADROMSIZE;
    }

    Uint8 *pBuf;
    if (pBuffer && nBufferBytes >= uTotal)
    {
        pBuf       = pBuffer;
        m_pBiosMem = NULL;
    }
    else
    {
        m_pBiosMem = (Uint8 *)malloc(uTotal);
        if (!m_pBiosMem)
        {
            return LOADERROR_OUTOFSPACE;
        }
        pBuf = m_pBiosMem;
    }

    size_t nRead = pFileIO->Read(pBuf, (Int32)uTotal);
    if (nRead != uTotal)
    {
        Unload();
        return LOADERROR_READFILE;
    }

    m_pBiosData  = pBuf;
    m_uBiosBytes = uTotal;
    printf("[NesFDSBios] loaded: %u bytes\n", m_uBiosBytes);

    m_bLoaded = TRUE;
    return LOADERROR_NONE;
}
