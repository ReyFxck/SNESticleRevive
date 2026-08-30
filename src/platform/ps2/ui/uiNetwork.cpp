/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements uiNetwork behavior for the PlayStation 2 user interface.
 */

#include <stdio.h>
#include <string.h>
#include <libpad.h>

#include "types.h"
#include "font.h"
#include "poly.h"
#include "uiNetwork.h"
#include "uiVideo.h"
#include "mainloop_bgm.h"
#include "mainloop_smb.h"
#include "mainloop_ui.h"

/* The original iaddis Host/NetPlay screen was never a general remote ROM
   filesystem.  Keep its convenient tab and IP editor, but make the screen
   configure the read-only SMB browser that users actually need. */

static const char kSmbTextChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.$@!#%&()+,.;=[]^{}~";

static void SmbCenter(int x, int y, const char *text)
{
    FontPuts(x - FontGetStrWidth(text) / 2, y, text);
}

static void SmbHeader(int y, const char *text)
{
    PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f);
    PolyRect(32, y, 192, 9);
    FontColor4f(0.0f, 0.8f, 0.8f, 1.0f);
    SmbCenter(128, y, text);
}

static void SmbRow(int y, int index, int selected,
                   const char *label, const char *value)
{
    if (index == selected)
    {
        PolyColor4f(0.0f, 0.5f, 0.0f, 0.5f);
        PolyRect(44, y - 1, 168, FontGetHeight() + 2);
    }
    FontColor4f(0.55f, 0.55f, 0.55f, 1.0f);
    FontPuts(50, y, label);
    FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    FontPuts(126, y, value);
}

static void SmbAction(int y, int index, int selected, const char *text)
{
    if (index == selected)
    {
        PolyColor4f(0.0f, 0.5f, 0.0f, 0.5f);
        PolyRect(64, y - 1, 128, FontGetHeight() + 2);
    }
    FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    SmbCenter(128, y, text);
}

CNetworkScreen::CNetworkScreen()
{
    m_iSelect = 0;
    m_iDigitIP = -1;
    m_iEditField = -1;
    m_iTextCursor = 0;
    m_bLoaded = FALSE;
    SmbConfigDefaults(&m_Config);
    SetEditIP(m_Config.serverIp);
}

void CNetworkScreen::Process()
{
}

void CNetworkScreen::LoadConfig()
{
    if (m_bLoaded)
        return;
    BgmIOBegin();
    if (SmbLoadCurrentConfig(&m_Config) < 0)
        SmbConfigDefaults(&m_Config);
    BgmIOEnd();
    SetEditIP(m_Config.serverIp);
    m_bLoaded = TRUE;
}

void CNetworkScreen::SetEditIP(const char *address)
{
    unsigned int octet[4] = {192, 168, 0, 2};
    int i;

    if (address)
        sscanf(address, "%u.%u.%u.%u", &octet[0], &octet[1],
               &octet[2], &octet[3]);
    for (i = 0; i < 4; ++i)
    {
        if (octet[i] > 255)
            octet[i] = 0;
        m_NetworkIP[i * 3 + 0] = (octet[i] / 100) % 10;
        m_NetworkIP[i * 3 + 1] = (octet[i] / 10) % 10;
        m_NetworkIP[i * 3 + 2] = octet[i] % 10;
    }
}

void CNetworkScreen::CommitEditIP()
{
    unsigned int a = ((unsigned int)GetOctet(0)) & 255;
    unsigned int b = ((unsigned int)GetOctet(1)) & 255;
    unsigned int c = ((unsigned int)GetOctet(2)) & 255;
    unsigned int d = ((unsigned int)GetOctet(3)) & 255;
    snprintf(m_Config.serverIp, sizeof(m_Config.serverIp), "%u.%u.%u.%u",
             a, b, c, d);
}

int CNetworkScreen::GetOctet(int index) const
{
    int base = index * 3;
    return m_NetworkIP[base] * 100 + m_NetworkIP[base + 1] * 10 +
           m_NetworkIP[base + 2];
}

char *CNetworkScreen::GetEditText(int field, int *maxLength)
{
    if (field == 2)
    {
        *maxLength = 40;
        return m_Config.share;
    }
    if (field == 3)
    {
        *maxLength = 32;
        return m_Config.user;
    }
    *maxLength = 32;
    return m_Config.password;
}

void CNetworkScreen::BeginTextEdit(int field)
{
    char *text;
    int maxLength;

    m_iEditField = field;
    text = GetEditText(field, &maxLength);
    (void)maxLength;
    m_iTextCursor = strlen(text);
}

void CNetworkScreen::InputIP(Uint32 trigger)
{
    int base;
    int octet;

    if (trigger & PAD_LEFT)
    {
        --m_iDigitIP;
        if (m_iDigitIP < 0) m_iDigitIP = 3;
    }
    if (trigger & PAD_RIGHT)
    {
        ++m_iDigitIP;
        if (m_iDigitIP > 3) m_iDigitIP = 0;
    }
    if (trigger & (PAD_UP | PAD_DOWN))
    {
        base = m_iDigitIP * 3;
        octet = GetOctet(m_iDigitIP);
        if (trigger & PAD_UP) octet = (octet + 1) & 255;
        else                  octet = (octet + 255) & 255;
        m_NetworkIP[base] = (octet / 100) % 10;
        m_NetworkIP[base + 1] = (octet / 10) % 10;
        m_NetworkIP[base + 2] = octet % 10;
    }
    if (trigger & (PAD_CROSS | PAD_TRIANGLE | PAD_START))
    {
        CommitEditIP();
        m_iDigitIP = -1;
    }
}

void CNetworkScreen::InputText(Uint32 trigger)
{
    char *text;
    int maxLength;
    int length;
    int charsetLength = strlen(kSmbTextChars);

    text = GetEditText(m_iEditField, &maxLength);
    length = strlen(text);

    if (trigger & PAD_LEFT)
    {
        if (m_iTextCursor > 0) --m_iTextCursor;
    }
    if (trigger & PAD_RIGHT)
    {
        if (m_iTextCursor < length) ++m_iTextCursor;
    }

    if (trigger & (PAD_UP | PAD_DOWN))
    {
        int character = 0;
        int direction = (trigger & PAD_UP) ? 1 : -1;

        if (m_iTextCursor == length && length < maxLength)
        {
            text[length++] = kSmbTextChars[0];
            text[length] = '\0';
        }
        if (m_iTextCursor < length)
        {
            const char *found = strchr(kSmbTextChars, text[m_iTextCursor]);
            if (found) character = found - kSmbTextChars;
            character = (character + direction + charsetLength) % charsetLength;
            text[m_iTextCursor] = kSmbTextChars[character];
        }
    }

    if (trigger & PAD_SQUARE)
    {
        if (length > 0)
        {
            if (m_iTextCursor >= length) m_iTextCursor = length - 1;
            memmove(text + m_iTextCursor, text + m_iTextCursor + 1,
                    length - m_iTextCursor);
            --length;
            if (m_iTextCursor > length) m_iTextCursor = length;
        }
    }

    if (trigger & PAD_CROSS)
    {
        length = strlen(text);
        if (m_iTextCursor < length) ++m_iTextCursor;
        else if (length < maxLength)
        {
            text[length] = kSmbTextChars[0];
            text[length + 1] = '\0';
            m_iTextCursor = length;
        }
    }

    if (trigger & (PAD_TRIANGLE | PAD_START))
    {
        if (m_iEditField == 2 && !m_Config.share[0])
            strcpy(m_Config.share, "ROMS");
        if (m_iEditField == 3 && !m_Config.user[0])
            strcpy(m_Config.user, "GUEST");
        m_iEditField = -1;
    }
}

void CNetworkScreen::Input(Uint32 buttons, Uint32 trigger)
{
    (void)buttons;
    LoadConfig();

    if (m_iDigitIP >= 0)
    {
        InputIP(trigger);
        return;
    }
    if (m_iEditField >= 0)
    {
        InputText(trigger);
        return;
    }

    if (trigger & PAD_UP)
    {
        --m_iSelect;
        if (m_iSelect < 0) m_iSelect = 6;
    }
    if (trigger & PAD_DOWN)
    {
        ++m_iSelect;
        if (m_iSelect > 6) m_iSelect = 0;
    }

    if ((trigger & (PAD_LEFT | PAD_RIGHT)) && m_iSelect == 1)
        m_Config.serverPort = (m_Config.serverPort == 445) ? 139 : 445;

    if (trigger & PAD_SQUARE)
    {
        if (m_iSelect == 0)
        {
            strcpy(m_Config.serverIp, "192.168.0.2");
            SetEditIP(m_Config.serverIp);
        }
        else if (m_iSelect == 1) m_Config.serverPort = 445;
        else if (m_iSelect == 2) strcpy(m_Config.share, "ROMS");
        else if (m_iSelect == 3) strcpy(m_Config.user, "GUEST");
        else if (m_iSelect == 4) m_Config.password[0] = '\0';
    }

    if (trigger & (PAD_CROSS | PAD_START))
    {
        if (m_iSelect == 0)
            m_iDigitIP = 0;
        else if (m_iSelect >= 2 && m_iSelect <= 4)
            BeginTextEdit(m_iSelect);
        else if (m_iSelect == 1)
            m_Config.serverPort = (m_Config.serverPort == 445) ? 139 : 445;
        else if (m_iSelect == 5)
        {
            CommitEditIP();
            MainLoopModalPrintf(1, "SMB: Saving config...");
            if (SmbSaveAndConnect(&m_Config) == 0)
                MainLoopModalPrintf(60 * 2, "SMB: Connected\n%s",
                                    SmbGetConfigPath());
            else
                MainLoopModalPrintf(60 * 3, "SMB: %s (error %d)",
                                    SmbGetStatusText(), SmbGetLastError());
            VideoSettingsSave();
        }
        else if (m_iSelect == 6)
        {
            BgmIOBegin();
            SmbDisconnect();
            BgmIOEnd();
            MainLoopModalPrintf(60, "SMB: Disconnected");
        }
    }

    /* Circle reloads the saved values without attempting a connection. */
    if (trigger & PAD_CIRCLE)
    {
        m_bLoaded = FALSE;
        LoadConfig();
    }
}

void CNetworkScreen::DrawIP(int x, int y)
{
    char part[4][8];
    int i;
    int cursor = x;

    for (i = 0; i < 4; ++i)
        snprintf(part[i], sizeof(part[i]), "%d", GetOctet(i));

    for (i = 0; i < 4; ++i)
    {
        if (m_iDigitIP == i)
        {
            PolyColor4f(0.0f, 0.7f, 0.0f, 0.7f);
            PolyRect(cursor - 1, y - 1, FontGetStrWidth(part[i]) + 2,
                     FontGetHeight() + 2);
        }
        FontPuts(cursor, y, part[i]);
        cursor += FontGetStrWidth(part[i]);
        if (i != 3)
        {
            FontPuts(cursor, y, ".");
            cursor += FontGetStrWidth(".");
        }
    }
}

void CNetworkScreen::BuildDisplayText(char *output, int outputSize,
                                      const char *text, int password,
                                      int editing)
{
    int i;
    int out = 0;
    int length = strlen(text);
    int start = 0;

    if (editing && m_iTextCursor > 12)
        start = m_iTextCursor - 12;

    for (i = start; i < length && out < outputSize - 4 && i < start + 14; ++i)
    {
        if (editing && i == m_iTextCursor) output[out++] = '[';
        output[out++] = password ? '*' : text[i];
        if (editing && i == m_iTextCursor) output[out++] = ']';
    }
    if (editing && m_iTextCursor == length && out < outputSize - 4)
    {
        output[out++] = '[';
        output[out++] = '_';
        output[out++] = ']';
    }
    output[out] = '\0';
    if (!output[0]) strcpy(output, password ? "Guest" : "(empty)");
}

void CNetworkScreen::Draw()
{
    char port[16];
    char share[80];
    char user[80];
    char password[80];
    char pathDisplay[48];
    const char *path;
    int y = 15;

    LoadConfig();
    snprintf(port, sizeof(port), "%d", m_Config.serverPort);
    BuildDisplayText(share, sizeof(share), m_Config.share, 0,
                     m_iEditField == 2);
    BuildDisplayText(user, sizeof(user), m_Config.user, 0,
                     m_iEditField == 3);
    BuildDisplayText(password, sizeof(password), m_Config.password, 1,
                     m_iEditField == 4);

    FontSelect(0);
    SmbHeader(y, "SMB Network");
    y += 15;

    FontColor4f(0.55f, 0.55f, 0.55f, 1.0f);
    FontPuts(50, y, "Status");
    FontColor4f(SmbIsMounted() ? 0.3f : 1.0f,
                SmbIsMounted() ? 1.0f : 0.85f, 0.3f, 1.0f);
    FontPuts(126, y, SmbGetStatusText());
    y += 15;

    SmbHeader(y, "Server / Share");
    y += 13;
    SmbRow(y, 0, m_iSelect, "Server IP", "");
    FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrawIP(126, y); y += 12;
    SmbRow(y, 1, m_iSelect, "Port", port); y += 12;
    SmbRow(y, 2, m_iSelect, "Share", share); y += 12;
    SmbRow(y, 3, m_iSelect, "Username", user); y += 12;
    SmbRow(y, 4, m_iSelect, "Password", password); y += 15;

    SmbHeader(y, "Actions"); y += 13;
    SmbAction(y, 5, m_iSelect, "Save & Connect"); y += 12;
    SmbAction(y, 6, m_iSelect, "Disconnect");

    y = 183;
    FontColor4f(0.6f, 0.6f, 0.6f, 1.0f);
    if (m_iDigitIP >= 0)
    {
        SmbCenter(128, y, "L/R: octet  Up/Dn: value"); y += 11;
        SmbCenter(128, y, "X/Triangle: done");
    }
    else if (m_iEditField >= 0)
    {
        SmbCenter(128, y, "L/R: cursor  Up/Dn: character"); y += 11;
        SmbCenter(128, y, "X: next  Square: delete  Triangle: done");
    }
    else
    {
        SmbCenter(128, y, "X: edit/select  Square: reset field"); y += 11;
        SmbCenter(128, y, "Circle: reload saved config");
    }

    path = SmbGetConfigPath();
    if (path && path[0])
    {
        size_t pathLength = strlen(path);
        if (pathLength < sizeof(pathDisplay))
            strcpy(pathDisplay, path);
        else
        {
            strcpy(pathDisplay, "...");
            strncpy(pathDisplay + 3,
                    path + pathLength - (sizeof(pathDisplay) - 4),
                    sizeof(pathDisplay) - 4);
            pathDisplay[sizeof(pathDisplay) - 1] = '\0';
        }
        FontSelect(2);
        FontColor4f(0.35f, 0.65f, 0.65f, 1.0f);
        FontPuts(8, 207, pathDisplay);
    }
}
