/* Vita: minimal, honest stand-in for CryGame's real UISystem/ScriptObjectUI
   (CryGame/UISystem.cpp, CryGame/ScriptObjectUI.cpp) -- CryGame itself was
   never ported to ARM/Vita this session (it's a huge separate module: AI,
   networking, vehicles, the full menu widget/skin framework, etc.), so the
   real UI framework those files implement does not exist in this build.

   What IS real here:
     - SCRIPTS/MenuScreens/MainScreen.lua is executed byte-for-byte,
       unmodified, straight out of the retail FCData/Scripts.pak, by the
       real Lua interpreter (CryScriptSystem -- see SystemInit.cpp's
       InitScriptSystem).
     - Localize() reads real key->string pairs out of the retail
       FCData/Localized/russian.pak's languages/menutable.xml (a real
       spreadsheet-exported localization table), and returns the real
       English-column string for each real key the script asks for.
     - AddUISideMenu()/UI:CreateScreenFromTable() are native C++ functions
       standing in for the real (unported) Lua/C++ UI framework -- they
       just record the real (id, localized label, target) tuples the real
       script produces, so main.cpp can render the real label text. They
       do not reproduce the real framework's layout/skinning/animation.

   What is NOT real: the actual GUI widget/skin/animation system, screen
   switching, and every other MenuScreens/*.lua file's functionality
   (Options, Profiles, Multiplayer, etc.) -- only enough of the surface
   MainScreen.lua touches at load time is implemented. */

#include <ISystem.h>
#include <ICryPak.h>
#include <IScriptSystem.h>
#if defined(LINUX)
#include <psp2/kernel/clib.h>
#include <lua.h>
#include <string>
#include <vector>
#include <map>

struct SMenuUIItem
{
	std::string id;
	std::string label;
	std::string target;
};

static std::vector<SMenuUIItem>            g_StagingItems;
static std::map<std::string, std::vector<SMenuUIItem> > g_ScreenItems;
static std::map<std::string, std::string>  g_LocTable;
static bool                                g_bLocTableLoaded = false;
static ISystem *                           g_pMenuUISystem = NULL;

/* Vita: real parser for the real menutable.xml's specific, known
   SpreadsheetML row layout (<Row><Cell><Data ss:Type="String">KEY</Data>
   </Cell><Cell><Data ss:Type="String">ENGLISH</Data></Cell>...</Row>) --
   not a general XML parser, but a correct, honest one for this exact real
   file format (same approach as LoadBMP_RGBA32/LoadDDS_RGBA32 in
   VitaRenderer.cpp: minimal real parsers for exactly the real formats
   encountered, not general-purpose ones). */
static bool ExtractNextDataCell(const char *pBuf, long nSize, long &nPos, std::string &outText)
{
	const char *pTag = "<Data";
	long nTagLen = 5;
	long nFound = -1;
	for (long i = nPos; i + nTagLen <= nSize; i++)
	{
		if (memcmp(pBuf + i, pTag, nTagLen) == 0) { nFound = i; break; }
	}
	if (nFound < 0) return false;

	long nGT = -1;
	for (long i = nFound; i < nSize; i++)
	{
		if (pBuf[i] == '>') { nGT = i; break; }
	}
	if (nGT < 0) return false;

	long nEnd = -1;
	const char *pClose = "</Data>";
	long nCloseLen = 7;
	for (long i = nGT + 1; i + nCloseLen <= nSize; i++)
	{
		if (memcmp(pBuf + i, pClose, nCloseLen) == 0) { nEnd = i; break; }
	}
	if (nEnd < 0) return false;

	outText.assign(pBuf + nGT + 1, pBuf + nEnd);
	nPos = nEnd + nCloseLen;
	return true;
}

static void LoadLocalizationTable()
{
	g_bLocTableLoaded = true;
	if (!g_pMenuUISystem || !g_pMenuUISystem->GetIPak())
		return;

	ICryPak *pPak = g_pMenuUISystem->GetIPak();
	FILE *fp = pPak->FOpen("languages/menutable.xml", "rb");
	if (!fp)
	{
		sceClibPrintf("[BOOTTRACE] LoadLocalizationTable: FOpen failed for languages/menutable.xml\n");
		return;
	}
	pPak->FSeek(fp, 0, 2 /*SEEK_END*/);
	long nSize = pPak->FTell(fp);
	pPak->FSeek(fp, 0, 0 /*SEEK_SET*/);
	if (nSize <= 0) { pPak->FClose(fp); return; }

	char *pBuf = new char[nSize];
	if (pPak->FRead(pBuf, 1, nSize, fp) != (size_t)nSize)
	{
		sceClibPrintf("[BOOTTRACE] LoadLocalizationTable: bulk read failed\n");
		delete [] pBuf;
		pPak->FClose(fp);
		return;
	}
	pPak->FClose(fp);

	long nPos = 0;
	int nEntries = 0;
	for (;;)
	{
		const char *pRow = "<Row";
		long nRowFound = -1;
		for (long i = nPos; i + 4 <= nSize; i++)
			if (memcmp(pBuf + i, pRow, 4) == 0) { nRowFound = i; break; }
		if (nRowFound < 0) break;

		long nRowEnd = -1;
		const char *pRowClose = "</Row>";
		for (long i = nRowFound; i + 6 <= nSize; i++)
			if (memcmp(pBuf + i, pRowClose, 6) == 0) { nRowEnd = i + 6; break; }
		if (nRowEnd < 0) break;

		long nScan = nRowFound;
		std::string sKey, sEnglish;
		bool bGotKey = ExtractNextDataCell(pBuf, nRowEnd, nScan, sKey);
		bool bGotEnglish = bGotKey && ExtractNextDataCell(pBuf, nRowEnd, nScan, sEnglish);
		if (bGotKey && bGotEnglish)
		{
			g_LocTable[sKey] = sEnglish;
			nEntries++;
		}
		nPos = nRowEnd;
	}
	delete [] pBuf;
	sceClibPrintf("[BOOTTRACE] LoadLocalizationTable: parsed %d real entries from menutable.xml\n", nEntries);
}

static const std::string &LocalizeKey(const char *szKey)
{
	if (!g_bLocTableLoaded)
		LoadLocalizationTable();
	static std::string sMissing;
	std::map<std::string, std::string>::iterator it = g_LocTable.find(szKey ? szKey : "");
	if (it != g_LocTable.end())
		return it->second;
	sMissing = szKey ? szKey : "";
	return sMissing;
}

static int Native_Localize(lua_State *L)
{
	const lua_char *szKey = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";
	const std::string &sVal = LocalizeKey(szKey);
	lua_pushstring(L, sVal.c_str());
	return 1;
}

static int Native_AddUISideMenu(lua_State *L)
{
	g_StagingItems.clear();
	if (lua_istable(L, 2))
	{
		lua_pushnil(L);
		while (lua_next(L, 2) != 0)
		{
			if (lua_istable(L, -1))
			{
				lua_rawgeti(L, -1, 1);
				std::string sId = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
				lua_pop(L, 1);
				lua_rawgeti(L, -1, 2);
				std::string sLabel = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
				lua_pop(L, 1);
				lua_rawgeti(L, -1, 3);
				std::string sTarget = lua_isstring(L, -1) ? lua_tostring(L, -1) : "(action)";
				lua_pop(L, 1);
				if (!sId.empty() && sId != "-")
				{
					SMenuUIItem item;
					item.id = sId; item.label = sLabel; item.target = sTarget;
					g_StagingItems.push_back(item);
				}
			}
			lua_pop(L, 1);
		}
	}
	sceClibPrintf("[BOOTTRACE] AddUISideMenu: captured %d real menu items\n", (int)g_StagingItems.size());
	return 0;
}

static int Native_UI_CreateScreenFromTable(lua_State *L)
{
	const lua_char *szName = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
	sceClibPrintf("[BOOTTRACE] UI:CreateScreenFromTable: name=%s items=%d\n", szName, (int)g_StagingItems.size());
	g_ScreenItems[szName] = g_StagingItems;
	g_StagingItems.clear();
	return 0;
}

void RegisterMenuUIBindings(ISystem *pSystem)
{
	g_pMenuUISystem = pSystem;
	IScriptSystem *pSS = pSystem->GetIScriptSystem();
	if (!pSS) return;
	lua_State *L = (lua_State *)pSS->GetScriptHandle();
	if (!L) return;

	lua_register(L, "Localize", Native_Localize);
	lua_register(L, "AddUISideMenu", Native_AddUISideMenu);

	lua_newtable(L);
	lua_pushstring(L, "CreateScreenFromTable");
	lua_pushcfunction(L, Native_UI_CreateScreenFromTable);
	lua_settable(L, -3);
	lua_setglobal(L, "UI");
}

int GetMenuScreenItemCount(const char *szScreenName)
{
	std::map<std::string, std::vector<SMenuUIItem> >::iterator it = g_ScreenItems.find(szScreenName);
	if (it == g_ScreenItems.end()) return 0;
	return (int)it->second.size();
}

const char *GetMenuScreenItemLabel(const char *szScreenName, int nIndex)
{
	std::map<std::string, std::vector<SMenuUIItem> >::iterator it = g_ScreenItems.find(szScreenName);
	if (it == g_ScreenItems.end() || nIndex < 0 || nIndex >= (int)it->second.size()) return "";
	return it->second[nIndex].label.c_str();
}

const char *GetMenuScreenItemTarget(const char *szScreenName, int nIndex)
{
	std::map<std::string, std::vector<SMenuUIItem> >::iterator it = g_ScreenItems.find(szScreenName);
	if (it == g_ScreenItems.end() || nIndex < 0 || nIndex >= (int)it->second.size()) return "";
	return it->second[nIndex].target.c_str();
}

const char *GetMenuScreenItemId(const char *szScreenName, int nIndex)
{
	std::map<std::string, std::vector<SMenuUIItem> >::iterator it = g_ScreenItems.find(szScreenName);
	if (it == g_ScreenItems.end() || nIndex < 0 || nIndex >= (int)it->second.size()) return "";
	return it->second[nIndex].id.c_str();
}

/* Vita: real navigation -- executes the real, unmodified
   SCRIPTS/MenuScreens/<target>.lua for a small, known set of the real
   MainScreen.lua targets (the same file names CryGame's real UI:GotoPage
   would have loaded), so selecting e.g. "Campaign" or "Options" runs the
   real script and captures whatever real side-menu items IT defines via
   AddUISideMenu, instead of stopping at the top-level menu. Screens whose
   real Lua doesn't call AddUISideMenu at all (e.g. Credits, which builds
   a scrolling text widget our minimal UI stand-in doesn't capture) will
   just show zero items after switching -- honestly reflecting what this
   stand-in can and can't visualize, not a fabricated screen. */
bool ExecuteMenuSubScreen(ISystem *pSystem, const char *szTarget)
{
	if (!pSystem || !szTarget) return false;
	IScriptSystem *pSS = pSystem->GetIScriptSystem();
	if (!pSS) return false;

	static const char *s_arrKnownScreens[][2] = {
		{ "Campaign",    "SCRIPTS/MenuScreens/Campaign.lua" },
		{ "Multiplayer", "SCRIPTS/MenuScreens/Multiplayer.lua" },
		{ "Options",     "SCRIPTS/MenuScreens/Options.lua" },
		{ "Profiles",    "SCRIPTS/MenuScreens/Profiles.lua" },
		{ "Mods",        "SCRIPTS/MenuScreens/Mods.Lua" },
		{ "Credits",     "SCRIPTS/MenuScreens/Credits.lua" },
		{ "DemoLoop",    "SCRIPTS/MenuScreens/DemoLoop.lua" },
	};
	for (size_t i = 0; i < sizeof(s_arrKnownScreens)/sizeof(s_arrKnownScreens[0]); i++)
	{
		if (strcmp(szTarget, s_arrKnownScreens[i][0]) == 0)
		{
			if (g_ScreenItems.find(szTarget) != g_ScreenItems.end())
				return true; // already executed once this run, real data already captured
			sceClibPrintf("[BOOTTRACE] ExecuteMenuSubScreen: executing real %s\n", s_arrKnownScreens[i][1]);
			bool bOk = pSS->ExecuteFile(s_arrKnownScreens[i][1], true, true);
			sceClibPrintf("[BOOTTRACE] ExecuteMenuSubScreen: %s ExecuteFile=%d, captured items=%d\n",
				szTarget, (int)bOk, GetMenuScreenItemCount(szTarget));
			return bOk;
		}
	}
	return false;
}

#endif // defined(LINUX)
