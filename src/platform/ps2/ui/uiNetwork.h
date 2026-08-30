/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the uiNetwork interface for the PlayStation 2 user interface.
 */

#ifndef _UINETWORK_H
#define _UINETWORK_H

#include "uiScreen.h"
#include "mainloop_smb.h"

class CNetworkScreen : public CScreen
{
    int m_iSelect;
    int m_iDigitIP;
    int m_iEditField;
    int m_iTextCursor;
    Int8 m_NetworkIP[12];
    Bool m_bLoaded;
    SmbConfigT m_Config;

    void LoadConfig();
    void SetEditIP(const char *address);
    void CommitEditIP();
    int GetOctet(int index) const;
    char *GetEditText(int field, int *maxLength);
    void BeginTextEdit(int field);
    void InputIP(Uint32 trigger);
    void InputText(Uint32 trigger);
    void DrawIP(int x, int y);
    void BuildDisplayText(char *output, int outputSize, const char *text,
                          int password, int editing);

public:
    CNetworkScreen();

    void Draw();
    void Process();
    void Input(Uint32 buttons, Uint32 trigger);
};

#endif
