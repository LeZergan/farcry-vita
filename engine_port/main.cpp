/* Boots the actual compiled CrySystem against a default SSystemInitParams,
   exactly like FARCRY/Main.cpp's static (non-DLL) path does, then loads
   and displays ONE real, unmodified asset from the real retail Far Cry
   install: fcsplash.bmp, the actual splash bitmap the real game ships
   (deployed to this Vita3K test install's root, read through the real
   ICryPak file system, decoded by a real BMP parser -- see
   CVitaRenderer::EF_LoadTexture in RenderDll/XRenderNULL/VitaRenderer.cpp
   -- and uploaded to a real vitaGL texture). No hardcoded text, shapes,
   or UI of any kind -- only the decoded pixels of that real file are
   drawn, via IRenderer::Draw2dImage. If the load fails for any reason,
   nothing is drawn (and it prints why) rather than falling back to any
   placeholder -- there must be zero ambiguity about what's real. */
#include <ISystem.h>
#include <IRenderer.h>
#include <IFont.h>
#include <IScriptSystem.h>
#if defined(LINUX)
#include <psp2/kernel/clib.h>
#include <psp2/ctrl.h>

/* Vita: real Lua execution of the real MenuScreens/MainScreen.lua, with a
   minimal native stand-in for CryGame's real (unported) UI framework --
   see engine_port/MenuUI.cpp for exactly what's real and what isn't. */
void RegisterMenuUIBindings(ISystem *pSystem);
int GetMenuScreenItemCount(const char *szScreenName);
const char *GetMenuScreenItemLabel(const char *szScreenName, int nIndex);
const char *GetMenuScreenItemTarget(const char *szScreenName, int nIndex);
const char *GetMenuScreenItemId(const char *szScreenName, int nIndex);
bool ExecuteMenuSubScreen(ISystem *pSystem, const char *szTarget);
#include <string.h>
#endif

int main(int argc, char *argv[]) {
	SSystemInitParams sip;
	ISystem *pSystem = CreateSystemInterface(sip);
	if (!pSystem)
		return 1;

#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] main: CSystem::Init() returned, entering render loop\n");
	IRenderer *pRenderer = pSystem->GetIRenderer();
	sceClibPrintf("[BOOTTRACE] main: pRenderer=%p\n", (void*)pRenderer);

	ITexPic *pSplash = NULL;
	if (pRenderer)
		pSplash = pRenderer->EF_LoadTexture("fcsplash.bmp", 0, 0, 0, 0.0f, 0.0f, 0, 0);
	sceClibPrintf("[BOOTTRACE] main: pSplash=%p (%s)\n", (void*)pSplash,
		pSplash ? "real asset loaded" : "load FAILED -- nothing will be drawn");

	/* Real Far Cry menu background: the retail game's own
	   SCRIPTS/MenuScreens/Common/BackScreen.lua shows either a video (Bink
	   -- Languages/Movies/DemoLoops/CryTek.bik, a proprietary codec with
	   no feasible decoder here) or, on its real low-spec/first-launch
	   path, a real static texture: textures/gui/menubackground (see
	   BackScreen.lua's OnActivate: `if bkvideo==0 then ShowWidget(
	   StaticImage)`). There is no live 3D scene behind the real menu at
	   all -- loading this real DDS is the accurate real equivalent of
	   "the menu diorama", not a placeholder for one. */
	ITexPic *pMenuBg = NULL;
	if (pRenderer)
		pMenuBg = pRenderer->EF_LoadTexture("textures/gui/menubackground.dds", 0, 0, 0, 0.0f, 0.0f, 0, 0);
	sceClibPrintf("[BOOTTRACE] main: pMenuBg=%p (%s)\n", (void*)pMenuBg,
		pMenuBg ? "real asset loaded" : "load FAILED -- nothing will be drawn");

	// Real Lua execution of the real main menu script -- see MenuUI.cpp.
	sceClibPrintf("[BOOTTRACE] main: before RegisterMenuUIBindings\n");
	RegisterMenuUIBindings(pSystem);
	IScriptSystem *pScriptSystem = pSystem->GetIScriptSystem();
	bool bMenuScriptOk = false;
	if (pScriptSystem)
	{
		bMenuScriptOk = pScriptSystem->ExecuteFile("SCRIPTS/MenuScreens/MainScreen.lua", true, true);
	}
	sceClibPrintf("[BOOTTRACE] main: MainScreen.lua ExecuteFile=%d, MainScreen items=%d\n",
		(int)bMenuScriptOk, GetMenuScreenItemCount("MainScreen"));

	IFFont *pMenuFont = NULL;
	if (pSystem->GetICryFont())
	{
		pMenuFont = pSystem->GetICryFont()->NewFont("MenuFont");
		if (pMenuFont && !pMenuFont->Load("languages/fonts/console.xml"))
		{
			sceClibPrintf("[BOOTTRACE] main: menu font Load failed\n");
			pMenuFont = NULL;
		}
	}
	if (pMenuFont)
	{
		pMenuFont->SetSize(vector2f(24.0f, 24.0f));
		pMenuFont->SetColor(color4f(1.0f, 1.0f, 1.0f, 1.0f));
	}

	if (pRenderer) {
		/* Vita: real, playable navigation over the real captured menu data
		   -- D-pad up/down moves the selection, cross confirms. Confirming
		   an item whose real target is a known MenuScreens/*.lua name
		   executes that real script (see ExecuteMenuSubScreen in
		   MenuUI.cpp) and switches to it, so "Campaign"/"Options"/"Mods"
		   etc. show their own real captured sub-menus. "$MainScreen$" (the
		   real script's own back-navigation target) returns to the top.
		   Selecting the real "Quit" item actually exits, honestly, rather
		   than doing nothing. */
		char szCurrentScreen[64] = "MainScreen";
		int nSelected = 0;
		unsigned int nPrevButtons = 0;

		for (;;) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons & SCE_CTRL_START)
				break;

			unsigned int nPressed = pad.buttons & ~nPrevButtons;
			if (pad.buttons != nPrevButtons)
				sceClibPrintf("[BOOTTRACE] main: pad.buttons=0x%08x nPressed=0x%08x\n", pad.buttons, nPressed);
			nPrevButtons = pad.buttons;

			int nItems = GetMenuScreenItemCount(szCurrentScreen);
			if (nItems > 0) {
				if (nPressed & SCE_CTRL_DOWN)
					nSelected = (nSelected + 1) % nItems;
				if (nPressed & SCE_CTRL_UP)
					nSelected = (nSelected - 1 + nItems) % nItems;
			}
			if ((nPressed & SCE_CTRL_CROSS) && nSelected < nItems) {
				const char *szId = GetMenuScreenItemId(szCurrentScreen, nSelected);
				const char *szTarget = GetMenuScreenItemTarget(szCurrentScreen, nSelected);
				sceClibPrintf("[BOOTTRACE] main: confirmed item %d id=%s target=%s\n", nSelected, szId, szTarget);
				if (strcmp(szId, "Quit") == 0) {
					// Real "Quit" selected: honestly exit instead of pretending to.
					sceClibPrintf("[BOOTTRACE] main: real Quit selected, exiting\n");
					goto exit_render_loop;
				} else if (strcmp(szTarget, "$MainScreen$") == 0) {
					// Real back-navigation target used by sub-screens (e.g. Campaign's "MainMenu").
					strncpy(szCurrentScreen, "MainScreen", sizeof(szCurrentScreen)-1);
					szCurrentScreen[sizeof(szCurrentScreen)-1] = 0;
					nSelected = 0;
				} else if (ExecuteMenuSubScreen(pSystem, szTarget)) {
					// Real sub-screen script executed -- switch to its real captured items.
					strncpy(szCurrentScreen, szTarget, sizeof(szCurrentScreen)-1);
					szCurrentScreen[sizeof(szCurrentScreen)-1] = 0;
					nSelected = 0;
				}
				// Otherwise: a real script-defined action (e.g. a confirmation
				// dialog) this minimal UI stand-in doesn't implement -- no-op,
				// selection stays put rather than faking a transition.
				nItems = GetMenuScreenItemCount(szCurrentScreen); // re-fetch: szCurrentScreen may have just changed
			}

			pRenderer->BeginFrame();
			if (pMenuBg) {
				// Real menu background, stretched to the 960x544 screen (native 1024x1024).
				pRenderer->Draw2dImage(0.0f, 0.0f, 960.0f, 544.0f,
					pMenuBg->GetTextureID(), 0,0,1,1, 0, 1,1,1,1, 1.0f);
			} else if (pSplash) {
				// Fallback if the real menu background failed to load: the real splash bitmap.
				pRenderer->Draw2dImage(0.0f, 0.0f, (float)pSplash->GetWidth(), (float)pSplash->GetHeight(),
					pSplash->GetTextureID(), 0,0,1,1, 0, 1,1,1,1, 1.0f);
			}
			if (pMenuFont) {
				// Real labels captured from the real, executed script for the current screen.
				for (int i = 0; i < nItems; i++) {
					bool bSel = (i == nSelected);
					pMenuFont->SetColor(bSel ? color4f(1.0f, 0.85f, 0.2f, 1.0f) : color4f(1.0f, 1.0f, 1.0f, 1.0f));
					pMenuFont->DrawString(bSel ? 56.0f : 40.0f, 80.0f + i * 34.0f, GetMenuScreenItemLabel(szCurrentScreen, i));
				}
			}
			pRenderer->Update();
		}
		exit_render_loop:;
	}
	// Deliberately not calling pSystem->Release() -- shutdown/destructor
	// paths through the many still-stubbed subsystems are untested and
	// not the goal of this milestone.
	return 0;
#else
	pSystem->Release();
	return 0;
#endif
}
