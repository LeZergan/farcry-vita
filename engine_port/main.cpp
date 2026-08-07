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

	ITexPic *pSplash = NULL;
	if (pRenderer)
		pSplash = pRenderer->EF_LoadTexture("fcsplash.bmp", 0, 0, 0, 0.0f, 0.0f, 0, 0);
	sceClibPrintf("[BOOTTRACE] main: pSplash=%p (%s)\n", (void*)pSplash,
		pSplash ? "real asset loaded" : "load FAILED -- nothing will be drawn");

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
