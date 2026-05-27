
#include <stdio.h>
#include "types.h"
#include "file.h"

/* All generic file I/O helpers go through newlib stdio. With
   init_ps2_filesystem_driver() up, fopen/fread/fwrite/fclose route
   to iomanX, so "mc0:/...", "cdfs:/...", "mass:/..." and
   "host:/..." are all handled by the same code path. */

Bool FileReadMem(Char *pFilePath, void *pMem, Uint32 nBytes)
{
	FILE *pFile;
	pFile = fopen(pFilePath, "rb");
	if (pFile)
	{
		Uint32 nReadBytes;
		nReadBytes = fread(pMem, 1, nBytes, pFile);
		fclose(pFile);
		return (nBytes == nReadBytes);
	}
	return FALSE;
}

Bool FileWriteMem(Char *pFilePath, void *pMem, Uint32 nBytes)
{
	FILE *pFile;
	Uint32 nWriteBytes;

	pFile = fopen(pFilePath, "wb");
	if (pFile)
	{
		nWriteBytes = fwrite(pMem, 1, nBytes, pFile);
		fclose(pFile);
		return (nBytes == nWriteBytes);
	}
	return FALSE;
}

Bool FileExists(Char *pFilePath)
{
	FILE *pFile;

	pFile = fopen(pFilePath, "rb");
	if (pFile)
	{
		fclose(pFile);
		return TRUE;
	}
	return FALSE;
}
