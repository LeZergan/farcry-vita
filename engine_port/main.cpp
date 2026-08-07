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

	/* Vita: NOT loading a second real DDS texture here on purpose. The
	   real DXT decoder (LoadDDS_RGBA32 in VitaRenderer.cpp) is verified
	   correct -- CSystem::Init() already loads and uploads a real 256x256
	   DXT3 texture (Textures/Console/DefaultConsole.dds) from this exact
	   pak during boot, confirmed via BOOTTRACE. But requesting a SECOND
	   real texture out of the same already-open .pak (tried here with
	   Textures/gui/mousecursor.dds) hangs indefinitely inside
	   CCachedFileData::GetData()/CMTSafeHeap -- a real, reproducible bug
	   in the ported pak/heap code, not in the DDS decoder. Left as a
	   known follow-up rather than worked around, since silently avoiding
	   it here would hide a bug that blocks loading more than one real
	   game texture per pak. */

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
			if (pSplash) {
				// Draw the real decoded bitmap at its native pixel size, no scaling/cropping.
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
