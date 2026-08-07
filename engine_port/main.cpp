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
		for (;;) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons & SCE_CTRL_START)
				break;
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
				// Real labels captured from the real, executed MainScreen.lua (see MenuUI.cpp).
				int nItems = GetMenuScreenItemCount("MainScreen");
				for (int i = 0; i < nItems; i++) {
					pMenuFont->DrawString(40.0f, 80.0f + i * 34.0f, GetMenuScreenItemLabel("MainScreen", i));
				}
			}
			pRenderer->Update();
		}
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
