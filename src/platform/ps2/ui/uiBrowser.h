
#ifndef _UIBROWSER_H
#define _UIBROWSER_H

#include "uiScreen.h"
#include "uiMenu.h"

/* NAME_MAX on PS2's iomanX-backed filesystems is 255 chars (matching
   POSIX). The original iaddis build hard-capped each entry at 64,
   which silently truncated names like
   "Super Mario All-Stars + Super Mario World (USA).sfc" (52 chars,
   fits) but blew up on No-Intro / GoodSNES dumps that routinely run
   80-120 chars. When that happened, GetEntryPath() rebuilt the path
   with the truncated name and fopen() returned NULL, producing the
   classic "ERROR: <path>" modal users see on long-named ROMs. 256 is
   one byte over NAME_MAX so any single dirent fits, and the per-entry
   cost (256 + 4 + 4 = 264 bytes) keeps a 6000-entry browser under
   1.6 MB - well within the 32 MB EE RAM budget. */
#define BROWSER_ENTRY_MAXCHARS (256)

enum BrowserEntryTypeE
{
	BROWSER_ENTRYTYPE_DIR,
	BROWSER_ENTRYTYPE_DRIVE,
	BROWSER_ENTRYTYPE_EXECUTABLE,
	BROWSER_ENTRYTYPE_OTHER,

	BROWSER_ENTRYTYPE_NUM
};

struct BrowserEntryT
{
	Char name[BROWSER_ENTRY_MAXCHARS];
	Int32 size;
	BrowserEntryTypeE eType;
};

typedef BrowserEntryTypeE (*BrowserNameResolveFuncT)(const char *pName);

class CBrowserScreen : public CScreen
{
	BrowserEntryT 	*m_pDirEntries;
	Int32 			m_nEntries;
	Int32   		m_MaxEntries;

	Char 	m_Dir[512];

	Int32 	m_iSelect;

	Int32 	m_iScroll;
	Int32 	m_MaxLines;

	/* Retained for callers that still inspect it; with the iomanX
	   migration, directory iteration is the same for every device
	   so we no longer branch on this flag. */
	Bool	m_bMCDir;

	Bool	m_bSubMenu;
	CMenuScreen m_SubMenu;

	static int MenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2);

public:
	CBrowserScreen(Uint32 uMaxEntries);
	~CBrowserScreen();

	void Draw();
	void Process();
	void Input(Uint32 Buttons, Uint32 Trigger);

	void ResetEntries();
	void SortEntries();
	void AddEntry(const Char *pName, BrowserEntryTypeE eType, Int32 size);

	int GetEntryPath(char *pStr, int nChars);
	Char *GetEntryName();
	BrowserEntryTypeE GetEntryType();

	void SetDir(const Char *pDir);
	void Chdir(const Char *pSubDir);

};

#endif

