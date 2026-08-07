/* Boots the actual compiled CrySystem against a default SSystemInitParams,
   exactly like FARCRY/Main.cpp's static (non-DLL) path does, then runs a
   real frame loop (BeginFrame/Update -- see
   RenderDll/XRenderNULL/VitaRenderer.cpp) that draws a basic real menu
   every frame: solid-color panel/button rectangles via
   IRenderer::Draw2dImage, real text via the actual CryFont pipeline
   (real FreeType2-rasterized glyphs from the real, retail Far Cry font
   data, drawn through real vitaGL texture/vertex-array calls). No game
   update logic or 3D scene yet -- this is the 2D UI primitives' first
   real on-screen milestone, not a running game. */
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
	if (pFont)
		pFont->SetSameSize(true);

	static const char *items[] = { "NEW GAME", "LOAD GAME", "OPTIONS", "QUIT" };
	const int nItems = 4;
	const float panelX = 60.0f, panelY = 40.0f, panelW = 400.0f, panelH = 460.0f;
	const float btnW = 320.0f, btnH = 56.0f, btnGap = 20.0f;
	const float btnX = panelX + (panelW - btnW) * 0.5f;
	const float firstBtnY = 220.0f;

	if (pRenderer) {
		for (;;) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons & SCE_CTRL_START)
				break;
			pRenderer->BeginFrame();

			// Menu background panel (solid color, texture_id<=0 -> untextured quad).
			pRenderer->Draw2dImage(panelX, panelY, panelW, panelH, 0, 0,0,1,1, 0,
				0.08f, 0.10f, 0.16f, 0.92f, 1.0f);

			if (pFont) {
				pFont->SetSize(vector2f(64.0f, 64.0f));
				pFont->DrawString(panelX + 40.0f, panelY + 30.0f, "FAR CRY", true);
			}

			for (int i = 0; i < nItems; i++) {
				float by = firstBtnY + i * (btnH + btnGap);
				// Button rectangle.
				pRenderer->Draw2dImage(btnX, by, btnW, btnH, 0, 0,0,1,1, 0,
					0.20f, 0.24f, 0.32f, 1.0f, 1.0f);
				if (pFont) {
					pFont->SetSize(vector2f(28.0f, 28.0f));
					pFont->DrawString(btnX + 24.0f, by + 14.0f, items[i], true);
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
