/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop smb interface for the PlayStation 2 application runtime.
 */

#pragma once

/* Read-only SMB ROM source. The IOP driver itself implements a complete
 * filesystem, but SNESticle only exposes browsing/loading operations for
 * smb: and never offers it as a save-state/SRAM destination. */
typedef struct
{
    char serverIp[16];
    int  serverPort;
    char share[256];
    char user[256];
    char password[256];
    int  passwordType;
} SmbConfigT;

void SmbConfigDefaults(SmbConfigT *config);
int SmbLoadCurrentConfig(SmbConfigT *config);
int SmbSaveConfig(const SmbConfigT *config);
int SmbSaveAndConnect(const SmbConfigT *config);
int SmbEnsureMounted(void);
int SmbIsMounted(void);
void SmbDisconnect(void);
void SmbReportBrowseSuccess(void);
void SmbReportBrowseError(int error);
const char *SmbGetStatusText(void);
const char *SmbGetConfigPath(void);
int SmbGetLastError(void);
