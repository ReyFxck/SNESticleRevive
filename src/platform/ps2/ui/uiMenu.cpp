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
	Bool shoulders = !strcmp(m_strTitle, "Save States") ? TRUE : FALSE;

	FontSelect(0);
	UiChromeHeader(m_strTitle, shoulders);

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

	/* Existing context/help strings remain available, but use the same
	   orange section language as the configuration screen. */
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

	UiChromeHint(UI_ICON_UP, UI_ICON_DOWN, 55, 198, "Select");
	UiChromeHint(UI_ICON_CIRCLE, UI_ICON_COUNT, 151, 198, "Choose");
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

	if (trigger & (PAD_CIRCLE | PAD_START))
{
    SendMessage(1, m_iSelect, m_pUserData);
}

}
