/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the uiVideo interface for the PlayStation 2 user interface.
 */

#ifndef _UIVIDEO_H
#define _UIVIDEO_H

#include "uiScreen.h"

class CVideoScreen : public CScreen
{
	int m_iSelect;

public:
	CVideoScreen();

	void Draw();
	void Process();
	void Input(Uint32 Buttons, Uint32 Trigger);
};

/* Video settings persistence -> mc0:/SNESticle/video.cfg.
   VideoSettingsLoad() is called at boot (after the memory card is up)
   to populate g_GskVideoMode / g_GskDispOffX / g_GskDispOffY. */
void VideoSettingsLoad(void);
void VideoSettingsSave(void);

#endif
