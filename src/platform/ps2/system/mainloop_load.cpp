#include <string.h>
#include <fcntl.h>
#include <unistd.h>
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
#include "emumovie.h"
#include "mainloop_load.h"
#include "zlib.h"

extern SnesRom *_pSnesRom;
extern CRenderSurface *_fbTexture[2];
extern TextureT _OutTex;
extern Uint8 _RomData[4 * 1024 * 1024 + 1024];
extern Emu::MovieClip *s_pMovieClip;
extern Float32 _MainLoop_fOutputIntensity;

void _MainLoopSetSampleRate(Uint32 uSampleRate);
void _MainLoopResetInputChecksums();
#if MAINLOOP_HISTORY
void _MainLoopResetHistory();
#endif


extern "C" {
#include "unzip.h"
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
        int nBytes = 0;
        int hFile;

        hFile = open(pRomFile, O_RDONLY);
        if (hFile < 0)
        {
                return -1;
        }

        nBytes = read(hFile, pBuffer, nBufferBytes);
        close(hFile);

        return nBytes;
}

int _MainLoopReadGZData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
        int nBytes = 0;
        gzFile pFile;

        pFile = gzopen(pRomFile, "rb");
        if (!pFile)
        {
                return -1;
        }
        nBytes = gzread(pFile, pBuffer, nBufferBytes);
        gzclose(pFile);

        return nBytes;
}

int _MainLoopReadZipData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pZipFile, char *pFileName)
{
        unzFile hFile;
        unz_file_info file_info;
        char filename[256];
        int nBytes = 0;

        hFile = unzOpen(pZipFile);
        if (!hFile)
        {
                return -1;
        }
        printf("ZIP: file opened\n");

        do
        {
                if (unzGetCurrentFileInfo(hFile, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK)
                        break;

                printf("ZIP: file %s (%d)\n", filename, (int)file_info.uncompressed_size);

                if (file_info.uncompressed_size <= (unsigned)nBufferBytes)
                {
                        PathExtTypeE eType;
                        // do we recognize this file type?
                        if (PathExtResolve(filename, &eType, FALSE))
                        {
                                printf("ZIP: read %s (%d)\n", filename, (int)file_info.uncompressed_size);

                                // if so, read it
                                if (unzOpenCurrentFile(hFile) == UNZ_OK)
                                {
                                        if (unzReadCurrentFile(hFile, pBuffer, file_info.uncompressed_size) > 0)
                                        {
                                                if (pFileName)
                                                        strcpy(pFileName, filename);
                                                nBytes = (int)file_info.uncompressed_size;
                                        }
                                        unzCloseCurrentFile(hFile);
                                }
                        }
                }

        } while (nBytes == 0 && unzGoToNextFile(hFile) == UNZ_OK);

        printf("ZIP: file closed (%d)\n", nBytes);

        unzClose(hFile);

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
#if 0
	_pNes->SetRom(NULL);
	_pNesRom->Unload();
	_pNesFDSDisk->Unload();
#endif
    _bStateSaved = FALSE;
    _pSystem = NULL;

	_fbTexture[0]->Clear();
	_fbTexture[1]->Clear();
}


Bool _MainLoopExecuteFile(const char *pFileName, Bool bLoadSRAM)
{
	PathExtTypeE eType;
	Emu::Rom *pRom = NULL;
	Emu::System *pSystem = NULL;
	Emu::Rom *pBios = NULL;
	char FileName[256];

	if (pFileName==NULL)
	{
		return FALSE;
	}

	// make copy of filename
	strcpy(FileName, pFileName);

	// see if file exists first...
	int hFile;
    hFile = open(pFileName, O_RDONLY);
	if (hFile < 0)
	{
		return FALSE;
	}
	close(hFile);


	// resolve file extension of filename
	if (!PathExtResolve(FileName, &eType, TRUE))
	{
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

	// clear rom data buffer
    memset(pBuffer, 0, nBufferBytes);

	// load rom data from disk into our buffer
	if (eType == MAINLOOP_ENTRYTYPE_GZ)
	{
		// if its a GZ file, then the next extension is the one we use
		if (!PathExtResolve(FileName, &eType, TRUE))
		{
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
		return FALSE;
	}

    printf("ROM data read: %s (%d bytes)\n", pFileName, nRomBytes);

	_MainLoopGetName(_RomName, FileName);
	printf("ROMName: '%s'\n", _RomName);

	// determine what kind of system to use for this rom
	switch (eType)
	{
#if 0		
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
#endif
		case MAINLOOP_ENTRYTYPE_SNESROM:
			pSystem = _pSnes;
			pRom    = _pSnesRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 1.0f;
			break;
		default:
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
				char diskrompath[256];
                            Char *pFileName;
				strcpy(diskrompath, FileName);
				pFileName = strrchr(diskrompath, '/');
				if (!pFileName) 
					pFileName = strrchr(diskrompath, ':');
				if (!pFileName)
					return FALSE;

				// 
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
#if 0
		_pNes->SetNesDisk(_pNesFDSDisk);
#else
		_pSnes->SetSnesRom(_pSnesRom);
#endif
	} 
	else
	{
		pSystem->SetRom(pRom);
	}

	pSystem->Reset();

    _pSystem = pSystem;

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

		// print info about rom regions
		nRegions = pRom->GetNumRomRegions();
		for (iRegion=0; iRegion < nRegions; iRegion++)
		{
			printf("%s: %d bytes\n", pRom->GetRomRegionName(iRegion), pRom->GetRomRegionSize(iRegion));
		}
	}

    _MainLoopSetSampleRate(pSystem->GetSampleRate());

	if (bLoadSRAM)
	{
		_MainLoopLoadSRAM();
	}

	// clear screen
    _fbTexture[0]->Clear();
    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
#if 0
	if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
	{
		// default to disk 0
		_MainLoop_iDisk=0;
		_MainLoop_bDiskInserted=TRUE;
		_pNes->GetMMU()->InsertDisk(_MainLoop_iDisk);
	}
#endif
	return TRUE;
}
