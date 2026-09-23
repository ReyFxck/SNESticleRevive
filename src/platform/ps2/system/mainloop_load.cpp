/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements mainloop load behavior for the PlayStation 2 application runtime.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE
#include "types.h"
#include "console.h"
#include "file.h"
#include "dataio.h"
#include "pathext.h"
#include "snppucolor.h"
#include "emurom.h"
#include "mainloop.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "snes.h"
#include "rendersurface.h"
#include "texture.h"
#include "audmixbuffer.h"
#include "emumovie.h"
#include "mainloop_load.h"
#include "embedded_irx.h"   /* HddMapPath (hdd0:/PART -> pfs0:) */
#include "sndbglog.h"

static Char s_MainLoopLoadError[192] = "Unknown ROM load error";

static void _MainLoopSetLoadError(const Char *pFormat, ...)
{
        va_list args;
        va_start(args, pFormat);
        vsnprintf(s_MainLoopLoadError, sizeof(s_MainLoopLoadError), pFormat, args);
        va_end(args);
}

const Char *MainLoopGetLastLoadError()
{
        return s_MainLoopLoadError;
}

extern "C" int MainBuildHostIOPath(const char *pGuestPath, char *pOut, int nOut);

extern "C" {
#if SNDBG_LOG
#include "miniz.h"
#endif
#include "miniz_compat.h"
}

void _MainLoopGetName(Char *pName, const Char *pPath)
{
        const Char *pFileName;

        pFileName = strrchr(pPath, '/');
        if (pFileName==NULL)
        {
                pFileName = pPath;
        } else
        {
                // skip /
                pFileName = pFileName + 1;
        }
        strcpy(pName, pFileName);
}

int _MainLoopReadBinaryData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
        int fd;
        int nBytes;

        /* One fileXio read becomes one EE->IOP RPC; the IOP-side fileXio
           server chunks and DMA-copies the request internally until EOF.
           newlib fread() uses its own small buffering and is substantially
           slower for multi-megabyte ROMs, especially on cdfs/mass/SMB. */
        /* fileXioOpen() is an IOP API, so it must receive the IOP/FIO flag.
           newlib's O_RDONLY is zero; passing it directly makes drivers such
           as cdfs reject every ROM before the first byte is read. */
        fd = fileXioOpen(pRomFile, FIO_O_RDONLY, 0);
        if (fd < 0 && strncmp(pRomFile, "host:", 5) == 0)
        {
                char uriPath[1024];
                if (MainBuildHostIOPath(pRomFile, uriPath, sizeof(uriPath)))
                {
#if SNDBG_LOG
                        DLog("[hostfs-open] binary retry uri=%s first=%d", uriPath, fd);
#endif
                        fd = fileXioOpen(uriPath, FIO_O_RDONLY, 0);
                }
        }
        if (fd < 0)
        {
                return fd;
        }

        nBytes = fileXioRead(fd, pBuffer, nBufferBytes);
        fileXioClose(fd);

        return nBytes;
}

int _MainLoopReadGZData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
        return MinizReadGZToBuffer(pRomFile, pBuffer, nBufferBytes);
}

/* Filter callback for the .zip walk: accept only entries whose name
   resolves to a recognised PathExtTypeE (i.e. a SNES rom / palette /
   etc., not arbitrary text files that happened to be archived). */
static int _MainLoopZipNameIsRom(const char *pName)
{
        PathExtTypeE eType;
        /* PathExtResolve so' escreve em pPath quando bTruncatePath=TRUE;
           passamos FALSE, entao o cast que descarta o const e' seguro.
           (O callback do MinizReadZipFirstMatch exige int(*)(const char*),
            por isso pName e' const e nao da' pra mudar a assinatura.) */
        return PathExtResolve((char *)pName, &eType, FALSE) ? 1 : 0;
}

int _MainLoopReadZipData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pZipFile, char *pFileName)
{
        int nBytes;

        nBytes = MinizReadZipFirstMatch(
                pZipFile,
                pBuffer,
                nBufferBytes,
                pFileName,
                256,
                _MainLoopZipNameIsRom);

        if (nBytes > 0)
        {
                printf("ZIP: read %s (%d)\n", pFileName ? pFileName : "", nBytes);
        }
        else
        {
                printf("ZIP: no compatible entry in %s\n", pZipFile ? pZipFile : "");
        }
        return nBytes;
}

Bool _MainLoopLoadRomData(Emu::Rom *pRom, Uint8 *pRomData, Int32 nRomBytes)
{
        CMemFileIO romfile;
        Emu::Rom::LoadErrorE eError;

        // open memoryfile for rom data
        romfile.Open(pRomData, nRomBytes);

        // load rom
        eError = pRom->LoadRom(&romfile);
        romfile.Close();

        if (eError!=Emu::Rom::LoadErrorE::LOADERROR_NONE)
        {
                ConPrint("ERROR: loading rom %d\n", eError);
                _MainLoopSetLoadError("ROM parser rejected image (code %d)", (int)eError);
                return FALSE;
        }
        return TRUE;
}

Bool _MainLoopLoadBios(Emu::Rom *pRom, const Char *pFilePath)
{
        CFileIO romfile;
        Emu::Rom::LoadErrorE eError;

        // open memoryfile for rom data
        if (!romfile.Open(pFilePath, "rb"))
        {
                ConPrint("ERROR: loading fds bios!\n");
                return FALSE;
        }

        // load rom
        eError = pRom->LoadRom(&romfile);
        romfile.Close();

        if (eError!=Emu::Rom::LoadErrorE::LOADERROR_NONE)
        {
                ConPrint("ERROR: loading rom %d\n", eError);
                return FALSE;
        }
        return TRUE;
}

Bool _MainLoopLoadSnesPalette(const char *pFileName)
{
        Uint32 *pPalData;
        pPalData = SNPPUColorGetPalette();

        return _MainLoopReadBinaryData((Uint8 *)pPalData, SNPPUCOLOR_NUM * sizeof(Uint32), pFileName) > 0;
}

void _MainLoopUnloadRom()
{

    // stop recording if we are recording
    if (s_pMovieClip->IsRecording())
    {
        printf("Movie: Record End\n");
        s_pMovieClip->RecordEnd();
    }
    // stop playing if we are playing
    if (s_pMovieClip->IsPlaying())
    {
        printf("Movie: Play End\n");
        s_pMovieClip->PlayEnd();
    }

	// unload old rom
	_pSnes->SetRom(NULL);
	_pSnesRom->Unload();

	/* Phase 2: NES unload mirrors the SNES path. NesDisk is unloaded
	   even though disk-swap input is still gated for Phase 5 - the
	   wrapper itself exists and owns memory. */
	_pNes->SetRom(NULL);
	_pNesRom->Unload();
	_pNesFDSDisk->Unload();
    _bStateSaved = FALSE;
    _pSystem = NULL;
    _RomPath[0] = 0;
    MainLoopStateOnRomChanged();

	_fbTexture[0]->Clear();
	_fbTexture[1]->Clear();
}

Bool _MainLoopExecuteFile(const char *pFileName, Bool bLoadSRAM)
{
	PathExtTypeE eType;
	Emu::Rom *pRom = NULL;
	Emu::System *pSystem = NULL;
	Emu::Rom *pBios = NULL;
	/* CBrowserScreen now builds paths into a 1024-byte buffer (m_Dir up
	   to 512 + a per-entry name up to 255), so the bespoke copy that
	   _MainLoopExecuteFile keeps for PathExtResolve()'s in-place
	   truncation has to match that size. Otherwise a long ROM path
	   silently overflows the old FileName[256] in strcpy() below. */
	char FileName[1024];
	char OriginalPath[1024];

	_MainLoopSetLoadError("Unknown ROM load error");
	if (pFileName==NULL)
	{
		_MainLoopSetLoadError("No ROM path was provided");
		return FALSE;
	}

	/* Keep the browser-facing path as well as the mapped I/O path.  For an
	   internal-HDD ROM this preserves hdd0:/PARTITION/... so save states
	   can remount that exact APA partition later; pFileName itself becomes
	   pfs0:/... below and no longer contains the partition identity. */
	snprintf(OriginalPath, sizeof(OriginalPath), "%s", pFileName);

	/* HD interno (APA): traduz "hdd0:/PARTICAO/.../rom" -> "pfs0:/.../rom"
	   (monta a particao em pfs0:).  Para os demais dispositivos e' no-op,
	   entao pFileName segue inalterado. */
	char hddPath[1024];
	if (HddMapPath(pFileName, hddPath, sizeof(hddPath)) == 1)
		pFileName = hddPath;

	// make copy of filename
	snprintf(FileName, sizeof(FileName), "%s", pFileName);

	// resolve file extension of filename
	if (!PathExtResolve(FileName, &eType, TRUE))
	{
		_MainLoopSetLoadError("Unsupported file extension");
		return FALSE;
	}

	if (eType == MAINLOOP_ENTRYTYPE_SNESPALETTE)
	{
		return _MainLoopLoadSnesPalette(pFileName);
	}

	// unload existing game
    _MainLoopUnloadRom();

    #if MAINLOOP_HISTORY
    _MainLoopResetHistory();
    #endif
	_MainLoopResetInputChecksums();

	int nRomBytes = 0;
	Uint8 *pBuffer = _RomData;
	Int32 nBufferBytes = sizeof(_RomData);
#if SNDBG_LOG
	Uint32 uRomCRC = 0;
#endif

	// load rom data from disk into our buffer
	if (eType == MAINLOOP_ENTRYTYPE_GZ)
	{
		// if its a GZ file, then the next extension is the one we use
		if (!PathExtResolve(FileName, &eType, TRUE))
		{
			_MainLoopSetLoadError("GZIP does not contain a recognized ROM name");
			return FALSE;
		}

		// load GZ-ipped data
		nRomBytes = _MainLoopReadGZData(pBuffer, nBufferBytes, pFileName);

	} else
	if (eType == MAINLOOP_ENTRYTYPE_ZIP)
	{
		// if it is a ZIP file then we have to look in the file to find the right file to load
		nRomBytes = _MainLoopReadZipData(pBuffer, nBufferBytes, pFileName, FileName);
		if (nRomBytes > 0)
		{
			// resolve extension of unzipped file
			if (!PathExtResolve(FileName, &eType, TRUE))
			{
				_MainLoopSetLoadError("ZIP entry has an unsupported ROM type");
				return FALSE;
			}
		}

	} else
	{
		// read as binary data
		nRomBytes = _MainLoopReadBinaryData(pBuffer, nBufferBytes, pFileName);
	}

	// was load successful?
	if (nRomBytes <= 0)
	{
		if (eType == MAINLOOP_ENTRYTYPE_ZIP)
		{
			switch (nRomBytes)
			{
				case MINIZ_READ_BAD_ARCHIVE:
					_MainLoopSetLoadError("ZIP is invalid or damaged");
					break;
				case MINIZ_READ_NO_MATCH:
					_MainLoopSetLoadError("ZIP has no supported ROM inside");
					break;
				case MINIZ_READ_TOO_LARGE:
					_MainLoopSetLoadError("ROM inside ZIP is too large");
					break;
				case MINIZ_READ_EXTRACT_FAIL:
					_MainLoopSetLoadError("ZIP decompression failed");
					break;
				case MINIZ_READ_IO_FAIL:
					_MainLoopSetLoadError("ZIP read failed");
					break;
				case MINIZ_READ_NO_MEMORY:
					_MainLoopSetLoadError("Not enough memory to open ZIP");
					break;
				default:
					if (!strncmp(pFileName, "host:", 5))
						_MainLoopSetLoadError("HostFS could not open ZIP (I/O %d)", nRomBytes);
					else
						_MainLoopSetLoadError("Could not open ZIP (I/O %d)", nRomBytes);
					break;
			}
		}
		else
		{
			if (nRomBytes < 0 && !strncmp(pFileName, "host:", 5))
				_MainLoopSetLoadError("HostFS could not open ROM (I/O %d)", nRomBytes);
			else if (nRomBytes < 0)
				_MainLoopSetLoadError("Could not open ROM (I/O %d)", nRomBytes);
			else
				_MainLoopSetLoadError("ROM file is empty");
		}
#if SNDBG_LOG
		DLog("[rom-load-error] file=%s reason=%s code=%d",
		     pFileName, s_MainLoopLoadError, nRomBytes);
#endif
		return FALSE;
	}

#if SNDBG_LOG
	/* Hash the bytes exactly as they came from disk/archive, before a ROM
	   parser removes a copier header or deinterleaves the image. */
	uRomCRC = (Uint32)mz_crc32(MZ_CRC32_INIT, pBuffer, (size_t)nRomBytes);
#endif

    /* Parsers receive the exact byte count, so clearing all 8 MiB before
       every launch was redundant. Keep only a small zero guard for old
       cartridge code that may legally perform aligned look-ahead reads. */
    {
        Int32 nGuardBytes = nBufferBytes - nRomBytes;
        if (nGuardBytes > 1024)
            nGuardBytes = 1024;
        if (nGuardBytes > 0)
            memset(pBuffer + nRomBytes, 0, nGuardBytes);
    }

    printf("ROM data read: %s (%d bytes)\n", pFileName, nRomBytes);

	_MainLoopGetName(_RomName, FileName);
	printf("ROMName: '%s'\n", _RomName);

	// determine what kind of system to use for this rom
	switch (eType)
	{
		/* Phase 2 of the NES integration: route .nes/.fds/disksys.rom
		   to _pNes (the NesSystem). FDS support is enabled here so
		   the loader accepts the file, but ExecuteFrame is a stub
		   today and disk-swap input is still gated until Phase 5. */
		case MAINLOOP_ENTRYTYPE_NESROM:
			pSystem = _pNes;
			pRom    = _pNesRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 0.8f;
			break;

		case MAINLOOP_ENTRYTYPE_NESFDSDISK:
			pSystem = _pNes;
			pRom    = _pNesFDSDisk;
			pBios   = _pNesFDSBios;
			_MainLoop_fOutputIntensity = 0.8f;
			break;

		case MAINLOOP_ENTRYTYPE_NESFDSBIOS:
			pSystem = _pNes;
			pRom    = NULL;
			pBios   = _pNesFDSBios;
			_MainLoop_fOutputIntensity = 0.8f;
			break;
		case MAINLOOP_ENTRYTYPE_SNESROM:
			pSystem = _pSnes;
			pRom    = _pSnesRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 1.0f;
			break;
		default:
			_MainLoopSetLoadError("Unsupported ROM type");
			return FALSE;
	}

	if (pBios)
	{
		if (pRom==NULL)
		{
			// try to load disksys.rom directly
			if (!_MainLoopLoadBios(pBios, pFileName))
			{
				MainLoopModalPrintf(60*5, "ERROR: Cannot load disksys.rom");
				return FALSE;
			}
		} else
		{
			// can't run disks unless we have the FDS Bios loaded
			if (!pBios->IsLoaded())
			{
				char diskrompath[1024];
                            Char *pFileName;
				snprintf(diskrompath, sizeof(diskrompath), "%s", FileName);
				pFileName = strrchr(diskrompath, '/');
				if (!pFileName)
					pFileName = strrchr(diskrompath, ':');
				if (!pFileName)
					return FALSE;

				strcpy(pFileName + 1, "disksys.rom");

				printf("FDSRom: '%s'\n", diskrompath);

				// try to load disksys.rom
				if (!_MainLoopLoadBios(pBios, diskrompath))
				{
					MainLoopModalPrintf(60*5, "ERROR: Cannot load disksys.rom");
					return FALSE;
				}
			}
		}
	}

	if (pRom)
	{
		// attempt to load rom for that system
		if (!_MainLoopLoadRomData(pRom, _RomData, nRomBytes))
		{
			return FALSE;
		}
	}

	if (pBios)
	{
		// setup disk system
		pSystem->SetRom(pBios);
		/* Phase 2: NesSystem accepts the FDS disk pointer but the
		   real swap mux (NesMMU) is still a Phase 5 task, so this
		   stores the pointer without actually selecting a disk. The
		   SNES SetSnesRom path is kept as a safety net in case the
		   ROM that triggered pBios was somehow a SNES image. */
		if (pSystem == _pNes)
		{
			_pNes->SetNesDisk(_pNesFDSDisk);
		}
		else
		{
			_pSnes->SetSnesRom(_pSnesRom);
		}
	}
	else
	{
		pSystem->SetRom(pRom);
	}

	pSystem->Reset();

    _pSystem = pSystem;
    snprintf(_RomPath, sizeof(_RomPath), "%s", OriginalPath);
    MainLoopStateOnRomChanged();

	ConPrint("ROM Loaded: %s\n", pFileName);

	if (pRom)
	{
		int nRegions, iRegion;
		Char *pRomTitle;
		Char *pRomMapper;

		// print mapper info
		pRomMapper = pRom->GetMapperName();
		if (pRomMapper && !strcmp(pRomMapper, "<unknown>"))
		{
			MainLoopModalPrintf(60*1, "WARNING: Unsupported NES Mapper");
		}

		// print rom title
		pRomTitle = pRom->GetRomTitle();
		if (pRomTitle)
		{
		    printf("Rom Title: %s\n", pRomTitle);
		}

#if SNDBG_LOG
		DLog("[rom] file='%s' bytes=%d crc32=%08X title='%s' mapper='%s'",
			_RomName, (int)nRomBytes, (unsigned)uRomCRC,
			pRomTitle ? pRomTitle : "<none>",
			pRomMapper ? pRomMapper : "<none>");
#endif

		// print info about rom regions
		nRegions = pRom->GetNumRomRegions();
		for (iRegion=0; iRegion < nRegions; iRegion++)
		{
			printf("%s: %d bytes\n", pRom->GetRomRegionName(iRegion), pRom->GetRomRegionSize(iRegion));
		}
	}

    _MainLoopSetSampleRate(pSystem->GetSampleRate());

	if (bLoadSRAM)
		_MainLoopLoadSRAM();

	// clear screen
    _fbTexture[0]->Clear();
    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
	_MainLoopSetLoadError("");
	if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
	{
		/* Phase 2: track disk-inserted state so the SRAM/state path
		   builder picks up the right name, but the real disk swap
		   (NesMMU::InsertDisk) is a Phase 5 task. _pNes->GetMMU()
		   currently returns NULL so we deliberately skip that call. */
		_MainLoop_iDisk         = 0;
		_MainLoop_bDiskInserted = TRUE;
	}
	return TRUE;
}

void _MainLoopSetSampleRate(Uint32 uSampleRate)
{
    _AudMix->SetSampleRate(uSampleRate);
}
