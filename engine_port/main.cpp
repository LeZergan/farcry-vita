/* Boots the actual compiled CrySystem against a default SSystemInitParams,
   exactly like FARCRY/Main.cpp's static (non-DLL) path does, then runs a
   real frame loop (BeginFrame/Update -- see
   RenderDll/XRenderNULL/VitaRenderer.cpp) that draws real text every
   frame via the actual CryFont pipeline (real FreeType2-rasterized
   glyphs from the real, retail Far Cry font data now deployed, drawn
   through real vitaGL texture/vertex-array calls -- see
   CVitaRenderer::FontCreateTexture/GetDynVBPtr/DrawDynVB). No game
   update logic or 3D scene yet -- this is the text-rendering pipeline's
   first real on-screen output milestone. */
#include <ISystem.h>
#include <IRenderer.h>
#include <IFont.h>
#if defined(LINUX)
#include <psp2/kernel/clib.h>
#include <psp2/ctrl.h>
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

	IFFont *pFont = NULL;
	if (pSystem->GetICryFont())
		pFont = pSystem->GetICryFont()->GetFont("Default");
	sceClibPrintf("[BOOTTRACE] main: pFont=%p\n", (void*)pFont);
	if (pFont) {
		/* Vita: make this unmistakable on screen -- large and centered,
		   not the default (likely small, ~16px) console-text size. */
		pFont->SetSize(vector2f(96.0f, 96.0f));
		pFont->SetSameSize(true);
	}

	if (pRenderer) {
		for (;;) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons & SCE_CTRL_START)
				break;
			pRenderer->BeginFrame();
			if (pFont)
				pFont->DrawString(60.0f, 200.0f, "FAR CRY", true);
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
