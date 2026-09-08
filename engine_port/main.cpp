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
#include <IGame.h>
#include <IInput.h>
#include <IConsole.h>
#include <ILog.h>
#include <IRenderer.h>
#include <IFont.h>
#include <IScriptSystem.h>
#include <I3DEngine.h>
#include <Cry_Camera.h>
#if defined(LINUX)
#include <psp2/kernel/clib.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>	// SCE_SYSMODULE_NET must be loaded before sceNetInit
#include <unistd.h>	// chdir -- see the data-root selection in main()
#include <stdarg.h>	// VitaBootMark's varargs
#include <stdio.h>

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

#if defined(LINUX)
/* VitaSDK's default newlib heap is 128 MiB.  A complete Far Cry level
   legitimately exceeds that while the static-object cache and AI graph are
   resident, even though the hardware still has unassigned user RAM.  Keep
   CPU allocations and vitaGL's GPU-facing reserve balanced at 192 MiB each;
   CDRAM/phycont are configured separately by CVitaRenderer. */
/* Sizing note for hardware.  An ordinary Vita application gets about 256 MiB of
   user RAM, and param.sfo here has ATTRIBUTE2=0 / EBOOT_APP_MEMSIZE=0, so no
   extended memory is granted.  This heap plus vitaGL's main-RAM pool, phycont
   and common-dialog reserves is what has to fit in that budget.

   Measured on a full Training load: 120 MiB loads the level then dies during
   precaching, 160 MiB dies the same way, 224 MiB is stable.  So Far Cry's level
   data genuinely needs a heap larger than the standard budget can leave room
   for, and this port cannot fit in 256 MiB however the pools are divided --
   raising CDRAM to carry the textures instead crashes at the first frame.

   224 MiB here plus the trimmed 96 MiB vitaGL pool (CVitaRenderer::Init) totals
   roughly 337 MiB of main RAM, down from about 400 MiB.  That fits the extended
   memory budget, so running on real hardware needs ATTRIBUTE2=12 in param.sfo
   (~365 MiB); it will not start without it. */
/* 192 MiB left no headroom once a level was loaded: opening the in-game menu
   asked for a ~2 MiB video frame buffer and malloc returned null.  The earlier
   224 MiB attempt failed to reserve, but that was alongside a 96 MiB vitaGL
   pool; the pool is 64 MiB now, so this total is smaller than that one was.
   If boot_marker.txt stops being written at all, this is the first thing to
   put back. */
extern "C" unsigned int _newlib_heap_size_user = 216u * 1024u * 1024u;

/* Vita: real, standalone proof that CVitaRenderer's 3D draw path (matrix
   stack, SetCamera, CreateBuffer/CreateIndexBuffer, DrawBuffer -- see
   RenderDll/XRenderNULL/VitaRenderer.cpp) actually draws real submitted
   geometry through real vitaGL calls, decoupled from level loading (which
   is separately blocked on what looks like a Vita3K file-I/O emulation
   bug -- see FReadChunked's comment in VitaRenderer.cpp). A real 8-vertex,
   12-triangle, per-vertex-colored cube, submitted through the exact same
   CreateBuffer/UpdateBuffer/CreateIndexBuffer/DrawBuffer calls
   Cry3DEngine's terrain/static-object code uses -- not a shortcut path. */
static CVertexBuffer *g_pTestCubeVB = NULL;
static SVertexStream g_TestCubeIB;

static void InitTestCube(IRenderer *pRenderer)
{
	struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F verts[8];
	const float s = 1.0f;
	const Vec3 pos[8] = {
		Vec3(-s,-s,-s), Vec3( s,-s,-s), Vec3( s, s,-s), Vec3(-s, s,-s),
		Vec3(-s,-s, s), Vec3( s,-s, s), Vec3( s, s, s), Vec3(-s, s, s),
	};
	const byte col[8][3] = {
		{255,0,0}, {0,255,0}, {0,0,255}, {255,255,0},
		{255,0,255}, {0,255,255}, {255,255,255}, {128,128,128},
	};
	for (int i = 0; i < 8; i++)
	{
		verts[i].xyz = pos[i];
		verts[i].color.bcolor[0] = col[i][0];
		verts[i].color.bcolor[1] = col[i][1];
		verts[i].color.bcolor[2] = col[i][2];
		verts[i].color.bcolor[3] = 255;
		verts[i].st[0] = 0.0f;
		verts[i].st[1] = 0.0f;
	}

	g_pTestCubeVB = pRenderer->CreateBuffer(8, VERTEX_FORMAT_P3F_COL4UB_TEX2F, "TestCube", false);
	pRenderer->UpdateBuffer(g_pTestCubeVB, verts, 8, true, 0, VSF_GENERAL);

	static const ushort inds[36] = {
		0,1,2, 0,2,3,       // back
		5,4,7, 5,7,6,       // front
		4,0,3, 4,3,7,       // left
		1,5,6, 1,6,2,       // right
		3,2,6, 3,6,7,       // top
		4,5,1, 4,1,0,       // bottom
	};
	pRenderer->CreateIndexBuffer(&g_TestCubeIB, inds, 36);
}

extern "C" void Vita_DebugSetSimpleCamera(float dist, int width, int height);

static void DrawTestCube(IRenderer *pRenderer, float fAngleDeg)
{
	// Vita: TEMPORARY -- bypasses CCamera/SetCamera entirely (see
	// Vita_DebugSetSimpleCamera's own comment in VitaRenderer.cpp) to rule
	// out CCamera's axis convention as the reason nothing lands on screen.
	Vita_DebugSetSimpleCamera(6.0f, pRenderer->GetWidth(), pRenderer->GetHeight());

	pRenderer->SetState(GS_DEPTHWRITE);
	pRenderer->SetCullMode(R_CULL_NONE);
	pRenderer->SetTexture(0, eTT_Base);
	pRenderer->PushMatrix();
	pRenderer->RotateMatrix(fAngleDeg, 0.0f, 0.0f, 1.0f);
	pRenderer->RotateMatrix(fAngleDeg * 0.6f, 1.0f, 0.0f, 0.0f);
	pRenderer->DrawBuffer(g_pTestCubeVB, &g_TestCubeIB, 36, 0, R_PRIMV_TRIANGLES, 0, 8, NULL);
	pRenderer->PopMatrix();
}
#endif

#if defined(LINUX)
/* Appends one line to ux0:data/farcry/boot_marker.txt and closes it again, so
   the file on disk is always current even if the next call never returns. The
   working directory is not set until part way through main(), hence the
   absolute path with a relative fallback. */
static void VitaBootMark(const char *format, ...)
{
	FILE *marker = fopen("ux0:data/farcry/boot_marker.txt", "ab");
	if (!marker)
		marker = fopen("boot_marker.txt", "ab");
	if (!marker)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(marker, format, args);
	va_end(args);
	fputc('\n', marker);
	fclose(marker);
}
#endif

int main(int argc, char *argv[]) {
#if defined(LINUX)
	// Fresh file each boot: truncate before the first append below.
	{
		FILE *reset = fopen("ux0:data/farcry/boot_marker.txt", "wb");
		if (!reset)
			reset = fopen("boot_marker.txt", "wb");
		if (reset)
			fclose(reset);
	}
#endif
#if defined(LINUX)
	/* Game data lives in ux0:data, not next to the executable.  ux0:app holds
	   what the VPK installed and is not where several gigabytes of retail
	   assets belong -- it is rewritten on every reinstall.  Everything the
	   engine opens is a path relative to the working directory (ICryPak's
	   "fcdata/...", "Levels/...", the .bik movies under Languages/), so point
	   the working directory at the data root and the whole engine follows.
	   Falls through to the executable's own directory when that folder is not
	   present, which keeps a Vita3K install that has the data alongside
	   eboot.bin working unchanged. */
	/* Boot markers.  On hardware a startup failure can kill the process before
	   CSystem ever opens Log.txt, which leaves nothing to look at at all.  Each
	   stage appends here and flushes, so the last line in the file is the last
	   thing that completed -- that is what localises a hard crash with no log.
	   VitaBootMark is defined above main(). */
	VitaBootMark("main() entered, heap=%u MiB", _newlib_heap_size_user / (1024u * 1024u));

	bool bDataRootReady = false;
	if (chdir("ux0:data/farcry") == 0)
	{
		/* The diagnostics directory is created before a retail-data install and
		   can therefore exist with only Log.txt/boot_marker.txt in it.  Treating
		   directory existence as proof of a game install made the engine miss
		   Scripts.pak, lose the whole UI/game script layer and then die during
		   startup.  Validate one mandatory retail pack before committing to the
		   writable data root. */
		FILE *pScripts = fopen("fcdata/Scripts.pak", "rb");
		if (pScripts)
		{
			fclose(pScripts);
			bDataRootReady = true;
			sceClibPrintf("[BOOTTRACE] main: data root ux0:data/farcry\n");
		}
		else
			sceClibPrintf("[BOOTTRACE] main: ux0:data/farcry has no fcdata/Scripts.pak, trying app data\n");
	}
	if (!bDataRootReady)
	{
		if (chdir("ux0:app/FCRY00002") == 0)
			sceClibPrintf("[BOOTTRACE] main: data root ux0:app/FCRY00002\n");
		else
			sceClibPrintf("[BOOTTRACE] main: no usable data root found, using current directory\n");
	}

	// Proven settings used by mature vitaGL FPS ports (vitaRTCW/d3es-vita):
	// run the retail Vita clocks, including the GPU crossbar, before engine
	// initialization and asset loading begin.
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	/* Read the clocks back rather than trusting the setters: a failed
	   overclock is silent, and a Vita quietly running at 333/111 looks exactly
	   like a port that is simply slow. */
	VitaBootMark("clocks: arm=%d bus=%d gpu=%d xbar=%d",
		scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
		scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());

	/* Vita: real network stack init -- CryGame always creates a local
	   CXServer even for single-player (CXGame::StartupServer ->
	   INetwork::CreateServer -> real socket() calls), and sceNetInit() is
	   never called anywhere else in this codebase. Without it, every real
	   Vita socket call fails immediately (this is a genuine platform
	   requirement, not a guess -- confirmed nowhere else in this tree calls
	   it, and StartupServer's real failure path -- IsOK() false on two
	   consecutive ports -- matches exactly what an uninitialized network
	   stack produces). 1MB pool is the standard size used by real vitaGL
	   network homebrew (vitaRTCW etc). */
	{
		static char s_netMemory[1 * 1024 * 1024];
		SceNetInitParam netInitParam;
		netInitParam.memory = s_netMemory;
		netInitParam.size = sizeof(s_netMemory);
		netInitParam.flags = 0;
		/* The net library is not resident by default: on real hardware
		   sceNetInit without SCE_SYSMODULE_NET loaded jumps into an unmapped
		   import stub and takes the process down before anything can be
		   logged.  Vita3K resolves those imports regardless, which is why this
		   only ever showed up on a device.  If the module will not load, skip
		   networking entirely rather than calling into it -- CryNetwork's
		   local server only needs sockets for multiplayer. */
		VitaBootMark("before sceSysmoduleLoadModule(NET)");
		const int nNetModuleRes = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		VitaBootMark("sceSysmoduleLoadModule(NET)=0x%08x", (unsigned)nNetModuleRes);

		int nNetInitRes = -1, nNetCtlInitRes = -1;
		if (nNetModuleRes >= 0)
		{
			VitaBootMark("before sceNetInit");
			nNetInitRes = sceNetInit(&netInitParam);
			nNetCtlInitRes = sceNetCtlInit();
			VitaBootMark("sceNetInit=0x%08x sceNetCtlInit=0x%08x",
				(unsigned)nNetInitRes, (unsigned)nNetCtlInitRes);
		}
		else
			VitaBootMark("net module unavailable, skipping sceNetInit");
		sceClibPrintf("[BOOTTRACE] main: sceNetInit=%d sceNetCtlInit=%d\n", nNetInitRes, nNetCtlInitRes);
	}
#endif
	SSystemInitParams sip;
#if defined(LINUX)
	/* Give the game loop a core of its own.  Nothing in this port had ever set a
	   thread affinity, so every thread inherited the default mask and the
	   scheduler was free to put the engine and the audio mixer on the same core
	   -- which is what "all the load on one core" looks like.  The engine itself
	   is single threaded, so this does not make it faster on its own; what it
	   does is stop the one genuinely concurrent piece of work in the process
	   from being scheduled on top of it.  The mixer takes core 2
	   (engine_port/CrySoundVita.cpp). */
	{
		const int nAffinity = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
		VitaBootMark("main thread pinned to CPU 0 (result 0x%08x)", (unsigned)nAffinity);
	}

	VitaBootMark("before CreateSystemInterface");
#endif
	ISystem *pSystem = CreateSystemInterface(sip);
#if defined(LINUX)
	VitaBootMark("CreateSystemInterface returned %p", (void *)pSystem);
#endif
	if (!pSystem)
		return 1;

#if defined(FARCRY_VITA_FULL_GAME)
	/* Run the actual statically linked CryGame module.  This mirrors the
	   original FARCRY/Main.cpp startup contract; the renderer/menu harness
	   below remains available only when the full-game build is disabled. */
#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] main: system initialized; creating CryGame\n");
#endif
	if (pSystem->GetILog())
		pSystem->GetILog()->EnableVerbosity(true);
	if (pSystem->GetIConsole())
	{
		pSystem->GetIConsole()->ShowConsole(false);
		pSystem->GetIConsole()->SetScrollMax(300);
	}

	SGameInitParams gameParams;
	if (!pSystem->CreateGame(gameParams))
	{
#if defined(LINUX)
		sceClibPrintf("[BOOTTRACE] main: CSystem::CreateGame FAILED\n");
#endif
		return 2;
	}

	/* Far Cry's defaults describe a 2004 desktop GPU: 55x object view-distance
	   ratio, stencil and projected shadows, terrain detail texturing as an
	   extra pass over every sector, grass, 2048 particles.  The SGX543 cannot
	   pay for any of that, and none of it had ever been turned down here --
	   which is most of why the open world collapses to single-digit frame
	   rates.  Applied after CreateGame so every module has registered its
	   variables and these win over the shipped configs.  Set through ICVar
	   rather than the console because most of them are VF_CHEAT. */
	if (IConsole *pConsole = pSystem->GetIConsole())
	{
		struct SVitaCVarTuning { const char *szName; const char *szValue; };
		static const SVitaCVarTuning s_arrVitaTuning[] = {
			/* Draw distance and LOD: the single biggest lever in the open world.
			   Tightened again after measuring real gameplay -- the frame rate held
			   60 standing still but fell to 11-15 moving through the jungle, which
			   is where the object and vegetation counts actually bite.  Character
			   skinning now runs for real as well, so there is less budget left for
			   scenery than when these were first chosen. */
			{ "e_obj_view_dist_ratio",              "6.5"  },
			{ "e_obj_lod_ratio",                    "2.0"  },
			{ "e_terrain_lod_ratio",                "5.0"  },
			/* The Vita billboard path now renders real generated tree sprites.
			   Switch earlier and omit tiny undergrowth so CPU submission is spent on
			   actors and nearby cover rather than hundreds of distant meshes. */
			{ "e_vegetation_sprites_distance_ratio","0.15" },
			{ "e_vegetation_sprites_min_distance",  "4.0"  },
			{ "e_vegetation_sprites_slow_switch",   "0"    },
			{ "e_vegetation_min_size",              "7.0"  },
			{ "e_vegetation_sprites_texres",         "1"    },
			{ "e_vegetation_bending",               "0"    },
			{ "e_objects_fade_on_distance",          "1"    },
			// Whole subsystems the hardware cannot afford.
			{ "e_detail_objects",                   "0"    },
			/* CE1's close terrain detail path regenerates splat geometry and submits
			   many extra draws.  The Vita base cover is retained at 512px instead. */
			{ "e_detail_texture",                   "0"    },
			{ "e_beach",                            "0"    },
			{ "e_shadow_maps",                      "0"    },
			{ "e_shadow_maps_from_static_objects",  "0"    },
			{ "e_stencil_shadows",                  "0"    },
			{ "e_bflyes",                           "0"    },
			// Fill-rate and per-frame CPU work.
			{ "e_particles_max_count",              "16"   },
			{ "e_particles_lod",                    "0.4"  },
			{ "e_max_entity_lights",                "0"    },
			{ "e_decals",                           "0"    },
			{ "e_deformable_terrain",                "0"    },
			{ "es_EnableCloth",                     "0"    },
			{ "es_HitDeadBodies",                   "0"    },
			/* Fight spikes are dominated by collision solving, visibility rays and
			   short-lived effects.  Keep gameplay simulation at the 30 FPS target
			   while bounding worst-case work instead of allowing desktop-scale
			   solver iteration/contact budgets. */
			{ "p_max_substeps",                     "3"    },
			{ "p_max_MC_iters",                     "2000" },
			{ "p_max_contacts",                     "96"   },
			{ "p_max_LCPCG_subiters",               "64"   },
			{ "p_max_LCPCG_subiters_final",         "128"  },
			{ "p_max_LCPCG_microiters",             "4096" },
			{ "p_max_LCPCG_microiters_final",       "8192" },
			{ "p_max_LCPCG_iters",                  "4"    },
			{ "ai_max_vis_rays_per_frame",          "16"   },
			/* The Vita mixer exposes 32 channels, so traversing one hundred active
			   spots and ten connected indoor areas on core 0 can only discard work.
			   Keep headroom for combat dialogue while bounding SoundSystem tail spikes. */
			{ "s_MaxActiveSoundSpots",              "40"   },
			{ "s_VisAreasPropagation",              "6"    },
			{ "g_maxfps",                           "30"   },
			{ "r_lightmaps",                        "1"    },
			/* The compact backend already renders one diffuse stage plus baked
			   lighting.  Prevent the desktop material system from scheduling detail
			   and accurate-particle work that has no faithful Vita stage. */
			{ "r_DetailTextures",                   "0"    },
			{ "r_AccurateParticles",                "0"    },
			/* A bounded tail of the previous run is now safe to preload: 64
			   textures maximum and a 48 MB vitaGL reserve. */
			{ "r_vita_progcache_warm",              "1"    },
			/* Character cost.  Skinning is CPU work per visible character per
			   frame and it is new -- it never ran on this port until the renderer
			   started calling ProcessSkinning.  ca_LodBias multiplies the distance
			   at which characters drop to a cheaper LOD, so raising it moves them
			   down sooner; the higher MinVertexWeight thresholds truncate the
			   weakest bone influences, which is the per-vertex cost itself.  None
			   of this touches the animation, only how finely it is applied. */
			/* The previous 3.2x LOD bias and doubled weight cutoff visibly snapped
			   characters between meshes and removed weakly weighted limbs/clothing.
			   Skinning is only ~0.3 ms in device captures, so preserve animation
			   correctness instead of spending quality for negligible savings. */
			{ "ca_LodBias",                         "0.18" },
			{ "ca_MinVertexWeightLOD0",             "0.08" },
			{ "ca_MinVertexWeightLOD1",             "0.3"  },
			/* This renderer has no normal/tangent lighting stage, no stencil
			   shadows and no character-decal pass.  Do not run their character-side
			   preparation every frame only to discard the result. */
			{ "ca_EnableTangentSkinning",            "0"    },
			{ "ca_EnableCharacterShadowVolume",      "0"    },
			{ "ca_EnableDecals",                     "0"    },
			{ "ca_EnableLightUpdate",                "0"    },
			// Water is a large, always-visible surface on this level.
			{ "e_water_ocean_sun_reflection",       "0"    },
			{ "e_water_ocean_tesselation",          "0"    },
			/* Keep baked lighting loaded.  The lightmap-off experiment made most
			   world vertex colours multiply down to black on the Vita backend. */
			{ "e_light_maps",                       "1"    },
		};
		int nApplied = 0;
		for (unsigned i = 0; i < sizeof(s_arrVitaTuning) / sizeof(s_arrVitaTuning[0]); ++i)
		{
			if (ICVar *pVar = pConsole->GetCVar(s_arrVitaTuning[i].szName))
			{
				pVar->Set(s_arrVitaTuning[i].szValue);
				++nApplied;
			}
		}
		if (pSystem->GetILog())
			pSystem->GetILog()->LogToFile("\001[VITA][PERF] applied %d of %u tuning cvars",
				nApplied, (unsigned)(sizeof(s_arrVitaTuning) / sizeof(s_arrVitaTuning[0])));
	}

	IInput *pInput = pSystem->GetIInput();
	if (pInput)
	{
		pInput->ClearKeyState();
		pInput->SetMouseExclusive(true);
		pInput->SetKeyboardExclusive(true);
	}

	IGame *pGame = pSystem->GetIGame();
#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] main: CryGame initialized pGame=%p; entering stock loop\n", (void *)pGame);
#endif
	if (!pGame)
		return 3;

#if defined(VITA_DEBUG_AUTOLOAD_TRAINING)
	/* Deterministic Vita3K gameplay test: enter the retail single-player
	   path through CryGame's normal queued StartLevel message. This exercises
	   server/client creation, entities, AI, scripts and the 3D engine instead
	   of calling I3DEngine::LoadLevel in isolation. */
	sceClibPrintf("[VITA AUTOTEST] queueing StartLevel Training training\n");
	if (pSystem->GetILog())
		pSystem->GetILog()->Log("[VITA AUTOTEST] queueing StartLevel Training training");
	FILE *autotestMarker = fopen("ux0:data/farcry_autotest_marker.txt", "wb");
	if (autotestMarker)
	{
		fputs("queued StartLevel Training training\n", autotestMarker);
		fclose(autotestMarker);
	}
	pGame->SendMessage("StartLevel Training training");
#endif

#if defined(VITA_DEBUG_TEST_CUBE)
	/* Vita: TEMPORARY verification-only path -- CreateGameInstance/CreateGame
	   above are real (needed to link at all), but CXGame::Run()'s real
	   internal render-gate (bRenderFrame in CryGame/Game.cpp's Update())
	   doesn't go true until the real game's own menu/camera state machine
	   reaches a point this port hasn't gotten the real UI system to yet --
	   confirmed by a real framebuffer capture (CVitaRenderer::ScreenShot)
	   showing genuinely black output, not a guess. Bypassing CXGame::Run()
	   here with a minimal, directly-controlled loop that still calls the
	   exact same real CVitaRenderer::DrawBuffer/SetCamera/matrix-stack path
	   (see DrawTestCube above) to prove that path draws real geometry,
	   independent of CXGame's own unfinished render-gating. Reverted once
	   no longer needed. */
	IRenderer *pTestRenderer = pSystem->GetIRenderer();
	if (pTestRenderer)
		InitTestCube(pTestRenderer);
	for (int nFrame = 0; nFrame < 120; nFrame++) {
		if (pTestRenderer) {
			pTestRenderer->BeginFrame();
			DrawTestCube(pTestRenderer, (float)nFrame);
			if (nFrame == 60)
				pTestRenderer->ScreenShot("ux0:data/screenshot.bmp");
			pTestRenderer->Update();
		}
	}
	sceClibPrintf("[BOOTTRACE] main: VITA_DEBUG_TEST_CUBE loop finished 120 frames\n");
	return 0;
#else
	bool relaunch = false;
	const bool runResult = pGame->Run(relaunch);
#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] main: stock game loop exited result=%d relaunch=%d\n",
		(int)runResult, (int)relaunch);
#endif
	return runResult ? 0 : 4;
#endif
#endif

#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] main: CSystem::Init() returned, entering render loop\n");
	IRenderer *pRenderer = pSystem->GetIRenderer();
	sceClibPrintf("[BOOTTRACE] main: pRenderer=%p\n", (void*)pRenderer);

#if defined(VITA_DEBUG_TEST_CUBE)
	if (pRenderer)
		InitTestCube(pRenderer);
#endif

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
		int nFrameCount = 0;

		for (;;) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons & SCE_CTRL_START)
				break;

			unsigned int nPressed = pad.buttons & ~nPrevButtons;
#if defined(VITA_DEBUG_AUTOLOAD_TRAINING)
			// Vita: TEMPORARY verification-only build -- fires the real SELECT
			// action below on frame 30 instead of waiting for a real button
			// press, since this sandbox can't inject controller input into
			// Vita3K's guest (same limitation noted in git history for
			// VITA_DEBUG_AUTOPLAY). Reverted before committing.
			if (++nFrameCount == 30)
				nPressed |= SCE_CTRL_SELECT;
#endif
			if (pad.buttons != nPrevButtons)
				sceClibPrintf("[BOOTTRACE] main: pad.buttons=0x%08x nPressed=0x%08x\n", pad.buttons, nPressed);
			nPrevButtons = pad.buttons;

			/* Vita: debug-only level load trigger, same "prove the pipeline
			   standalone, bypassing the still-broken Campaign.lua menu path"
			   pattern as VITA_DEBUG_AUTOPLAY. SELECT loads the real, unmodified
			   retail Training level (deployed loose under Levels/Training/ next
			   to fcdata) so CVitaRenderer's new DrawBuffer/CLeafBuffer path (see
			   engine_port/LeafBufferVita.cpp) gets exercised by the real 3D
			   engine instead of only compiling. Not wired to the real "Campaign"
			   menu item -- that's still blocked on Campaign.lua's own,
			   separate, undiagnosed ExecuteFile failure. */
			if (nPressed & SCE_CTRL_SELECT) {
				sceClibPrintf("[BOOTTRACE] main: SELECT pressed, calling I3DEngine::LoadLevel(Levels/Training/, training)\n");
				I3DEngine *p3DEngine = pSystem->GetI3DEngine();
				sceClibPrintf("[BOOTTRACE] main: p3DEngine=%p\n", (void*)p3DEngine);
				if (p3DEngine) {
					bool bLoaded = p3DEngine->LoadLevel("Levels/Training/", "training", false);
					sceClibPrintf("[BOOTTRACE] main: LoadLevel returned %d\n", (int)bLoaded);
				}
			}

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
#if defined(VITA_DEBUG_TEST_CUBE)
			// Vita: drawn LAST -- the menu background above is an opaque
			// fullscreen quad; drawing the cube before it just meant it got
			// immediately painted over every frame (real bug, first attempt
			// rendered nothing visible for exactly this reason).
			{
				static float fAngle = 0.0f;
				fAngle += 1.0f;
				DrawTestCube(pRenderer, fAngle);
			}
#endif
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
