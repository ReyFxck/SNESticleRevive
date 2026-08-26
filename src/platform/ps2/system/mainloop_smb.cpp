#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NEWLIB_PORT_AWARE
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE

extern "C" {
#include <ps2smb.h>
}

#include "embedded_irx.h"
#include "mainloop_bgm.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"
#include "mainloop_smb.h"

enum SmbStatusE
{
    SMB_STATUS_ENABLED = 0,
    SMB_STATUS_CONNECTING,
    SMB_STATUS_CONNECTED,
    SMB_STATUS_CONFIG_MISSING,
    SMB_STATUS_CONFIG_INVALID,
    SMB_STATUS_CONFIG_SAVE_ERROR,
    SMB_STATUS_NETWORK_ERROR,
    SMB_STATUS_DHCP_TIMEOUT,
    SMB_STATUS_DRIVER_ERROR,
    SMB_STATUS_CONNECTION_ERROR,
    SMB_STATUS_PROTOCOL_ERROR,
    SMB_STATUS_AUTH_ERROR,
    SMB_STATUS_SHARE_ERROR,
    SMB_STATUS_BROWSE_ERROR
};

static SmbStatusE s_status = SMB_STATUS_ENABLED;
static int s_last_error = 0;
static int s_mounted = 0;
static int s_driver_ready = 0;
static char s_config_path[512] = "";

static const char *s_mass_config_paths[] = {
    "mass0:/SNESticle/SMB.CNF",
    "mass1:/SNESticle/SMB.CNF",
    "mass:/SNESticle/SMB.CNF",
    NULL
};

static const char *s_mmce_config_paths[] = {
    "mmce0:/SNESticle/SMB.CNF",
    "mmce1:/SNESticle/SMB.CNF",
    NULL
};

static char *SmbTrim(char *text)
{
    char *end;

    while (*text && isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static void SmbUnquote(char *text)
{
    size_t length = strlen(text);

    if (length >= 2 &&
        ((text[0] == '"' && text[length - 1] == '"') ||
         (text[0] == '\'' && text[length - 1] == '\'')))
    {
        memmove(text, text + 1, length - 2);
        text[length - 2] = '\0';
    }
}

static int SmbCopyValue(char *destination, size_t destinationSize,
                        const char *value)
{
    size_t length = strlen(value);
    if (length >= destinationSize)
        return -1;
    memcpy(destination, value, length + 1);
    return 0;
}

static int SmbParseInteger(const char *value, int *result)
{
    char *end;
    long number = strtol(value, &end, 10);

    if (!value[0] || *end != '\0')
        return -1;
    *result = (int)number;
    return 0;
}

static int SmbValidIPv4(const char *address)
{
    unsigned int a, b, c, d;
    char tail;

    return sscanf(address, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
           a <= 255 && b <= 255 && c <= 255 && d <= 255;
}

void SmbConfigDefaults(SmbConfigT *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    strcpy(config->serverIp, "192.168.0.2");
    config->serverPort = 445;
    strcpy(config->share, "ROMS");
    strcpy(config->user, "GUEST");
    config->passwordType = NO_PASSWORD;
}

static int SmbHasLineBreak(const char *value)
{
    return value && (strchr(value, '\r') || strchr(value, '\n'));
}

static int SmbValidateConfig(const SmbConfigT *config)
{
    if (!config ||
        !SmbValidIPv4(config->serverIp) ||
        config->serverPort < 1 || config->serverPort > 65535 ||
        !config->share[0] ||
        strchr(config->share, '/') || strchr(config->share, '\\') ||
        strchr(config->share, ':') ||
        SmbHasLineBreak(config->share) ||
        SmbHasLineBreak(config->user) ||
        SmbHasLineBreak(config->password) ||
        config->passwordType < NO_PASSWORD ||
        config->passwordType > HASHED_PASSWORD)
        return 0;
    return 1;
}

/* Returns 1 when loaded, 0 when the file does not exist and -1 when it exists
 * but is malformed. Password contents are never printed. */
static int SmbReadConfigFile(const char *path, SmbConfigT *config)
{
    FILE *file;
    char line[640];
    char mappedPath[512];
    const char *openPath = path;
    int passwordTypeSeen = 0;

    /* hdd0: is the logical browser path. PFS files become accessible only
       after the partition is mounted as pfs0:, so configs beside an ELF on
       the internal HDD need the same translation used by the ROM browser. */
    if (path && strncasecmp(path, "hdd0:", 5) == 0)
    {
        if (HddMapPath(path, mappedPath, sizeof(mappedPath)) != 1)
            return 0;
        openPath = mappedPath;
    }

    file = fopen(openPath, "rb");
    if (!file)
        return 0;

    SmbConfigDefaults(config);

    while (fgets(line, sizeof(line), file))
    {
        char *key;
        char *value;
        char *equals;

        if (!strchr(line, '\n') && !feof(file))
        {
            fclose(file);
            return -1;
        }

        key = SmbTrim(line);
        if (!key[0] || key[0] == '#' || key[0] == ';')
            continue;

        equals = strchr(key, '=');
        if (!equals)
        {
            fclose(file);
            return -1;
        }
        *equals = '\0';
        value = SmbTrim(equals + 1);
        key = SmbTrim(key);
        SmbUnquote(value);

        if (!strcasecmp(key, "SERVER_IP") ||
            !strcasecmp(key, "smbServer_IP"))
        {
            if (SmbCopyValue(config->serverIp,
                             sizeof(config->serverIp), value) < 0)
                goto invalid;
        }
        else if (!strcasecmp(key, "SERVER_PORT") ||
                 !strcasecmp(key, "smbServer_Port"))
        {
            if (SmbParseInteger(value, &config->serverPort) < 0)
                goto invalid;
        }
        else if (!strcasecmp(key, "SHARE") ||
                 !strcasecmp(key, "smbShare"))
        {
            if (SmbCopyValue(config->share,
                             sizeof(config->share), value) < 0)
                goto invalid;
        }
        else if (!strcasecmp(key, "USER") ||
                 !strcasecmp(key, "smbUsername"))
        {
            if (SmbCopyValue(config->user,
                             sizeof(config->user), value) < 0)
                goto invalid;
        }
        else if (!strcasecmp(key, "PASSWORD") ||
                 !strcasecmp(key, "smbPassword"))
        {
            if (SmbCopyValue(config->password,
                             sizeof(config->password), value) < 0)
                goto invalid;
        }
        else if (!strcasecmp(key, "PASSWORD_TYPE") ||
                 !strcasecmp(key, "smbPasswordType"))
        {
            if (SmbParseInteger(value, &config->passwordType) < 0)
                goto invalid;
            passwordTypeSeen = 1;
        }
        /* Unknown wLaunchELF fields are intentionally ignored, allowing a
           familiar SMB.CNF to be reduced to one server plus smbShare. */
    }

    fclose(file);

    if (!config->user[0])
        strcpy(config->user, "GUEST");
    if (!config->password[0])
        config->passwordType = NO_PASSWORD;
    else if (!passwordTypeSeen)
        config->passwordType = HASHED_PASSWORD;

    if (!SmbValidateConfig(config))
        return -1;

    return 1;

invalid:
    fclose(file);
    return -1;
}

static int SmbTryConfig(const char *path, SmbConfigT *config)
{
    int result;

    if (!path || !path[0])
        return 0;
    result = SmbReadConfigFile(path, config);
    if (result != 0)
    {
        strncpy(s_config_path, path, sizeof(s_config_path) - 1);
        s_config_path[sizeof(s_config_path) - 1] = '\0';
    }
    return result;
}

static int SmbLoadConfig(SmbConfigT *config)
{
    static const char *ownedMemoryCardPaths[] = {
        "mc0:/SNESticle/SMB.CNF",
        "mc1:/SNESticle/SMB.CNF",
        NULL
    };
    static const char *sharedMemoryCardPaths[] = {
        "mc0:/SYS-CONF/SMB.CNF",
        "mc1:/SYS-CONF/SMB.CNF",
        NULL
    };
    char bootPath[512];
    int result;
    int index;

    s_config_path[0] = '\0';

    /* User-created memory-card config wins over a bundled ISO/ELF config. */
    for (index = 0; ownedMemoryCardPaths[index]; ++index)
    {
        result = SmbTryConfig(ownedMemoryCardPaths[index], config);
        if (result != 0)
            return result;
    }

    /* A writable standalone ELF directory is more specific than a generic
       device fallback. Defer read-only disc paths until after mass/MMCE so a
       config saved by this screen can override the SMB.CNF bundled in an ISO. */
    if (_MainLoop_BootDir[0] &&
        strncasecmp(_MainLoop_BootDir, "host:", 5) != 0 &&
        strncasecmp(_MainLoop_BootDir, "smb:", 4) != 0 &&
        strncasecmp(_MainLoop_BootDir, "cdfs:", 6) != 0 &&
        strncasecmp(_MainLoop_BootDir, "cdrom", 5) != 0 &&
        strncasecmp(_MainLoop_BootDir, "rom", 3) != 0 &&
        snprintf(bootPath, sizeof(bootPath), "%sSMB.CNF",
                 _MainLoop_BootDir) < (int)sizeof(bootPath))
    {
        result = SmbTryConfig(bootPath, config);
        if (result != 0)
            return result;
    }

    /* A console without a usable memory card can keep the emulator-owned
       config on USB/MX4SIO or MMCE. Probe only storage the user enabled, and
       only after SMB was explicitly requested, so boot remains lazy. */
    if ((MassStorageIsEnabled() || Mx4sioIsEnabled()) &&
        (UsbBdmIsLoaded() || Mx4sioIsLoaded() ||
         (MassStorageIsEnabled() && UsbBdmLoadEmbeddedIrx() >= 0)))
    {
        for (index = 0; s_mass_config_paths[index]; ++index)
        {
            result = SmbTryConfig(s_mass_config_paths[index], config);
            if (result != 0)
                return result;
        }
    }

    if (MmceSupportIsEnabled() && !MmceNeedsRestart())
    {
        int slots = MmceGetAvailableSlots();
        if (!slots)
            slots = MmceProbeAvailableSlots();
        for (index = 0; s_mmce_config_paths[index]; ++index)
        {
            if (!(slots & (1 << index)))
                continue;
            result = SmbTryConfig(s_mmce_config_paths[index], config);
            if (result != 0)
                return result;
        }
    }

    /* Legacy/shared wLaunchELF configs remain readable, but an emulator-owned
       file on any writable device must win so Save & Connect can actually
       replace the active server without modifying another application's file. */
    for (index = 0; sharedMemoryCardPaths[index]; ++index)
    {
        result = SmbTryConfig(sharedMemoryCardPaths[index], config);
        if (result != 0)
            return result;
    }

    /* ISO config is last because it is read-only and cannot be replaced by
       the setup screen. This probe only occurs after the user requests SMB. */
    if (CdfsIsLoaded() ||
        ((!strncasecmp(_MainLoop_BootDir, "cdfs:", 6) ||
          !strncasecmp(_MainLoop_BootDir, "cdrom", 5)) &&
         CdfsLoadEmbeddedIrx() >= 0))
    {
        result = SmbTryConfig("cdfs:/SMB.CNF", config);
        if (result != 0)
            return result;
    }
    return 0;
}

int SmbLoadCurrentConfig(SmbConfigT *config)
{
    int result;

    if (!config)
        return -1;
    result = SmbLoadConfig(config);
    if (result == 0)
        SmbConfigDefaults(config);
    return result;
}

static int SmbPathIsWritable(const char *path)
{
    if (!path || !path[0])
        return 0;
    return strncasecmp(path, "cdfs:", 6) != 0 &&
           strncasecmp(path, "host:", 5) != 0 &&
           strncasecmp(path, "smb:", 4) != 0 &&
           strncasecmp(path, "rom", 3) != 0;
}

static int SmbPathIsSharedConfig(const char *path)
{
    /* The wLaunchELF-compatible SYS-CONF file may be shared by unrelated
       homebrew. It is readable as a fallback but never overwritten here. */
    return path &&
           (strstr(path, "/SYS-CONF/") || strstr(path, "/sys-conf/"));
}

static void SmbEnsureParentDirectory(const char *path)
{
    char directory[512];
    char *slash;
    size_t length;

    if (!path || strlen(path) >= sizeof(directory))
        return;
    strcpy(directory, path);
    slash = strrchr(directory, '/');
    if (!slash)
        return;
    *slash = '\0';
    length = strlen(directory);
    if (length > 0 && directory[length - 1] != ':')
        mkdir(directory, 0777);
}

static int SmbWriteConfigFile(const char *path, const SmbConfigT *config)
{
    FILE *file;
    char mappedPath[512];
    const char *openPath = path;
    int ok;

    if (!SmbPathIsWritable(path))
        return -1;

    if (strncasecmp(path, "hdd0:", 5) == 0)
    {
        if (HddMapPath(path, mappedPath, sizeof(mappedPath)) != 1)
            return -1;
        openPath = mappedPath;
    }

    /* Works for mc, mass, mmce and an already mounted pfs0:. The root exists;
       only the emulator-owned SNESticle child may need to be created. */
    SmbEnsureParentDirectory(openPath);

    file = fopen(openPath, "wb");
    if (!file)
        return -1;

    ok = fprintf(file,
                 "# SNESticle Revive SMB configuration\n"
                 "SERVER_IP=%s\n"
                 "SERVER_PORT=%d\n"
                 "SHARE=%s\n"
                 "USER=%s\n"
                 "PASSWORD=%s\n"
                 "PASSWORD_TYPE=%d\n",
                 config->serverIp, config->serverPort, config->share,
                 config->user[0] ? config->user : "GUEST",
                 config->password, config->passwordType) >= 0;
    if (fclose(file) != 0)
        ok = 0;
    if (!ok)
        return -1;

    strncpy(s_config_path, path, sizeof(s_config_path) - 1);
    s_config_path[sizeof(s_config_path) - 1] = '\0';
    return 0;
}

int SmbSaveConfig(const SmbConfigT *source)
{
    SmbConfigT config;
    char bootPath[512];
    const char *paths[] = {
        "mc0:/SNESticle/SMB.CNF",
        "mc1:/SNESticle/SMB.CNF",
        NULL
    };
    int index;

    if (!source)
        return -1;
    memcpy(&config, source, sizeof(config));
    config.serverIp[sizeof(config.serverIp) - 1] = '\0';
    config.share[sizeof(config.share) - 1] = '\0';
    config.user[sizeof(config.user) - 1] = '\0';
    config.password[sizeof(config.password) - 1] = '\0';
    if (!config.user[0])
        strcpy(config.user, "GUEST");
    config.passwordType = config.password[0] ? HASHED_PASSWORD : NO_PASSWORD;
    if (!SmbValidateConfig(&config))
        return -2;

    /* If the setup screen loaded an existing writable emulator/user file,
       update that same file first. Do not overwrite a shared SYS-CONF file. */
    if (s_config_path[0] && !SmbPathIsSharedConfig(s_config_path) &&
        SmbWriteConfigFile(s_config_path, &config) == 0)
        return 0;

    /* Always prefer the emulator-owned config. Do not overwrite a shared
       mc?:/SYS-CONF/SMB.CNF that may belong to wLaunchELF or another app.
       The save directory is created during normal memory-card init; mc1 is
       a transparent fallback. */
    for (index = 0; paths[index]; ++index)
        if (SmbWriteConfigFile(paths[index], &config) == 0)
            return 0;

    /* No memory card: save beside a writable mass/mmce/pfs ELF. */
    if (_MainLoop_BootDir[0] &&
        snprintf(bootPath, sizeof(bootPath), "%sSMB.CNF",
                 _MainLoop_BootDir) < (int)sizeof(bootPath) &&
        SmbWriteConfigFile(bootPath, &config) == 0)
        return 0;

    /* No memory card and a read-only boot device (usually an ISO): fall back
       to every enabled writable storage family. mass: is kept after mass0/1
       for older drivers that expose only the unnumbered alias. */
    if (MassStorageIsEnabled() || Mx4sioIsEnabled())
    {
        for (index = 0; s_mass_config_paths[index]; ++index)
            if (SmbWriteConfigFile(s_mass_config_paths[index], &config) == 0)
                return 0;
    }

    if (MmceSupportIsEnabled() && !MmceNeedsRestart())
    {
        int slots = MmceGetAvailableSlots();
        if (!slots)
            slots = MmceProbeAvailableSlots();
        for (index = 0; s_mmce_config_paths[index]; ++index)
            if ((slots & (1 << index)) &&
                SmbWriteConfigFile(s_mmce_config_paths[index], &config) == 0)
                return 0;
    }

    return -3;
}

int SmbSaveAndConnect(const SmbConfigT *config)
{
    int result;

    /* File writes, DHCP and authentication are synchronous PS2SDK calls.
       Keep the already-loaded menu tracker serviced from the EE I/O helper
       while the UI thread waits for them. */
    BgmIOBegin();
    result = SmbSaveConfig(config);
    if (result < 0)
    {
        s_status = (result == -2) ? SMB_STATUS_CONFIG_INVALID
                                  : SMB_STATUS_CONFIG_SAVE_ERROR;
        s_last_error = result;
        s_mounted = 0;
        BgmIOEnd();
        return result;
    }

    SmbDisconnect();
    SmbSupportSetEnabled(1);
    result = SmbEnsureMounted();
    BgmIOEnd();
    return result;
}

static void SmbSetFailure(SmbStatusE status, int error)
{
    s_status = status;
    s_last_error = error;
    s_mounted = 0;
}

int SmbEnsureMounted(void)
{
    SmbConfigT config;
    smbLogOn_in_t logon;
    smbOpenShare_in_t openShare;
    smbGetPasswordHashes_in_t hashInput;
    smbGetPasswordHashes_out_t hashes;
    int result;

    if (!SmbSupportIsEnabled())
        return -1;
    if (s_mounted)
        return 0;

    s_status = SMB_STATUS_CONNECTING;
    s_last_error = 0;

    result = SmbLoadConfig(&config);
    if (result == 0)
    {
        SmbSetFailure(SMB_STATUS_CONFIG_MISSING, -1);
        return -1;
    }
    if (result < 0)
    {
        SmbSetFailure(SMB_STATUS_CONFIG_INVALID, -2);
        return -1;
    }

    if (!_MainLoopInitNetwork(_MainLoop_NetConfigPaths) ||
        !_MainLoopConfigureNetwork(_MainLoop_NetConfigPaths,
                                   (char *)"ipconfig.dat"))
    {
        SmbSetFailure(SMB_STATUS_NETWORK_ERROR, -3);
        return -1;
    }
    if (!_MainLoopWaitForNetwork(15000))
    {
        SmbSetFailure(SMB_STATUS_DHCP_TIMEOUT, -4);
        return -1;
    }

    result = SmbLoadEmbeddedIrx();
    if (result < 0)
    {
        SmbSetFailure(SMB_STATUS_DRIVER_ERROR, result);
        return -1;
    }
    s_driver_ready = 1;

    memset(&logon, 0, sizeof(logon));
    memset(&openShare, 0, sizeof(openShare));
    SmbCopyValue(logon.serverIP, sizeof(logon.serverIP), config.serverIp);
    logon.serverPort = config.serverPort;
    SmbCopyValue(logon.User, sizeof(logon.User), config.user);
    SmbCopyValue(openShare.ShareName, sizeof(openShare.ShareName),
                 config.share);

    if (config.passwordType == HASHED_PASSWORD)
    {
        memset(&hashInput, 0, sizeof(hashInput));
        memset(&hashes, 0, sizeof(hashes));
        SmbCopyValue(hashInput.password, sizeof(hashInput.password),
                     config.password);
        result = fileXioDevctl("smb:", SMB_DEVCTL_GETPASSWORDHASHES,
                              &hashInput, sizeof(hashInput),
                              &hashes, sizeof(hashes));
        if (result < 0)
        {
            SmbSetFailure(SMB_STATUS_DRIVER_ERROR, result);
            return -1;
        }
        memcpy(logon.Password, &hashes, sizeof(hashes));
        memcpy(openShare.Password, &hashes, sizeof(hashes));
    }
    else if (config.passwordType == PLAINTEXT_PASSWORD)
    {
        SmbCopyValue(logon.Password, sizeof(logon.Password), config.password);
        SmbCopyValue(openShare.Password, sizeof(openShare.Password),
                     config.password);
    }
    logon.PasswordType = config.passwordType;
    openShare.PasswordType = config.passwordType;

    result = fileXioDevctl("smb:", SMB_DEVCTL_LOGON,
                          &logon, sizeof(logon), NULL, 0);
    if (result < 0)
    {
        if (result == -SMB_DEVCTL_LOGON_ERR_CONN)
            SmbSetFailure(SMB_STATUS_CONNECTION_ERROR, result);
        else if (result == -SMB_DEVCTL_LOGON_ERR_PROT)
            SmbSetFailure(SMB_STATUS_PROTOCOL_ERROR, result);
        else
            SmbSetFailure(SMB_STATUS_AUTH_ERROR, result);
        return -1;
    }

    result = fileXioDevctl("smb:", SMB_DEVCTL_OPENSHARE,
                          &openShare, sizeof(openShare), NULL, 0);
    if (result < 0)
    {
        fileXioDevctl("smb:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
        SmbSetFailure(SMB_STATUS_SHARE_ERROR, result);
        return -1;
    }

    memset(config.password, 0, sizeof(config.password));
    memset(&hashInput, 0, sizeof(hashInput));
    memset(&hashes, 0, sizeof(hashes));
    memset(logon.Password, 0, sizeof(logon.Password));
    memset(openShare.Password, 0, sizeof(openShare.Password));

    s_mounted = 1;
    s_status = SMB_STATUS_CONNECTED;
    s_last_error = 0;
    printf("SMB mounted: %s:%d/%s (config %s)\n",
           config.serverIp, config.serverPort, config.share, s_config_path);
    return 0;
}

int SmbIsMounted(void)
{
    return s_mounted;
}

void SmbDisconnect(void)
{
    if (s_driver_ready)
    {
        fileXioDevctl("smb:", SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0);
        fileXioDevctl("smb:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
    }
    s_mounted = 0;
    s_last_error = 0;
    s_status = SMB_STATUS_ENABLED;
}

void SmbReportBrowseError(int error)
{
    if (SmbSupportIsEnabled())
    {
        s_status = SMB_STATUS_BROWSE_ERROR;
        s_last_error = error;
    }
}

void SmbReportBrowseSuccess(void)
{
    if (s_mounted)
    {
        s_status = SMB_STATUS_CONNECTED;
        s_last_error = 0;
    }
}

const char *SmbGetStatusText(void)
{
    if (!SmbSupportIsEnabled())
        return "Off";

    switch (s_status)
    {
        case SMB_STATUS_CONNECTING:       return "Connecting";
        case SMB_STATUS_CONNECTED:        return "Connected";
        case SMB_STATUS_CONFIG_MISSING:   return "No SMB.CNF";
        case SMB_STATUS_CONFIG_INVALID:   return "Bad SMB.CNF";
        case SMB_STATUS_CONFIG_SAVE_ERROR:return "Save Error";
        case SMB_STATUS_NETWORK_ERROR:    return "Network Error";
        case SMB_STATUS_DHCP_TIMEOUT:     return "DHCP Timeout";
        case SMB_STATUS_DRIVER_ERROR:     return "Driver Error";
        case SMB_STATUS_CONNECTION_ERROR: return "Connect Error";
        case SMB_STATUS_PROTOCOL_ERROR:   return "SMB1 Required";
        case SMB_STATUS_AUTH_ERROR:       return "Auth Error";
        case SMB_STATUS_SHARE_ERROR:      return "Share Error";
        case SMB_STATUS_BROWSE_ERROR:     return "Browse Error";
        default:                          return "Enabled";
    }
}

const char *SmbGetConfigPath(void)
{
    return s_config_path;
}

int SmbGetLastError(void)
{
    return s_last_error;
}
