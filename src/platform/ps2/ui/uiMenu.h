/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the uiMenu interface for the PlayStation 2 user interface.
 */

#ifndef _UIMENU_H
#define _UIMENU_H

#include "uiScreen.h"

class CMenuScreen : public CScreen
{
	void 	*m_pUserData;

	char	m_strTitle[64];
	char    *m_pEntries[32];

	Int32 	m_iSelect;   // current selected item
	Int32	m_nItems;	 // total number of items
	Int32	m_iTop;		 // title/header vertical position

	char 	m_strText[4][256];

public:
	CMenuScreen();

	void SetUserData(void *pUserData) {m_pUserData = pUserData;}

	void SetEntries(char **ppStrings);
	void SetTitle(const char *pTitle);
	void SetText(int iText, const char *pStr);
	void SetSelection(Int32 iSelect);
	void SetTop(Int32 iTop) {m_iTop = iTop;}
	char *GetText(int iText) {return m_strText[iText];}

	void Draw();
	void Process();
	void Input(Uint32 Buttons, Uint32 Trigger);
};

#endif
