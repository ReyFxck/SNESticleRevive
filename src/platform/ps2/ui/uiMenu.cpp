/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements uiMenu behavior for the PlayStation 2 user interface.
 */

#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#include "font.h"
#include "poly.h"
#include "uiMenu.h"
#include "uiChrome.h"

void CMenuScreen::SetEntries(char **ppStrings)
{
	m_nItems = 0;

	while (*ppStrings && m_nItems < 32)
	{
		m_pEntries[m_nItems++] = *ppStrings;
		ppStrings++;
	}
}

CMenuScreen::CMenuScreen()
{
	m_iSelect = 0;
	m_nItems  = 0;
	m_iTop    = 40;
	memset(m_strText, 0, sizeof(m_strText));
	m_strTitle[0] = 0;
///	SetEntries(_TestStr);
}

void CMenuScreen::SetTitle(const char *pTitle)
{
	strcpy(m_strTitle, pTitle);
}

void CMenuScreen::SetText(int iText, const char *pStr)
{
	strcpy(m_strText[iText], pStr);
}

void CMenuScreen::SetSelection(Int32 iSelect)
{
	if (m_nItems <= 0)
	{
		m_iSelect = 0;
		return;
	}

	if (iSelect < 0) iSelect = 0;
	if (iSelect >= m_nItems) iSelect = m_nItems - 1;
	m_iSelect = iSelect;
}

static void _MenuPrintAlignCenter(int x, int y, const char *str, Bool bHighlight = FALSE)
{
    x-= FontGetStrWidth(str) / 2;
    FontPuts(x, y, str);

    if (bHighlight)
    {
		PolyColor4f(0.0f, 1.0f, 0.0f, 0.5f);
		PolyRect(x-1, y-1, FontGetStrWidth(str) + 2, FontGetHeight() + 2);
    }
}

static void _MenuHeader(int vy, const char *str)
{
    PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f);
	PolyRect(32, vy, 256-64, 9);
	FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
    _MenuPrintAlignCenter(128, vy, str);
}

void CMenuScreen::Draw()
{
	Int32 iLine;
	Int32 vx = 48;
	Int32 vy = 38;
	Bool bStateManager = !strcmp(m_strTitle, "Save States") ? TRUE : FALSE;
	Bool bMemCardPrompt = !strcmp(m_strTitle, "Memory Card") ? TRUE : FALSE;
	Bool bStateLocation = !strcmp(m_strTitle, "Save State Location") ? TRUE : FALSE;
	Bool shoulders = bStateManager;

	FontSelect(0);
	UiChromeHeader(m_strTitle, shoulders);

	if (bMemCardPrompt)
	{
		UiChromeSection(34, "Format Card");
		for (iLine = 0; iLine < m_nItems; iLine++)
		{
			Int32 rowY = 49 + iLine * 16;
			if (iLine == m_iSelect)
			{
				PolyTexture(NULL);
				PolyBlend(TRUE);
				PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
				PolyRect(44, rowY - 1, 168, FontGetHeight() + 2);
			}
			FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			if (m_pEntries[iLine])
				FontPuts(52, rowY, m_pEntries[iLine]);
		}

		UiChromeSection(91, "Warning");
		FontColor4f(0.86f, 0.86f, 0.86f, 1.0f);
		if (m_strText[0][0]) _MenuPrintAlignCenter(128, 107, m_strText[0]);
		FontColor4f(1.0f, 0.48f, 0.48f, 1.0f);
		_MenuPrintAlignCenter(128, 123, "Formatting erases the entire card.");
		FontColor4f(0.62f, 0.62f, 0.62f, 1.0f);
		_MenuPrintAlignCenter(128, 143, "No / Cancel is selected by default");

		UiChromeHint(UI_ICON_UP, UI_ICON_DOWN, 45, 198, "Select");
		UiChromeHint(UI_ICON_CROSS, UI_ICON_COUNT, 126, 198, "Choose");
		UiChromeHint(UI_ICON_CIRCLE, UI_ICON_COUNT, 184, 198, "Cancel");
		return;
	}

	if (bStateManager)
	{
		/* Save States deserves its own layout: the old generic menu dumped
		   four help sentences into the middle of the screen and looked much
		   denser than the rest of the refreshed frontend. */
		UiChromeSection(34, "State Files");

		/* Browse is the single file-management action. */
		if (m_nItems > 0 && m_pEntries[0])
		{
			if (m_iSelect == 0)
			{
				PolyTexture(NULL);
				PolyBlend(TRUE);
				PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
				PolyRect(44, 47, 168, FontGetHeight() + 2);
			}
			FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			FontPuts(52, 48, m_pEntries[0]);
		}

		UiChromeSection(69, "Quick Save");

		/* Storage and slot are rendered as label/value pairs so the changing
		   value stays aligned like Configurations instead of moving the row. */
		for (iLine = 1; iLine <= 2 && iLine < m_nItems; iLine++)
		{
			Char label[32];
			const Char *pValue = "";
			const Char *pColon;
			Int32 rowY = (iLine == 1) ? 84 : 98;
			Int32 labelLen;

			pColon = strchr(m_pEntries[iLine], ':');
			labelLen = pColon ? (Int32)(pColon - m_pEntries[iLine]) :
			                   (Int32)strlen(m_pEntries[iLine]);
			if (labelLen >= (Int32)sizeof(label)) labelLen = sizeof(label) - 1;
			memcpy(label, m_pEntries[iLine], labelLen);
			label[labelLen] = 0;
			if (pColon)
			{
				pValue = pColon + 1;
				while (*pValue == ' ') pValue++;
			}

			if (m_iSelect == iLine)
			{
				PolyTexture(NULL);
				PolyBlend(TRUE);
				PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
				PolyRect(44, rowY - 1, 168, FontGetHeight() + 2);
			}
			FontColor4f(0.62f, 0.62f, 0.62f, 1.0f);
			FontPuts(52, rowY, label);
			FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			FontPuts(137, rowY, pValue);
		}

		if (m_nItems > 3 && m_pEntries[3])
		{
			if (m_iSelect == 3)
			{
				PolyTexture(NULL);
				PolyBlend(TRUE);
				PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
				PolyRect(44, 112, 168, FontGetHeight() + 2);
			}
			FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			FontPuts(52, 113, "Choose Save Location Again");
		}

		UiChromeSection(137, "Current Target");
		if (m_strText[0][0])
		{
			const Char *p = strchr(m_strText[0], ':');
			if (p) p++; else p = m_strText[0];
			while (*p == ' ') p++;
			FontColor4f(0.78f, 0.78f, 0.78f, 1.0f);
			_MenuPrintAlignCenter(128, 151, p);
		}

		/* Keep only the one useful state-browser warning here. The file-menu
		   controls are shown by the browser itself once it is opened. */
		FontColor4f(0.48f, 0.48f, 0.48f, 1.0f);
		_MenuPrintAlignCenter(128, 171, "Deleting a state removes both banks");

		UiChromeHint(UI_ICON_UP, UI_ICON_DOWN, 39, 198, "Select");
		if (m_iSelect == 1 || m_iSelect == 2)
		{
			UiChromeHint(UI_ICON_LEFT, UI_ICON_RIGHT, 132, 198, "Change");
		}
		else
		{
			UiChromeHint(UI_ICON_CROSS, UI_ICON_COUNT, 157, 198, "Choose");
		}
		return;
	}

	for (iLine = 0; iLine < m_nItems; iLine++)
	{
		Char *pStr = m_pEntries[iLine];
		if (pStr)
		{
			if (iLine == m_iSelect)
			{
				PolyTexture(NULL);
				PolyBlend(TRUE);
				PolyColor4f(0.0f, 0.50f, 0.0f, 0.50f);
				PolyRect(vx - 4, vy - 1, 164, FontGetHeight() + 2);
			}
			FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			FontPuts(vx, vy, pStr);
		}
		vy += FontGetHeight() + 2;
	}

	{
		Bool anyText = FALSE;
		for (iLine = 0; iLine < 4; iLine++)
			if (m_strText[iLine][0]) anyText = TRUE;
		if (anyText && vy < 164)
		{
			vy += 4;
			UiChromeSection(vy, "Info");
			vy += 12;
			for (iLine = 0; iLine < 4 && vy < 190; iLine++)
			{
				if (m_strText[iLine][0])
				{
					FontColor4f(0.70f, 0.70f, 0.70f, 1.0f);
					_MenuPrintAlignCenter(128, vy, m_strText[iLine]);
					vy += FontGetHeight() + 2;
				}
			}
		}
	}

	UiChromeHint(UI_ICON_UP, UI_ICON_DOWN, 45, 198, "Select");
	if (bStateLocation)
	{
		UiChromeHint(UI_ICON_CROSS, UI_ICON_COUNT, 127, 198, "Choose");
		UiChromeHint(UI_ICON_CIRCLE, UI_ICON_COUNT, 185, 198, "Cancel");
	}
	else
	{
		UiChromeHint(UI_ICON_CIRCLE, UI_ICON_COUNT, 151, 198, "Choose");
	}
}

void CMenuScreen::Process()
{
}

void CMenuScreen::Input(Uint32 buttons, Uint32 trigger)
{
	if (trigger & PAD_UP)
	{
		m_iSelect--;
	}

	if (trigger & PAD_DOWN)
	{
		m_iSelect++;
	}

	if (m_iSelect < 0) m_iSelect = 0;
	if (m_iSelect > (m_nItems - 1)) m_iSelect = (m_nItems - 1);

	if (!strcmp(m_strTitle, "Save States"))
	{
		if (trigger & PAD_LEFT)
			SendMessage(2, m_iSelect, m_pUserData);
		if (trigger & PAD_RIGHT)
			SendMessage(3, m_iSelect, m_pUserData);
		if (trigger & (PAD_CROSS | PAD_START))
			SendMessage(1, m_iSelect, m_pUserData);
	}
	else if (!strcmp(m_strTitle, "Save State Location") ||
	         !strcmp(m_strTitle, "Memory Card"))
	{
		if (trigger & (PAD_CROSS | PAD_START))
			SendMessage(1, m_iSelect, m_pUserData);
	}
	else if (trigger & (PAD_CIRCLE | PAD_START))
	{
		SendMessage(1, m_iSelect, m_pUserData);
	}

}
