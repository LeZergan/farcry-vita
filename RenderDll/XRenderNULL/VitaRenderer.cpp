//////////////////////////////////////////////////////////////////////
// Vita: see VitaRenderer.h for why this exists instead of a real
// CRenderer-derived backend.
//////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "VitaRenderer.h"
#include <CryHeaders.h>

#if defined(LINUX)
#include <vitaGL.h>
#include "VitaFixedFunction.h"
#include <psp2/kernel/processmgr.h>
#include <CRESky.h>
#include <CREDummy.h>
#include <CRE2DQuad.h>
#include <CREScreenProcess.h>
#include <CRETerrainSector.h>
#include <CREOcLeaf.h>
/* For IDeformableRenderMesh::ProcessSkinning -- IShader.h only forward-declares
   the interface, and the render pipeline is what drives character skinning. */
#include <ICryAnimation.h>
/* Far vegetation is handed to IRenderer as CStatObjInst pointers.  The
   desktop OpenGL backend includes these concrete engine types in its sprite
   unit too (GLObjSprites.cpp); without them the compact Vita backend cannot
   recover the authored LOD size or the 24 generated view textures. */
#include "../../Cry3DEngine/stdafx.h"
#include "../../Cry3DEngine/StatObj.h"
#include "../../Cry3DEngine/ObjMan.h"
#endif

#if defined(LINUX)
/* One integer increment per submitted draw is cheap enough for production and
   gives the adaptive-quality controller the metric that correlates most
   strongly with real-device render time.  The detailed counters remain behind
   VITA_PERF_TELEMETRY. */
static unsigned int g_nVitaDrawCalls = 0;
static unsigned int g_nVitaPreviousDrawCalls = 0;
extern "C" unsigned int Vita_GetLastFrameDrawCallCount()
{
	return g_nVitaPreviousDrawCalls;
}
#define VITA_DRAW_INCREMENT() (++g_nVitaDrawCalls)
#if defined(VITA_PERF_TELEMETRY)
static unsigned int g_nVitaDrawIndices = 0;
static unsigned int g_nVitaVBODraws = 0;
static unsigned int g_nVitaMappedDraws = 0;
static unsigned int g_nVitaClientDraws = 0;
static unsigned int g_nVitaDynamicDraws = 0;
static unsigned int g_nVitaDynamicUploadCount = 0;
static unsigned int g_nVitaDynamicUploadBytes = 0;
static SceUInt64 g_nVitaDynamicUploadUs = 0;
static unsigned int g_nVitaClientArrayTransitions = 0;
static unsigned int g_nVitaPointerConfigurations = 0;
#define VITA_PERF_INCREMENT(counter) (++(counter))
#define VITA_PERF_ADD(counter, value) ((counter) += (value))
#else
#define VITA_PERF_INCREMENT(counter) ((void)0)
#define VITA_PERF_ADD(counter, value) ((void)0)
#endif
struct SVitaDeferredMappedFree
{
	void *pMemory;
	unsigned int nRetireFrame;
	SVitaDeferredMappedFree(void *p, unsigned int nFrame) : pMemory(p), nRetireFrame(nFrame) {}
};
static std::vector<SVitaDeferredMappedFree> g_vVitaDeferredMappedFrees;
static unsigned int g_nVitaRendererFrameSerial = 0;

static void VitaDeferMappedFree(void *pMemory)
{
	if (pMemory)
		g_vVitaDeferredMappedFrees.push_back(
			SVitaDeferredMappedFree(pMemory, g_nVitaRendererFrameSerial + 4));
}

static void VitaDrainDeferredMappedFrees()
{
	for (size_t i = 0; i < g_vVitaDeferredMappedFrees.size(); )
	{
		if ((int)(g_nVitaRendererFrameSerial - g_vVitaDeferredMappedFrees[i].nRetireFrame) >= 0)
		{
			vglFree(g_vVitaDeferredMappedFrees[i].pMemory);
			g_vVitaDeferredMappedFrees[i] = g_vVitaDeferredMappedFrees.back();
			g_vVitaDeferredMappedFrees.pop_back();
		}
		else
			++i;
	}
}
#endif

/* Last value handed to SetState, so an identical one can skip the GL calls
   entirely.  -1 is "unknown": no real GS_* combination is negative, so it can
   never match and always forces a full re-issue. */
static int g_nCachedRenderState = -1;
static inline void VitaInvalidateRenderStateCache() { g_nCachedRenderState = -1; }

#if defined(LINUX)
/* Upstream vitaGL marks both fixed-function vertex and fragment state dirty on
   every glEnableClientState/glDisableClientState call, even when the requested
   bit already has that value.  This renderer used to issue six of those calls
   around every draw.  At the post-pickup workload that is well over a thousand
   needless dirty events per frame, followed by fixed-function state rebuilding
   in the driver.

   Keep unit-0 array enables live between draws and only send real transitions.
   Pointer calls still select the current buffer/data, so leaving an enabled
   array alone is standard OpenGL behaviour and does not retain old geometry. */
static int g_nVitaVertexArrayEnabled = -1;
static int g_nVitaColorArrayEnabled = -1;
static int g_nVitaTexCoord0ArrayEnabled = -1;
static bool LightMapsEnabled();
/* Draw2dImage is called many times while the HUD/radar has already bracketed
   an orthographic pass with Set2DMode.  Track that bracket so individual
   images do not query GL state and push/pop both matrices again. */
static int g_nVita2DModeDepth = 0;
/* How the virtual 800x600 2D canvas maps onto the projection the active
   Set2DMode bracket installed.  1:1 for the 800x600 brackets the HUD opens,
   and the real framebuffer ratio for the one CUISystem::Draw opens. */
static float g_fVita2DModeScaleX = 1.0f;
static float g_fVita2DModeScaleY = 1.0f;
static void VitaFlushProgCacheRecords();
static void VitaDisableLightMapStage();
static GLuint g_nVitaPointerArrayBuffer = 0xFFFFFFFFu;
static const byte *g_pVitaPointerBase = (const byte *)(size_t)~0u;
static int g_nVitaPointerFormat = -1;
static int g_nVitaPointerUsesColor = -1;
static int g_nVitaPointerUsesTexCoord = -1;
static const CVertexBuffer *g_pVitaMappedPointerBuffer = NULL;
static int g_nVitaMappedPointerUsesColor = -1;
static int g_nVitaMappedPointerUsesTexCoord = -1;
static float g_arrVitaConstantColor[4] = {-1000.0f, -1000.0f, -1000.0f, -1000.0f};
static GLuint g_nVitaBoundArrayBuffer = 0xFFFFFFFFu;
static GLuint g_nVitaBoundElementBuffer = 0xFFFFFFFFu;

static inline void VitaBindArrayBuffer(GLuint nBuffer)
{
	if (g_nVitaBoundArrayBuffer == nBuffer)
		return;
	glBindBuffer(GL_ARRAY_BUFFER, nBuffer);
	g_nVitaBoundArrayBuffer = nBuffer;
}

static inline void VitaBindElementBuffer(GLuint nBuffer)
{
	if (g_nVitaBoundElementBuffer == nBuffer)
		return;
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nBuffer);
	g_nVitaBoundElementBuffer = nBuffer;
}

static inline void VitaInvalidateVertexPointerCache()
{
	g_nVitaPointerArrayBuffer = 0xFFFFFFFFu;
	g_pVitaPointerBase = (const byte *)(size_t)~0u;
	g_nVitaPointerFormat = -1;
	g_pVitaMappedPointerBuffer = NULL;
	g_nVitaMappedPointerUsesColor = -1;
	g_nVitaMappedPointerUsesTexCoord = -1;
}

static inline void VitaSetClientArrayState(GLenum array, bool bEnable)
{
	int *pCached = NULL;
	switch (array)
	{
		case GL_VERTEX_ARRAY:        pCached = &g_nVitaVertexArrayEnabled; break;
		case GL_COLOR_ARRAY:         pCached = &g_nVitaColorArrayEnabled; break;
		case GL_TEXTURE_COORD_ARRAY: pCached = &g_nVitaTexCoord0ArrayEnabled; break;
		default: break;
	}
	const int nWanted = bEnable ? 1 : 0;
	if (pCached && *pCached == nWanted)
		return;
	if (bEnable)
		glEnableClientState(array);
	else
		glDisableClientState(array);
	VITA_PERF_INCREMENT(g_nVitaClientArrayTransitions);
	if (pCached)
		*pCached = nWanted;
}

static inline void VitaSetConstantColor(float r, float g, float b, float a)
{
	if (g_arrVitaConstantColor[0] == r && g_arrVitaConstantColor[1] == g &&
		g_arrVitaConstantColor[2] == b && g_arrVitaConstantColor[3] == a)
		return;
	glColor4f(r, g, b, a);
	g_arrVitaConstantColor[0] = r;
	g_arrVitaConstantColor[1] = g;
	g_arrVitaConstantColor[2] = b;
	g_arrVitaConstantColor[3] = a;
}

static float g_arrVitaMaterialColor[4] = {1, 1, 1, 1};
static bool g_bVitaCustomColorOp = false;

// Immediate world submissions own their material state. Engine effect draws
// can then select a combiner without SetTexture silently replacing it.
void VitaResetFixedFunctionMaterial()
{
	g_bVitaCustomColorOp = false;
	for (int i = 0; i < 4; ++i)
		g_arrVitaMaterialColor[i] = 1.0f;
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/* Secondary fixed-function textures (baked lighting and terrain detail) alter
   surface colour only.  Their alpha channels contain authored/packed data and
   must never turn the underlying material translucent.  Call with the target
   texture unit active. */
static inline void VitaSetRGBModulatePreserveAlpha(float fRGBScale = 1.0f)
{
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, fRGBScale);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
}
#endif

CVitaRenderer *gcpVitaRenderer = NULL;

#if defined(LINUX)
static void VitaWarmFixedFunctionProgramCache();
#endif

/* Vita: this class is fully self-contained (doesn't link NULL_System.cpp,
   which would otherwise also define these), so it owns the definitions
   IRenderer's global accessors (iLog, GetISystem(), etc.) rely on. */
ILog     *iLog;
IConsole *iConsole;
ITimer   *iTimer;
ISystem  *iSystem;
int *pTest_int;
IPhysicalWorld *pIPhysicalWorld;

/* CRenderer normally owns and registers the renderer console variables from
   RenderDll/Common/Renderer.cpp.  The Vita backend intentionally does not
   derive from that desktop renderer (doing so would pull the Cg/D3D pipeline
   into the executable), but Cry3DEngine still queries the screen-effect
   variables directly.  Keep the original console contract with stable
   backing storage.  Expensive desktop post effects default off on Vita. */
static int s_vitaResetScreenFx = 0;
static int s_vitaRenderMode = 0;
static int s_vitaGlare = 0;
static int s_vitaGlareQuality = 0;
static int s_vitaMotionBlur = 0;
static float s_vitaMotionBlurAmount = 0.0f;
static int s_vitaMotionBlurDisplace = 0;
static int s_vitaDisableSfx = 1;
static int s_vitaScreenColorTransfer = 0;
/* CRenderer normally registers these too.  Gameplay weapon/HUD scripts read
   r_TexResolution directly during OnInit; leaving it undefined aborted the
   player's entire weapon initialization in the retail scripts.  The wider
   set keeps the unmodified video-options scripts valid and selects sensible
   Vita-cost defaults for effects this compact backend does not implement. */
static int s_vitaTexResolution = 2;
/* Largest mip actually uploaded, per axis.  Lower is less GPU memory and less
   sampling bandwidth; 0 uploads the authored top level. */
static int s_vitaMaxTextureSize = 512;
/* Upload a full mip chain per texture.  Off by default: see the texture upload
   loop -- the per-level growth is the path vitaGL crashes on. */
static int s_vitaTexMips = 0;
/* Pre-load a small, memory-bounded tail of the previous run. */
static int s_vitaProgCacheWarm = 0;
static int s_vitaTexBumpResolution = 2;
static int s_vitaTexSkyResolution = 2;
static int s_vitaTexAnisotropy = 1;
static int s_vitaScopeLensFx = 0;
static int s_vitaFsaa = 0;
static int s_vitaFsaaSamples = 0;
static int s_vitaFsaaQuality = 0;
static int s_vitaVsync = 1;
/* Native panel dimensions.  The production build currently renders at 75%
	 (720x408) and lets the Vita display path scale to the 960x544 panel.  Real
	 hardware timing showed native resolution missing a hard 33.3 ms budget in
	 outdoor gameplay; 720x408 is a common Vita-class internal resolution and
	 removes 44% of the fragment/depth traffic while retaining a readable HUD. */
#define VITA_RENDER_WIDTH  960
#define VITA_RENDER_HEIGHT 544

static int s_vitaWidth = VITA_RENDER_WIDTH;
static int s_vitaHeight = VITA_RENDER_HEIGHT;
static int s_vitaColorBits = 32;
static int s_vitaFullscreen = 1;
static int s_vitaBeams = 0;
static int s_vitaCheckSunVis = 0;
static int s_vitaCoronas = 0;
static int s_vitaFlares = 0;
static int s_vitaProcFlares = 0;
static int s_vitaCryvisionType = 0;
static int s_vitaDetailTextures = 1;
static int s_vitaDetailNumLayers = 1;
static int s_vitaEnvCMResolution = 0;
static int s_vitaEnvTexResolution = 0;
static int s_vitaEnvLightCMSize = 4;
static int s_vitaHDRLevel = 0;
static int s_vitaHDRRendering = 0;
static int s_vitaHeatHaze = 0;
static int s_vitaQualityBump = 0;
static int s_vitaQualityReflection = 0;
static int s_vitaSelfShadow = 0;
static int s_vitaShadowBlur = 0;
static int s_vitaVegetationPerPixel = 0;
static int s_vitaVolumetricFog = 0;
static int s_vitaWaterReflections = 0;
static int s_vitaWaterRefractions = 0;
static float s_vitaBrightness = 0.5f;
static float s_vitaContrast = 0.5f;
static float s_vitaGamma = 1.0f;
static float s_vitaCoronaFade = 0.5f;
static float s_vitaDetailDistance = 4.0f;
static float s_vitaEnvCMUpdate = 1.0f;
static float s_vitaEnvLCMUpdate = 1.0f;
static float s_vitaEnvTexUpdate = 1.0f;
static float s_vitaWaterUpdateFactor = 0.05f;
static bool s_vitaFogEnabled = false;
static float s_vitaFogDensity = 0.0f;
static float s_vitaFogStart = 0.0f;
static float s_vitaFogEnd = 1000.0f;
static int s_vitaFogMode = R_FOGMODE_LINEAR;
static CFColor s_vitaFogColor(0.5f, 0.5f, 0.5f, 1.0f);

static void RegisterVitaRendererCVars()
{
	if (!iConsole)
		return;

#define VITA_REGISTER_INT(name, storage, value) \
	do { if (!iConsole->GetCVar(name)) iConsole->Register(name, &(storage), value, 0); } while (0)
#define VITA_REGISTER_FLOAT(name, storage, value) \
	do { if (!iConsole->GetCVar(name)) iConsole->Register(name, &(storage), value, 0); } while (0)
	VITA_REGISTER_INT("r_ResetScreenFx", s_vitaResetScreenFx, 0);
	VITA_REGISTER_INT("r_RenderMode", s_vitaRenderMode, 0);
	VITA_REGISTER_INT("r_Glare", s_vitaGlare, 0);
	VITA_REGISTER_INT("r_GlareQuality", s_vitaGlareQuality, 0);
	VITA_REGISTER_INT("r_MotionBlur", s_vitaMotionBlur, 0);
	VITA_REGISTER_FLOAT("r_MotionBlurAmount", s_vitaMotionBlurAmount, 0.0f);
	VITA_REGISTER_INT("r_MotionBlurDisplace", s_vitaMotionBlurDisplace, 0);
	VITA_REGISTER_INT("r_DisableSfx", s_vitaDisableSfx, 1);
	VITA_REGISTER_INT("r_ScreenColorTransfer", s_vitaScreenColorTransfer, 0);
	VITA_REGISTER_INT("r_TexResolution", s_vitaTexResolution, 2);
	VITA_REGISTER_INT("r_vita_max_texture_size", s_vitaMaxTextureSize, 512);
	VITA_REGISTER_INT("r_vita_tex_mips", s_vitaTexMips, 0);
	VITA_REGISTER_INT("r_vita_progcache_warm", s_vitaProgCacheWarm, 0);
	VITA_REGISTER_INT("r_TexBumpResolution", s_vitaTexBumpResolution, 2);
	VITA_REGISTER_INT("r_TexSkyResolution", s_vitaTexSkyResolution, 2);
	VITA_REGISTER_INT("r_Texture_Anisotropic_Level", s_vitaTexAnisotropy, 1);
	VITA_REGISTER_INT("r_ScopeLens_fx", s_vitaScopeLensFx, 0);
	VITA_REGISTER_INT("r_FSAA", s_vitaFsaa, 0);
	VITA_REGISTER_INT("r_FSAA_samples", s_vitaFsaaSamples, 0);
	VITA_REGISTER_INT("r_FSAA_quality", s_vitaFsaaQuality, 0);
	VITA_REGISTER_INT("r_VSync", s_vitaVsync, 1);
	VITA_REGISTER_INT("r_Width", s_vitaWidth, VITA_RENDER_WIDTH);
	VITA_REGISTER_INT("r_Height", s_vitaHeight, VITA_RENDER_HEIGHT);
	VITA_REGISTER_INT("r_ColorBits", s_vitaColorBits, 32);
	VITA_REGISTER_INT("r_Fullscreen", s_vitaFullscreen, 1);
	VITA_REGISTER_INT("r_Beams", s_vitaBeams, 0);
	VITA_REGISTER_INT("r_checkSunVis", s_vitaCheckSunVis, 0);
	VITA_REGISTER_INT("r_Coronas", s_vitaCoronas, 0);
	VITA_REGISTER_INT("r_Flares", s_vitaFlares, 0);
	VITA_REGISTER_INT("r_ProcFlares", s_vitaProcFlares, 0);
	VITA_REGISTER_INT("r_CryvisionType", s_vitaCryvisionType, 0);
	VITA_REGISTER_INT("r_DetailTextures", s_vitaDetailTextures, 1);
	VITA_REGISTER_INT("r_DetailNumLayers", s_vitaDetailNumLayers, 1);
	VITA_REGISTER_INT("r_EnvCMResolution", s_vitaEnvCMResolution, 0);
	VITA_REGISTER_INT("r_EnvTexResolution", s_vitaEnvTexResolution, 0);
	VITA_REGISTER_INT("r_EnvLightCMSize", s_vitaEnvLightCMSize, 4);
	VITA_REGISTER_INT("r_HDRLevel", s_vitaHDRLevel, 0);
	VITA_REGISTER_INT("r_HDRRendering", s_vitaHDRRendering, 0);
	VITA_REGISTER_INT("r_HeatHaze", s_vitaHeatHaze, 0);
	VITA_REGISTER_INT("r_Quality_BumpMapping", s_vitaQualityBump, 0);
	VITA_REGISTER_INT("r_Quality_Reflection", s_vitaQualityReflection, 0);
	VITA_REGISTER_INT("r_SelfShadow", s_vitaSelfShadow, 0);
	VITA_REGISTER_INT("r_ShadowBlur", s_vitaShadowBlur, 0);
	VITA_REGISTER_INT("r_Vegetation_PerpixelLight", s_vitaVegetationPerPixel, 0);
	VITA_REGISTER_INT("r_VolumetricFog", s_vitaVolumetricFog, 0);
	VITA_REGISTER_INT("r_WaterReflections", s_vitaWaterReflections, 0);
	VITA_REGISTER_INT("r_WaterRefractions", s_vitaWaterRefractions, 0);
	VITA_REGISTER_FLOAT("r_Brightness", s_vitaBrightness, 0.5f);
	VITA_REGISTER_FLOAT("r_Contrast", s_vitaContrast, 0.5f);
	VITA_REGISTER_FLOAT("r_Gamma", s_vitaGamma, 1.0f);
	VITA_REGISTER_FLOAT("r_CoronaFade", s_vitaCoronaFade, 0.5f);
	VITA_REGISTER_FLOAT("r_DetailDistance", s_vitaDetailDistance, 4.0f);
	VITA_REGISTER_FLOAT("r_EnvCMupdateInterval", s_vitaEnvCMUpdate, 1.0f);
	VITA_REGISTER_FLOAT("r_EnvLCMupdateInterval", s_vitaEnvLCMUpdate, 1.0f);
	VITA_REGISTER_FLOAT("r_EnvTexUpdateInterval", s_vitaEnvTexUpdate, 1.0f);
	VITA_REGISTER_FLOAT("r_WaterUpdateFactor", s_vitaWaterUpdateFactor, 0.05f);
#undef VITA_REGISTER_FLOAT
#undef VITA_REGISTER_INT
}

/* These definitions and initialisation rules are the CCObject contract from
   Crytek's RenderDll/Common/Renderer.cpp.  The former Vita stub returned NULL
   from EF_GetObject, so the first bending vegetation object wrote through
   address zero and dispatched a null AddWaves vtable entry. */
TArray<SWaveForm2> CCObject::m_Waves;
MatrixArray16 CCObject::m_ObjMatrices;
/* RenderDll/Common/RendElements/RendElement.cpp is part of the deferred
   desktop renderer and is intentionally not linked by the compact Vita
   backend.  It normally owns this render-pass depth counter. */
int SRendItem::m_RecurseLevel = 0;

void CCObject::Init()
{
	m_ObjFlags = 0;
	if (m_ShaderParams && m_bShaderParamCreatedInRenderer)
	{
		m_bShaderParamCreatedInRenderer = false;
		delete m_ShaderParams;
	}
	m_ShaderParams = NULL;
	m_nLMId = m_nLMDirId = m_nOcclId = m_nHDRLMId = 0;
	m_InvMatrixId = -1;
	m_VPMatrixId = -1;
	m_RE = NULL;
	m_EF = NULL;
	m_CustomData = NULL;
	m_DynLMMask = 0;
	m_fDistanceToCam = -1.0f;
	m_RenderState = 0;
	m_fHeatFactor = 1.0f;
	m_NumCM = -1;
	m_SortId = 0;
	m_NumWFX = 0;
	m_NumWFY = 0;
	m_fLightFadeTime = 0.0f;
	m_pShadowCasters = NULL;
	m_bVisible = false;
	m_AmbColor = Vec3d(1.0f, 1.0f, 1.0f);
	m_Color = CFColor(1.0f);
	m_pCharInstance = NULL;
	m_pLightImage = NULL;
	m_pLMTCBufferO = NULL;
	m_nScissorX1 = m_nScissorX2 = m_nScissorY1 = m_nScissorY2 = 0;
}

CCObject::~CCObject()
{
	if (m_ShaderParams && m_bShaderParamCreatedInRenderer)
		delete m_ShaderParams;
}

void CCObject::SetShaderFloat(const char *Name, float Val)
{
	string name = Name ? Name : "";
	std::transform(name.begin(), name.end(), name.begin(), tolower);
	if (!m_ShaderParams)
		m_ShaderParams = new TArray<SShaderParam>;
	int i;
	for (i = 0; i < m_ShaderParams->Num(); ++i)
		if (!strcmp(name.c_str(), m_ShaderParams->Get(i).m_Name))
			break;
	if (i == m_ShaderParams->Num())
	{
		SShaderParam pr;
		memset(&pr, 0, sizeof(pr));
		strncpy(pr.m_Name, name.c_str(), sizeof(pr.m_Name) - 1);
		m_ShaderParams->AddElem(pr);
	}
	SShaderParam *pr = &m_ShaderParams->Get(i);
	pr->m_Type = eType_FLOAT;
	pr->m_Value.m_Float = Val;
	m_bShaderParamCreatedInRenderer = true;
}

void CCObject::AddWaves(SWaveForm2 **pWF)
{
	// The desktop pipeline preallocates 32 shared wave slots; retain that
	// reserved prefix because zero is the object's "no wave" sentinel.
	if (!m_Waves.Num())
		m_Waves.Create(32);

	int n1 = m_NumWFX;
	int n2 = m_NumWFY;
	if (!n1)
	{
		n1 = m_Waves.Num();
		m_Waves.AddIndex(1);
		m_NumWFX = n1;
		memset(&m_Waves[n1], 0, sizeof(SWaveForm2));
		m_Waves[n1].m_eWFType = eWF_Sin;
	}
	if (!n2)
	{
		n2 = m_Waves.Num();
		m_Waves.AddIndex(1);
		m_NumWFY = n2;
		memset(&m_Waves[n2], 0, sizeof(SWaveForm2));
		m_Waves[n2].m_eWFType = eWF_Sin;
	}
	if (pWF)
	{
		pWF[0] = &m_Waves[n1];
		pWF[1] = &m_Waves[n2];
	}
}

void CCObject::RemovePermanent()
{
	m_ObjFlags |= FOB_REMOVED;
}

static std::string LookupVitaSkyBoxBase(const char *shaderName)
{
	static bool parsed = false;
	static bool reportedWaiting = false;
	static std::map<std::string, std::string> skyBoxes;
	if (!parsed)
	{
		ICryPak *pak = iSystem ? iSystem->GetIPak() : NULL;
		FILE *file = pak ? pak->FOpen("Shaders/Scripts/CryShaders/Sky.csl", "rb", ICryPak::FOPEN_HINT_QUIET) : NULL;
		if (!file && pak)
			file = pak->FOpen("Shaders\\Scripts\\CryShaders\\Sky.csl", "rb", ICryPak::FOPEN_HINT_QUIET);
		if (!file && pak)
			file = pak->FOpen("Scripts/CryShaders/Sky.csl", "rb", ICryPak::FOPEN_HINT_QUIET);
		if (!file && pak)
			file = pak->FOpen("Scripts\\CryShaders\\Sky.csl", "rb", ICryPak::FOPEN_HINT_QUIET);
		if (file)
		{
			/* Do not poison the cache when early bootstrap shaders are made
			   before Shaders.pak is mounted.  Only a real opened source file
			   makes this a completed parse. */
			parsed = true;
			pak->FSeek(file, 0, SEEK_END);
			long length = pak->FTell(file);
			pak->FSeek(file, 0, SEEK_SET);
			if (length > 0 && length < 1024 * 1024)
			{
				std::vector<char> bytes((size_t)length + 1, 0);
				if (pak->FRead(&bytes[0], 1, (size_t)length, file) == (size_t)length)
				{
					std::string text(&bytes[0], (size_t)length);
					size_t shaderPos = 0;
					while ((shaderPos = text.find("Shader", shaderPos)) != std::string::npos)
					{
						size_t quote0 = text.find('\'', shaderPos + 6);
						size_t quote1 = quote0 == std::string::npos ? std::string::npos : text.find('\'', quote0 + 1);
						if (quote0 == std::string::npos || quote1 == std::string::npos)
							break;
						size_t nextShader = text.find("Shader", quote1 + 1);
						size_t skyPos = text.find("SkyBox", quote1 + 1);
						if (skyPos != std::string::npos && (nextShader == std::string::npos || skyPos < nextShader))
						{
							size_t equals = text.find('=', skyPos + 6);
							if (equals != std::string::npos && (nextShader == std::string::npos || equals < nextShader))
							{
								size_t value0 = text.find_first_not_of(" \t\r\n", equals + 1);
								size_t value1 = value0 == std::string::npos ? value0 : text.find_first_of(" \t\r\n)", value0);
								if (value0 != std::string::npos)
								{
									std::string key = text.substr(quote0 + 1, quote1 - quote0 - 1);
									std::string value = text.substr(value0, value1 == std::string::npos ? std::string::npos : value1 - value0);
									for (size_t i = 0; i < key.size(); ++i) key[i] = (char)tolower((unsigned char)key[i]);
									skyBoxes[key] = value;
								}
							}
						}
						shaderPos = quote1 + 1;
					}
				}
			}
			pak->FClose(file);
			if (iLog)
				iLog->LogToFile("\001[VITA][SKY] parsed Sky.csl: %u skyboxes", (unsigned int)skyBoxes.size());
		}
		else if (!reportedWaiting && iLog)
		{
			reportedWaiting = true;
			iLog->LogToFile("\001[VITA][SKY] Sky.csl is not mounted yet; lookup will retry");
		}
	}

	std::string key = shaderName ? shaderName : "";
	for (size_t i = 0; i < key.size(); ++i) key[i] = (char)tolower((unsigned char)key[i]);
	std::map<std::string, std::string>::const_iterator found = skyBoxes.find(key);
	return found == skyBoxes.end() ? std::string() : found->second;
}

/* CryEngine's material manager requires a valid IShader even when the
   platform backend uses one compact fixed-function/fallback program.  Keep
   the engine-side metadata and reference contract intact; the Vita draw path
   can then choose texture/blend state from SRenderShaderResources without
   compiling the desktop Cg shader system. */
class CVitaShader : public IShader
{
public:
	CVitaShader(int id, const char *name, uint64 generationMask)
		: m_id(id), m_refs(1), m_name(name ? name : ""), m_sort(eS_Opaque),
		  m_flags(0), m_flags2(EF2_OPAQUE), m_flags3(0), m_renderFlags(0),
		  m_generationMask(generationMask)
	{
		m_skyTextureIds[0] = m_skyTextureIds[1] = m_skyTextureIds[2] = 0;
		if (stricmp(m_name.c_str(), "NoDraw") == 0)
			m_flags3 |= EF3_NODRAW;
	}

	virtual int GetID() { return m_id; }
	virtual void AddRef() { ++m_refs; }
	virtual void Release(bool bForce=false) { if (!bForce && m_refs > 1) --m_refs; }
	virtual int GetRefCount() { return m_refs; }
	virtual const char *GetName() { return m_name.c_str(); }
	virtual EF_Sort GetSort() { return m_sort; }
	virtual int GetFlags() { return m_flags; }
	virtual int GetFlags2() { return m_flags2; }
	virtual int GetFlags3() { return m_flags3; }
	virtual int GetRenderFlags() { return m_renderFlags; }
	virtual void SetRenderFlags(int flags) { m_renderFlags = flags; }
	virtual int GetLFlags() { return 0; }
	virtual int GetCull() { return -1; }
	virtual uint GetPreprocessFlags() { return 0; }
	virtual void SetFlags3(int flags) { m_flags3 = flags; }
	virtual bool Reload(int) { return true; }
	virtual TArray<CRendElement *> *GetREs() { return &m_renderElements; }
	virtual bool AddTemplate(SRenderShaderResources *, int& templateId, const char * = NULL, bool = false, uint64 = 0)
	{
		templateId = -1;
		return true;
	}
	virtual void RemoveTemplate(int) {}
	virtual IShader *GetTemplate(int) { return this; }
	virtual SEfTemplates *GetTemplates() { return NULL; }
	virtual TArray<SShaderParam>& GetPublicParams() { return m_publicParams; }
	virtual int GetTexId() { return 0; }
	virtual ITexPic *GetBaseTexture(int *, int *) { return NULL; }
	virtual unsigned int GetUsedTextureTypes() { return 0; }
	virtual int GetVertexFormat() { return VERTEX_FORMAT_P3F_COL4UB_TEX2F; }
	virtual int Size(int) { return sizeof(*this) + m_name.size(); }
	virtual uint64 GetGenerationMask() { return m_generationMask; }
	virtual SShaderGen *GetGenerationParams() { return NULL; }
	const std::string &GetSkyBoxBase()
	{
		/* Shaders are constructed during renderer bootstrap, before Shaders.pak
		   is guaranteed to be mounted. Resolve lazily so retail mission skies
		   are not permanently cached as empty. */
		if (m_skyBoxBase.empty())
			m_skyBoxBase = LookupVitaSkyBoxBase(m_name.c_str());
		return m_skyBoxBase;
	}
	int GetSkyTextureId(int index) const { return index >= 0 && index < 3 ? m_skyTextureIds[index] : 0; }
	void SetSkyTextureId(int index, int id) { if (index >= 0 && index < 3) m_skyTextureIds[index] = id; }

private:
	int m_id;
	int m_refs;
	std::string m_name;
	EF_Sort m_sort;
	int m_flags;
	int m_flags2;
	int m_flags3;
	int m_renderFlags;
	uint64 m_generationMask;
	std::string m_skyBoxBase;
	int m_skyTextureIds[3];
	TArray<CRendElement *> m_renderElements;
	TArray<SShaderParam> m_publicParams;
};

/* Vita: deliberately NOT defining GetISystem() here -- CrySystem/System.cpp
   owns the one real definition (see its comment); a second one here would
   silently win under -Wl,--allow-multiple-definition and reintroduce the
   exact bug fixed earlier this session (see git log). */

CVitaRenderer::CVitaRenderer()
	: m_nWidth(0), m_nHeight(0), m_nColorBpp(32), m_nDepthBpp(24), m_nStencilBpp(8), m_cType(0),
	  m_nViewportX(0), m_nViewportY(0), m_nViewportWidth(0), m_nViewportHeight(0),
	  // Black, as the retail backends clear to. The dark blue this used to be
	  // was a bring-up aid, and it is what shows through in the gap between the
	  // loading screen being torn down and the first rendered game frame.
	  m_nFrameId(0), m_vClearColor(0.0f, 0.0f, 0.0f), m_nDynVBCursor(0),
	  m_nActiveLights(0), m_bSwapBuffersEnabled(true),
	  m_nCullMode(-1), m_nNextShaderId(1), m_nTempRenderObjectCursor(0)
{
	sceClibPrintf("[BOOTTRACE] CVitaRenderer ctor entered\n");
	gcpVitaRenderer = this;
	sceClibPrintf("[BOOTTRACE] CVitaRenderer ctor done\n");
}

CVitaRenderer::~CVitaRenderer()
{
	ShutDown(false);
	for (size_t i = 0; i < m_TempRenderObjects.size(); ++i)
		delete m_TempRenderObjects[i];
	for (size_t i = 0; i < m_PermanentRenderObjects.size(); ++i)
		delete m_PermanentRenderObjects[i];
	for (std::map<std::string, CVitaShader *>::iterator it = m_ShaderByName.begin(); it != m_ShaderByName.end(); ++it)
		delete it->second;
	m_ShaderByName.clear();
}

WIN_HWND CVitaRenderer::Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit)
{
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init entered, width=%d height=%d cbpp=%u\n", width, height, cbpp);
	RegisterVitaRendererCVars();
	m_nColorBpp = cbpp;
	m_nDepthBpp = zbpp;
	m_nStencilBpp = sbits;
#if defined(LINUX)
	/* FARCRY_VITA_RENDER_SCALE (percent of the native 960x544) selects the
	   internal render target; the display path scales it to the panel. */
	width = VITA_RENDER_WIDTH;
	height = VITA_RENDER_HEIGHT;
#if defined(FARCRY_VITA_RENDER_SCALE_PERCENT)
	{
		/* This size is handed to vglInitWithCustomThreshold, which is the real
		   display resolution, not an internal target the panel rescales for
		   free.  The Vita's display controller only accepts 960x544, 640x368 and
		   480x272, so an arbitrary percentage -- 75 gave 720x408 -- is refused
		   and vitaGL silently falls back to a mode that no longer fills the
		   screen.  Snap to the nearest real mode instead of computing one. */
		const int nScale = FARCRY_VITA_RENDER_SCALE_PERCENT;
		if (nScale > 0 && nScale < 59)
		{
			width = 480; height = 272;
		}
		else if (nScale < 84)
		{
			width = 640; height = 368;
		}
		else
		{
			width = 960; height = 544;
		}
	}
#endif
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: rendering at %dx%d\n", width, height);
	s_vitaWidth = width;
	s_vitaHeight = height;
#endif
	m_nWidth = width;
	m_nHeight = height;
	m_nViewportX = m_nViewportY = 0;
	m_nViewportWidth = width;
	m_nViewportHeight = height;
#if defined(LINUX)
	/* Keep a real CryEngine-sized main-RAM reserve.  vitaGL's simple vglInit
	   consumes virtually every free pool except 16 MiB and enables 4x MSAA;
	   that is unsuitable for Far Cry's post-context level allocations on
	   512 MiB/128 MiB Vita hardware.  The threshold API is the same upstream
	   path used by d3es-vita, with no MSAA and CDRAM kept available for BCn
	   textures. */
	/* Registered here rather than lazily on first use so System.Cfg /
	   SystemCfgOverride.Cfg can set it -- console variables created after the
	   config is parsed never see its value. */
	/* Baked lightmaps.  The blotching this used to produce came from binding a
	   coordinate array that did not cover the mesh being drawn; EF_AddEf now
	   checks that before using it, so a mismatched brush is drawn unlit rather
	   than wrong.  Set r_lightmaps 0 to compare against no baked lighting. */
	if (iConsole && !iConsole->GetCVar("r_lightmaps"))
		iConsole->CreateVariable("r_lightmaps", "1", 0,
			"Use compact baked lightmaps in scenes with enough renderer headroom");

	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: before vglInit\n");
	/* This main-RAM pool competes with the engine heap for the application's
	   RAM budget, so it is trimmed from the 160 MiB it used to take down to
	   96 MiB, which measured fine through a full Training load and gameplay.
	   Together with the 224 MiB heap that brings the main-RAM total from about
	   400 MiB to roughly 337 MiB.  Raising CDRAM to hold the textures instead
	   was tried and does not work: 112 MiB of CDRAM crashes at the first
	   rendered frame, so it stays at 8 MiB.  See engine_port/main.cpp. */
	GLboolean vglResolutionFallback = vglInitWithCustomThreshold(
		0x200000, width, height,
		64 * 1024 * 1024,
		8 * 1024 * 1024,
		8 * 1024 * 1024,
		0x8C6000, // upstream vitaGL's full common-dialog pool: reserve it
		SCE_GXM_MULTISAMPLE_NONE);
	// vitaGL returns whether it had to fall back to a smaller display
	// resolution, not a conventional success boolean (GL_FALSE is the
	// normal 960x544 result).
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: resolutionFallback=%d\n", (int)vglResolutionFallback);
	sceClibPrintf("[VITAMEM] vitaGL pools ram=%u/%u cdram=%u/%u phycont=%u/%u\n",
		(unsigned)vglMemFree(VGL_MEM_RAM), (unsigned)vglMemTotal(VGL_MEM_RAM),
		(unsigned)vglMemFree(VGL_MEM_VRAM), (unsigned)vglMemTotal(VGL_MEM_VRAM),
		(unsigned)vglMemFree(VGL_MEM_SLOW), (unsigned)vglMemTotal(VGL_MEM_SLOW));
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: after vglInit, before glViewport\n");
	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	VitaInvalidateRenderStateCache();
	/* The Vita panel is 60 Hz.  An EGL swap interval of two is the driver's
	   native 30 FPS lock and avoids a second, drifting CPU-side sleep clock. */
	eglSwapInterval(EGL_NO_DISPLAY, 2);
	/* Do not pre-draw fixed-function variants here.  The 2026-08-21 package
	   performed eighteen synthetic draws immediately after the vitaGL splash,
	   before CrySystem could open Log.txt; the reported failure is a permanent
	   black screen at exactly that boundary.  Normal draws compile the variants
	   lazily, which restores the hardware-proven startup path.  Texture-list
	   warming is likewise opt-in until a fresh device run proves it safe. */
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: after glViewport\n");
	return (WIN_HWND)this; // just checked against NULL by callers
#else
	return 0;
#endif
}

void CVitaRenderer::ShutDown(bool bReInit)
{
	VitaFlushProgCacheRecords();
}

void CVitaRenderer::Release()
{
}

void CVitaRenderer::PreLoad()
{
}

void CVitaRenderer::PostLoad()
{
}

/* Materials whose diffuse has not been loaded yet get loaded from inside the
   draw call.  That is a file read, a decompress and a full mip-chain upload
   happening in the middle of a frame, and a fresh view can need dozens at
   once -- a visible stall rather than a hitch.  Cap how many may be loaded per
   frame: anything over budget is left out for that frame and is picked up on
   the next, so the cost spreads instead of spiking.

   The budget follows the backlog instead of being a flat two per frame.  A flat
   two is what made the world assemble itself in front of the player after a
   level load: a fresh view references hundreds of distinct materials, every
   chunk whose diffuse has not arrived yet is skipped entirely, and at two loads
   a frame that takes the best part of two hundred frames.  Geometry appears a
   piece at a time, and because which chunks are drawn depends on view order,
   pieces that were visible drop out again as the camera turns -- the assets
   flashing after a load.  Sizing the allowance from how many chunks actually
   went without last frame clears that backlog in a handful of frames and then
   costs nothing at all, because once everything in view is resolved the backlog
   is zero and the budget falls back to the minimum. */
static int g_nLazyTextureLoadsThisFrame = 0;
static int g_nChunksWaitingOnTextureLastFrame = 0;
static int g_nChunksWaitingOnTextureThisFrame = 0;
static int g_nLazyTextureBudgetThisFrame = 4;
static const int kMinLazyTextureLoadsPerFrame = 4;
static const int kMaxLazyTextureLoadsPerFrame = 32;

/* Persistent texture warm list ("ProgCache" in the port's UI/logs).  vitaGL's
   fixed-function path has no shaders to compile, so the useful persistent
   cache is the set of real DDS assets encountered by the previous run.  Warm a
   bounded recent tail during the opening screens; subsequent gameplay then
   reuses both CryPak/newlib file pages and already-uploaded textures instead of
   hitching the first time a gun, enemy, or effect enters view. */
static bool g_bVitaProgCacheInitialized = false;
static std::vector<std::string> g_vVitaProgCacheWarm;
static std::set<std::string> g_sVitaProgCacheRecorded;
static std::vector<std::string> g_vVitaProgCachePending;
static size_t g_nVitaProgCacheCursor = 0;
static const size_t kVitaProgCacheWarmLimit = 64;

/* How much of the vitaGL RAM pool the optional consumers must leave alone.
   Textures are the one allocation on this pool that cannot fail safely --
   vitaGL memcpys into whatever gpu_alloc_mapped_for_gpu hands back, NULL
   included -- so everything that is merely an optimisation (the ProgCache warm
   list, the mapped static-geometry copies) has to stop short of the reserve and
   let the level's own textures have it.  Both of those callers fall back
   cleanly: the warm list simply stops warming, and mapped geometry reverts to
   the client-array path it used before. */
static const unsigned int kVitaGpuPoolTextureReserveBytes = 12u * 1024u * 1024u;
static const unsigned int kVitaProgCacheWarmReserveBytes  = 48u * 1024u * 1024u;

static void VitaInitProgCache()
{
	if (g_bVitaProgCacheInitialized)
		return;
	g_bVitaProgCacheInitialized = true;
	FILE *fp = fopen("ux0:data/farcry/ProgCache.txt", "rb");
	if (!fp)
		fp = fopen("ProgCache.txt", "rb");
	if (!fp)
		return;
	char line[512];
	std::vector<std::string> all;
	while (fgets(line, sizeof(line), fp))
	{
		size_t n = strlen(line);
		while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
		if (!n)
			continue;
		const std::string path(line);
		g_sVitaProgCacheRecorded.insert(path);
		all.push_back(path);
	}
	fclose(fp);
	const size_t first = all.size() > kVitaProgCacheWarmLimit ?
		all.size() - kVitaProgCacheWarmLimit : 0;
	for (size_t i = first; i < all.size(); ++i)
		g_vVitaProgCacheWarm.push_back(all[i]);
	if (iLog)
		iLog->LogToFile("\001[VITA][PROGCACHE] loaded=%u warm=%u",
			(unsigned)all.size(), (unsigned)g_vVitaProgCacheWarm.size());
}

static void VitaRecordProgCacheTexture(const std::string &path)
{
	VitaInitProgCache();
	if (!g_sVitaProgCacheRecorded.insert(path).second)
		return;
	/* Do not touch storage on the first frame an asset appears.  That exact
	   synchronous open/write/close was itself a gameplay hitch. */
	g_vVitaProgCachePending.push_back(path);
}

static void VitaFlushProgCacheRecords()
{
	if (g_vVitaProgCachePending.empty())
		return;
	FILE *fp = fopen("ux0:data/farcry/ProgCache.txt", "ab");
	if (!fp)
		fp = fopen("ProgCache.txt", "ab");
	if (fp)
	{
		for (size_t i = 0; i < g_vVitaProgCachePending.size(); ++i)
		{
			const std::string &path = g_vVitaProgCachePending[i];
			fwrite(path.c_str(), 1, path.size(), fp);
			fwrite("\n", 1, 1, fp);
		}
		fclose(fp);
		if (iLog)
			iLog->LogToFile("\001[VITA][PROGCACHE] recorded=%u",
				(unsigned)g_vVitaProgCachePending.size());
		g_vVitaProgCachePending.clear();
	}
}

void CVitaRenderer::BeginFrame()
{
#if defined(LINUX)
	++g_nVitaRendererFrameSerial;
	VitaDrainDeferredMappedFrees();
	/* Never carry a secondary texture stage across frame boundaries.  Within a
	   frame it is retained between adjacent baked-lightmapped draws. */
	VitaDisableLightMapStage();
	VitaResetFixedFunctionMaterial();
	/* Vita: real frame clear. This and Update()'s swap below are the
	   first genuine on-screen output driven by the actual engine, not a
	   standalone test -- everything else in this class is a mechanical
	   IRenderer stub (see VitaRenderer.h). */
	++m_nFrameId;
	m_nDynVBCursor = 0;
	g_nVitaPreviousDrawCalls = g_nVitaDrawCalls;
	g_nVitaDrawCalls = 0;
#if defined(VITA_PERF_TELEMETRY)
	g_nVitaDrawIndices = 0;
	g_nVitaDynamicUploadCount = g_nVitaDynamicUploadBytes = 0;
	g_nVitaDynamicUploadUs = 0;
	g_nVitaClientArrayTransitions = g_nVitaPointerConfigurations = 0;
	g_nVitaVBODraws = g_nVitaMappedDraws = g_nVitaClientDraws = g_nVitaDynamicDraws = 0;
#endif
	g_nLazyTextureLoadsThisFrame = 0;
	g_nChunksWaitingOnTextureLastFrame = g_nChunksWaitingOnTextureThisFrame;
	g_nChunksWaitingOnTextureThisFrame = 0;
	g_nLazyTextureBudgetThisFrame =
		kMinLazyTextureLoadsPerFrame + g_nChunksWaitingOnTextureLastFrame;
	if (g_nLazyTextureBudgetThisFrame > kMaxLazyTextureLoadsPerFrame)
		g_nLazyTextureBudgetThisFrame = kMaxLazyTextureLoadsPerFrame;
	VitaInitProgCache();
	/* One load every other frame bounds opening-screen latency and memory traffic.
	   Stop after the opening window even if a malformed/huge old list exists. */
	/* The old 160-entry version could exhaust vitaGL before the level loaded and
	   was therefore disabled.  Sixty-four recent assets plus a 48 MB hard stop
	   is small enough to be safe while still covering the weapons, enemies and
	   effects that caused the previous run's first-use hitches. */
	if (s_vitaProgCacheWarm != 0 &&
		m_nFrameId > 8 && m_nFrameId < 360 && (m_nFrameId & 1) == 0 &&
		g_nVitaProgCacheCursor < g_vVitaProgCacheWarm.size())
	{
		/* Warming is a latency optimisation, never a reason to run out of GPU
		   memory.  Every one of these loads pins a texture for the whole session
		   (FT_NOREMOVE), and they come out of the same vitaGL RAM pool as the
		   vertex/index buffers and every level texture.  vitaGL does not survive
		   exhaustion: gpu_alloc_compressed_texture takes the pointer from
		   gpu_alloc_mapped_for_gpu without a NULL check and memcpys straight
		   into it, so an over-eager warm list is a data abort, not a slow frame.
		   Stop as soon as the pool drops past the reserve the level itself
		   needs, and stop for good rather than retrying every other frame. */
		if (vglMemFree(VGL_MEM_VRAM) < kVitaProgCacheWarmReserveBytes)
		{
			if (iLog)
				iLog->LogToFile("\001[VITA][PROGCACHE] stopped at %u/%u: %u KB pool free is below the %u KB reserve",
					(unsigned)g_nVitaProgCacheCursor,
					(unsigned)g_vVitaProgCacheWarm.size(),
					(unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024u),
					(unsigned)(kVitaProgCacheWarmReserveBytes / 1024u));
			g_nVitaProgCacheCursor = g_vVitaProgCacheWarm.size();
		}
		else
		{
			const std::string path = g_vVitaProgCacheWarm[g_nVitaProgCacheCursor++];
			EF_LoadTexture(path.c_str(), FT_NOREMOVE, 0, eTT_Base, 1.0f, 1.0f, -1, -1);
			if (iLog && (g_nVitaProgCacheCursor == 1 || (g_nVitaProgCacheCursor % 32) == 0))
				iLog->LogToFile("\001[VITA][PROGCACHE] warmed=%u/%u pool free=%u KB",
					(unsigned)g_nVitaProgCacheCursor, (unsigned)g_vVitaProgCacheWarm.size(),
					(unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024u));
		}
	}
	if (m_nFrameId == 359)
		VitaFlushProgCacheRecords();
	glViewport(m_nViewportX, m_nViewportY, m_nViewportWidth, m_nViewportHeight);
	/* Start every frame unclipped.  Nothing here turned the scissor test off,
	   and the only thing that ever does is a caller passing an all-zero
	   rectangle -- so a HUD element that sets a scissor and does not restore it
	   (a script that returns early, or one whose restoring call never runs)
	   leaves the test enabled for good.  From that point on every draw in every
	   later frame is clipped to that one rectangle, and because glClear obeys
	   the scissor as well, the clear below stops covering the screen too: the
	   rest of the framebuffer keeps whatever was in it.  A frame beginning is
	   the definition of "no clip region is in effect yet". */
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glClearColor(m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 1.0f);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	// glDepthMask above was set behind SetState's back.
	VitaInvalidateRenderStateCache();

#if defined(VITA_DEBUG_CLEARCOLOR)
	/* Ground truth for the white-sky bug: how many frames actually reach the
	   screen cleared to white. Should be zero once the shadow pass restores
	   the colour it borrows (Cry3DEngine/ObjManShadows.cpp). */
	{
		static int s_nFrames = 0, s_nWhiteFrames = 0;
		if (m_vClearColor.x > 0.99f && m_vClearColor.y > 0.99f && m_vClearColor.z > 0.99f)
			++s_nWhiteFrames;
		if ((++s_nFrames % 300) == 0 && iLog)
			iLog->LogToFile("\001[VITA CLEARSTAT] frames=%d whiteFrames=%d current=%.3f %.3f %.3f",
				s_nFrames, s_nWhiteFrames, m_vClearColor.x, m_vClearColor.y, m_vClearColor.z);
	}
#endif
#endif
}

void CVitaRenderer::Update()
{
#if defined(LINUX)
	/* CryEngine renders each indoor area and outdoor prefetch camera in six
	   directions during OnLevelLoaded, bracketed by EnableSwapBuffers(false).
	   The Vita stub used to ignore that bracket and present every hidden
	   precache view, producing the long burst of half-populated geometry the
	   player saw after a load.  glFinish ends/submits the current GXM scene and
	   preserves the resource warm-up without putting that back buffer on the
	   display queue. */
	if (!m_bSwapBuffersEnabled)
	{
		glFinish();
		return;
	}
	// Vita: TEMPORARY verification capture -- one real screenshot of frame
	// 90 (skips past the first couple of still-loading frames), written to
	// a real, host-visible path. Reverted once no longer needed.

	// Vita: present the frame cleared in BeginFrame() above.
#if defined(VITA_PERF_TELEMETRY)
	const SceUInt64 nSwapStartUs = sceKernelGetProcessTimeWide();
	vglSwapBuffers(GL_FALSE);
	const SceUInt64 nSwapUs = sceKernelGetProcessTimeWide() - nSwapStartUs;
	static unsigned int s_nFrames = 0;
	static SceUInt64 s_nDraws = 0, s_nIndices = 0, s_nVBO = 0, s_nMapped = 0;
	static SceUInt64 s_nClient = 0, s_nDynamic = 0, s_nSwapUs = 0;
	static SceUInt64 s_nDynamicUploads = 0, s_nDynamicUploadBytes = 0;
	static SceUInt64 s_nDynamicUploadUs = 0;
	static SceUInt64 s_nArrayTransitions = 0, s_nPointerConfigurations = 0;
	s_nDraws += g_nVitaDrawCalls;
	s_nIndices += g_nVitaDrawIndices;
	s_nVBO += g_nVitaVBODraws;
	s_nMapped += g_nVitaMappedDraws;
	s_nClient += g_nVitaClientDraws;
	s_nDynamic += g_nVitaDynamicDraws;
	s_nSwapUs += nSwapUs;
	s_nDynamicUploads += g_nVitaDynamicUploadCount;
	s_nDynamicUploadBytes += g_nVitaDynamicUploadBytes;
	s_nDynamicUploadUs += g_nVitaDynamicUploadUs;
	s_nArrayTransitions += g_nVitaClientArrayTransitions;
	s_nPointerConfigurations += g_nVitaPointerConfigurations;
	if ((++s_nFrames % 120) == 0 && iLog)
	{
		iLog->LogToFile("\001[VITA][DRAWPERF] avg draws=%u indices=%u mapped=%u vbo=%u client=%u dynamic=%u swapUs=%u",
			(unsigned)(s_nDraws / 120), (unsigned)(s_nIndices / 120),
			(unsigned)(s_nMapped / 120), (unsigned)(s_nVBO / 120), (unsigned)(s_nClient / 120),
			(unsigned)(s_nDynamic / 120), (unsigned)(s_nSwapUs / 120));
			s_nDraws = s_nIndices = s_nMapped = s_nVBO = s_nClient = s_nDynamic = s_nSwapUs = 0;
		/* Pool headroom, not just our own accounting.  Exhausting this is a NULL
		   memcpy inside vitaGL's texture allocator, so the trend across a level
		   load is the number that matters when a load dies. */
		iLog->LogToFile("\001[VITA][GPUMEM] pool free ram=%u KB vram=%u KB",
			(unsigned)(vglMemFree(VGL_MEM_RAM) / 1024u),
			(unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024u));
		iLog->LogToFile("\001[VITA][DYNVBO] avg uploads=%u KB=%u uploadUs=%u",
			(unsigned)(s_nDynamicUploads / 120),
			(unsigned)(s_nDynamicUploadBytes / (120 * 1024)),
			(unsigned)(s_nDynamicUploadUs / 120));
		s_nDynamicUploads = s_nDynamicUploadBytes = s_nDynamicUploadUs = 0;
		iLog->LogToFile("\001[VITA][GLSTATE] avg arrayTransitions=%u pointerConfigs=%u",
			(unsigned)(s_nArrayTransitions / 120),
			(unsigned)(s_nPointerConfigurations / 120));
		s_nArrayTransitions = s_nPointerConfigurations = 0;
	}
#else
	vglSwapBuffers(GL_FALSE);
#endif
#if defined(FARCRY_VITA3K_LAB)
	extern void Vita3KLabAfterPresentBridge();
	Vita3KLabAfterPresentBridge();
#endif
#endif
}

int CVitaRenderer::GetWidth() { return m_nWidth; }
int CVitaRenderer::GetHeight() { return m_nHeight; }
int CVitaRenderer::GetColorBpp() { return m_nColorBpp; }
int CVitaRenderer::GetDepthBpp() { return m_nDepthBpp; }
int CVitaRenderer::GetStencilBpp() { return m_nStencilBpp; }
char CVitaRenderer::GetType() { return m_cType; }
void CVitaRenderer::SetType(char type) { m_cType = type; }
WIN_HWND CVitaRenderer::GetHWND() { return (WIN_HWND)this; }

//////////////////////////////////////////////////////////////////////
// Mechanically-stubbed remainder of the IRenderer interface (see
// VitaRenderer.h) -- honest no-ops, not yet implemented.
//////////////////////////////////////////////////////////////////////

/* Vita: real matrix-stack + camera plumbing. CryEngine's per-object draw
   pattern is PushMatrix(); [Rotate/Translate/Scale/Mult]Matrix(...);
   DrawBuffer(...); PopMatrix() -- ordinary GL_MODELVIEW composition on top
   of whatever SetCamera last loaded as the base, exactly like the real
   reference GL backend (RenderDll/XRenderOGL/GL_Renderer.cpp's
   CGLRenderer::PushMatrix/RotateMatrix/etc, all thin glPushMatrix/
   glRotatef/etc wraps) -- not reimplemented from scratch, just the same
   calls Draw2dImage already makes for GL_PROJECTION, applied to
   GL_MODELVIEW instead. */
void CVitaRenderer::PushMatrix()
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
#endif
}
void CVitaRenderer::PopMatrix()
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
#endif
}
void CVitaRenderer::MultMatrix(float * mat)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glMultMatrixf(mat);
#endif
}
void CVitaRenderer::LoadMatrix(const Matrix44 * src)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	if (src)
		glLoadMatrixf(src->GetData());
	else
		glLoadIdentity();
#endif
}
void CVitaRenderer::RotateMatrix(float a, float x, float y, float z)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glRotatef(a, x, y, z);
#endif
}
void CVitaRenderer::RotateMatrix(const Vec3 & angles)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glRotatef(angles.z, 0, 0, 1);
	glRotatef(angles.y, 0, 1, 0);
	glRotatef(angles.x, 1, 0, 0);
#endif
}
void CVitaRenderer::TranslateMatrix(float x, float y, float z)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glTranslatef(x, y, z);
#endif
}
void CVitaRenderer::TranslateMatrix(const Vec3 & pos)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glTranslatef(pos.x, pos.y, pos.z);
#endif
}
void CVitaRenderer::ScaleMatrix(float x, float y, float z)
{
#if defined(LINUX)
	glMatrixMode(GL_MODELVIEW);
	glScalef(x, y, z);
#endif
}

/* Vita: real camera -> GL_PROJECTION/GL_MODELVIEW setup. vitaGL has no GLU,
   so this is glFrustum instead of the reference renderer's gluPerspective
   (RenderDll/XRenderOGL/GL_Renderer.cpp::SetCamera) -- same math (fovy =
   horizontal fov * ProjRatio, aspect = 1/ProjRatio, per that function's own
   comment), just expressed as frustum bounds instead of a fovy+aspect pair.
   GL_MODELVIEW is loaded from CCamera::GetVCMatrixD3D9(), the engine's own
   ready-made, already-axis-corrected (CryEngine is Y-forward/Z-up; GL
   expects the standard convention) view matrix -- see Cry_Camera.h:358 and
   the same accessor's use in the reference SetCamera. */
/* Vita: TEMPORARY diagnostic -- standard-convention OpenGL camera (eye at
   origin, looking down -Z, world translated by -dist) with NO dependency on
   CCamera's Y-forward/Z-up axis convention or GetVCMatrixD3D9's correction
   matrix, to rule those out as the reason DrawBuffer's real, error-free
   (glGetError()==0) draws aren't landing on screen (confirmed via a real
   glReadPixels capture showing black despite successful draw calls).
   Reverted once no longer needed. */
extern "C" void Vita_DebugSetSimpleCamera(float dist, int width, int height)
{
#if defined(LINUX)
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	float aspect = (float)width / (float)height;
	float fNear = 0.5f, fFar = 100.0f;
	float fFovYRad = 60.0f * 3.14159265f / 180.0f;
	float fTop = fNear * tanf(fFovYRad * 0.5f);
	float fRight = fTop * aspect;
	glFrustum(-fRight, fRight, -fTop, fTop, fNear, fFar);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -dist);
	sceClibPrintf("[BOOTTRACE] Vita_DebugSetSimpleCamera: dist=%f w=%d h=%d aspect=%f fRight=%f fTop=%f\n",
		dist, width, height, aspect, fRight, fTop);
#endif
}

void CVitaRenderer::SetCamera(const CCamera & cam)
{
#if defined(LINUX)
	m_Camera = cam;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	float fNear = cam.GetZMin();
	float fFar = cam.GetZMax();
	float fFovY = cam.GetFov() * cam.GetProjRatio();
	float fTop = fNear * tanf(fFovY * 0.5f);
	float fRight = fTop / cam.GetProjRatio();
	glFrustum(-fRight, fRight, -fTop, fTop, fNear, fFar);

	glMatrixMode(GL_MODELVIEW);
	Matrix44 mat = cam.GetVCMatrixD3D9();
	glLoadMatrixf(mat.GetData());
#endif
}
const CCamera & CVitaRenderer::GetCamera() { return m_Camera; }

void CVitaRenderer::SetCullMode(int mode)
{
#if defined(LINUX)
	/* Material submission calls this for every chunk.  Most consecutive chunks
	   use back-face culling, and reissuing glEnable/glCullFace makes vitaGL dirty
	   its fixed-function state even when nothing changed. */
	if (mode == m_nCullMode)
		return;
	m_nCullMode = mode;
	if (mode == R_CULL_NONE)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		// Vita: CryEngine's R_CULL_BACK/FRONT already account for its own
		// winding convention the same way the reference GL renderer's
		// SetCullMode does (GL_Renderer.cpp) -- back/front map straight
		// across, no flip needed.
		glCullFace(mode == R_CULL_FRONT ? GL_FRONT : GL_BACK);
	}
#endif
}

bool CVitaRenderer::SetCurrentContext(WIN_HWND hWnd) { return false; }
bool CVitaRenderer::CreateContext(WIN_HWND hWnd, bool bAllowFSAA) { return false; }
bool CVitaRenderer::DeleteContext(WIN_HWND hWnd) { return false; }
int CVitaRenderer::GetFeatures()
{
	/* The Vita GPU is newer than a GeForce 2, but this backend intentionally
	   exposes CryEngine's single-texture fixed-function contract: SelectTMU and
	   the desktop shader-template multipass pipeline are not implemented.  The
	   stock engine has a complete authored GF2 fallback (ambient-lit terrain,
	   translucent ocean and reduced detail layers), which is both visually
	   correct for this renderer and appropriate for Vita's 128 MiB VRAM budget.
	   Advertising multitexture made the retail content choose paths we silently
	   could not execute. */
	return RFT_HW_GF2 | RFT_RGBA | RFT_COMPRESSTEXTURE;
}
int CVitaRenderer::GetMaxTextureMemory() { return 128 * 1024 * 1024; }
int CVitaRenderer::EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset) { return 0; }
bool CVitaRenderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen) { return false; }
void CVitaRenderer::FreeResources(int nFlags) { }
void CVitaRenderer::RefreshResources(int nFlags) { }
void CVitaRenderer::ShareResources(IRenderer * renderer) { }
void CVitaRenderer::GetViewport(int * x, int * y, int * width, int * height)
{
	if (x) *x = m_nViewportX;
	if (y) *y = m_nViewportY;
	if (width) *width = m_nViewportWidth;
	if (height) *height = m_nViewportHeight;
}
void CVitaRenderer::SetViewport(int x, int y, int width, int height)
{
#if defined(LINUX)
	/* Crytek's OpenGL backend treats an all-zero call as "restore the
	   remembered viewport" and every other call as a new remembered value. */
	if (x || y || width || height)
	{
		m_nViewportX = x;
		m_nViewportY = y;
		m_nViewportWidth = width;
		m_nViewportHeight = height;
	}
	glViewport(m_nViewportX, m_nViewportY, m_nViewportWidth, m_nViewportHeight);
#endif
}
void CVitaRenderer::SetScissor(int x, int y, int width, int height)
{
#if defined(LINUX)
	if (!x && !y && !width && !height)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	/* glScissor's origin is the bottom-left of the framebuffer, but every
	   caller here is working in screen space with y increasing downwards --
	   CScriptObjectSystem::SetScissor feeds it ScaleCoordY'd HUD coordinates
	   straight from the scripts.  Passing those through unflipped puts the
	   rectangle on the opposite side of the screen from the thing it is meant
	   to clip, so HUD elements that draw inside a scissor region (the health
	   and stamina bars use one) are clipped away entirely.

	   Clamped to the framebuffer, and a degenerate rectangle turns the test off
	   rather than being submitted: a negative width or height is a GL error,
	   and handing the driver a rectangle outside the render target is the kind
	   of thing that wedges the GPU rather than failing cleanly. */
	int nScissorY = m_nHeight - (y + height);
	int nW = width, nH = height;
	if (nW <= 0 || nH <= 0)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	/* Clamping the left edge has to take the width with it.  Moving nX up to 0
	   without shortening nW widened the rectangle by however far off-screen it
	   started, so a HUD element hanging off the left of the screen would clip
	   too much on its right -- the same class of mistake as the unflipped y,
	   just quieter.  Same for the top edge, where nScissorY going negative
	   already reduces nH. */
	int nX = x;
	if (nX < 0) { nW += nX; nX = 0; }
	if (nScissorY < 0) { nH += nScissorY; nScissorY = 0; }
	if (nW <= 0 || nH <= 0)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	if (nX + nW > m_nWidth)  nW = m_nWidth  - nX;
	if (nScissorY + nH > m_nHeight) nH = m_nHeight - nScissorY;
	if (nW <= 0 || nH <= 0)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	glEnable(GL_SCISSOR_TEST);
	glScissor(nX, nScissorY, nW, nH);
#endif
}
void CVitaRenderer::MakeCurrent() { }
/* Vita: real vertex-format-generic immediate-mode draw. gBufInfoTable/
   m_VertexSize (CryCommon/VertexFormats.h) give the real per-format byte
   offsets/stride Crytek's own renderers use -- position is always a Vec3 at
   offset 0 in every format (see the struct_VERTEX_FORMAT_* definitions), so
   this one implementation covers every format the engine can hand us,
   without hand-special-casing each one. An offset of 0 in gBufInfoTable
   means "not present" (see that table's comment) -- safe as a sentinel
   because position always occupies bytes 0-11 first, so no other attribute
   can legitimately sit at offset 0 too. */
#if defined(LINUX)
/* True while the bound "texture" is the untextured white fallback.  The draw
   path then ignores the mesh's vertex colours, because those are what the
   surface would otherwise be painted with -- see CVitaRenderer::SetWhiteTexture. */
static bool g_bUntexturedSurface = false;
/* Flat tint for a surface that has no diffuse and should not be painted white
   -- water, whose retail shaders build their colour from a reflection and a sky
   sample this fixed-function path cannot produce.  Applied through the same
   glColor4f the untextured branch already uses, because SetMaterialColor on this
   renderer is a stub and never reaches GL. */
static bool g_bSurfaceTintActive = false;
static float g_arrSurfaceTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
/* Water's generated vertex colours carry both the crest/trough shading and the
   authored transparency.  They must survive the static-lighting fallback. */
static bool g_bForceVertexColours = false;
/* Published before vertex-array setup so baked-lightmapped surfaces can use
   the diffuse texture at full strength.  CGRCTexLM.crycg never reads vertex
   colour; multiplying it here as well applied lighting twice and crushed most
   interiors to black. */
static bool g_bVitaLightMapActive = false;
/* Baked world geometry arrives in long runs.  The old path enabled, configured
   with seven glTexEnv calls, and disabled texture unit 1 around every draw.
   vitaGL rebuilds its fixed-function state for those calls, so hardware paid
   that setup hundreds of times per dense frame.  Retain the stage between
   adjacent lightmapped draws and change only the texture and UV pointer. */
static bool g_bVitaLightMapStageEnabled = false;
static GLuint g_nVitaBoundLightMap = 0;

static void VitaDisableLightMapStage()
{
	g_bVitaLightMapActive = false;
	if (!g_bVitaLightMapStageEnabled)
	{
		/* A deleted texture is implicitly unbound by GL.  Forget our mirror even
		   when the stage was already disabled so a later recycled name can never
		   make us skip the bind. */
		g_nVitaBoundLightMap = 0;
		return;
	}
	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	g_bVitaLightMapStageEnabled = false;
	g_nVitaBoundLightMap = 0;
}

static void VitaEnableLightMapStage(GLuint nTexture, int nStride, const void *pTexCoords)
{
	VitaBindArrayBuffer(0);
	glActiveTexture(GL_TEXTURE1);
	if (!g_bVitaLightMapStageEnabled)
	{
		glEnable(GL_TEXTURE_2D);
		VitaSetRGBModulatePreserveAlpha(4.0f);
		glClientActiveTexture(GL_TEXTURE1);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		g_bVitaLightMapStageEnabled = true;
	}
	else
		glClientActiveTexture(GL_TEXTURE1);
	if (g_nVitaBoundLightMap != nTexture)
	{
		glBindTexture(GL_TEXTURE_2D, nTexture);
		g_nVitaBoundLightMap = nTexture;
	}
	glTexCoordPointer(2, GL_FLOAT, nStride, pTexCoords);
	glClientActiveTexture(GL_TEXTURE0);
	glActiveTexture(GL_TEXTURE0);
	g_bVitaLightMapActive = true;
}


/* SetTexture runs once per material chunk -- thousands of times a frame -- and
   re-binding the texture that is already bound still makes vitaGL re-validate
   its fixed-function state.  Remember what unit 0 holds and skip the no-ops.

   Only unit 0 is tracked: glBindTexture applies to whichever unit is active,
   so the lightmap binds (which run under glActiveTexture(GL_TEXTURE1)) must
   stay direct, and anything that binds unit 0 behind this function's back has
   to call VitaInvalidateTextureCache(). */
static GLuint g_nBoundTexture0 = 0xFFFFFFFFu;
/* Tri-state on purpose: -1 unknown, 0 disabled, 1 enabled.  A plain bool
   cleared to false would claim texturing is off when it is really unknown, so
   a following SetWhiteTexture would skip its glDisable and leave the previous
   texture bound on the surface. */
static int    g_nTexture2DEnabled = -1;
static inline void VitaInvalidateTextureCache()
{
	g_nBoundTexture0 = 0xFFFFFFFFu;
	g_nTexture2DEnabled = -1;
}

/* Are this mesh's vertex colours actually carrying light, or are they just
   zero?  Baked vertex lighting modulates the texture, so an all-black colour
   array multiplies the surface out to solid black -- which is what the large
   black shapes in interiors are: ordinary textured geometry whose vertex
   colours never received any lighting.  Retail never shows that because its
   shaders add an ambient term this fixed-function path has no equivalent for.

   A surface that is genuinely meant to be pure black at every single vertex
   does not occur in this game's data, so treating that case as "no lighting
   supplied" and drawing the texture unmodulated is the safe reading.  The scan
   is cached per buffer -- geometry is static and this is a few hundred byte
   reads once, against doing it every frame. */
static void VitaPrepareStaticVertexColours(CVertexBuffer *pBuffer)
{
	/* Value 2 is the explicit "ambient floor already applied" sentinel.  A
	   previous diagnostic scan may have cached 0/1 before this preparation is
	   reached, so those values must not suppress the one-time repair. */
	if (!pBuffer || pBuffer->m_bDynamic || pBuffer->m_nVitaUnlitColours == 2)
		return;
	byte *pData = (byte *)pBuffer->m_VS[VSF_GENERAL].m_VData;
	const SBufInfoTable &tbl = gBufInfoTable[pBuffer->m_vertexformat];
	if (!pData || !tbl.OffsColor || pBuffer->m_NumVerts <= 0)
		return;

	/* The fixed-function path has no shader ambient term.  Preserve the authored
	   per-vertex lighting, but add the missing minimum ambient instead of either
	   multiplying whole rooms to black or disabling all lighting globally. */
	const byte kAmbientFloor = 72;
	const int nStride = m_VertexSize[pBuffer->m_vertexformat];
	for (int i = 0; i < pBuffer->m_NumVerts; ++i)
	{
		byte *pColour = pData + (size_t)i * nStride + tbl.OffsColor;
		for (int c = 0; c < 3; ++c)
			if (pColour[c] < kAmbientFloor)
				pColour[c] = kAmbientFloor;
	}
	pBuffer->m_nVitaUnlitColours = 2;
	pBuffer->m_bGLDirty = true;
	pBuffer->m_bVitaMappedVertexDirty = true;
}

static bool VitaVertexColoursAreUnlit(CVertexBuffer *pBuffer)
{
	if (!pBuffer)
		return false;
	if (g_bForceVertexColours)
		return false;
	/* Static colours are repaired once with the missing ambient floor above;
	   retain their variation instead of flattening the entire scene. */
	if (!pBuffer->m_bDynamic)
	{
		VitaPrepareStaticVertexColours(pBuffer);
		return false;
	}
	const byte *pData = (const byte *)pBuffer->m_VS[VSF_GENERAL].m_VData;
	const int nVertexFormat = pBuffer->m_vertexformat;
	const int nNumVerts = pBuffer->m_NumVerts;
	const SBufInfoTable &tbl = gBufInfoTable[nVertexFormat];
	if (!pData || !tbl.OffsColor || nNumVerts <= 0)
		return false;
	if (pBuffer->m_nVitaUnlitColours >= 0)
		return pBuffer->m_nVitaUnlitColours != 0;

	const int nStride = m_VertexSize[nVertexFormat];
	int nBrightest = 0;
	// A few hundred vertices is plenty to decide; whole meshes can be large.
	const int nSampleStep = (nNumVerts > 256) ? (nNumVerts / 256) : 1;
	/* The stock fixed-function fallback has no ambient term.  Real hardware
	   captures showed that authored-but-effectively-unlit indoor geometry can
	   contain values in the high teens; multiplying those through makes the
	   entire spawn room black.  The earlier 24/255 cutoff made those surfaces
	   visible without flattening normally lit geometry, so keep that proven
	   cutoff instead of requiring mathematically pure black. */
	const int kUnlitThreshold = 24;
	for (int i = 0; i < nNumVerts && nBrightest < kUnlitThreshold; i += nSampleStep)
	{
		const byte *c = pData + (size_t)i * nStride + tbl.OffsColor;
		const int nMax = (c[0] > c[1] ? c[0] : c[1]) > c[2] ? (c[0] > c[1] ? c[0] : c[1]) : c[2];
		if (nMax > nBrightest)
			nBrightest = nMax;
	}
	pBuffer->m_nVitaUnlitColours = (nBrightest < kUnlitThreshold) ? 1 : 0;
	return pBuffer->m_nVitaUnlitColours != 0;
}

static void SetupVertexArraysForFormat(const byte *pBase, int nVertexFormat,
	bool bUnlitColours = false, GLuint nArrayBuffer = 0)
{
	if (!g_bVitaLightMapActive)
		VitaDisableLightMapStage();
	VitaBindArrayBuffer(nArrayBuffer);
	const SBufInfoTable &tbl = gBufInfoTable[nVertexFormat];
	int nStride = m_VertexSize[nVertexFormat];
	const bool bUseColor = tbl.OffsColor && !g_bUntexturedSurface &&
		!bUnlitColours && !g_bSurfaceTintActive && !g_bVitaLightMapActive;
	const bool bUseTexCoord = tbl.OffsTC != 0;

	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);
	VitaSetClientArrayState(GL_COLOR_ARRAY, bUseColor);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, bUseTexCoord);
	if (!bUseColor)
	{
		if (g_bSurfaceTintActive)
			VitaSetConstantColor(g_arrSurfaceTint[0], g_arrSurfaceTint[1], g_arrSurfaceTint[2], g_arrSurfaceTint[3]);
		else
			VitaSetConstantColor(g_arrVitaMaterialColor[0], g_arrVitaMaterialColor[1], g_arrVitaMaterialColor[2], g_arrVitaMaterialColor[3]);
	}

	if (g_nVitaPointerArrayBuffer == nArrayBuffer &&
		g_pVitaPointerBase == pBase && g_nVitaPointerFormat == nVertexFormat &&
		g_nVitaPointerUsesColor == (bUseColor ? 1 : 0) &&
		g_nVitaPointerUsesTexCoord == (bUseTexCoord ? 1 : 0))
		return;

	glVertexPointer(3, GL_FLOAT, nStride, pBase);
	if (bUseColor)
		glColorPointer(4, GL_UNSIGNED_BYTE, nStride, pBase + tbl.OffsColor);
	if (bUseTexCoord)
		glTexCoordPointer(2, GL_FLOAT, nStride, pBase + tbl.OffsTC);
	/* The ordinary GL pointer calls above replace the same vitaGL legacy
	   attribute objects used by vgl*PointerMapped. */
	g_pVitaMappedPointerBuffer = NULL;
	g_nVitaMappedPointerUsesColor = -1;
	g_nVitaMappedPointerUsesTexCoord = -1;
	g_nVitaPointerArrayBuffer = nArrayBuffer;
	g_pVitaPointerBase = pBase;
	g_nVitaPointerFormat = nVertexFormat;
	g_nVitaPointerUsesColor = bUseColor ? 1 : 0;
	g_nVitaPointerUsesTexCoord = bUseTexCoord ? 1 : 0;
	VITA_PERF_INCREMENT(g_nVitaPointerConfigurations);
}

/* vitaGL's fixed-function implementation generates GXP programs on demand and
   persists them under ux0:data/shader_cache.  Pre-touch the combinations this
   renderer actually uses (HUD/particles/world, with colour/fog/alpha-test)
   before gameplay so first contact with a muzzle flash or impact effect does
   not compile a new program in the middle of a fight.  The generated binaries
   are then loaded from the persistent cache on later launches. */
static void VitaWarmFixedFunctionProgramCache()
{
	const float verts[9] = {-4.0f,-4.0f,0.0f, -3.0f,-4.0f,0.0f, -4.0f,-3.0f,0.0f};
	const float uvs[6] = {0,0, 1,0, 0,1};
	const unsigned char colors[12] = {
		255,255,255,255, 255,255,255,255, 255,255,255,255};
	const unsigned int pixel = 0xFFFFFFFFu;
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	glVertexPointer(3, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
	glTexCoordPointer(2, GL_FLOAT, 0, uvs);
	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);

	unsigned int nVariants = 0;
	for (int nFog = 0; nFog < 2; ++nFog)
	for (int nAlpha = 0; nAlpha < 2; ++nAlpha)
	for (int nTextured = 0; nTextured < 2; ++nTextured)
	for (int nColored = 0; nColored < 2; ++nColored)
	{
		if (nFog) glEnable(GL_FOG); else glDisable(GL_FOG);
		if (nAlpha) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5f);
		if (nTextured)
		{
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		}
		else
			glDisable(GL_TEXTURE_2D);
		VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, nTextured != 0);
		VitaSetClientArrayState(GL_COLOR_ARRAY, nColored != 0);
		if (!nColored) glColor4f(1,1,1,1);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		++nVariants;
	}
	/* Terrain uses two fixed-function texture units.  Compile both fog forms now;
	   otherwise the first detailed sector would create these programs in play. */
	glDisable(GL_ALPHA_TEST);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture);
	glClientActiveTexture(GL_TEXTURE0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture);
	/* CGRCTexLM stores baked colour quarter-scale and multiplies it by four. */
	VitaSetRGBModulatePreserveAlpha(4.0f);
	glClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, uvs);
	glClientActiveTexture(GL_TEXTURE0);
	glActiveTexture(GL_TEXTURE0);
	for (int nFog = 0; nFog < 2; ++nFog)
	{
		if (nFog) glEnable(GL_FOG); else glDisable(GL_FOG);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		++nVariants;
	}
	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glActiveTexture(GL_TEXTURE1);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
	glDisable(GL_TEXTURE_2D);
	glClientActiveTexture(GL_TEXTURE0);
	glActiveTexture(GL_TEXTURE0);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glDeleteTextures(1, &texture);
	glDisable(GL_FOG);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	VitaInvalidateVertexPointerCache();
	VitaInvalidateRenderStateCache();
	VitaInvalidateTextureCache();
	if (iLog)
		iLog->LogToFile("\001[VITA][PROGCACHE] warmed %u fixed-function variants; persistent vitaGL cache enabled",
			nVariants);
}

/* Published by CLeafBuffer::AddRenderElements for the duration of one buffer's
   draw, so every chunk of a terrain sector can reach the texgen offsets that
   only the first chunk's render element actually carries. */
const float *g_pVitaTerrainTexGen = NULL;
int g_nVitaTerrainDetailTexture = 0;
/* scale U/V, translate U/V; populated from the authored terrain layer's
   projection coefficients for the duration of one sector draw. */
float g_arrVitaTerrainDetailTransform[4] = {12.0f, 12.0f, 0.0f, 0.0f};
/* The desktop terrain shader blends up to seven close detail layers.  The Vita
   fixed-function path cannot reproduce that shader, but it can modulate one
   authored layer into the existing sector draw on texture unit 1.  Reusing the
   base UV stream with a texture-matrix repeat adds ground grain without adding
   a second draw call or any CPU-generated geometry. */
static bool VitaBeginTerrainDetail(const byte *pBase, int nVertexFormat)
{
	/* Disabled until the retail terrain shader blend can be reproduced.  The
	   single fixed-function modulation pass multiplies unrelated authored layer
	   data into the base texture and produces blue/black terrain corruption. */
	return false;
#if 0
	if (g_nVitaTerrainDetailTexture <= 0 || !gBufInfoTable[nVertexFormat].OffsTC)
		return false;
	const int nStride = m_VertexSize[nVertexFormat];
	const size_t nOffset = (size_t)gBufInfoTable[nVertexFormat].OffsTC;
	const void *pTexCoords = pBase ? (const void *)(pBase + nOffset) : (const void *)nOffset;

	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, (GLuint)g_nVitaTerrainDetailTexture);
	VitaSetRGBModulatePreserveAlpha();
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(g_arrVitaTerrainDetailTransform[2],
		g_arrVitaTerrainDetailTransform[3], 0.0f);
	glScalef(g_arrVitaTerrainDetailTransform[0],
		g_arrVitaTerrainDetailTransform[1], 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, nStride, pTexCoords);
	glClientActiveTexture(GL_TEXTURE0);
	glActiveTexture(GL_TEXTURE0);
	return true;
#endif
}

static void VitaEndTerrainDetail(bool bActive)
{
	if (!bActive)
		return;
	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
}

/* Terrain sector vertices (VERTEX_FORMAT_P3F_N_COL4UB_COL4UB) carry no texture
   coordinates at all -- the retail terrain shader derives them from world
   position using the three "texgen offset" floats the 3D engine hangs off the
   render element (terrain_render.cpp sets them; CSectorInfo::UpdateVarBuffer
   points pRE->m_CustomData at them).  There is no shader here to do that, and
   SetupVertexArraysForFormat disables GL_TEXTURE_COORD_ARRAY when the format has
   no OffsTC -- so every terrain vertex sampled the one constant default
   coordinate, giving the whole world a single flat colour out of the sector
   texture.  That is the white terrain.

   Rebuild the same mapping on the CPU: u from Y, v from X, both scaled by
   offsets[2] and biased by offsets[0]/[1], matching the ordering the engine
   writes them in.  The array is regenerated per draw into a reused buffer,
   which costs a couple of thousand multiply-adds against a draw that is already
   pushing that many vertices. */
static const float *VitaBuildTerrainTexCoords(const byte *pData, int nVertexFormat,
	int nNumVerts, const float *pTexGenOffsets)
{
	if (!pData || !pTexGenOffsets || nNumVerts <= 0)
		return NULL;
	const float fScale = pTexGenOffsets[2];
	if (fScale == 0.0f)
		return NULL;
	/* Cache per vertex buffer, not globally.  A sector is drawn as dozens of
	   strip chunks sharing one buffer and one mapping, and ~30 sectors are
	   visible at once -- so a single remembered result is missed by every
	   sector in turn and the whole thing is rebuilt on each one, every frame.
	   Direct-mapped on the buffer address: sector geometry and its offsets only
	   change on an LOD transition, so in the steady state this is a hit. */
	struct STerrainUVCacheEntry
	{
		const byte *pData;
		int nVerts;
		float fScale, fOffU, fOffV;
		std::vector<float> arrUVs;
		STerrainUVCacheEntry() : pData(NULL), nVerts(0), fScale(0), fOffU(0), fOffV(0) {}
	};
	const int kCacheSlots = 32;
	static STerrainUVCacheEntry s_arrCache[kCacheSlots];
	STerrainUVCacheEntry &rEntry =
		s_arrCache[(((size_t)pData) >> 5) & (kCacheSlots - 1)];

	if (rEntry.pData == pData && rEntry.nVerts == nNumVerts && rEntry.fScale == fScale &&
		rEntry.fOffU == pTexGenOffsets[0] && rEntry.fOffV == pTexGenOffsets[1] &&
		rEntry.arrUVs.size() == (size_t)nNumVerts * 2)
		return &rEntry.arrUVs[0];

	rEntry.pData = pData;
	rEntry.nVerts = nNumVerts;
	rEntry.fScale = fScale;
	rEntry.fOffU = pTexGenOffsets[0];
	rEntry.fOffV = pTexGenOffsets[1];
	rEntry.arrUVs.resize((size_t)nNumVerts * 2);

	const int nStride = m_VertexSize[nVertexFormat];
	for (int i = 0; i < nNumVerts; ++i)
	{
		const float *pPos = (const float *)(pData + (size_t)i * nStride);
		rEntry.arrUVs[(size_t)i * 2 + 0] = pPos[1] * fScale + pTexGenOffsets[0];
		rEntry.arrUVs[(size_t)i * 2 + 1] = pPos[0] * fScale + pTexGenOffsets[1];
	}
	return &rEntry.arrUVs[0];
}

/* Attach generated coordinates as a client-side array.  Binding buffer zero
   first matters: the gl*Pointer call captures whatever GL_ARRAY_BUFFER is bound
   at the time, so on the VBO path the pointer would otherwise be read as an
   offset into the vertex buffer object. */
static void VitaApplyGeneratedTexCoords(const float *pUVs)
{
	if (!pUVs)
		return;
	VitaBindArrayBuffer(0);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, true);
	glTexCoordPointer(2, GL_FLOAT, 0, pUVs);
	VitaInvalidateVertexPointerCache();
}

static void TeardownVertexArrays()
{
	/* SetupVertexArraysForFormat transitions the retained mask on the next draw. */
}

/* Drawing from client-side pointers makes vitaGL copy the vertices and indices
   into GPU-visible memory on every single draw call.  With a terrain sector at
   ~1100 vertices and the low-res terrain at 16641, redrawn every frame, that
   copying dominates the frame far more than the triangles themselves do.
   Static geometry never changes, so upload it once into real buffer objects
   and let the GPU read it directly.

   Two deliberate limits.  Only meshes worth the bookkeeping are promoted --
   small ones are dominated by per-call overhead, and the level has thousands
   of them.  And the total is capped: the vitaGL pool is shared with textures,
   and quietly exhausting it would trade frame rate for a crash. */
/* Anything that misses these two limits is drawn from client memory, which on
   this driver means the vertex data is copied into a command buffer again on
   every single draw call, every frame, for geometry that never changes.

   The vertex floor was 64, and a Far Cry level is mostly small props -- crates,
   rails, pipes, foliage cards -- so the majority of the draw calls in a view
   were taking the copy path.  A mesh below the floor is cheap to keep resident
   precisely because it is small, so the floor buys nothing; 8 still excludes
   the handful-of-vertices quads where a buffer object is pure overhead.

   The budget was 20 MB against a 96 MB vitaGL main-RAM pool with CDRAM on top,
   so it ran out part way through a level and everything after it fell back to
   copying forever.  48 MB leaves comfortable headroom and covers far more of
   the static world.  Say so once when it does run out, since the symptom --
   the frame rate quietly depending on which order geometry happened to load --
   is otherwise invisible. */
static const int kGLBufferMinVerts = 8;
static const unsigned int kGLBufferBudgetBytes = 48u * 1024u * 1024u;
static const unsigned int kGLStaticBufferBudgetBytes = 44u * 1024u * 1024u;
static unsigned int g_nGLBufferBytesUsed = 0;
static GLenum PrimTypeToGL(int prmode);

static unsigned int VitaMappedVertexBytes(const CVertexBuffer *src)
{
	if (!src || src->m_NumVerts <= 0)
		return 0;
	const SBufInfoTable &tbl = gBufInfoTable[src->m_vertexformat];
	unsigned int nPerVertex = 3u * sizeof(float);
	if (tbl.OffsColor)
		nPerVertex += 4u * sizeof(byte);
	if (tbl.OffsTC)
		nPerVertex += 2u * sizeof(float);
	return nPerVertex * (unsigned int)src->m_NumVerts;
}

static void VitaReleaseMappedVertices(CVertexBuffer *src)
{
	if (!src || !src->m_pVitaMappedVertices)
		return;
	/* Geometry can be destroyed while a submitted frame still references it.
	   vitaGL's lazy free is the matching safe lifetime operation. */
	VitaDeferMappedFree(src->m_pVitaMappedVertices);
	g_nGLBufferBytesUsed = (g_nGLBufferBytesUsed > src->m_nVitaMappedVertexBytes) ?
		g_nGLBufferBytesUsed - src->m_nVitaMappedVertexBytes : 0;
	src->m_pVitaMappedVertices = NULL;
	src->m_nVitaMappedVerts = 0;
	src->m_nVitaMappedVertexBytes = 0;
	src->m_bVitaMappedVertexDirty = true;
	if (g_pVitaMappedPointerBuffer == src)
		VitaInvalidateVertexPointerCache();
}

static void VitaReleaseMappedIndices(SVertexStream *inds)
{
	if (!inds || !inds->m_pVitaMappedIndices)
		return;
	VitaDeferMappedFree(inds->m_pVitaMappedIndices);
	g_nGLBufferBytesUsed = (g_nGLBufferBytesUsed > inds->m_nVitaMappedIndexBytes) ?
		g_nGLBufferBytesUsed - inds->m_nVitaMappedIndexBytes : 0;
	inds->m_pVitaMappedIndices = NULL;
	inds->m_nVitaMappedIndexItems = 0;
	inds->m_nVitaMappedIndexBytes = 0;
	inds->m_bVitaMappedIndexDirty = true;
}

/* Upstream vitaGL exposes a lean legacy submission API specifically for
   already GPU-mapped streams.  Its fixed-function implementation binds three
   tightly-packed streams directly and calls sceGxmDraw, avoiding the generic
   glDrawElements VAO setup that costs heavily across Far Cry's hundreds of
   tiny material chunks.  Deinterleave immutable single-texture geometry once;
   dynamic meshes and multi-texture passes stay on the ordinary VBO path. */
static bool EnsureVitaMappedGeometry(CVertexBuffer *src, SVertexStream *inds)
{
	if (!src || !inds || src->m_bDynamic || inds->m_bDynamic ||
		src->m_NumVerts < kGLBufferMinVerts || !src->m_VS[VSF_GENERAL].m_VData ||
		!inds->m_VData || inds->m_nItems <= 0)
		return false;

	/* Do not duplicate an existing GL allocation merely to change submission
	   APIs.  A buffer already seen in a lightmap/detail pass remains there. */
	if ((!src->m_pVitaMappedVertices && src->m_nGLVBO) ||
		(!inds->m_pVitaMappedIndices && inds->m_nGLIBO))
		return false;

	const unsigned int nVertexBytes = VitaMappedVertexBytes(src);
	const unsigned int nIndexBytes = (unsigned int)inds->m_nItems * sizeof(ushort);
	if (!nVertexBytes || !nIndexBytes)
		return false;

	if (src->m_pVitaMappedVertices &&
		(src->m_nVitaMappedVerts != src->m_NumVerts ||
		 src->m_nVitaMappedVertexBytes != nVertexBytes))
		VitaReleaseMappedVertices(src);
	if (inds->m_pVitaMappedIndices &&
		(inds->m_nVitaMappedIndexItems != inds->m_nItems ||
		 inds->m_nVitaMappedIndexBytes != nIndexBytes))
		VitaReleaseMappedIndices(inds);

	const unsigned int nNeeded =
		(src->m_pVitaMappedVertices ? 0u : nVertexBytes) +
		(inds->m_pVitaMappedIndices ? 0u : nIndexBytes);
	if (g_nGLBufferBytesUsed + nNeeded > kGLStaticBufferBudgetBytes)
	{
		static bool s_bReportedMappedBudgetFull = false;
		if (!s_bReportedMappedBudgetFull && iLog)
		{
			s_bReportedMappedBudgetFull = true;
			iLog->LogToFile("\001[VITA][FASTDRAW] mapped buffer budget exhausted at %u KB",
				g_nGLBufferBytesUsed / 1024u);
		}
		return false;
	}

	/* The byte budget above only counts what this path itself has taken.  It
	   says nothing about what textures have already claimed, and speeding up a
	   draw is never worth the texture allocation that would fail behind it. */
	const unsigned int nPoolFree = (unsigned int)vglMemFree(VGL_MEM_VRAM);
	if (nPoolFree < nNeeded ||
		nPoolFree - nNeeded < kVitaGpuPoolTextureReserveBytes)
	{
		static bool s_bReportedMappedPoolLow = false;
		if (!s_bReportedMappedPoolLow && iLog)
		{
			s_bReportedMappedPoolLow = true;
			iLog->LogToFile("\001[VITA][FASTDRAW] pool free %u KB is at the texture reserve; staying on the client path",
				nPoolFree / 1024u);
		}
		return false;
	}

	const bool bNewVertices = src->m_pVitaMappedVertices == NULL;
	if (bNewVertices)
	{
		src->m_pVitaMappedVertices = vglAlloc(nVertexBytes, VGL_MEM_RAM);
		if (!src->m_pVitaMappedVertices)
			return false;
		src->m_nVitaMappedVerts = src->m_NumVerts;
		src->m_nVitaMappedVertexBytes = nVertexBytes;
		src->m_bVitaMappedVertexDirty = true;
		g_nGLBufferBytesUsed += nVertexBytes;
	}

	const bool bNewIndices = inds->m_pVitaMappedIndices == NULL;
	if (bNewIndices)
	{
		inds->m_pVitaMappedIndices = vglAlloc(nIndexBytes, VGL_MEM_RAM);
		if (!inds->m_pVitaMappedIndices)
		{
			if (bNewVertices)
				VitaReleaseMappedVertices(src);
			return false;
		}
		inds->m_nVitaMappedIndexItems = inds->m_nItems;
		inds->m_nVitaMappedIndexBytes = nIndexBytes;
		inds->m_bVitaMappedIndexDirty = true;
		g_nGLBufferBytesUsed += nIndexBytes;
	}

	if (src->m_bVitaMappedVertexDirty)
	{
		const byte *pSrc = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
		byte *pDst = (byte *)src->m_pVitaMappedVertices;
		const SBufInfoTable &tbl = gBufInfoTable[src->m_vertexformat];
		const int nStride = m_VertexSize[src->m_vertexformat];
		byte *pPositions = pDst;
		byte *pColors = pPositions + (size_t)src->m_NumVerts * 3 * sizeof(float);
		byte *pTexCoords = pColors + (tbl.OffsColor ? (size_t)src->m_NumVerts * 4 : 0);
		for (int i = 0; i < src->m_NumVerts; ++i)
		{
			const byte *pVertex = pSrc + (size_t)i * nStride;
			memcpy(pPositions + (size_t)i * 3 * sizeof(float), pVertex, 3 * sizeof(float));
			if (tbl.OffsColor)
				memcpy(pColors + (size_t)i * 4, pVertex + tbl.OffsColor, 4);
			if (tbl.OffsTC)
				memcpy(pTexCoords + (size_t)i * 2 * sizeof(float), pVertex + tbl.OffsTC, 2 * sizeof(float));
		}
		src->m_bVitaMappedVertexDirty = false;
	}
	if (inds->m_bVitaMappedIndexDirty)
	{
		memcpy(inds->m_pVitaMappedIndices, inds->m_VData, nIndexBytes);
		inds->m_bVitaMappedIndexDirty = false;
	}
	return true;
}

static void DrawVitaMappedGeometry(CVertexBuffer *src, SVertexStream *inds,
	int numindices, int offsindex, int prmode)
{
	const SBufInfoTable &tbl = gBufInfoTable[src->m_vertexformat];
	const bool bUseColor = tbl.OffsColor && !g_bUntexturedSurface &&
		!VitaVertexColoursAreUnlit(src) && !g_bSurfaceTintActive &&
		!g_bVitaLightMapActive;
	const bool bUseTexCoord = tbl.OffsTC != 0;
	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);
	VitaSetClientArrayState(GL_COLOR_ARRAY, bUseColor);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, bUseTexCoord);
	if (!bUseColor)
	{
		if (g_bSurfaceTintActive)
			VitaSetConstantColor(g_arrSurfaceTint[0], g_arrSurfaceTint[1], g_arrSurfaceTint[2], g_arrSurfaceTint[3]);
		else
			VitaSetConstantColor(g_arrVitaMaterialColor[0], g_arrVitaMaterialColor[1], g_arrVitaMaterialColor[2], g_arrVitaMaterialColor[3]);
	}

	if (g_pVitaMappedPointerBuffer != src ||
		g_nVitaMappedPointerUsesColor != (bUseColor ? 1 : 0) ||
		g_nVitaMappedPointerUsesTexCoord != (bUseTexCoord ? 1 : 0))
	{
		const byte *pBase = (const byte *)src->m_pVitaMappedVertices;
		const byte *pColors = pBase + (size_t)src->m_NumVerts * 3 * sizeof(float);
		const byte *pTexCoords = pColors + (tbl.OffsColor ? (size_t)src->m_NumVerts * 4 : 0);
		vglVertexPointerMapped(3, pBase);
		if (bUseColor)
			vglColorPointerMapped(GL_UNSIGNED_BYTE, pColors);
		if (bUseTexCoord)
			vglTexCoordPointerMapped(pTexCoords);
		/* Force the following ordinary GL draw to restore its descriptors. */
		g_nVitaPointerArrayBuffer = 0xFFFFFFFFu;
		g_pVitaPointerBase = (const byte *)(size_t)~0u;
		g_nVitaPointerFormat = -1;
		g_pVitaMappedPointerBuffer = src;
		g_nVitaMappedPointerUsesColor = bUseColor ? 1 : 0;
		g_nVitaMappedPointerUsesTexCoord = bUseTexCoord ? 1 : 0;
		VITA_PERF_INCREMENT(g_nVitaPointerConfigurations);
	}
	vglIndexPointerMapped((const ushort *)inds->m_pVitaMappedIndices + offsindex);
		/* Use the pinned archive's two-argument legacy API, not the unrelated
		   SDK header's older custom-shader variant. Matrices are GL state. */
		vglDrawObjects(PrimTypeToGL(prmode), numindices);
}

static bool EnsureGLVertexBuffer(CVertexBuffer *src)
{
	if (!src || src->m_NumVerts < kGLBufferMinVerts)
		return false;
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	if (!pData)
		return false;
	const unsigned int nBytes = (unsigned int)(m_VertexSize[src->m_vertexformat] * src->m_NumVerts);
	const bool bDynamic = src->m_bDynamic != 0;
	if (src->m_nGLVBO && !src->m_bGLDirty && src->m_nGLVBOVerts == src->m_NumVerts)
		return true;
	if (!src->m_nGLVBO)
	{
		/* Do not let level geometry consume the last four MiB of the VBO
		   allowance.  Characters are discovered later and need a small guaranteed
		   pool or the first-person weapon falls back to per-chunk client copies. */
		const unsigned int nBudget = bDynamic ? kGLBufferBudgetBytes : kGLStaticBufferBudgetBytes;
		if (g_nGLBufferBytesUsed + nBytes > nBudget)
		{
			static bool s_bReportedBudgetFull = false;
			if (!s_bReportedBudgetFull && iLog)
			{
				s_bReportedBudgetFull = true;
				iLog->LogToFile("\001[VITA][PERF] GPU buffer budget exhausted at %u KB -- "
					"remaining geometry is copied from client memory every draw",
					g_nGLBufferBytesUsed / 1024u);
			}
			return false;
		}
		/* The byte budget only tracks this path's own allocations; it cannot see
		   what textures have taken.  Level geometry no longer welds shared
		   vertices for lightmapped brushes, so a level can now claim far more of
		   the pool than it used to and starve the texture uploads behind it --
		   and a starved texture upload is a NULL memcpy inside vitaGL, not a
		   recoverable error.  Copying this buffer from client memory every draw
		   is slower; it is not a crash. */
		const unsigned int nPoolFree = (unsigned int)vglMemFree(VGL_MEM_VRAM);
		if (nPoolFree < nBytes ||
			nPoolFree - nBytes < kVitaGpuPoolTextureReserveBytes)
		{
			static bool s_bReportedPoolLow = false;
			if (!s_bReportedPoolLow && iLog)
			{
				s_bReportedPoolLow = true;
				iLog->LogToFile("\001[VITA][PERF] pool free %u KB is at the texture reserve -- "
					"remaining geometry is copied from client memory every draw",
					nPoolFree / 1024u);
			}
			return false;
		}
		GLuint nName = 0;
		glGenBuffers(1, &nName);
		if (!nName)
			return false;
		src->m_nGLVBO = nName;
		g_nGLBufferBytesUsed += nBytes;
	}
	VitaBindArrayBuffer(src->m_nGLVBO);
	/* CryEngine's dynamic leaf buffers are principally skinned characters.  A
	   character owns one vertex array but draws it once per material chunk.  The
	   old Vita path rejected every dynamic buffer, so vitaGL copied that same
	   complete array into GPU-visible memory again for every chunk -- the local
	   weapon alone turns roughly 140 draws per frame into client-copy draws.

	   Upload a changed dynamic array once, then reuse that VBO for all remaining
	   chunks. Hardware timing shows these uploads take only tens of microseconds,
	   so keep one buffer per mesh instead of multiplying its memory footprint. */
#if defined(VITA_VALIDATE_BUFFER_UPLOADS)
	if (!bDynamic)
		while (glGetError() != GL_NO_ERROR) {}	// validation builds only: this can drain queued GPU work
#endif
#if defined(VITA_PERF_TELEMETRY)
	const SceUInt64 nUploadStartUs = bDynamic ? sceKernelGetProcessTimeWide() : 0;
#endif
	/* Keep dynamic character storage stable.  Reissuing glBufferData for every
	   skinned mesh every frame asks vitaGL/GXM to orphan and reallocate the
	   backing block; under several enemies that caused allocator churn and, on
	   a refused allocation, transient missing body parts.  Allocate once, then
	   replace the contents in place. */
	if (bDynamic && src->m_nGLVBOVerts == src->m_NumVerts)
		glBufferSubData(GL_ARRAY_BUFFER, 0, nBytes, pData);
	else
		glBufferData(GL_ARRAY_BUFFER, nBytes, pData,
			bDynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
#if defined(VITA_PERF_TELEMETRY)
	if (bDynamic)
	{
		VITA_PERF_INCREMENT(g_nVitaDynamicUploadCount);
		VITA_PERF_ADD(g_nVitaDynamicUploadBytes, nBytes);
		VITA_PERF_ADD(g_nVitaDynamicUploadUs, sceKernelGetProcessTimeWide() - nUploadStartUs);
	}
#endif
	/* Our own byte counter only knows what we asked for; the driver has its own
	   pools and can refuse before that ceiling is reached.  An upload that fails
	   leaves the buffer object empty, and drawing from an empty buffer renders
	   nothing at all -- geometry silently disappearing is far worse than the
	   copy path this exists to avoid.  Give the memory back and fall back. */
#if defined(VITA_VALIDATE_BUFFER_UPLOADS)
	if (!bDynamic && glGetError() != GL_NO_ERROR)
	{
		GLuint nDead = (GLuint)src->m_nGLVBO;
		VitaBindArrayBuffer(0);
		glDeleteBuffers(1, &nDead);
		src->m_nGLVBO = 0;
		src->m_nGLVBOVerts = 0;
		if (g_nGLBufferBytesUsed >= nBytes)
			g_nGLBufferBytesUsed -= nBytes;
		static bool s_bReportedUploadFail = false;
		if (!s_bReportedUploadFail && iLog)
		{
			s_bReportedUploadFail = true;
			iLog->LogToFile("\001[VITA][PERF] GPU buffer upload refused by the driver at %u KB -- "
				"falling back to client memory for the rest", g_nGLBufferBytesUsed / 1024u);
		}
		return false;
	}
#endif
	VitaBindArrayBuffer(0);
	src->m_nGLVBOVerts = src->m_NumVerts;
	src->m_bGLDirty = false;
	return true;
}

static bool EnsureGLIndexBuffer(SVertexStream *inds)
{
	if (!inds || !inds->m_VData || inds->m_nItems <= 0)
		return false;
	const unsigned int nBytes = (unsigned int)(inds->m_nItems * sizeof(ushort));
	const bool bDynamic = inds->m_bDynamic;
	if (inds->m_nGLIBO && !inds->m_bGLDirty && inds->m_nGLIBOItems == inds->m_nItems)
		return true;
	if (!inds->m_nGLIBO)
	{
		const unsigned int nBudget = bDynamic ? kGLBufferBudgetBytes : kGLStaticBufferBudgetBytes;
		if (g_nGLBufferBytesUsed + nBytes > nBudget)
			return false;
		/* Same reserve as the vertex path: indices are worth even less than
		   vertices here, and terrain now uploads three times as many of them
		   since sectors became triangle lists. */
		const unsigned int nPoolFree = (unsigned int)vglMemFree(VGL_MEM_VRAM);
		if (nPoolFree < nBytes ||
			nPoolFree - nBytes < kVitaGpuPoolTextureReserveBytes)
			return false;
		GLuint nName = 0;
		glGenBuffers(1, &nName);
		if (!nName)
			return false;
		inds->m_nGLIBO = nName;
		g_nGLBufferBytesUsed += nBytes;
	}
	VitaBindElementBuffer(inds->m_nGLIBO);
	/* glGetError is a synchronous pipeline query in vitaGL.  Index buffers are
	   first promoted when they become visible, so polling around every upload
	   converted ordinary streaming into camera-turn and weapon-pickup hitches.
	   Keep it available for explicit validation builds, not normal gameplay. */
#if defined(VITA_VALIDATE_BUFFER_UPLOADS)
	while (glGetError() != GL_NO_ERROR) {}
#endif
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, nBytes, inds->m_VData,
		bDynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
	// Same as the vertex side: an empty index buffer draws nothing.
#if defined(VITA_VALIDATE_BUFFER_UPLOADS)
	if (glGetError() != GL_NO_ERROR)
	{
		GLuint nDead = (GLuint)inds->m_nGLIBO;
		VitaBindElementBuffer(0);
		glDeleteBuffers(1, &nDead);
		inds->m_nGLIBO = 0;
		inds->m_nGLIBOItems = 0;
		if (g_nGLBufferBytesUsed >= nBytes)
			g_nGLBufferBytesUsed -= nBytes;
		return false;
	}
#endif
	VitaBindElementBuffer(0);
	inds->m_nGLIBOItems = inds->m_nItems;
	inds->m_bGLDirty = false;
	return true;
}

static GLenum PrimTypeToGL(int prmode)
{
	switch (prmode)
	{
		case R_PRIMV_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
		case R_PRIMV_MULTI_STRIPS:    return GL_TRIANGLE_STRIP;
		case R_PRIMV_TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
		case R_PRIMV_QUADS:          return GL_TRIANGLE_FAN; // quads are always 4 verts; a fan draws the same single quad
		default:                     return GL_TRIANGLES;
	}
}
#endif

void CVitaRenderer::DrawTriStrip(CVertexBuffer * src, int vert_num)
{
#if defined(LINUX)
	if (!src || vert_num <= 0 || vert_num > src->m_NumVerts ||
		src->m_vertexformat < 0 || src->m_vertexformat >= VERTEX_FORMAT_NUMS)
		return;
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	if (!pData)
		return;
	SetupVertexArraysForFormat(pData, src->m_vertexformat,
		VitaVertexColoursAreUnlit(src));
	VITA_DRAW_INCREMENT();
	VITA_PERF_ADD(g_nVitaDrawIndices, (unsigned)vert_num);
	VITA_PERF_INCREMENT(g_nVitaClientDraws);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, vert_num);
	TeardownVertexArrays();
#endif
}
void * CVitaRenderer::GetDynVBPtr(int nVerts, int & nOffs, int Pool)
{
	/* Vita: real backing store, see VitaRenderer.h. Wrap to the start
	   instead of failing -- callers (CFFont) don't check for null and
	   this is a small ring buffer refilled every frame, not persistent
	   geometry, so a wrap just means an old, already-drawn string's
	   vertices get overwritten before its next use. */
	if (nVerts <= 0 || nVerts > DYNVB_CAPACITY)
		return 0;
	if (m_nDynVBCursor + nVerts > DYNVB_CAPACITY)
		m_nDynVBCursor = 0;
	nOffs = m_nDynVBCursor;
	void *p = &m_DynVB[m_nDynVBCursor];
	m_nDynVBCursor += nVerts;
	return p;
}

void CVitaRenderer::DrawDynVB(int nOffs, int Pool, int nVerts)
{
#if defined(LINUX)
	if (nOffs < 0 || nVerts <= 0 || nVerts > DYNVB_CAPACITY ||
		nOffs > DYNVB_CAPACITY - nVerts)
		return;
	struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pV = &m_DynVB[nOffs];
	if (!g_bVitaLightMapActive)
		VitaDisableLightMapStage();
	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	VitaInvalidateVertexPointerCache();
	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);
	VitaSetClientArrayState(GL_COLOR_ARRAY, true);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, true);
	glVertexPointer(3, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->xyz);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->color);
	glTexCoordPointer(2, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->st);
	VITA_DRAW_INCREMENT();
	VITA_PERF_ADD(g_nVitaDrawIndices, (unsigned)nVerts);
	VITA_PERF_INCREMENT(g_nVitaDynamicDraws);
	glDrawArrays(GL_TRIANGLES, 0, nVerts);
	TeardownVertexArrays();
#endif
}

void CVitaRenderer::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F * pBuf, ushort * pInds, int nVerts, int nInds, int nPrimType)
{
#if defined(LINUX)
	if (!pBuf || nVerts <= 0)
		return;
	SetupVertexArraysForFormat((const byte *)pBuf, VERTEX_FORMAT_P3F_COL4UB_TEX2F);
	VitaBindElementBuffer(0);
	VITA_DRAW_INCREMENT();
	VITA_PERF_ADD(g_nVitaDrawIndices, (unsigned)((pInds && nInds > 0) ? nInds : nVerts));
	VITA_PERF_INCREMENT(g_nVitaDynamicDraws);
	if (pInds && nInds > 0)
		glDrawElements(PrimTypeToGL(nPrimType), nInds, GL_UNSIGNED_SHORT, pInds);
	else
		glDrawArrays(PrimTypeToGL(nPrimType), 0, nVerts);
	TeardownVertexArrays();
#endif
}
void CVitaRenderer::SetFenceCompleted(CVertexBuffer * buffer) { }
/* Vita: real system-memory vertex buffers -- same shape as the reference
   (uncompiled-in) RenderDll/XRenderNULL/NULL_VertBuffer.cpp, which this
   whole file otherwise follows: plain new[]/memcpy, no HW/video-memory
   buffer, consumed directly by DrawBuffer/DrawTriStrip below via
   glVertexPointer et al against the raw client-side bytes. */
CVertexBuffer * CVitaRenderer::CreateBuffer(int vertexcount, int vertexformat, const char * szSource, bool bDynamic)
{
	if (vertexcount < 0 || vertexformat < 0 || vertexformat >= VERTEX_FORMAT_NUMS)
		return NULL;
	CVertexBuffer *vb = new CVertexBuffer;
	vb->m_bDynamic = bDynamic;
	vb->m_vertexformat = vertexformat;
	vb->m_NumVerts = vertexcount;
	const size_t nSize = (size_t)m_VertexSize[vertexformat] * (size_t)vertexcount;
	vb->m_VS[VSF_GENERAL].m_VData = nSize > 0 ? new byte[nSize] : NULL;
	return vb;
}
void CVitaRenderer::ReleaseBuffer(CVertexBuffer * bufptr)
{
	if (!bufptr)
		return;
#if defined(LINUX)
	VitaReleaseMappedVertices(bufptr);
	if (bufptr->m_nGLVBO)
	{
		GLuint nName = bufptr->m_nGLVBO;
		if (g_nVitaBoundArrayBuffer == nName)
			VitaBindArrayBuffer(0);
		glDeleteBuffers(1, &nName);
		const unsigned int nBytes = (unsigned int)(m_VertexSize[bufptr->m_vertexformat] * bufptr->m_nGLVBOVerts);
		g_nGLBufferBytesUsed = (g_nGLBufferBytesUsed > nBytes) ? g_nGLBufferBytesUsed - nBytes : 0;
		bufptr->m_nGLVBO = 0;
		bufptr->m_nGLVBOVerts = 0;
	}
#endif
	delete [] (byte*)bufptr->m_VS[VSF_GENERAL].m_VData;
	delete [] (byte*)bufptr->m_VS[VSF_TANGENTS].m_VData;
	delete bufptr;
}
void CVitaRenderer::UpdateBuffer(CVertexBuffer * dest, const void * src, int vertexcount, bool bUnLock, int nOffs, int Type)
{
	if (!dest || !src || vertexcount <= 0 || nOffs < 0 || Type < 0 || Type >= VSF_NUM ||
		dest->m_vertexformat < 0 || dest->m_vertexformat >= VERTEX_FORMAT_NUMS ||
		nOffs > dest->m_NumVerts || vertexcount > dest->m_NumVerts - nOffs)
		return;
	byte *pDst = (byte*)dest->m_VS[Type].m_VData;
	if (!pDst)
		return;
#if defined(LINUX)
	// CPU-side contents changed: any GPU copy is now stale.
	dest->m_bGLDirty = true;
	dest->m_bVitaMappedVertexDirty = true;
	if (Type == VSF_GENERAL)
		dest->m_nVitaUnlitColours = -1;
#endif
	if (Type == VSF_GENERAL)
	{
		int nStride = m_VertexSize[dest->m_vertexformat];
		memcpy(pDst + nStride * nOffs, src, nStride * vertexcount);
	}
	else if (Type == VSF_TANGENTS)
	{
		memcpy(pDst + sizeof(SPipTangents) * nOffs, src, sizeof(SPipTangents) * vertexcount);
	}
}

void CVitaRenderer::CreateIndexBuffer(SVertexStream * dest, const void * src, int indexcount)
{
	if (!dest)
		return;
#if defined(LINUX)
	dest->m_bGLDirty = true;
	dest->m_bVitaMappedIndexDirty = true;
#endif
	delete [] (ushort*)dest->m_VData;
	dest->m_VData = NULL;
	dest->m_nItems = 0;
	if (indexcount > 0)
	{
		dest->m_VData = new ushort[indexcount];
		dest->m_nItems = indexcount;
		if (src)
			memcpy(dest->m_VData, src, indexcount * sizeof(ushort));
	}
}
void CVitaRenderer::UpdateIndexBuffer(SVertexStream * dest, const void * src, int indexcount, bool bUnLock)
{
	if (!dest || !src || indexcount <= 0)
		return;
	/* Terrain rebuilds its logical strip/chunk description every frame even
	   when no neighbour or LOD changed.  Avoid invalidating and re-uploading an
	   identical static index buffer to vitaGL on every visible sector. */
	if (dest->m_VData && dest->m_nItems == indexcount &&
		memcmp(dest->m_VData, src, (size_t)indexcount * sizeof(ushort)) == 0)
		return;
#if defined(LINUX)
	dest->m_bGLDirty = true;
	dest->m_bVitaMappedIndexDirty = true;
#endif
	if (!dest->m_VData || dest->m_nItems < indexcount)
	{
		delete [] (ushort*)dest->m_VData;
		dest->m_VData = new ushort[indexcount];
		dest->m_nItems = indexcount;
	}
	memcpy(dest->m_VData, src, indexcount * sizeof(ushort));
}
void CVitaRenderer::ReleaseIndexBuffer(SVertexStream * dest)
{
	if (!dest)
		return;
#if defined(LINUX)
	VitaReleaseMappedIndices(dest);
	if (dest->m_nGLIBO)
	{
		GLuint nName = dest->m_nGLIBO;
		if (g_nVitaBoundElementBuffer == nName)
			VitaBindElementBuffer(0);
		glDeleteBuffers(1, &nName);
		const unsigned int nBytes = (unsigned int)(dest->m_nGLIBOItems * sizeof(ushort));
		g_nGLBufferBytesUsed = (g_nGLBufferBytesUsed > nBytes) ? g_nGLBufferBytesUsed - nBytes : 0;
		dest->m_nGLIBO = 0;
		dest->m_nGLIBOItems = 0;
	}
	dest->m_bGLDirty = true;
#endif
	delete [] (ushort*)dest->m_VData;
	dest->Reset();
}

/* Vita: real indexed-geometry draw, the payoff of everything else in this
   block -- same glVertexPointer/glColorPointer/glTexCoordPointer +
   glDrawElements shape as Draw2dImage (see its own comment) applied to the
   already-set-up GL_MODELVIEW/GL_PROJECTION from SetCamera/PushMatrix/etc,
   instead of Draw2dImage's own glOrtho. `mi` (shader/material) is ignored
   for this pass -- see this file's header comment: no shader pipeline yet,
   flat vertex-color/texture-modulate only. */
void CVitaRenderer::DrawBuffer(CVertexBuffer * src, SVertexStream * indicies, int numindices, int offsindex, int prmode, int vert_start, int vert_stop, CMatInfo * mi)
{
#if defined(LINUX)
	if (!src || !indicies || src->m_vertexformat < 0 ||
		src->m_vertexformat >= VERTEX_FORMAT_NUMS || src->m_NumVerts <= 0)
	{
		return;
	}
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	const ushort *pInds = (const ushort *)indicies->m_VData;
	if (!pData || !pInds || numindices <= 0 || offsindex < 0 ||
		offsindex > indicies->m_nItems || numindices > indicies->m_nItems - offsindex)
	{
		return;
	}

	// Vita: this fired unconditionally on every single draw call -- once
	// real gameplay started issuing hundreds of small unbatched draws per
	// frame (grass/detail objects, HUD text), that meant thousands of
	// synchronous sceClibPrintf log lines per second, which is real,
	// self-inflicted I/O overhead heavy enough to destabilize a debug run
	// (a 2.5s run produced 94K log lines here). The draw path itself is
	// long since confirmed correct (glGetError()==0 verified extensively
	// already); throttle this to an occasional sample instead of every call.
	/* The sampled draw trace is gone.  Throttling it to every 512th call still
	   fires several times a second at a normal draw count, sceClibPrintf is a
	   synchronous debug write, and the companion trace after glDrawElements
	   called glGetError() -- which forces the driver to finish outstanding work
	   before it can answer.  A pipeline stall several times a second is a real
	   cost, and the draw path it was watching is long since confirmed. */
	const bool bTrace = false;
	/* Prefer real buffer objects; fall back to the client pointers for dynamic
	   or small meshes, and for anything past the upload budget. */
	/* Only formats with no coordinates of their own need them synthesised, and
	   only the terrain hands over texgen offsets to synthesise them from. */
	const float *pGeneratedUVs = NULL;
	if (!gBufInfoTable[src->m_vertexformat].OffsTC)
	{
		/* Only chunk 0 of a terrain sector owns a render element -- SetChunk
		   creates one for nMatID 0 and the engine hangs the texgen offsets off
		   whichever chunks have one -- but all ~34 strips of the sector share
		   the same vertex buffer and the same mapping.  Reading the offsets
		   only from the chunk being drawn textured the first strip and left the
		   other thirty-three sampling the constant default coordinate, so the
		   sector still came out almost entirely flat.  Fall back to the offsets
		   the leaf buffer published for the whole draw. */
		const float *pTexGen = (mi && mi->pRE) ? mi->pRE->m_CustomData : NULL;
		if (!pTexGen)
			pTexGen = g_pVitaTerrainTexGen;
		pGeneratedUVs = VitaBuildTerrainTexCoords(pData, src->m_vertexformat,
			src->m_NumVerts, pTexGen);
	}

	VITA_DRAW_INCREMENT();
	VITA_PERF_ADD(g_nVitaDrawIndices, (unsigned)numindices);
	/* The mapped legacy path supports only texture unit 0.  Detect every case
	   requiring secondary/generated coordinates before allocating anything so
	   lightmaps and detailed terrain stay byte-for-byte on the current path. */
	const bool bTerrainDetailRequested =
		g_nVitaTerrainDetailTexture > 0 && gBufInfoTable[src->m_vertexformat].OffsTC;
	/* Do not re-enable EnsureVitaMappedGeometry here.  Real Vita captures from
	   2026-08-13 prove vglDrawObjects corrupts the static index/attribute state:
	   world triangles stretch across the frame and render time rises above
	   100 ms.  This is intentionally compile-time disabled so a persisted cfg
	   cannot silently turn the broken path back on.  The ordinary VBO/client
	   path below is the last hardware-proven renderer. */

	/* Repair static lighting before the first VBO upload so the resident copy and
	   the client fallback see identical colour data. */
	VitaPrepareStaticVertexColours(src);
	const bool bGPUResident = EnsureGLVertexBuffer(src) && EnsureGLIndexBuffer(indicies);
	if (bGPUResident)
	{
		VITA_PERF_INCREMENT(g_nVitaVBODraws);
		VitaBindArrayBuffer(src->m_nGLVBO);
		VitaBindElementBuffer(indicies->m_nGLIBO);
		SetupVertexArraysForFormat(NULL, src->m_vertexformat,
			VitaVertexColoursAreUnlit(src),
			(GLuint)src->m_nGLVBO);
		VitaApplyGeneratedTexCoords(pGeneratedUVs);
		const bool bTerrainDetail = VitaBeginTerrainDetail(NULL, src->m_vertexformat);
		glDrawElements(PrimTypeToGL(prmode), numindices, GL_UNSIGNED_SHORT,
			(const void *)(size_t)(offsindex * sizeof(ushort)));
		VitaEndTerrainDetail(bTerrainDetail);
		TeardownVertexArrays();
		/* Keep resident buffers bound. Client-pointer paths explicitly select
		   buffer zero before publishing their pointers. */
		return;
	}
	VITA_PERF_INCREMENT(g_nVitaClientDraws);

	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	SetupVertexArraysForFormat(pData, src->m_vertexformat,
		VitaVertexColoursAreUnlit(src));
	VitaApplyGeneratedTexCoords(pGeneratedUVs);
	const bool bTerrainDetail = VitaBeginTerrainDetail(pData, src->m_vertexformat);
	glDrawElements(PrimTypeToGL(prmode), numindices, GL_UNSIGNED_SHORT, pInds + offsindex);
	VitaEndTerrainDetail(bTerrainDetail);
	TeardownVertexArrays();
#endif
}
void CVitaRenderer::CheckError(const char * comment) { }
void CVitaRenderer::Draw3dBBox(const Vec3 & mins, const Vec3 & maxs, int nPrimType) { }
bool CVitaRenderer::SetGammaDelta(const float fGamma) { return false; }
bool CVitaRenderer::ChangeDisplay(unsigned int width, unsigned int height, unsigned int cbpp) { return false; }
void CVitaRenderer::ChangeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
	SetViewport((int)x, (int)y, (int)width, (int)height);
	m_nWidth = (int)width;
	m_nHeight = (int)height;
}
bool CVitaRenderer::SaveTga(unsigned char * sourcedata, int sourceformat, int w, int h, const char * filename, bool flip) { return false; }
/* Vita: real texture bind -- tnum is a GL texture id directly, same
   convention FontSetTexture(int,int) above already uses (both are fed IDs
   handed out by FontCreateTexture/EF_LoadTexture, both real glGenTextures
   results). */
void CVitaRenderer::SetTexture(int tnum, ETexType Type)
{
#if defined(LINUX)
	if (tnum > 0)
	{
		g_bUntexturedSurface = false;
		if (g_nTexture2DEnabled != 1)
		{
			glEnable(GL_TEXTURE_2D);
			g_nTexture2DEnabled = 1;
		}
		if ((GLuint)tnum != g_nBoundTexture0)
		{
			glBindTexture(GL_TEXTURE_2D, (GLuint)tnum);
			if (!g_bVitaCustomColorOp)
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			g_nBoundTexture0 = (GLuint)tnum;
		}
	}
	else
	{
		g_bUntexturedSurface = true;
		if (g_nTexture2DEnabled != 0)
		{
			glDisable(GL_TEXTURE_2D);
			g_nTexture2DEnabled = 0;
		}
	}
#endif
}
void CVitaRenderer::SetWhiteTexture()
{
#if defined(LINUX)
	/* The glColor4f here never actually took effect: DrawBuffer re-enables
	   GL_COLOR_ARRAY for every vertex format that carries colours, and the
	   array wins over the current colour.  So a surface that fell back to
	   "white" was really drawn in its raw vertex colour -- white where those
	   happened to be white, and solid black where they were dark, which is
	   where the black slabs in the world came from.  Flag it so the draw path
	   drops the colour array and this white is the colour that is actually
	   used. */
	g_bUntexturedSurface = true;
	if (g_nTexture2DEnabled != 0)
	{
		glDisable(GL_TEXTURE_2D);
		g_nTexture2DEnabled = 0;
	}
	/* Mid grey rather than pure white.  A surface that lands here has no
	   texture, and full white makes it the brightest thing on screen -- the
	   blown-out slabs showing through openings, which read as a lighting fault
	   rather than as missing art.  Grey sits in the range real surfaces occupy,
	   so a missing texture looks like untextured geometry instead of a light
	   source.  Exposed as a cvar so it can be judged on the device without a
	   rebuild. */
	static ICVar *s_pUntexturedLevel = NULL;
	if (!s_pUntexturedLevel && iConsole)
		s_pUntexturedLevel = iConsole->CreateVariable("r_vita_untextured_level", "0.6", 0,
			"Grey level drawn for surfaces with no texture (1 = the old pure white)");
	const float fLevel = s_pUntexturedLevel ? s_pUntexturedLevel->GetFVal() : 0.6f;
	VitaSetConstantColor(fLevel, fLevel, fLevel, 1);
#endif
}

#if defined(LINUX)
static GLenum GSBlendSrcToGL(int nState)
{
	switch (nState & GS_BLSRC_MASK)
	{
		case GS_BLSRC_ZERO:              return GL_ZERO;
		case GS_BLSRC_ONE:                return GL_ONE;
		case GS_BLSRC_DSTCOL:             return GL_DST_COLOR;
		case GS_BLSRC_ONEMINUSDSTCOL:     return GL_ONE_MINUS_DST_COLOR;
		case GS_BLSRC_SRCALPHA:           return GL_SRC_ALPHA;
		case GS_BLSRC_ONEMINUSSRCALPHA:   return GL_ONE_MINUS_SRC_ALPHA;
		case GS_BLSRC_DSTALPHA:           return GL_DST_ALPHA;
		case GS_BLSRC_ONEMINUSDSTALPHA:   return GL_ONE_MINUS_DST_ALPHA;
		case GS_BLSRC_ALPHASATURATE:      return GL_SRC_ALPHA_SATURATE;
		case GS_BLSRC_SRCCOL:             return GL_SRC_COLOR;
		default:                          return GL_ONE;
	}
}
static GLenum GSBlendDstToGL(int nState)
{
	switch (nState & GS_BLDST_MASK)
	{
		case GS_BLDST_ZERO:              return GL_ZERO;
		case GS_BLDST_ONE:                return GL_ONE;
		case GS_BLDST_SRCCOL:             return GL_SRC_COLOR;
		case GS_BLDST_ONEMINUSSRCCOL:     return GL_ONE_MINUS_SRC_COLOR;
		case GS_BLDST_SRCALPHA:           return GL_SRC_ALPHA;
		case GS_BLDST_ONEMINUSSRCALPHA:   return GL_ONE_MINUS_SRC_ALPHA;
		case GS_BLDST_DSTALPHA:           return GL_DST_ALPHA;
		case GS_BLDST_ONEMINUSDSTALPHA:   return GL_ONE_MINUS_DST_ALPHA;
		default:                          return GL_ZERO;
	}
}
#endif

/* Vita: real (partial) render-state mapping -- covers blend, depth
   test/write and alpha test, the flags terrain/basic opaque geometry
   actually sets (see CryCommon/IRenderer.h's GS_* for the full set this
   doesn't cover yet, e.g. stencil/texgen -- honest scope cut, not silently
   ignored: SetTexgen/EnableFog etc remain their own separate,
   still-stubbed entry points). */
void CVitaRenderer::SetState(int State)
{
#if defined(LINUX)
	if (State == g_nCachedRenderState)
		return;
	g_nCachedRenderState = State;

	int nBlend = State & GS_BLEND_MASK;
	if (nBlend)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GSBlendSrcToGL(State), GSBlendDstToGL(State));
	}
	else
	{
		glDisable(GL_BLEND);
	}

	glDepthMask((State & GS_DEPTHWRITE) ? GL_TRUE : GL_FALSE);

	if (State & GS_NODEPTHTEST)
	{
		glDisable(GL_DEPTH_TEST);
	}
	else
	{
		glEnable(GL_DEPTH_TEST);
		glDepthFunc((State & GS_DEPTHFUNC_EQUAL) ? GL_EQUAL : (State & GS_DEPTHFUNC_GREAT) ? GL_GREATER : GL_LEQUAL);
	}

	if (State & GS_ALPHATEST_MASK)
	{
		glEnable(GL_ALPHA_TEST);
		if (State & GS_ALPHATEST_GREATER0)      glAlphaFunc(GL_GREATER, 0.0f);
		else if (State & GS_ALPHATEST_LESS128)  glAlphaFunc(GL_LESS, 0.5f);
		else if (State & GS_ALPHATEST_GEQUAL128) glAlphaFunc(GL_GEQUAL, 0.5f);
		else if (State & GS_ALPHATEST_GEQUAL64)  glAlphaFunc(GL_GEQUAL, 0.25f);
	}
	else
	{
		glDisable(GL_ALPHA_TEST);
	}

	/* The retail radar builds its circular mask in the framebuffer's alpha
	   channel, then blends the rotating compass through destination alpha.
	   Ignoring these two write masks paints the mask's black RGB square onto the
	   HUD and leaves destination alpha undefined -- exactly the broken locator
	   seen on hardware.  Every ordinary state restores all four channels. */
	if (State & GS_COLMASKONLYALPHA)
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	else if (State & GS_COLMASKONLYRGB)
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	else
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

#endif
}
void CVitaRenderer::WriteXY(CXFont * currfont, int x, int y, float xscale, float yscale, float r, float g, float b, float a, const char * message, ...)
{
	if (!message)
		return;
	char text[4096];
	va_list args;
	va_start(args, message);
	vsnprintf(text, sizeof(text), message, args);
	va_end(args);
	text[sizeof(text)-1] = 0;

	SDrawTextInfo info;
	info.xscale = xscale;
	info.yscale = yscale;
	info.color[0] = r;
	info.color[1] = g;
	info.color[2] = b;
	info.color[3] = a;
	info.xfont = currfont;
	Draw2dText((float)x, (float)y, text, info);
}

bool CVitaRenderer::BeginVitaLightMap(CCObject *pObject, CVertexBuffer *pGeometry)
{
#if defined(LINUX)
	g_bVitaLightMapActive = false;
	if (!LightMapsEnabled() || !pObject || !pGeometry || pObject->m_nLMId <= 0 ||
		!pObject->m_pLMTCBufferO || !pObject->m_pLMTCBufferO->m_pVertexBuffer)
	{
		VitaDisableLightMapStage();
		return false;
	}
	CVertexBuffer *pLMVB = pObject->m_pLMTCBufferO->m_pVertexBuffer;
	const byte *pLMData = (const byte *)pLMVB->m_VS[VSF_GENERAL].m_VData;
	/* Lightmap UVs are indexed by the primary geometry's vertex index.  Never
	   bind an undersized stream: that would read into unrelated memory and turn
	   baked lighting into flashing black/white polygons. */
	if (!pLMData || pLMVB->m_NumVerts < pGeometry->m_NumVerts)
	{
		VitaDisableLightMapStage();
		return false;
	}

	VitaEnableLightMapStage((GLuint)pObject->m_nLMId,
		m_VertexSize[pLMVB->m_vertexformat], pLMData);

#if defined(VITA_PERF_TELEMETRY)
	static bool s_bReportedStaticLightMap = false;
	if (!s_bReportedStaticLightMap && iLog)
	{
		s_bReportedStaticLightMap = true;
			iLog->LogToFile("\001[VITA][LIGHTMAP] static stage active tex=%d uv=%d geometry=%d",
			pObject->m_nLMId, pLMVB->m_NumVerts, pGeometry->m_NumVerts);
	}
#endif
	return true;
#else
	return false;
#endif
}

void CVitaRenderer::EndVitaLightMap()
{
#if defined(LINUX)
	/* Keep unit 1 configured for the next adjacent world draw.  Draw entry
	   points disable it before any non-lightmapped geometry, and BeginFrame
	   establishes a hard boundary between frames. */
	g_bVitaLightMapActive = false;
#endif
}

void CVitaRenderer::Draw2dText(float posX, float posY, const char * szText, SDrawTextInfo & info)
{
	/* Keep Crytek's CRenderer implementation contract.  This backend does not
	   inherit CRenderer, so leaving these two methods as NULL-renderer stubs
	   silently removed profiler labels, debug overlays and any UI path using
	   IRenderer text instead of CUIHud's direct font calls. */
	if (!iSystem || !szText)
		return;
	ICryFont *pCryFont = iSystem->GetICryFont();
	IFFont *pFont = pCryFont ? pCryFont->GetFont("Default") : 0;
	if (!pFont)
		return;

	const float r = CLAMP(info.color[0], 0.0f, 1.0f);
	const float g = CLAMP(info.color[1], 0.0f, 1.0f);
	const float b = CLAMP(info.color[2], 0.0f, 1.0f);
	const float a = CLAMP(info.color[3], 0.0f, 1.0f);
	pFont->SetColor(color4f(r,g,b,a));
	pFont->SetCharWidthScale(1.0f);

	if (info.flags & eDrawText_FixedSize)
	{
		pFont->UseRealPixels(true);
		pFont->SetSize(vector2f(12.0f*info.xscale, 12.0f*info.yscale));
		pFont->SetSameSize(false);
		posX = ScaleCoordX(posX);
		posY = ScaleCoordY(posY);
	}
	else
	{
		pFont->UseRealPixels(false);
		pFont->SetSameSize(true);
		pFont->SetCharWidthScale(2.0f/3.0f);
		pFont->SetSize(vector2f(15.0f, 15.0f));
	}
	pFont->DrawString(posX, posY, szText);
}
void CVitaRenderer::Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z)
{
#if defined(LINUX)
	VitaResetFixedFunctionMaterial();
	/* Script and movie code can feed this API directly.  Reject degenerate or
	   non-finite geometry before it reaches vitaGL: NaNs in a client vertex
	   array are undefined for the fixed-function compiler and can poison the
	   whole command stream rather than merely drawing a bad quad. */
	if (!_finite(xpos) || !_finite(ypos) || !_finite(w) || !_finite(h) ||
		!_finite(s0) || !_finite(t0) || !_finite(s1) || !_finite(t1) ||
		w <= 0.0f || h <= 0.0f)
		return;
	if (!_finite(angle)) angle = 0.0f;
	if (!_finite(z)) z = 1.0f;
	r = _finite(r) ? CLAMP(r, 0.0f, 1.0f) : 1.0f;
	g = _finite(g) ? CLAMP(g, 0.0f, 1.0f) : 1.0f;
	b = _finite(b) ? CLAMP(b, 0.0f, 1.0f) : 1.0f;
	a = _finite(a) ? CLAMP(a, 0.0f, 1.0f) : 1.0f;
	/* A world/lightmap draw can leave the server or client selector on unit 1.
	   Binding a UI texture there makes it inherit the terrain-detail texture
	   matrix, producing the repeated 8x6 menu movie seen on hardware. */
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	/* Vita: retain Crytek's OpenGL/D3D Draw2dImage texture-coordinate
	   contract.  DDS and the rest of the retail UI use a top-left image
	   origin, so both original backends submit (1-t), not t, even though the
	   screen-space projection itself also has y increasing downwards. */
	/* Match both original Crytek backends: every ID <= 0 is an intentionally
	   untextured colour quad.  CUISystem uses -1 for panels, borders, focus
	   highlights and grey overlays, so treating negative IDs as failed texture
	   loads erased a large part of the retail menu.  Script texture failures do
	   not reach this call: LoadImage returns nil and its draw bindings reject
	   non-texture userdata before invoking the renderer. */
	const bool bInside2DMode = g_nVita2DModeDepth > 0;
	/* Far Cry's 2D API is authored in a virtual 800x600 canvas, so these
	   coordinates always have to be mapped onto whatever projection is current.
	   Assuming an active bracket is already 800x600 is not safe: every HUD
	   caller opens one that way, but CUISystem::Draw opens it at the real
	   framebuffer size (UISystem.cpp:689) and still submits virtual coordinates
	   -- AdjustWidth/AdjustHeight round-trip through real pixels only to snap,
	   and hand back virtual units.  Skipping the conversion there drew the whole
	   menu, and the movie panel with it, into the left 800/960 of the screen.
	   Scale by the active extents instead: an 800x600 bracket is then a no-op,
	   which is what the HUD and reticle need. */
	if (bInside2DMode)
	{
		xpos *= g_fVita2DModeScaleX;
		ypos *= g_fVita2DModeScaleY;
		w    *= g_fVita2DModeScaleX;
		h    *= g_fVita2DModeScaleY;
	}
	else
	{
		xpos = ScaleCoordX(xpos);
		ypos = ScaleCoordY(ypos) - 1.0f;
		w = ScaleCoordX(w) + 1.0f;
		h = ScaleCoordY(h) + 2.0f;
	}

	GLboolean wasDepth = GL_FALSE;
	GLboolean wasBlend = GL_FALSE;
	GLboolean wasTexture = GL_FALSE;
	if (!bInside2DMode)
	{
		wasDepth = glIsEnabled(GL_DEPTH_TEST);
		wasBlend = glIsEnabled(GL_BLEND);
		wasTexture = glIsEnabled(GL_TEXTURE_2D);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrthof(0.0f, (float)m_nWidth, (float)m_nHeight, 0.0f, -1.0f, 1.0f);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);
	}
	/* Honour a blend mode the caller already selected.  Script-driven HUD
	   drawing goes through CScriptObjectSystem::DrawImageColorCoords, which
	   calls SetState() with the requested blend and only then calls in here --
	   so overwriting it with src-alpha unconditionally threw that choice away.
	   Additive HUD elements are authored on a black backing, so drawn
	   alpha-blended they show the backing: black boxes behind HUD art.
	   Only impose the default when the caller left blending switched off. */
	if (!bInside2DMode && !wasBlend)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	bool bTextured = texture_id > 0;
	if (bTextured)
	{
		/* SetTexture caches both the enable and the binding.  The old raw calls
		   re-bound and re-dirtied vitaGL even for consecutive images from the
		   same atlas -- exactly the normal HUD workload. */
		SetTexture(texture_id, eTT_Base);
	}
	else
	{
		SetTexture(0, eTT_Base);
	}

	float verts[8] = {
		xpos,     ypos,
		xpos + w, ypos,
		xpos + w, ypos + h,
		xpos,     ypos + h
	};
	if (angle != 0.0f)
	{
		const float cx = xpos + w * 0.5f;
		const float cy = ypos + h * 0.5f;
		const float c = cry_cosf(DEG2RAD(angle));
		const float s = cry_sinf(DEG2RAD(angle));
		/* Crytek's desktop renderers map the 800x600 HUD coordinates to
		   framebuffer pixels before rotating.  Set2DMode keeps Vita vertices in
		   the active orthographic space, so rotating those values directly made
		   every compass and direction arrow follow the projection's non-square
		   pixel aspect.  Rotate deltas in physical-pixel space, then map them back
		   to the active projection. */
		float fToPhysicalX = 1.0f;
		float fToPhysicalY = 1.0f;
		if (bInside2DMode)
		{
			const float fOrthoW = 800.0f*g_fVita2DModeScaleX;
			const float fOrthoH = 600.0f*g_fVita2DModeScaleY;
			if (fOrthoW > 0.0f && fOrthoH > 0.0f)
			{
				fToPhysicalX = (float)m_nWidth/fOrthoW;
				fToPhysicalY = (float)m_nHeight/fOrthoH;
			}
		}
		for (int i = 0; i < 4; ++i)
		{
			const float x = (verts[i*2+0] - cx)*fToPhysicalX;
			const float y = (verts[i*2+1] - cy)*fToPhysicalY;
			verts[i*2+0] = (x*c - y*s)/fToPhysicalX + cx;
			verts[i*2+1] = (x*s + y*c)/fToPhysicalY + cy;
		}
	}
	const float uvs[8] = {
		s0, 1.0f-t0, s1, 1.0f-t0,
		s1, 1.0f-t1, s0, 1.0f-t1
	};
	unsigned char ur = (unsigned char)(r*255.0f), ug = (unsigned char)(g*255.0f),
		ub = (unsigned char)(b*255.0f), ua = (unsigned char)(a*255.0f);
	const unsigned char cols[16] = {
		ur,ug,ub,ua, ur,ug,ub,ua, ur,ug,ub,ua, ur,ug,ub,ua };

	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	VitaInvalidateVertexPointerCache();
	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);
	VitaSetClientArrayState(GL_COLOR_ARRAY, true);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, bTextured);
	if (bTextured)
	{
		glTexCoordPointer(2, GL_FLOAT, 0, uvs);
	}
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	TeardownVertexArrays();

	if (!bInside2DMode)
	{
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		// Blend/depth enables are restored here but the blend *function* is not,
		// so what SetState last recorded no longer describes the driver.
		VitaInvalidateRenderStateCache();
		if (wasDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		if (wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		if (wasTexture) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
		VitaInvalidateTextureCache();
	}
#endif
}
void CVitaRenderer::DrawImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float r, float g, float b, float a)
{
	Draw2dImage(xpos, ypos, w, h, texture_id, s0, t0, s1, t1, 0.0f, r, g, b, a, 1.0f);
}
int CVitaRenderer::SetPolygonMode(int mode) { return 0; }
void CVitaRenderer::GetMemoryUsage(ICrySizer* Sizer) { }
void CVitaRenderer::ScreenShot(const char * filename)
{
#if defined(LINUX)
	/* Vita: real framebuffer capture -- glReadPixels + a real, minimal
	   24bpp uncompressed BMP writer (same format LoadBMP_RGBA32 above
	   already decodes, just the encode direction). Written straight to
	   ux0:data (real, writable, host-visible under Vita3K's VitaFS path)
	   via plain fopen, not ICryPak -- this is a debug capture, not game
	   content. */
	int w = m_nWidth, h = m_nHeight;
	if (w <= 0 || h <= 0)
		return;
	// Vita: plain glReadPixels into ordinary heap memory consistently reads
	// back all-zero despite real, error-free (glGetError()==0) draw calls
	// beforehand. Two isolated fix attempts have now failed:
	//  - vglAlloc (GPU-mapped memory) + glFinish() together: real host-side
	//    crash.
	//  - glFinish() alone, plain heap buffer: also fatal -- this exact run
	//    died far earlier (~54K log lines) and far more abruptly (no crash
	//    trace at all, just a clean stop) than the many multi-million-line
	//    successful gameplay runs without it, and never even reached this
	//    function's own trace print after the call. Reverted.
	// Root cause still open, tracked separately from the renderer's actual
	// (confirmed-working) draw calls.
	byte *pPixels = new byte[w * h * 3];
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pPixels);
	sceClibPrintf("[BOOTTRACE] ScreenShot: glReadPixels err=%d first3=[%d,%d,%d]\n", glGetError(), pPixels[0], pPixels[1], pPixels[2]);

	FILE *fp = fopen(filename, "wb");
	if (!fp)
	{
		sceClibPrintf("[BOOTTRACE] ScreenShot: fopen failed for %s\n", filename);
		delete [] pPixels;
		return;
	}

	int rowSize = ((w * 3 + 3) / 4) * 4;
	int dataSize = rowSize * h;
	byte fileHeader[14] = {0};
	fileHeader[0] = 'B'; fileHeader[1] = 'M';
	*(unsigned int*)&fileHeader[2] = 14 + 40 + dataSize;
	*(unsigned int*)&fileHeader[10] = 14 + 40;
	byte infoHeader[40] = {0};
	*(unsigned int*)&infoHeader[0] = 40;
	*(int*)&infoHeader[4] = w;
	*(int*)&infoHeader[8] = h; // positive height = bottom-up, matches glReadPixels row order directly
	*(unsigned short*)&infoHeader[12] = 1;
	*(unsigned short*)&infoHeader[14] = 24;
	*(unsigned int*)&infoHeader[20] = dataSize;

	fwrite(fileHeader, 1, 14, fp);
	fwrite(infoHeader, 1, 40, fp);
	byte *pad = (byte*)"\0\0\0";
	for (int y = 0; y < h; y++)
	{
		const byte *pRow = pPixels + (long)y * w * 3;
		for (int x = 0; x < w; x++)
		{
			byte bgr[3] = { pRow[x*3+2], pRow[x*3+1], pRow[x*3+0] };
			fwrite(bgr, 1, 3, fp);
		}
		if (rowSize > w * 3)
			fwrite(pad, 1, rowSize - w * 3, fp);
	}
	fclose(fp);
	delete [] pPixels;
	sceClibPrintf("[BOOTTRACE] ScreenShot: wrote %s (%dx%d)\n", filename, w, h);
#endif
}
void CVitaRenderer::ProjectToScreen(float ptx, float pty, float ptz, float * sx, float * sy, float * sz)
{
#if defined(LINUX)
	float model[16], projection[16];
	int viewport[4];
	glGetFloatv(GL_MODELVIEW_MATRIX, model);
	glGetFloatv(GL_PROJECTION_MATRIX, projection);
	glGetIntegerv(GL_VIEWPORT, viewport);
	const float in[4] = { ptx, pty, ptz, 1.0f };
	float eye[4], clip[4];
	for (int row = 0; row < 4; ++row)
	{
		eye[row] = model[row] * in[0] + model[4 + row] * in[1] + model[8 + row] * in[2] + model[12 + row];
		clip[row] = projection[row] * eye[0] + projection[4 + row] * eye[1] + projection[8 + row] * eye[2] + projection[12 + row] * eye[3];
	}
	if (fabsf(clip[3]) < 1.0e-8f)
	{
		if (sx) *sx = 0.0f; if (sy) *sy = 0.0f; if (sz) *sz = 0.0f;
		return;
	}
	const float invW = 1.0f / clip[3];
	const float winX = viewport[0] + (clip[0] * invW + 1.0f) * viewport[2] * 0.5f;
	const float winY = viewport[1] + (clip[1] * invW + 1.0f) * viewport[3] * 0.5f;
	if (sx) *sx = winX * 100.0f / (float)m_nWidth;
	if (sy) *sy = 100.0f - winY * 100.0f / (float)m_nHeight;
	if (sz) *sz = (clip[2] * invW + 1.0f) * 0.5f;
#endif
}
int CVitaRenderer::UnProject(float sx, float sy, float sz, float * px, float * py, float * pz, const float modelMatrix[16], const float projMatrix[16], const int viewport[4])
{
	float combined[16];
	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row)
			combined[col * 4 + row] =
				projMatrix[row] * modelMatrix[col * 4] +
				projMatrix[4 + row] * modelMatrix[col * 4 + 1] +
				projMatrix[8 + row] * modelMatrix[col * 4 + 2] +
				projMatrix[12 + row] * modelMatrix[col * 4 + 3];
	float inverse[16];
	if (!QQinvertMatrixf(inverse, combined))
		return 0;
	const float in[4] = {
		(sx - viewport[0]) * 2.0f / viewport[2] - 1.0f,
		(sy - viewport[1]) * 2.0f / viewport[3] - 1.0f,
		2.0f * sz - 1.0f, 1.0f
	};
	float out[4];
	for (int row = 0; row < 4; ++row)
		out[row] = inverse[row] * in[0] + inverse[4 + row] * in[1] + inverse[8 + row] * in[2] + inverse[12 + row];
	if (fabsf(out[3]) < 1.0e-8f)
		return 0;
	const float invW = 1.0f / out[3];
	if (px) *px = out[0] * invW;
	if (py) *py = out[1] * invW;
	if (pz) *pz = out[2] * invW;
	return 1;
}
int CVitaRenderer::UnProjectFromScreen(float sx, float sy, float sz, float * px, float * py, float * pz)
{
#if defined(LINUX)
	float model[16], projection[16];
	int viewport[4];
	glGetFloatv(GL_MODELVIEW_MATRIX, model);
	glGetFloatv(GL_PROJECTION_MATRIX, projection);
	glGetIntegerv(GL_VIEWPORT, viewport);
	return UnProject(sx, sy, sz, px, py, pz, model, projection, viewport);
#else
	return 0;
#endif
}
void CVitaRenderer::GetModelViewMatrix(float * mat)
{
#if defined(LINUX)
	if (mat) glGetFloatv(GL_MODELVIEW_MATRIX, mat);
#endif
}
void CVitaRenderer::GetModelViewMatrix(double * mat)
{
#if defined(LINUX)
	if (!mat) return;
	float values[16]; glGetFloatv(GL_MODELVIEW_MATRIX, values);
	for (int i = 0; i < 16; ++i) mat[i] = values[i];
#endif
}
void CVitaRenderer::GetProjectionMatrix(double * mat)
{
#if defined(LINUX)
	if (!mat) return;
	float values[16]; glGetFloatv(GL_PROJECTION_MATRIX, values);
	for (int i = 0; i < 16; ++i) mat[i] = values[i];
#endif
}
void CVitaRenderer::GetProjectionMatrix(float * mat)
{
#if defined(LINUX)
	if (mat) glGetFloatv(GL_PROJECTION_MATRIX, mat);
#endif
}
Vec3 CVitaRenderer::GetUnProject(const Vec3 & WindowCoords, const CCamera & cam)
{
	Vec3 result(0, 0, 0);
	UnProjectFromScreen(WindowCoords.x, WindowCoords.y, WindowCoords.z, &result.x, &result.y, &result.z);
	return result;
}
void CVitaRenderer::RenderToViewport(const CCamera & cam, float x, float y, float width, float height) { }
void CVitaRenderer::WriteDDS(byte * dat, int wdt, int hgt, int Size, const char * name, EImFormat eF, int NumMips) { }
void CVitaRenderer::WriteTGA(byte * dat, int wdt, int hgt, const char * name, int bits) { }
void CVitaRenderer::WriteJPG(byte * dat, int wdt, int hgt, char * name) { }
bool CVitaRenderer::FontUploadTexture(class CFBitmap* a0, ETEX_Format eTF) { return false; }
int CVitaRenderer::FontCreateTexture(int Width, int Height, byte * pData, ETEX_Format eTF)
{
#if defined(LINUX)
	/* Vita: real texture upload. eTF_8888 (FONT_USE_32BIT_TEXTURE, the
	   path CryFont/FFont.cpp::RenderInit actually takes) is a 32bpp
	   buffer; eTF_8000 (unused here but handled defensively) is a
	   single-channel glyph mask. See CryCommon/IShader.h ETEX_Format. */
	sceClibPrintf("[BOOTTRACE] FontCreateTexture: Width=%d Height=%d pData=%p eTF=%d bytes=[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
		Width, Height, (void*)pData, (int)eTF,
		pData?pData[0]:0, pData?pData[1]:0, pData?pData[2]:0, pData?pData[3]:0,
		pData?pData[4]:0, pData?pData[5]:0, pData?pData[6]:0, pData?pData[7]:0);
	GLuint tex = 0;
	glGenTextures(1, &tex);
	sceClibPrintf("[BOOTTRACE] FontCreateTexture: glGenTextures tex=%u err=%d\n", tex, glGetError());
	glBindTexture(GL_TEXTURE_2D, tex);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (eTF == eTF_8000)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, Width, Height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pData);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	sceClibPrintf("[BOOTTRACE] FontCreateTexture: after glTexImage2D err=%d\n", glGetError());
	return (int)tex;
#else
	return 0;
#endif
}
bool CVitaRenderer::FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte * pData)
{
#if defined(LINUX)
	glBindTexture(GL_TEXTURE_2D, (GLuint)nTexId);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	glTexSubImage2D(GL_TEXTURE_2D, 0, X, Y, USize, VSize, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	return true;
#else
	return false;
#endif
}
void CVitaRenderer::FontReleaseTexture(class CFBitmap * pBmp) { }
void CVitaRenderer::FontSetTexture(class CFBitmap* a0, int nFilterMode) { }
void CVitaRenderer::FontSetTexture(int nTexId, int nFilterMode)
{
#if defined(LINUX)
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, (GLuint)nTexId);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
#endif
}
void CVitaRenderer::FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight)
{
#if defined(LINUX)
	/* Vita: text is authored in top-left-origin pixel space (see
	   CFFont::DrawStringW's fCharY += vSize.y on line breaks -- Y grows
	   downward). glOrtho with bottom/top swapped gives that directly. */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrthof(0.0f, (float)m_nWidth, (float)m_nHeight, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// Text sets depth and blend directly, so SetState's record is stale after it.
	VitaInvalidateRenderStateCache();
#endif
}
void CVitaRenderer::FontSetBlending(int src, int dst)
{
#if defined(LINUX)
	/* Deliberately empty, and not a gap to fill later: CD3D9Renderer and
	   CGLRenderer both define this as an empty function too (D3DFont.cpp,
	   GLFont.cpp).  Fonts render with the src-alpha/one-minus-src-alpha pair
	   FontSetRenderingState sets above, and CFFont's per-pass request is
	   ignored on every shipping backend.  Note the arguments are
	   CFFont::eBlendMode values (FFont.h), not the GS_BLSRC_x / GS_BLDST_x
	   flags SetState takes -- mapping them through GSBlendSrcToGL would be
	   wrong as well as unnecessary. */
#endif
}
void CVitaRenderer::FontRestoreRenderingState()
{
#if defined(LINUX)
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
#endif
}
bool CVitaRenderer::EF_PrecacheResource(IShader * pSH, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(ITexPic * pTP, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(CLeafBuffer * pPB, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(CDLight * pLS, float fDist, float fTimeToReady, int Flags) { return false; }
void CVitaRenderer::EF_EnableHeatVision(bool bEnable) { }
bool CVitaRenderer::EF_GetHeatVision() { return false; }
void CVitaRenderer::EF_PolygonOffset(bool bEnable, float fFactor, float fUnits) { }
void CVitaRenderer::EF_AddPolyToScene3D(int Ef, int numPts, SColorVert * verts, CCObject * obj, int nFogID)
{
	EF_AddSpriteToScene(Ef, numPts, verts, obj, NULL, 0, nFogID);
}
CCObject * CVitaRenderer::EF_AddSpriteToScene(int Ef, int numPts, SColorVert * verts, CCObject * obj, byte * inds, int ninds, int nFogID)
{
#if defined(LINUX)
	VitaResetFixedFunctionMaterial();
	if (!verts || numPts < 3)
		return obj;
	/* Reused across calls rather than built fresh each time.  This is the
	   vegetation-sprite and particle path: it runs hundreds of times a frame in
	   the open world, and two std::vector constructions per call meant hundreds
	   of allocate/free pairs every frame purely to hold four vertices.  Static
	   buffers keep their capacity after the first frame, so the steady state
	   does no allocation at all.  The renderer is single-threaded here, and the
	   contents never outlive the draw below. */
	static std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> vitaVerts;
	static std::vector<ushort> vitaIndices;
	vitaVerts.resize((size_t)numPts);
	for (int i = 0; i < numPts; ++i)
	{
		vitaVerts[i].xyz = verts[i].vert;
		vitaVerts[i].color = verts[i].color;
		vitaVerts[i].st[0] = verts[i].dTC[0];
		vitaVerts[i].st[1] = verts[i].dTC[1];
	}
	vitaIndices.clear();
	if (inds && ninds > 0)
	{
		vitaIndices.resize((size_t)ninds);
		for (int i = 0; i < ninds; ++i)
			vitaIndices[i] = inds[i];
	}
	else
	{
		vitaIndices.reserve((size_t)(numPts - 2) * 3);
		for (int i = 0; i < numPts - 2; ++i)
		{
			vitaIndices.push_back(0);
			vitaIndices.push_back((ushort)(i + 1));
			vitaIndices.push_back((ushort)(i + 2));
		}
	}
	SetCullMode(R_CULL_NONE);
	/* Sprites and particles arrive here with their blend already chosen by
	   CObjManager::AddPolygonToRenderer, which maps the effect's blend type to
	   additive (ONE/ONE), colour-based (ONE/ONEMINUSSRCCOL) or alpha.  An
	   additive effect drawn alpha-blended shows its black backing square, which
	   is the reported "black background" behind muzzle flashes and lights.
	   Report the state actually used per distinct effect id so it is clear
	   whether the blend type is being lost upstream or honoured here. */
	#if defined(VITA_PERF_TELEMETRY)
	{
		static std::map<int, int> s_reportedSpriteStates;
		const int nState = obj ? obj->m_RenderState : 0;
		if (iLog && s_reportedSpriteStates.size() < 64 &&
			s_reportedSpriteStates.find(Ef) == s_reportedSpriteStates.end())
		{
			s_reportedSpriteStates[Ef] = nState;
			iLog->LogToFile("\001[VITA][SPRITE] ef=%d state=0x%x additive=%d colorbased=%d alpha=%d tex=%d",
				Ef, (unsigned)nState,
				(nState & GS_BLEND_MASK) == (GS_BLSRC_ONE | GS_BLDST_ONE) ? 1 : 0,
				(nState & GS_BLEND_MASK) == (GS_BLSRC_ONE | GS_BLDST_ONEMINUSSRCCOL) ? 1 : 0,
				(nState & GS_BLEND_MASK) == (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA) ? 1 : 0,
				obj ? obj->m_NumCM : -1);
		}
	}
	#endif
	SetState(obj ? obj->m_RenderState : (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA));
	if (obj && obj->m_NumCM > 0)
		SetTexture(obj->m_NumCM, eTT_Base);
	else
		SetWhiteTexture();
	const bool hasObjectTransform = obj && (obj->m_ObjFlags & FOB_TRANS_MASK);
	if (hasObjectTransform)
	{
		PushMatrix();
		MultMatrix(obj->m_Matrix.GetData());
	}
	DrawDynVB(&vitaVerts[0], &vitaIndices[0], numPts, (int)vitaIndices.size(), R_PRIMV_TRIANGLES);
	if (hasObjectTransform)
		PopMatrix();
#endif
	return obj;
}
void CVitaRenderer::EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D * verts) { }
void CVitaRenderer::EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D * verts) { }
IShader * CVitaRenderer::EF_LoadShader(const char * name, EShClass Class, int flags, uint64 nMaskGen)
{
	std::string key = name ? name : "";
	for (size_t i = 0; i < key.size(); ++i)
		key[i] = (char)tolower((unsigned char)key[i]);
	std::map<std::string, CVitaShader *>::iterator found = m_ShaderByName.find(key);
	if (found != m_ShaderByName.end())
	{
		found->second->AddRef();
		return found->second;
	}
	CVitaShader *shader = new CVitaShader(m_nNextShaderId++, name, nMaskGen);
	m_ShaderByName[key] = shader;
	shader->AddRef();
	return shader;
}

SShaderItem CVitaRenderer::EF_LoadShaderItem(const char * name, EShClass Class, bool bShare, const char * templName, int flags, SInputShaderResources * Res, uint64 nMaskGen)
{
	SShaderItem item;
	item.m_pShader = EF_LoadShader(templName && *templName ? templName : name, Class, flags, nMaskGen);

	SRenderShaderResources *resources = new SRenderShaderResources;
	resources->m_nRefCounter = 1;
	resources->m_Id = -1;
	if (Res)
	{
		resources->m_TexturePath = Res->m_TexturePath;
		resources->m_ResFlags = Res->m_ResFlags;
		resources->m_Opacity = Res->m_Opacity;
		resources->m_AlphaRef = Res->m_AlphaRef;
		resources->m_LMaterial = NULL;
		resources->m_ShaderParams.Copy(Res->m_ShaderParams);
		for (int i = 0; i < EFTT_MAX; ++i)
		{
			if (!Res->m_Textures[i].m_Name.empty() || Res->m_Textures[i].m_TU.m_TexPic)
			{
				resources->AddTextureMap(i);
				*resources->m_Textures[i] = Res->m_Textures[i];
			}
		}
		resources->PostLoad();
	}
	item.m_pShaderResources = resources;
	return item;
}
bool CVitaRenderer::EF_ReloadFile(const char * szFileName) { return false; }
void CVitaRenderer::EF_ReloadShaderFiles(int nCategory) { }
void CVitaRenderer::EF_ReloadTextures() { }
IShader			* CVitaRenderer::EF_CopyShader(IShader * ef) { return 0; }
ITexPic * CVitaRenderer::EF_GetTextureByID(int Id)
{
	std::map<int, CVitaTexPic *>::iterator it = m_TextureById.find(Id);
	return it != m_TextureById.end() ? it->second : NULL;
}

void CVitaRenderer::UnregisterTexture(CVitaTexPic *pTexture)
{
	if (!pTexture)
		return;
	std::map<int, CVitaTexPic *>::iterator it = m_TextureById.find(pTexture->GetTextureID());
	if (it != m_TextureById.end() && it->second == pTexture)
		m_TextureById.erase(it);
	for (std::map<std::string, CVitaTexPic *>::iterator nameIt = m_TextureByName.begin();
		nameIt != m_TextureByName.end(); )
	{
		if (nameIt->second == pTexture)
			m_TextureByName.erase(nameIt++);
		else
			++nameIt;
	}
}

CVitaTexPic::~CVitaTexPic()
{
	if (gcpVitaRenderer)
		gcpVitaRenderer->UnregisterTexture(this);
	if (m_nGLTexId > 0)
	{
		GLuint tex = (GLuint)m_nGLTexId;
		glDeleteTextures(1, &tex);
	}
	if (m_pRGBA32)
		delete [] m_pRGBA32;
}

#if defined(LINUX)
/* Vita: chunked FRead -- a single FRead() call for a large loose file (a
   few hundred KB, e.g. a lightmap DDS) has been observed to hang
   indefinitely on Vita3K for specific files while the exact same bytes
   read instantly via a normal Win32 file read on the host -- reproducible,
   file-content-independent (the file itself is byte-correct), looks like a
   real bug in Vita3K's own sceIoRead emulation for certain read sizes/
   offsets, not something fixable from engine code. Reading in bounded
   chunks sidesteps whatever specific (size, offset) combination triggers
   it, and a hard retry cap means a genuinely bad read fails cleanly
   (honest failure -- see EF_LoadTexture's own comment) instead of hanging
   the whole engine. */
static size_t FReadChunked(ICryPak *pPak, void *pDst, size_t nTotal, FILE *fp)
{
	/* 16 KiB was chosen to dodge a Vita3K sceIoRead emulation bug.  On real
	   hardware that only buys syscalls: a 4 MiB texture became 256 reads
	   through CryPak and its zlib layer instead of a handful, and textures are
	   a large share of level load time.  256 KiB keeps the bounded-read
	   property (and the stall retry below) while cutting the call count by
	   16x. */
	static const size_t CHUNK = 256 * 1024;
	static const int MAX_STALLED_RETRIES = 8;
	byte *p = (byte*)pDst;
	size_t nGot = 0;
	int nStalled = 0;
	int nIter = 0;
	while (nGot < nTotal)
	{
		size_t nWant = (nTotal - nGot) < CHUNK ? (nTotal - nGot) : CHUNK;
		size_t nRead = pPak->FRead(p + nGot, 1, nWant, fp);
		nIter++;
		if (nRead == 0)
		{
			if (++nStalled > MAX_STALLED_RETRIES)
				break;
			continue;
		}
		nStalled = 0;
		nGot += nRead;
		if (nIter > 100000)
		{
			sceClibPrintf("[BOOTTRACE] FReadChunked: giving up after %d iterations, nGot=%u/%u\n", nIter, (unsigned)nGot, (unsigned)nTotal);
			break;
		}
	}
	return nGot;
}

/* Vita: real BMP decoder -- 24bpp uncompressed only, which is what the
   real, retail Far Cry install's fcsplash.bmp actually is (verified via
   `file`/hexdump before writing this). No fabricated pixels: every byte
   here comes from the real file read through the real ICryPak file
   system (so it also works from inside real .pak archives, not just
   loose files). Parsed field-by-field rather than via a struct overlay
   to avoid struct-packing/alignment surprises across compilers. */
static byte *LoadBMP_RGBA32(ICryPak *pPak, const char *path, int *pOutW, int *pOutH)
{
	FILE *fp = pPak->FOpen(path, "rb");
	if (!fp)
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: FOpen failed for %s\n", path);
		return NULL;
	}

	byte fileHeader[14];
	byte infoHeader[40];
	if (pPak->FRead(fileHeader, 1, 14, fp) != 14 || pPak->FRead(infoHeader, 1, 40, fp) != 40)
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: header read failed for %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	if (fileHeader[0] != 'B' || fileHeader[1] != 'M')
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: not a BMP (bad signature) %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	unsigned int dataOffset = fileHeader[10] | (fileHeader[11]<<8) | (fileHeader[12]<<16) | (fileHeader[13]<<24);
	int width  = infoHeader[4] | (infoHeader[5]<<8) | (infoHeader[6]<<16) | (infoHeader[7]<<24);
	int height = infoHeader[8] | (infoHeader[9]<<8) | (infoHeader[10]<<16) | (infoHeader[11]<<24);
	int bpp    = infoHeader[14] | (infoHeader[15]<<8);
	unsigned int compression = infoHeader[16] | (infoHeader[17]<<8) | (infoHeader[18]<<16) | (infoHeader[19]<<24);

	sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: %s width=%d height=%d bpp=%d compression=%u dataOffset=%u\n",
		path, width, height, bpp, compression, dataOffset);

	if (bpp != 24 || compression != 0 || width <= 0 || height == 0 ||
		width > 4096 || height > 4096 || height < -4096)
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: unsupported BMP variant (only uncompressed 24bpp handled) %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	bool bBottomUp = height > 0;
	int absHeight = bBottomUp ? height : -height;
	const size_t rowSizeSrc = (((size_t)width * 3u + 3u) / 4u) * 4u; // BMP rows are padded to 4 bytes

	/* Vita: read the WHOLE pixel block in a single FRead instead of one
	   FRead per row. Each ICryPak::FRead call was going through a real
	   critical section plus the underlying sceIoRead round-trip -- for a
	   640x480 image that's 480 separate locked I/O calls, ~40 seconds
	   wall-clock on real hardware/emulation overhead. One big read is
	   the same bytes off the same real file, just without 479 redundant
	   lock/syscall round-trips. */
	const size_t srcSize = rowSizeSrc * (size_t)absHeight;
	const size_t dstSize = (size_t)width * (size_t)absHeight * 4u;
	byte *pSrcBuf = new byte[srcSize];
	byte *pRGBA = new byte[dstSize];
	if (!pSrcBuf || !pRGBA)
	{
		delete [] pSrcBuf;
		delete [] pRGBA;
		pPak->FClose(fp);
		return NULL;
	}

	pPak->FSeek(fp, (long)dataOffset, 0 /*SEEK_SET*/);

	if (FReadChunked(pPak, pSrcBuf, srcSize, fp) != (size_t)srcSize)
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: bulk pixel read failed for %s\n", path);
		delete [] pSrcBuf;
		delete [] pRGBA;
		pPak->FClose(fp);
		return NULL;
	}

	for (int y = 0; y < absHeight; y++)
	{
		byte *pRow = pSrcBuf + (size_t)y * rowSizeSrc;
		// BMP stores rows bottom-to-top by default (positive height).
		int destRow = bBottomUp ? (absHeight - 1 - y) : y;
		byte *pDest = pRGBA + (size_t)destRow * width * 4u;
		for (int x = 0; x < width; x++)
		{
			byte b = pRow[x*3+0];
			byte g = pRow[x*3+1];
			byte r = pRow[x*3+2];
			pDest[x*4+0] = r;
			pDest[x*4+1] = g;
			pDest[x*4+2] = b;
			pDest[x*4+3] = 255;
		}
	}

	delete [] pSrcBuf;
	pPak->FClose(fp);

	*pOutW = width;
	*pOutH = absHeight;
	return pRGBA;
}

/* Vita: real DDS/DXT decoder. Real Far Cry game textures (as opposed to
   fcsplash.bmp) are DDS files, S3TC/DXT1/3/5 block-compressed (confirmed
   by unzipping the retail install's FCData/Textures.pak -- e.g.
   Textures/gui/mousecursor.dds). vitaGL/PowerVR doesn't take S3TC
   uploads, so this decodes each 4x4 block to real RGBA32 pixels in
   software and uploads uncompressed, same as the BMP path. Block
   decompression math (DecompressBlockDXT1/3/5) adapted from
   Benjamin-Dobell/s3tc-dxt-decompression (public reference S3TC decoder,
   https://github.com/Benjamin-Dobell/s3tc-dxt-decompression), rewritten
   here to write straight into a byte RGBA buffer instead of packing into
   an endianness-dependent unsigned long. Only the base (mip 0) level is
   decoded -- real, unmodified compressed bytes in, real pixels out, no
   fabricated data. */
static void DecompressBlockDXT1(int x, int y, int width, int height,
	const byte *blockStorage, byte *image)
{
	unsigned short color0 = blockStorage[0] | (blockStorage[1] << 8);
	unsigned short color1 = blockStorage[2] | (blockStorage[3] << 8);

	unsigned int temp;
	temp = (color0 >> 11) * 255 + 16; byte r0 = (byte)((temp/32 + temp)/32);
	temp = ((color0 & 0x07E0) >> 5) * 255 + 32; byte g0 = (byte)((temp/64 + temp)/64);
	temp = (color0 & 0x001F) * 255 + 16; byte b0 = (byte)((temp/32 + temp)/32);
	temp = (color1 >> 11) * 255 + 16; byte r1 = (byte)((temp/32 + temp)/32);
	temp = ((color1 & 0x07E0) >> 5) * 255 + 32; byte g1 = (byte)((temp/64 + temp)/64);
	temp = (color1 & 0x001F) * 255 + 16; byte b1 = (byte)((temp/32 + temp)/32);

	unsigned int code = blockStorage[4] | (blockStorage[5]<<8) | (blockStorage[6]<<16) | (blockStorage[7]<<24);

	for (int j = 0; j < 4; j++)
	{
		for (int i = 0; i < 4; i++)
		{
			byte r=0,g=0,b=0,a=255;
			byte positionCode = (code >> 2*(4*j+i)) & 0x03;
			if (color0 > color1)
			{
				switch (positionCode)
				{
					case 0: r=r0; g=g0; b=b0; break;
					case 1: r=r1; g=g1; b=b1; break;
					case 2: r=(2*r0+r1)/3; g=(2*g0+g1)/3; b=(2*b0+b1)/3; break;
					case 3: r=(r0+2*r1)/3; g=(g0+2*g1)/3; b=(b0+2*b1)/3; break;
				}
			}
			else
			{
				switch (positionCode)
				{
					case 0: r=r0; g=g0; b=b0; break;
					case 1: r=r1; g=g1; b=b1; break;
					case 2: r=(r0+r1)/2; g=(g0+g1)/2; b=(b0+b1)/2; break;
					case 3: r=0; g=0; b=0; a=0; break;
				}
			}
			if (x+i < width && y+j < height)
			{
				byte *pDest = image + ((y+j)*width + (x+i)) * 4;
				pDest[0]=r; pDest[1]=g; pDest[2]=b; pDest[3]=a;
			}
		}
	}
}

// Shared by DXT3/DXT5: the color block (last 8 bytes) is always the plain
// 4-color interpolation, never DXT1's 3-color+transparent special case.
static void DecompressColorBlock4(int x, int y, int width, int height,
	const byte *blockStorage, byte *image, const byte *alphas)
{
	unsigned short color0 = blockStorage[0] | (blockStorage[1] << 8);
	unsigned short color1 = blockStorage[2] | (blockStorage[3] << 8);

	unsigned int temp;
	temp = (color0 >> 11) * 255 + 16; byte r0 = (byte)((temp/32 + temp)/32);
	temp = ((color0 & 0x07E0) >> 5) * 255 + 32; byte g0 = (byte)((temp/64 + temp)/64);
	temp = (color0 & 0x001F) * 255 + 16; byte b0 = (byte)((temp/32 + temp)/32);
	temp = (color1 >> 11) * 255 + 16; byte r1 = (byte)((temp/32 + temp)/32);
	temp = ((color1 & 0x07E0) >> 5) * 255 + 32; byte g1 = (byte)((temp/64 + temp)/64);
	temp = (color1 & 0x001F) * 255 + 16; byte b1 = (byte)((temp/32 + temp)/32);

	unsigned int code = blockStorage[4] | (blockStorage[5]<<8) | (blockStorage[6]<<16) | (blockStorage[7]<<24);

	for (int j = 0; j < 4; j++)
	{
		for (int i = 0; i < 4; i++)
		{
			byte r=0,g=0,b=0;
			byte positionCode = (code >> 2*(4*j+i)) & 0x03;
			switch (positionCode)
			{
				case 0: r=r0; g=g0; b=b0; break;
				case 1: r=r1; g=g1; b=b1; break;
				case 2: r=(2*r0+r1)/3; g=(2*g0+g1)/3; b=(2*b0+b1)/3; break;
				case 3: r=(r0+2*r1)/3; g=(g0+2*g1)/3; b=(b0+2*b1)/3; break;
			}
			if (x+i < width && y+j < height)
			{
				byte *pDest = image + ((y+j)*width + (x+i)) * 4;
				pDest[0]=r; pDest[1]=g; pDest[2]=b; pDest[3]=alphas[j*4+i];
			}
		}
	}
}

static void DecompressBlockDXT3(int x, int y, int width, int height,
	const byte *blockStorage, byte *image)
{
	byte alphas[16];
	for (int j = 0; j < 4; j++)
		for (int i = 0; i < 2; i++)
		{
			byte packed = blockStorage[j*2+i];
			alphas[j*4+i*2+0] = (packed & 0x0F) * 17;
			alphas[j*4+i*2+1] = (packed >> 4) * 17;
		}
	DecompressColorBlock4(x, y, width, height, blockStorage + 8, image, alphas);
}

static void DecompressBlockDXT5(int x, int y, int width, int height,
	const byte *blockStorage, byte *image)
{
	byte alpha0 = blockStorage[0];
	byte alpha1 = blockStorage[1];
	const byte *bits = blockStorage + 2;
	unsigned int alphaCode1 = bits[2] | (bits[3]<<8) | (bits[4]<<16) | (bits[5]<<24);
	unsigned short alphaCode2 = bits[0] | (bits[1]<<8);

	byte alphas[16];
	for (int j = 0; j < 4; j++)
	{
		for (int i = 0; i < 4; i++)
		{
			int idx = 3*(4*j+i);
			int alphaCode;
			if (idx <= 12) alphaCode = (alphaCode2 >> idx) & 0x07;
			else if (idx == 15) alphaCode = (alphaCode2 >> 15) | ((alphaCode1 << 1) & 0x06);
			else alphaCode = (alphaCode1 >> (idx - 16)) & 0x07;

			byte finalAlpha;
			if (alphaCode == 0) finalAlpha = alpha0;
			else if (alphaCode == 1) finalAlpha = alpha1;
			else if (alpha0 > alpha1) finalAlpha = ((8-alphaCode)*alpha0 + (alphaCode-1)*alpha1)/7;
			else if (alphaCode == 6) finalAlpha = 0;
			else if (alphaCode == 7) finalAlpha = 255;
			else finalAlpha = ((6-alphaCode)*alpha0 + (alphaCode-1)*alpha1)/5;

			alphas[j*4+i] = finalAlpha;
		}
	}
	DecompressColorBlock4(x, y, width, height, blockStorage + 8, image, alphas);
}

enum EDdsFourCC { DDS_FOURCC_NONE=0, DDS_FOURCC_DXT1, DDS_FOURCC_DXT3, DDS_FOURCC_DXT5 };

/* Decode one BC level and pack it into a Vita-cheap 16-bit texture.  BC1 gets
   RGB5551 so cutout alpha survives; BC2/BC3 get RGBA4444 for their graduated
   alpha.  Optional power-of-two downsampling happens while packing, avoiding a
   second large GPU allocation and keeping every uploaded BC texture at or
   below the Vita texture budget. */
static byte *VitaDecodeDXTLevel16(const byte *pBlocks, int srcW, int srcH,
	int dstW, int dstH, int fourCC, GLenum *pPixelType)
{
	if (!pBlocks || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
		return NULL;
	if (srcW > 4096 || srcH > 4096 || dstW > srcW || dstH > srcH)
		return NULL;
	const size_t nSrcPixels = (size_t)srcW * (size_t)srcH;
	const size_t nDstPixels = (size_t)dstW * (size_t)dstH;
	if (nSrcPixels / (size_t)srcW != (size_t)srcH ||
		nSrcPixels > ((size_t)-1) / 4u ||
		nDstPixels / (size_t)dstW != (size_t)dstH ||
		nDstPixels > ((size_t)-1) / sizeof(unsigned short))
		return NULL;
	const int blockSize = fourCC == DDS_FOURCC_DXT1 ? 8 : 16;
	const int blocksX = (srcW + 3) / 4;
	const int blocksY = (srcH + 3) / 4;
	byte *pRGBA = new byte[nSrcPixels * 4u];
	if (!pRGBA)
		return NULL;
	for (int by = 0; by < blocksY; ++by)
	for (int bx = 0; bx < blocksX; ++bx)
	{
		const byte *pBlock = pBlocks + (size_t)(by * blocksX + bx) * blockSize;
		switch (fourCC)
		{
			case DDS_FOURCC_DXT1:
				DecompressBlockDXT1(bx * 4, by * 4, srcW, srcH, pBlock, pRGBA); break;
			case DDS_FOURCC_DXT3:
				DecompressBlockDXT3(bx * 4, by * 4, srcW, srcH, pBlock, pRGBA); break;
			case DDS_FOURCC_DXT5:
				DecompressBlockDXT5(bx * 4, by * 4, srcW, srcH, pBlock, pRGBA); break;
			default:
				delete [] pRGBA; return NULL;
		}
	}

	byte *pPackedBytes = new byte[nDstPixels * sizeof(unsigned short)];
	if (!pPackedBytes)
	{
		delete [] pRGBA;
		return NULL;
	}
	unsigned short *pPacked = (unsigned short *)pPackedBytes;
	for (int y = 0; y < dstH; ++y)
	for (int x = 0; x < dstW; ++x)
	{
		/* Box-filter reductions instead of selecting one source texel.  Base-only
		   1024px terrain covers otherwise turn into crawling, blocky ground after
		   the Vita quality cap; this costs load time only and materially improves
		   the pixels the GPU samples every gameplay frame. */
		const int sx0 = x * srcW / dstW;
		const int sy0 = y * srcH / dstH;
		int sx1 = (x + 1) * srcW / dstW;
		int sy1 = (y + 1) * srcH / dstH;
		if (sx1 <= sx0) sx1 = sx0 + 1;
		if (sy1 <= sy0) sy1 = sy0 + 1;
		unsigned int sum[4] = {0,0,0,0}, samples = 0;
		for (int sy = sy0; sy < sy1 && sy < srcH; ++sy)
		for (int sx = sx0; sx < sx1 && sx < srcW; ++sx)
		{
			const byte *q = pRGBA + ((size_t)sy * srcW + sx) * 4;
			sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2]; sum[3] += q[3];
			++samples;
		}
		byte filtered[4] = {
			(byte)(sum[0] / samples), (byte)(sum[1] / samples),
			(byte)(sum[2] / samples), (byte)(sum[3] / samples) };
		const byte *p = filtered;
		if (fourCC == DDS_FOURCC_DXT1)
			pPacked[(size_t)y * dstW + x] = (unsigned short)(((p[0] >> 3) << 11) |
				((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | (p[3] >= 128 ? 1 : 0));
		else
			pPacked[(size_t)y * dstW + x] = (unsigned short)(((p[0] >> 4) << 12) |
				((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
	}
	delete [] pRGBA;
	*pPixelType = fourCC == DDS_FOURCC_DXT1
		? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_4_4_4_4;
	return pPackedBytes;
}

static bool VitaIsCompactLightMapPath(const std::string &path)
{
	const size_t nSlash = path.find_last_of('/');
	const std::string base = nSlash == std::string::npos ? path : path.substr(nSlash + 1);
	return path.find("levels/") == 0 && base.size() >= 6 &&
		base[0] == 'x' && base[1] >= '0' && base[1] <= '9' &&
		base.compare(base.size() - 4, 4, ".dds") == 0;
}

static int VitaChooseTextureDecodeLimit(const std::string &path)
{
	const bool bCompactLightMap = VitaIsCompactLightMapPath(path);
	const bool bLevelGroundCover = path.find("levels/") == 0 &&
		path.find("/terrain/cover") != std::string::npos;
	const bool bCritical =
		path.find("textures/hud/") == 0 || path.find("textures/gui/") == 0 ||
		path.find("gui/") == 0 || path.find("objects/characters/") == 0 ||
		path.find("objects/weapons/") == 0 || path.find("skys/") == 0 ||
		path.find("textures/controls") == 0 || path.find("textures/menu/") == 0 ||
		bLevelGroundCover;
	const bool bDenseScenery =
		path.find("objects/natural/") == 0;

	/* Spend the Vita's measured spare texture pool on what the player actually
	   inspects: weapons, characters, HUD and sky can retain a 512 mip; ordinary
	   world materials retain 256; dense foliage stays at 128.  The previous
	   blanket 128 cap was safe but made even close-up walls look like PS1 art. */
	int nLimit = bCompactLightMap ? 256 : (bCritical ? 512 : (bDenseScenery ? 128 : 256));
	if (s_vitaMaxTextureSize > 0 && nLimit > s_vitaMaxTextureSize)
		nLimit = s_vitaMaxTextureSize;

	/* Adapt down before allocation instead of discovering an exhausted vitaGL
	   pool inside glTexImage2D (that allocator does not fail safely).  Critical
	   first-person/UI art may use a smaller reserve; scenery yields earlier. */
	const unsigned int nReserve = (bCritical ? 16u : 24u) * 1024u * 1024u;
	const unsigned int nFree = (unsigned int)vglMemFree(VGL_MEM_VRAM);
	while (nLimit > 128)
	{
		const unsigned int nWorstTexture = (unsigned int)nLimit * nLimit * 2u;
		if (nFree > nReserve + nWorstTexture)
			break;
		nLimit >>= 1;
	}
	return nLimit < 128 ? 128 : nLimit;
}

/* Baked lightmaps can be turned off at runtime with "r_lightmaps 0" -- useful
   for telling a lighting problem apart from a texture one without a rebuild. */
static bool LightMapsEnabled()
{
	static ICVar *s_pLightMaps = NULL;
	if (!s_pLightMaps && iConsole)
		s_pLightMaps = iConsole->GetCVar("r_lightmaps");
	/* Never toggle baked lighting with scene complexity: that made whole
	   structures flash between lit and flat as the draw count crossed a gate. */
	return !s_pLightMaps || s_pLightMaps->GetIVal() != 0;
}

//! Pulls one channel out of a packed DDS pixel using its header bit mask and
//! rescales it to 0..255, so 16/24/32bpp layouts all decode the same way.
static inline byte DDSExtractChannel(unsigned int pixel, unsigned int mask, byte fallback)
{
	if (!mask)
		return fallback;
	int shift = 0;
	while (shift < 32 && !((mask >> shift) & 1u))
		++shift;
	const unsigned int maxValue = mask >> shift;
	if (!maxValue)
		return fallback;
	const unsigned int value = (pixel & mask) >> shift;
	return (byte)((value * 255u) / maxValue);
}

static byte *LoadDDSForVita(ICryPak *pPak, const char *path, int *pOutW, int *pOutH,
	GLenum *pOutInternalFormat, GLenum *pOutPixelType, int *pOutDataSize,
	bool *pOutCompressed, int *pOutMipCount)
{
	*pOutInternalFormat = GL_RGBA;
	*pOutPixelType = GL_UNSIGNED_BYTE;
	*pOutDataSize = 0;
	*pOutCompressed = false;
	if (pOutMipCount)
		*pOutMipCount = 1;
	FILE *fp = pPak->FOpen(path, "rb");
	if (!fp)
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: FOpen failed for %s\n", path);
		return NULL;
	}

	byte header[128];
	if (pPak->FRead(header, 1, 128, fp) != 128 ||
		header[0]!='D' || header[1]!='D' || header[2]!='S' || header[3]!=' ')
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: not a DDS (bad signature) %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	int height = header[12] | (header[13]<<8) | (header[14]<<16) | (header[15]<<24);
	int width  = header[16] | (header[17]<<8) | (header[18]<<16) | (header[19]<<24);
	// DDS_PIXELFORMAT.dwFourCC is at byte offset 84 (magic(4)+dwSize..dwReserved1 = 76, +size(4)+flags(4)=84).
	const byte *fourCCBytes = header + 84;
	int fourCC = DDS_FOURCC_NONE;
	int blockSize = 0;
	if (memcmp(fourCCBytes, "DXT1", 4) == 0) { fourCC = DDS_FOURCC_DXT1; blockSize = 8; }
	else if (memcmp(fourCCBytes, "DXT3", 4) == 0) { fourCC = DDS_FOURCC_DXT3; blockSize = 16; }
	else if (memcmp(fourCCBytes, "DXT5", 4) == 0) { fourCC = DDS_FOURCC_DXT5; blockSize = 16; }

	// DDS_PIXELFORMAT.dwFlags at offset 80, dwRGBBitCount at 88, dwRBitMask
	// at 92, dwBBitMask at 100 -- DDPF_RGB (0x40) with no fourCC is a real,
	// common DDS variant (confirmed on this level's own real lightmaps,
	// e.g. Levels/Training/h0.dds: 32bpp, B=0x000000FF R=0x00FF0000, i.e.
	// stored byte order B,G,R,A -- standard uncompressed D3D ARGB8888).
	unsigned int pfFlags = header[80] | (header[81]<<8) | (header[82]<<16) | (header[83]<<24);
	int rgbBitCount = header[88] | (header[89]<<8) | (header[90]<<16) | (header[91]<<24);
	unsigned int rBitMask = header[92] | (header[93]<<8) | (header[94]<<16) | (header[95]<<24);
	unsigned int gBitMask = header[96] | (header[97]<<8) | (header[98]<<16) | (header[99]<<24);
	unsigned int bBitMask = header[100] | (header[101]<<8) | (header[102]<<16) | (header[103]<<24);
	unsigned int aBitMask = header[104] | (header[105]<<8) | (header[106]<<16) | (header[107]<<24);
	/* Any DDPF_RGB layout, not just 32bpp BGRA.  A lot of the HUD, lens and
	   sky art ships as plain 24bpp RGB (Textures/cloud1.dds and
	   Textures/Lens/haze2.dds are both 24bpp), and rejecting those is what left
	   those elements missing or white.  The channel masks in the header say
	   where each component sits, so honour them rather than assuming an order. */
	bool bUncompressed = (fourCC == DDS_FOURCC_NONE) && (pfFlags & 0x40 /*DDPF_RGB*/) &&
		(rgbBitCount == 16 || rgbBitCount == 24 || rgbBitCount == 32);

	if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
		(fourCC == DDS_FOURCC_NONE && !bUncompressed))
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: unsupported DDS variant (fourCC=none pfFlags=0x%x bpp=%d) %s\n",
			pfFlags, rgbBitCount, path);
		pPak->FClose(fp);
		return NULL;
	}

	if (bUncompressed)
	{
		// Vita: real base-mip-level read -- same "only mip 0" scope as the
		// DXT path below; any additional mip levels appended after it in
		// the file are simply not read.
		const int srcBytesPerPixel = rgbBitCount / 8;
		const size_t nPixels = (size_t)width * (size_t)height;
		if (nPixels / (size_t)width != (size_t)height ||
			nPixels > ((size_t)-1) / 4u)
		{
			pPak->FClose(fp);
			return NULL;
		}
		const size_t srcSize = nPixels * (size_t)srcBytesPerPixel;
		const size_t dstSize = nPixels * 4u;
		byte *pSrcBuf = new byte[srcSize];
		byte *pRGBA = new byte[dstSize];
		if (!pSrcBuf || !pRGBA)
		{
			delete [] pSrcBuf;
			delete [] pRGBA;
			pPak->FClose(fp);
			return NULL;
		}
		if (FReadChunked(pPak, pSrcBuf, srcSize, fp) != (size_t)srcSize)
		{
			sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: uncompressed pixel read failed for %s\n", path);
			delete [] pSrcBuf;
			delete [] pRGBA;
			pPak->FClose(fp);
			return NULL;
		}
		for (size_t i = 0; i < nPixels; i++)
		{
			// Pixels are little-endian packed into rgbBitCount bits.
			unsigned int pixel = 0;
			for (int byteIndex = 0; byteIndex < srcBytesPerPixel; ++byteIndex)
				pixel |= ((unsigned int)pSrcBuf[i*srcBytesPerPixel + byteIndex]) << (byteIndex * 8);
			pRGBA[i*4+0] = DDSExtractChannel(pixel, rBitMask, 255);
			pRGBA[i*4+1] = DDSExtractChannel(pixel, gBitMask, 255);
			pRGBA[i*4+2] = DDSExtractChannel(pixel, bBitMask, 255);
			// No DDPF_ALPHAPIXELS (or no mask) means the surface is opaque.
			pRGBA[i*4+3] = ((pfFlags & 0x1) && aBitMask)
				? DDSExtractChannel(pixel, aBitMask, 255) : 255;
		}
		delete [] pSrcBuf;
		pPak->FClose(fp);
		*pOutW = width;
		*pOutH = height;
		*pOutDataSize = (int)dstSize;
		return pRGBA;
	}

	/* Read the whole mip chain, not just the base level.  Without mip maps
	   every minified surface aliases -- the shimmering, grainy look on
	   anything at a distance -- and the GPU samples the full-size texture for
	   a handful of pixels, which wastes far more bandwidth than the extra
	   third of storage the chain costs.  dwMipMapCount sits at offset 28. */
	int nMipCount = (int)(header[28] | (header[29]<<8) | (header[30]<<16) | (header[31]<<24));
	if (nMipCount < 1)
		nMipCount = 1;
	{
		// Never index below a 1x1 level, whatever the header claims.
		int nMaxLevels = 1, w = width, h = height;
		while ((w > 1 || h > 1) && nMaxLevels < 16)
		{
			w = (w > 1) ? (w >> 1) : 1;
			h = (h > 1) ? (h >> 1) : 1;
			++nMaxLevels;
		}
		if (nMipCount > nMaxLevels)
			nMipCount = nMaxLevels;
	}

	long compressedSize = 0;
	{
		int w = width, h = height;
		for (int nLevel = 0; nLevel < nMipCount; ++nLevel)
		{
			compressedSize += (long)((w + 3) / 4) * ((h + 3) / 4) * blockSize;
			w = (w > 1) ? (w >> 1) : 1;
			h = (h > 1) ? (h >> 1) : 1;
		}
	}

	byte *pCompressed = new byte[compressedSize];
	if (!pCompressed)
	{
		pPak->FClose(fp);
		return NULL;
	}
	size_t nReadGot = FReadChunked(pPak, pCompressed, compressedSize, fp);
	if (nReadGot != (size_t)compressedSize && nMipCount > 1)
	{
		/* Some archived DDS files declare a chain they do not actually store.
		   Fall back to the base level rather than failing the texture. */
		nMipCount = 1;
		compressedSize = (long)((width + 3) / 4) * ((height + 3) / 4) * blockSize;
		if (nReadGot >= (size_t)compressedSize)
			nReadGot = (size_t)compressedSize;
	}
	if (pOutMipCount)
		*pOutMipCount = nMipCount;
	if (nReadGot != (size_t)compressedSize)
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: compressed data read failed for %s\n", path);
		delete [] pCompressed;
		pPak->FClose(fp);
		return NULL;
	}
	pPak->FClose(fp);

	/* Real-hardware captures prove the native BC upload path is not reliable on
	   this vitaGL build: BC1 terrain and BC3 characters/HUD all become the same
	   rainbow block noise.  Decode every BC family, not a guessed directory.
	   Pick an authored mip at or below the memory-aware quality tier where one
	   exists; base-only assets are decoded then downsampled to the same bound. */
	std::string normalizedPath = path ? path : "";
	for (size_t i = 0; i < normalizedPath.size(); ++i)
	{
		if (normalizedPath[i] == '\\') normalizedPath[i] = '/';
		else normalizedPath[i] = (char)tolower((unsigned char)normalizedPath[i]);
	}
	const int nDecodeLimit = VitaChooseTextureDecodeLimit(normalizedPath);
	int srcW = width, srcH = height, decodeLevel = 0;
	long decodeOffset = 0;
	while (decodeLevel + 1 < nMipCount && (srcW > nDecodeLimit || srcH > nDecodeLimit))
	{
		decodeOffset += (long)((srcW + 3) / 4) * ((srcH + 3) / 4) * blockSize;
		srcW = srcW > 1 ? srcW >> 1 : 1;
		srcH = srcH > 1 ? srcH >> 1 : 1;
		++decodeLevel;
	}
	int dstW = srcW, dstH = srcH;
	while (dstW > nDecodeLimit || dstH > nDecodeLimit)
	{
		dstW = dstW > 1 ? dstW >> 1 : 1;
		dstH = dstH > 1 ? dstH >> 1 : 1;
	}
	byte *pPacked = VitaDecodeDXTLevel16(pCompressed + decodeOffset,
		srcW, srcH, dstW, dstH, fourCC, pOutPixelType);
	delete [] pCompressed;
	if (!pPacked)
		return NULL;
	/* Retail CGRCTexLM adds a material ambient term after the x4 baked-light
	   modulation.  Fixed-function vitaGL has no equivalent combine source, so
	   bias only the compact level lightmaps by two RGB555 steps.  With RGB_SCALE
	   4 this supplies a small ambient floor without washing out authored shadow
	   contrast, and leaves BC1's cutout-alpha bit untouched. */
	if (fourCC == DDS_FOURCC_DXT1 && VitaIsCompactLightMapPath(normalizedPath))
	{
		unsigned short *pPixels = (unsigned short *)pPacked;
		const size_t nPixels = (size_t)dstW * dstH;
		for (size_t i = 0; i < nPixels; ++i)
		{
			const unsigned short px = pPixels[i];
			unsigned int r = (px >> 11) & 31u;
			unsigned int g = (px >> 6) & 31u;
			unsigned int b = (px >> 1) & 31u;
			r = r > 29u ? 31u : r + 2u;
			g = g > 29u ? 31u : g + 2u;
			b = b > 29u ? 31u : b + 2u;
			pPixels[i] = (unsigned short)((r << 11) | (g << 6) | (b << 1) | (px & 1u));
		}
	}
	/* The desktop radar writes compass_mask.dds into destination alpha and uses
	   its inverse to reveal compass.dds.  Destination-alpha blending is fragile
	   on this fixed-function path, but a guessed hard circle discarded the
	   authored feathered ring and still left the locator visibly wrong.  Bake
	   the exact inverse mask into a RGBA4444 compass once at load time. */
	if (normalizedPath == "textures/hud/compass.dds")
	{
		bool bAuthoredMaskBaked=false;
		int maskW=0, maskH=0, maskDataSize=0, maskMipCount=1;
		GLenum maskInternalFormat=GL_RGBA, maskPixelType=GL_UNSIGNED_BYTE;
		bool maskCompressed=false;
		byte *pMask=LoadDDSForVita(pPak, "Textures/hud/compass_mask.dds",
			&maskW, &maskH, &maskInternalFormat, &maskPixelType,
			&maskDataSize, &maskCompressed, &maskMipCount);
		const size_t nMaskPixels = maskW>0 && maskH>0 ?
			(size_t)maskW*(size_t)maskH : 0u;
		const bool bMask16 = maskPixelType==GL_UNSIGNED_SHORT_5_5_5_1 ||
			maskPixelType==GL_UNSIGNED_SHORT_4_4_4_4;
		const bool bMask32 = maskPixelType==GL_UNSIGNED_BYTE;
		const size_t nMaskBytesPerPixel = bMask16 ? 2u : (bMask32 ? 4u : 0u);
		if (pMask && maskW>0 && maskH>0 && !maskCompressed &&
			nMaskPixels/(size_t)maskW==(size_t)maskH &&
			nMaskBytesPerPixel>0u && nMaskPixels<=((size_t)-1)/nMaskBytesPerPixel &&
			(size_t)maskDataSize>=nMaskPixels*nMaskBytesPerPixel &&
			(*pOutPixelType==GL_UNSIGNED_SHORT_5_5_5_1 ||
			 *pOutPixelType==GL_UNSIGNED_SHORT_4_4_4_4))
		{
			const GLenum compassPixelType=*pOutPixelType;
			const unsigned short *pCompass=(const unsigned short *)pPacked;
			const unsigned short *pMaskPixels16=(const unsigned short *)pMask;
			byte *pMasked=new byte[(size_t)dstW*dstH*sizeof(unsigned short)];
			unsigned short *pOut=(unsigned short *)pMasked;
			if (pMasked)
			{
				for (int y=0; y<dstH; ++y)
				for (int x=0; x<dstW; ++x)
				{
					const unsigned short src=pCompass[(size_t)y*dstW+x];
					const int mx=x*maskW/dstW;
					const int my=y*maskH/dstH;
					const size_t maskIndex=(size_t)my*maskW+mx;
					unsigned int r4, g4, b4, srcA4, maskA4;
					if (compassPixelType==GL_UNSIGNED_SHORT_5_5_5_1)
					{
						r4=((src>>11)&31u)>>1;
						g4=((src>>6)&31u)>>1;
						b4=((src>>1)&31u)>>1;
						srcA4=(src&1u)?15u:0u;
					}
					else
					{
						r4=(src>>12)&15u;
						g4=(src>>8)&15u;
						b4=(src>>4)&15u;
						srcA4=src&15u;
					}
					if (bMask32)
						maskA4=((const byte *)pMask)[maskIndex*4u+3u]>>4;
					else
					{
						const unsigned short mask=pMaskPixels16[maskIndex];
						maskA4=(maskPixelType==GL_UNSIGNED_SHORT_5_5_5_1) ?
							((mask&1u)?15u:0u) : (mask&15u);
					}
					const unsigned int outA4=(srcA4*(15u-maskA4)+7u)/15u;
					pOut[(size_t)y*dstW+x]=(unsigned short)((r4<<12)|(g4<<8)|(b4<<4)|outA4);
				}
				delete [] pPacked;
				pPacked=pMasked;
				*pOutPixelType=GL_UNSIGNED_SHORT_4_4_4_4;
				bAuthoredMaskBaked=true;
			}
		}
		delete [] pMask;
		/* Missing/corrupt optional mask data must degrade to the safe circular
		   locator, never back to the opaque black DDS square. */
		if (!bAuthoredMaskBaked)
		{
			unsigned short *pPixels=(unsigned short *)pPacked;
			const float cx=((float)dstW-1.0f)*0.5f;
			const float cy=((float)dstH-1.0f)*0.5f;
			const float radius=(float)(dstW<dstH?dstW:dstH)*0.49f;
			const float radius2=radius*radius;
			for (int y=0; y<dstH; ++y)
			for (int x=0; x<dstW; ++x)
			{
				unsigned short &pixel=pPixels[(size_t)y*dstW+x];
				const float dx=(float)x-cx;
				const float dy=(float)y-cy;
				if (*pOutPixelType==GL_UNSIGNED_SHORT_5_5_5_1)
					pixel=(unsigned short)((pixel&~1u)|((dx*dx+dy*dy<=radius2)?1u:0u));
				else
					pixel=(unsigned short)((pixel&~15u)|((dx*dx+dy*dy<=radius2)?15u:0u));
			}
		}
	}
	*pOutW = dstW;
	*pOutH = dstH;
	*pOutInternalFormat = GL_RGBA;
	*pOutDataSize = dstW * dstH * (int)sizeof(unsigned short);
	*pOutCompressed = false;
	if (pOutMipCount) *pOutMipCount = 1;
	#if defined(VITA_PERF_TELEMETRY)
	{
		static unsigned int s_nReportedQualityTiers = 0;
		if (iLog && s_nReportedQualityTiers < 24 &&
			(width != dstW || height != dstH))
		{
			++s_nReportedQualityTiers;
			iLog->LogToFile("\001[VITA][TEXQUALITY] %s %dx%d -> %dx%d cap=%d pool=%uKB",
				path, width, height, dstW, dstH, nDecodeLimit,
				(unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024u));
		}
	}
	#endif
	return pPacked;
}
#endif

static std::string MakeTextureCacheKey(const char *nameTex)
{
	std::string key = nameTex ? nameTex : "";
	for (size_t i = 0; i < key.size(); ++i)
	{
		if (key[i] == '\\') key[i] = '/';
		else key[i] = (char)tolower((unsigned char)key[i]);
	}
	const size_t slash = key.find_last_of('/');
	const size_t dot = key.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		key += ".dds";
	else if (key.compare(dot, std::string::npos, ".bmp") != 0 &&
		key.compare(dot, std::string::npos, ".dds") != 0)
		key.replace(dot, std::string::npos, ".dds");
	return key;
}

/* Many retail CGF materials still carry the absolute path the texture had on
   the artist's workstation in 2004 -- "server2\Artists\masterCD\objects\..."
   and "Harry\MASTERCD\Objects\...". Nothing under those prefixes exists at
   runtime, so those materials loaded nothing and drew pure white (the jungle
   canopy and tree bark, most visibly). The retail tools resolved the name
   relative to the MasterCD root; do the same here, and fall back to the first
   recognisable asset root for paths that never went through MasterCD. */
static std::string NormalizeRetailTexturePath(const char *nameTex);
static std::string MakeTextureCacheKey(const char *nameTex);

bool CVitaRenderer::IsTextureKnownMissing(const char *szName)
{
	if (!szName || !szName[0])
		return true;
	return m_FailedTextureNames.find(MakeTextureCacheKey(NormalizeRetailTexturePath(szName).c_str()))
		!= m_FailedTextureNames.end();
}

static std::string NormalizeRetailTexturePath(const char *nameTex)
{
	std::string name(nameTex ? nameTex : "");
	if (name.empty())
		return name;

	std::string lower(name);
	for (size_t i = 0; i < lower.size(); ++i)
	{
		if (lower[i] == '/') { lower[i] = '\\'; name[i] = '\\'; }
		else lower[i] = (char)tolower((unsigned char)lower[i]);
	}

	const std::string masterCd = "mastercd\\";
	size_t cut = lower.rfind(masterCd);
	if (cut != std::string::npos)
		return name.substr(cut + masterCd.size());

	// No MasterCD marker: anchor on a known asset root instead, but only when
	// the name is clearly absolute, so ordinary relative names pass untouched.
	if (name.find(':') != std::string::npos || lower.find("\\artists\\") != std::string::npos)
	{
		static const char *roots[] = { "objects\\", "textures\\", "levels\\", "materials\\" };
		size_t best = std::string::npos;
		for (size_t r = 0; r < sizeof(roots) / sizeof(roots[0]); ++r)
		{
			const size_t at = lower.rfind(roots[r]);
			if (at != std::string::npos && (best == std::string::npos || at < best))
				best = at;
		}
		if (best != std::string::npos)
			return name.substr(best);
	}
	return name;
}

/* Presentation-safe substitutes only for assets proven absent in the device
   log.  Effect masks are mathematical equivalents of the missing radial masks;
   the few missing object/UI diffuses get restrained category-coloured material
   instead of a glaring solid-white polygon. */
static byte *GenerateKnownVitaTexture(const std::string &cacheKey, int *outW, int *outH)
{
	std::string lower(cacheKey);
	for (size_t i = 0; i < lower.size(); ++i)
	{
		lower[i] = (char)tolower((unsigned char)lower[i]);
		if (lower[i] == '\\') lower[i] = '/';
	}
	const bool bExplosion = lower.find("explo_decal") != std::string::npos;
	const bool bBlur = lower.find("blurmask") != std::string::npos;
	const bool bFlare = lower == "textures/fl.dds" || lower == "fl.dds";
	const bool bMapIcon = lower.find("gui/map_") != std::string::npos;
	const bool bCrate = lower.find("crate2") != std::string::npos;
	const bool bParrot = lower.find("parrot_big") != std::string::npos;
	if (!bExplosion && !bBlur && !bFlare && !bMapIcon && !bCrate && !bParrot)
		return NULL;

	const int size = bMapIcon ? 32 : 64;
	byte *rgba = new byte[size * size * 4];
	if (!rgba)
		return NULL;
	for (int y = 0; y < size; ++y)
	for (int x = 0; x < size; ++x)
	{
		byte r = 255, g = 255, b = 255, a = 255;
		if (bExplosion || bBlur || bFlare)
		{
			const float fx = (2.0f*x + 1.0f - size) / (float)size;
			const float fy = (2.0f*y + 1.0f - size) / (float)size;
			float falloff = 1.0f - sqrtf(fx*fx + fy*fy);
			if (falloff < 0.0f) falloff = 0.0f;
			falloff *= falloff;
			a = (byte)(falloff * 255.0f);
			if (bExplosion) { r = 230; g = 170; b = 85; }
			else if (bFlare) { r = 255; g = 235; b = 175; }
		}
		else if (bMapIcon)
		{
			const int cx = size/2, cy = size/2;
			const bool mark = abs(x-cx) <= 2 || abs(y-cy) <= 2 ||
				(abs(x-cx) + abs(y-cy) < size/4);
			r = 80; g = 210; b = 235; a = mark ? 255 : 0;
		}
		else if (bCrate)
		{
			const int grain = ((x/8) ^ (y/8)) & 1;
			r = (byte)(105 + grain*25); g = (byte)(70 + grain*16); b = (byte)(38 + grain*9);
		}
		else
		{
			const int feather = ((x + y*2) / 7) & 1;
			r = (byte)(45 + feather*65); g = (byte)(125 + feather*65); b = (byte)(55 + feather*20);
		}
		const int p = (y*size + x)*4;
		rgba[p+0] = r; rgba[p+1] = g; rgba[p+2] = b; rgba[p+3] = a;
	}
	*outW = *outH = size;
	return rgba;
}

ITexPic			* CVitaRenderer::EF_LoadTexture(const char* nameTexRaw, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId)
{
#if defined(LINUX)
	if (!nameTexRaw || !iSystem || !iSystem->GetIPak())
		return 0;
	const std::string normalizedName = NormalizeRetailTexturePath(nameTexRaw);
	const char *nameTex = normalizedName.c_str();
	const std::string cacheKey = MakeTextureCacheKey(nameTex);
	(void)0;
	std::map<std::string, CVitaTexPic *>::iterator cached = m_TextureByName.find(cacheKey);
	if (cached != m_TextureByName.end())
	{
#if defined(VITA_PERF_TELEMETRY)
		static bool s_reportedTextureReuse = false;
		if (!s_reportedTextureReuse && iLog)
		{
			iLog->LogToFile("[VITA] texture cache reuse active");
			s_reportedTextureReuse = true;
		}
#endif
		cached->second->AddRef();
		return cached->second;
	}

	/* Negative cache.  Only successful loads were remembered, so a texture that
	   does not exist was searched for again on every single draw of every
	   material that references it -- a full candidate-path sweep and a set of
	   failed CryPak opens, every frame, forever.  With a level that has a
	   number of missing textures that is a permanent per-frame cost for
	   something already known to be absent.  Remember the misses too. */
	if (m_FailedTextureNames.find(cacheKey) != m_FailedTextureNames.end())
		return 0;

	ICryPak *pPak = iSystem->GetIPak();
	int w = 0, h = 0;
	byte *pTextureData = NULL;
	GLenum internalFormat = GL_RGBA;
	GLenum pixelType = GL_UNSIGNED_BYTE;
	int dataSize = 0;
	bool bCompressed = false;
	int nMipCount = 1;

	std::vector<std::string> candidates;
	std::string requested(nameTex);
	const size_t slash = requested.find_last_of("/\\");
	const size_t dot = requested.find_last_of('.');
	const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
	const bool isBmp = hasExtension && stricmp(requested.c_str() + dot, ".bmp") == 0;
	const bool hasTexturesRoot = requested.size() >= 9 &&
		(strnicmp(requested.c_str(), "Textures/", 9) == 0 || strnicmp(requested.c_str(), "Textures\\", 9) == 0);

	/* Crytek's texture manager searches the Textures root and replaces source
	   extensions with the compiled DDS.  Material files often contain only a
	   bare source name (black.tga is the canonical example), so trying just
	   black.dds at the archive root loses a valid Textures/common/black.dds. */
	std::string compiled = requested;
	if (!isBmp)
	{
		if (hasExtension)
			compiled.replace(dot, std::string::npos, ".dds");
		else
			compiled += ".dds";
	}
	else
		compiled = requested;

	/* Normalise every candidate to forward slashes.  Material names arrive with
	   backslashes and the "Textures/" prefixes added here use forward ones, so
	   candidates ended up mixed ("Textures/Skys\Carrier\Box_12.dds") and failed
	   to resolve against the archives -- which is why the sky box textures never
	   loaded and the sky never drew. */
	auto addCandidate = [&candidates](const std::string &rawPath)
	{
		std::string path(rawPath);
		for (size_t i = 0; i < path.size(); ++i)
			if (path[i] == '\\')
				path[i] = '/';
		for (size_t i = 0; i < candidates.size(); ++i)
			if (stricmp(candidates[i].c_str(), path.c_str()) == 0)
				return;
		candidates.push_back(path);
	};
	addCandidate(compiled);
	if (!hasTexturesRoot)
		addCandidate(std::string("Textures/") + compiled);
	if (slash == std::string::npos)
		addCandidate(std::string("Textures/common/") + compiled);

	/* Materials frequently name a subdirectory that does not exist in the
	   shipped archives -- "Textures\hud\blue.dds" when the file is really
	   Textures/common/blue.dds, "textures\lens\haze2.pcx" when it is
	   Textures/Lens/haze2.dds.  Crytek's texture manager falls back to the
	   Textures root and its common/ folder by bare name; without that, a whole
	   class of HUD and lens art silently resolved to nothing and drew white. */
	{
		const size_t compiledSlash = compiled.find_last_of("/\\");
		if (compiledSlash != std::string::npos)
		{
			const std::string baseName = compiled.substr(compiledSlash + 1);
			addCandidate(std::string("Textures/") + baseName);
			addCandidate(std::string("Textures/common/") + baseName);
			addCandidate(baseName);
		}
	}

	for (size_t candidateIndex = 0; candidateIndex < candidates.size() && !pTextureData; ++candidateIndex)
	{
		const std::string &path = candidates[candidateIndex];
		const bool candidateBmp = path.size() >= 4 && stricmp(path.c_str() + path.size() - 4, ".bmp") == 0;
		w = h = dataSize = 0;
		internalFormat = GL_RGBA;
		pixelType = GL_UNSIGNED_BYTE;
		bCompressed = false;
		nMipCount = 1;
		if (candidateBmp)
		{
			pTextureData = LoadBMP_RGBA32(pPak, path.c_str(), &w, &h);
			if (pTextureData)
				dataSize = w * h * 4;
		}
		else
			pTextureData = LoadDDSForVita(pPak, path.c_str(), &w, &h, &internalFormat,
				&pixelType, &dataSize, &bCompressed, &nMipCount);
	}
	if (!pTextureData)
	{
		pTextureData = GenerateKnownVitaTexture(cacheKey, &w, &h);
		if (pTextureData)
		{
			dataSize = w*h*4;
			internalFormat = GL_RGBA;
			pixelType = GL_UNSIGNED_BYTE;
			bCompressed = false;
			nMipCount = 1;
			if (iLog)
				iLog->LogToFile("\001[VITA][TEXFALLBACK] generated %s %dx%d", nameTex, w, h);
		}
	}

	if (!pTextureData)
	{
		/* Honest failure, no fake pixels: no ITexPic is fabricated when
		   the real file can't be found or decoded. */
		static std::map<std::string, bool> reportedMissing;
		if (iLog && reportedMissing.find(cacheKey) == reportedMissing.end())
		{
			reportedMissing[cacheKey] = true;
			std::string tried;
			for (size_t i = 0; i < candidates.size(); ++i)
			{
				if (i) tried += " | ";
				tried += candidates[i];
			}
			iLog->LogToFile("\001[VITA][TEXMISS] %s  tried: %s", nameTex, tried.c_str());
		}
		// Do not search for this one again on every later draw.
		m_FailedTextureNames.insert(cacheKey);
		return 0;
	}
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	/* Uploading a mip chain level by level makes vitaGL grow the texture's
	   allocation on every call after the first, and that growth path
	   (gpu_alloc_compressed_texture -> vgl_realloc) is where this port has been
	   dying on every level load.  Sending only the base level keeps the
	   allocation a single up-front block and never enters it.  The cost is
	   aliasing on minified surfaces; the benefit is that the level loads.
	   r_vita_tex_mips 1 restores the full chain. */
	const bool bUseMips = bCompressed && nMipCount > 1 && s_vitaTexMips != 0;
	/* Trilinear once there is a chain to filter between: without it, minified
	   surfaces alias badly (the grainy shimmer at distance) and the GPU keeps
	   sampling full-size textures for a few pixels. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		bUseMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (bCompressed)
	{
		const int nBlockSize = (internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
			internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ? 8 : 16;
		const byte *pLevel = pTextureData;
		int lw = w, lh = h;
		/* Start the chain at a mip the Vita can afford instead of at the authored
		   top level.  Far Cry ships 1024 and 2048 textures for PC; uploading those
		   whole is what runs the GPU pool dry, and vitaGL does not survive that --
		   gpu_alloc_compressed_texture takes the pointer from
		   gpu_alloc_mapped_for_gpu with no NULL check and memcpys into it, so an
		   exhausted pool is a data abort mid-level-load rather than a failed
		   texture.  Every level dropped here is a factor of four off this
		   texture's footprint and off the bandwidth spent sampling it.  The mip
		   data is already in the file, so this costs nothing to produce. */
		int nSkip = 0;
		if (nMipCount > 1)
		{
			const int nLimit = s_vitaMaxTextureSize > 0 ? s_vitaMaxTextureSize : 512;
			while (nSkip + 1 < nMipCount && (lw > nLimit || lh > nLimit))
			{
				pLevel += ((lw + 3) / 4) * ((lh + 3) / 4) * nBlockSize;
				lw = (lw > 1) ? (lw >> 1) : 1;
				lh = (lh > 1) ? (lh >> 1) : 1;
				++nSkip;
			}
		}
		const int nUploadLevels = bUseMips ? (nMipCount - nSkip) : 1;
		for (int nLevel = 0; nLevel < nUploadLevels; ++nLevel)
		{
			const int nLevelSize = ((lw + 3) / 4) * ((lh + 3) / 4) * nBlockSize;
			glCompressedTexImage2D(GL_TEXTURE_2D, nLevel, internalFormat, lw, lh, 0,
				nLevelSize, pLevel);
			pLevel += nLevelSize;
			lw = (lw > 1) ? (lw >> 1) : 1;
			lh = (lh > 1) ? (lh >> 1) : 1;
		}
		// No GL_TEXTURE_MAX_LEVEL in this GL profile; supplying every level
		// down to 1x1 is what makes the chain complete.
	}
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, pixelType, pTextureData);
	GLenum uploadError = glGetError();
	if (uploadError != GL_NO_ERROR)
	{
		glDeleteTextures(1, &tex);
		delete [] pTextureData;
		return 0;
	}

	/* glTexImage2D has copied the staging pixels.  The only Vita GetData32
	   consumer used to be the low-spec lightmap combiner; it now combines its
	   source buffers before upload, so retaining one RGBA copy per raw texture
	   only burns main memory and eventually starves later level assets. */
	byte *pCPUData = NULL;
	delete [] pTextureData;
	CVitaTexPic *pResult = new CVitaTexPic(nameTex, (int)tex, w, h, pCPUData, (int)flags, (int)flags2);
	m_TextureById[(int)tex] = pResult;
	m_TextureByName[cacheKey] = pResult;
	VitaRecordProgCacheTexture(normalizedName);
	return pResult;
#else
	return 0;
#endif
}
int CVitaRenderer::EF_LoadLightmap(const char * name) { return 0; }
bool CVitaRenderer::EF_ScanEnvironmentCM(const char * name, int size, Vec3& Pos) { return false; }
int CVitaRenderer::EF_ReadAllImgFiles(IShader * ef, SShaderTexUnit * tl, STexAnim * ta, char * name) { return 0; }
char					** CVitaRenderer::EF_GetShadersForFile(const char * File, int num) { return 0; }
SLightMaterial * CVitaRenderer::EF_GetLightMaterial(char * Str) { return 0; }
bool CVitaRenderer::EF_RegisterTemplate(int nTemplId, char * Name, bool bReplace) { return false; }
void CVitaRenderer::EF_AddSplash(Vec3 Pos, eSplashType eST, float fForce, int Id) { }
bool CVitaRenderer::EF_HideTemplate(const char * name) { return false; }
bool CVitaRenderer::EF_UnhideTemplate(const char * name) { return false; }
bool CVitaRenderer::EF_UnhideAllTemplates() { return false; }
bool CVitaRenderer::EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale, bool bAdditive) { return false; }
/* Vita: real, unmodified CRendElement subclasses (CryCommon/CRESky.h etc)
   -- not stand-ins, the actual classes C3DEngine's constructor asks for by
   type, already compiled into this binary. Their mfPrepare/mfDraw are
   still real methods that call into the rest of the (not-yet-real-on-
   Vita) render pipeline, so this doesn't make the 3D engine draw
   anything by itself -- it just gives C3DEngine's constructor real,
   valid, non-null objects to hold instead of crashing on a null deref.
   Types C3DEngine doesn't touch at construction time are left as an
   honest null (see IRenderer.h's EDataType for what's not handled). */
CRendElement * CVitaRenderer::EF_CreateRE(EDataType edt)
{
#if defined(LINUX)
	switch (edt)
	{
		case eDATA_Sky:              return new CRESky();
		case eDATA_Dummy:            return new CREDummy();
		case eDATA_TerrainParticles: return new CRETerrainParticles();
		case eDATA_2DQuad:           return new CRE2DQuad();
		case eDATA_OcLeaf:           return new CREOcLeaf();
		case eDATA_ScreenProcess:    return new CREScreenProcess();
		default:
			sceClibPrintf("[BOOTTRACE] EF_CreateRE: unhandled EDataType=%d, returning null\n", (int)edt);
			return 0;
	}
#else
	return 0;
#endif
}
void CVitaRenderer::EF_StartEf()
{
	m_nTempRenderObjectCursor = 0;

	/* CryEngine's render depth is one-based while a pass is open.  Entity and
	   player drawing query this value and subtract one; leaving the Vita stub at
	   zero made the main pass look like recursion level -1.  In particular,
	   CPlayer::OnDraw then rejected the first-person weapon on every frame. */
	++SRendItem::m_RecurseLevel;

	/* AddWaves appends two entries to this shared array for every bending
	   object that asks for a wave form, and CCObject::Init() clears the
	   per-object indices every frame -- so the entries handed out last frame
	   can never be referenced again.  The desktop pipeline reclaims them here
	   (Renderer.cpp: CCObject::m_Waves.SetUse(1)); this backend did not, so the
	   array grew by two per bending object per frame and never shrank.  Left
	   running outdoors it reached ~491k entries, at which point the allocation
	   failed and AddWaves wrote through a null base -- and long before that the
	   index passed 32767 and no longer fit CCObject's short m_NumWFX.
	   SetUse only lowers the count, so the reserved capacity survives and no
	   reallocation happens on the following frame.  Index 0 stays reserved as
	   the "no wave" sentinel that a zero m_NumWFX means.

	   Once per frame, not once per call.  EF_StartEf runs several times a frame
	   here -- the main pass, shadows, and the UI each start their own -- and the
	   desktop reset is guarded by "if (!SRendItem::m_RecurseLevel)" for exactly
	   that reason.  Resetting on every call reclaimed slots that objects earlier
	   in the same frame had already recorded indices into, so their wave lookups
	   landed on whatever occupied those slots afterwards: water and vegetation
	   stopped waving correctly.  Keying off the frame id reproduces the
	   outermost-call-only behaviour without needing a recursion counter. */
	static int s_nLastWaveResetFrame = -1;
	if (m_nFrameId != s_nLastWaveResetFrame)
	{
		s_nLastWaveResetFrame = m_nFrameId;
		if (CCObject::m_Waves.Num() > 1)
			CCObject::m_Waves.SetUse(1);
	}
}
CCObject * CVitaRenderer::EF_GetObject(bool bTemp, int num)
{
	std::vector<CCObject *> *pool = bTemp ? &m_TempRenderObjects : &m_PermanentRenderObjects;
	size_t index;
	if (num >= 0)
		index = (size_t)num;
	else if (bTemp)
		index = m_nTempRenderObjectCursor++;
	else
		index = pool->size();

	if (index >= pool->size())
		pool->resize(index + 1, NULL);
	CCObject *obj = (*pool)[index];
	if (!obj)
	{
		obj = new CCObject;
		(*pool)[index] = obj;
	}
	obj->m_Id = (short)index;
	obj->m_VisId = (short)index;
	obj->Init();
	return obj;
}

#if defined(LINUX)
static bool DrawVitaSkyBox(CVitaRenderer *renderer, CRESky *sky, CVitaShader *shader, CCObject *object)
{
	static bool reportedNoShader = false;
	static bool reportedBase = false;
	static bool reportedTextures = false;
	static bool reportedDraw = false;
	if (!renderer || !sky || !shader)
	{
		if (!reportedNoShader && iLog)
		{
			reportedNoShader = true;
			iLog->LogToFile("\001[VITA][SKY] draw rejected: renderer=%p sky=%p shader=%p", renderer, sky, shader);
		}
		return false;
	}
	const std::string &skyBoxBase = shader->GetSkyBoxBase();
	if (skyBoxBase.empty())
	{
		if (!reportedNoShader && iLog)
		{
			reportedNoShader = true;
			iLog->LogToFile("\001[VITA][SKY] shader '%s' has no SkyBox mapping", shader->GetName());
		}
		return false;
	}
	if (!reportedBase && iLog)
	{
		reportedBase = true;
		iLog->LogToFile("\001[VITA][SKY] shader '%s' -> '%s'", shader->GetName(), skyBoxBase.c_str());
	}

	const char *suffixes[3] = { "_12", "_34", "_5" };
	for (int i = 0; i < 3; ++i)
	{
		if (!shader->GetSkyTextureId(i))
		{
			std::string path = skyBoxBase + suffixes[i];
			ITexPic *texture = renderer->EF_LoadTexture(path.c_str(), FT_NOREMOVE | FT_CLAMP, 0, eTT_Base, 1.0f, 1.0f, -1, -1);
			if (texture)
				shader->SetSkyTextureId(i, texture->GetTextureID());
		}
		if (!shader->GetSkyTextureId(i))
			return false;
	}
	if (!reportedTextures && iLog)
	{
		reportedTextures = true;
		iLog->LogToFile("\001[VITA][SKY] textures loaded: %d %d %d", shader->GetSkyTextureId(0), shader->GetSkyTextureId(1), shader->GetSkyTextureId(2));
	}

	/* Geometry and the 3-texture packing are copied from Crytek's original
	   CRESky::mfDraw.  The only removed pieces are desktop Cg/HDR hooks. */
	const float size = 32.0f;
	Vec3 camera = renderer->GetCamera().GetPos();
	camera.z = max(0.0f, camera.z);
	float waterCameraDifference = max(0.0f, camera.z - sky->m_fTerrainWaterLevel);
	float maxDistanceScale = renderer->GetCamera().GetZMax() / 1024.0f;
	if (maxDistanceScale < 0.001f) maxDistanceScale = 0.001f;
	float p = waterCameraDifference / 128.0f + max(0.0f, waterCameraDifference * 0.03f / maxDistanceScale);
	float d = waterCameraDifference / 10.0f * size / 124.0f - p + 8.0f;
	p *= sky->m_fSkyBoxStretching;

	/* Depth-test the sky, but pin every sky fragment to the far plane and never
	   write depth.  GS_NODEPTHTEST simply painted the sky over whatever was
	   already on screen, so anything drawn before it -- the wall you are
	   crouching behind -- was overwritten by sky.  Clamping the depth range
	   instead makes the draw independent of where the engine submits the sky in
	   the frame: geometry drawn earlier occludes it because its depth is less
	   than 1.0, and geometry drawn later still covers it because the sky left
	   the depth buffer untouched. */
	renderer->SetState(sky->m_fAlpha < 1.0f
		? GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA
		: 0);
	glDepthRangef(1.0f, 1.0f);
	renderer->SetCullMode(R_CULL_BACK);
	VitaSetConstantColor(1.0f, 1.0f, 1.0f, sky->m_fAlpha);
	const bool hasObjectTransform = object && (object->m_ObjFlags & FOB_TRANS_MASK);
	if (hasObjectTransform)
	{
		renderer->PushMatrix();
		renderer->MultMatrix(object->m_Matrix.GetData());
	}

	struct_VERTEX_FORMAT_P3F_TEX2F top[] = {
		{ Vec3( size,-size, size), 1, 0 }, { Vec3(-size,-size, size), 0, 0 },
		{ Vec3( size, size, size), 1, 1 }, { Vec3(-size, size, size), 0, 1 }
	};
	renderer->SetTexture(shader->GetSkyTextureId(2), eTT_Base);
	renderer->SetTexClampMode(true);
	renderer->DrawTriStrip(&(CVertexBuffer(top, VERTEX_FORMAT_P3F_TEX2F)), 4);

	struct_VERTEX_FORMAT_P3F_TEX2F south[] = {
		{ Vec3(-size,-size, size), 1, 0 }, { Vec3( size,-size, size), 0, 0 },
		{ Vec3(-size,-size,-p),   1, .5f }, { Vec3( size,-size,-p),   0, .5f },
		{ Vec3(-size,-size,-d),   1, .5f }, { Vec3( size,-size,-d),   0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(1), eTT_Base);
	renderer->SetTexClampMode(true);
	renderer->DrawTriStrip(&(CVertexBuffer(south, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F east[] = {
		{ Vec3(-size, size, size), 1, 1 }, { Vec3(-size,-size, size), 0, 1 },
		{ Vec3(-size, size,-p),    1, .5f }, { Vec3(-size,-size,-p),    0, .5f },
		{ Vec3(-size, size,-d),    1, .5f }, { Vec3(-size,-size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(1), eTT_Base);
	renderer->SetTexClampMode(true);
	renderer->DrawTriStrip(&(CVertexBuffer(east, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F north[] = {
		{ Vec3( size, size, size), 1, 0 }, { Vec3(-size, size, size), 0, 0 },
		{ Vec3( size, size,-p),    1, .5f }, { Vec3(-size, size,-p),    0, .5f },
		{ Vec3( size, size,-d),    1, .5f }, { Vec3(-size, size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(0), eTT_Base);
	renderer->SetTexClampMode(true);
	renderer->DrawTriStrip(&(CVertexBuffer(north, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F west[] = {
		{ Vec3( size,-size, size), 1, 1 }, { Vec3( size, size, size), 0, 1 },
		{ Vec3( size,-size,-p),    1, .5f }, { Vec3( size, size,-p),    0, .5f },
		{ Vec3( size,-size,-d),    1, .5f }, { Vec3( size, size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(0), eTT_Base);
	renderer->SetTexClampMode(true);
	renderer->DrawTriStrip(&(CVertexBuffer(west, VERTEX_FORMAT_P3F_TEX2F)), 6);

	if (hasObjectTransform)
		renderer->PopMatrix();
	glDepthRangef(0.0f, 1.0f);
	renderer->SetState(GS_DEPTHWRITE);
	renderer->SetCullMode(R_CULL_BACK);
	VitaSetConstantColor(1, 1, 1, 1);
	if (!reportedDraw && iLog)
	{
		reportedDraw = true;
		iLog->LogToFile("\001[VITA][SKY] skybox draw submitted");
	}
	return true;
}
#endif

void CVitaRenderer::EF_AddEf(int NumFog, CRendElement * re, IShader * ef, SRenderShaderResources * sr, CCObject * obj, int nTempl, IShader * efState, int nSort)
{
#if defined(LINUX)
	if (!re)
		return;
	if (re->mfGetType() == eDATA_OcLeaf)
	{
		VitaResetFixedFunctionMaterial();
		CREOcLeaf *leafElement = static_cast<CREOcLeaf *>(re);
		CLeafBuffer *leaf = leafElement->m_pBuffer;
		CMatInfo *chunk = leafElement->m_pChunk;

		/* Record what actually arrives here and whether it survives to the
		   draw.  Invisible geometry is otherwise indistinguishable from
		   geometry that was never submitted, and guessing between those two
		   has cost several round trips already. */
		#if defined(VITA_PERF_TELEMETRY)
		{
			/* Construct the key only while the report can still fire.  This is
			   EF_AddEf: it runs for every render element of every object, every
			   frame, and building (then hashing, then discarding) a std::string
			   on each one just to learn the cap was reached long ago is pure
			   overhead in the busiest function in the renderer. */
			static std::map<std::string, bool> s_reportedSubmit;
			static unsigned int s_nSubmitReportChecks = 0;
			if (iLog && s_nSubmitReportChecks < 4096 && s_reportedSubmit.size() < 96)
			{
			++s_nSubmitReportChecks;
			const std::string src = (leaf && leaf->m_sSource) ? leaf->m_sSource : "<null-leaf>";
			if (s_reportedSubmit.find(src) == s_reportedSubmit.end())
			{
				s_reportedSubmit[src] = true;
				iLog->LogToFile("\001[VITA][SUBMIT] %s vb=%s inds=%d dyn=%d shader=%s",
					src.c_str(),
					(leaf && leaf->m_pVertexBuffer) ? "yes" : "NO",
					chunk ? chunk->nNumIndices : -1,
					(leaf && leaf->m_pVertexBuffer) ? (int)leaf->m_pVertexBuffer->m_bDynamic : -1,
					(ef && ef->GetName()) ? ef->GetName() : "<null>");
			}
			}
		}
		#endif

		if (!leaf || !chunk || !leaf->m_pVertexBuffer || chunk->nNumIndices <= 0)
			return;

		/* Skin the character before drawing it.  Animating a character updates
		   its bones, but nothing applies those bones to the vertices until
		   somebody calls ProcessSkinning -- and in this engine that somebody is
		   the renderer, not the animation system (CryModelState.h says as much:
		   the model matrix is "passed to the character in ProcessSkinning() by
		   the renderer from EF_ObjectChange()").  All three shipping backends do
		   it from their render pipeline; this one never did, so every character
		   drew from its unmodified bind-pose vertex buffer no matter how well
		   the animation underneath was running.  That is the T-pose: animation
		   layers active, bone-driven bounding box changing shape every frame,
		   and a mesh that never moved.

		   Matches the D3D9 call, including forcing the update when the vertex
		   container was already rebuilt this frame. */
		if (obj && obj->m_pCharInstance)
		{
			/* Once per character per frame, not once per material chunk.  The
			   desktop pipeline calls this once per object; here EF_AddEf runs for
			   every render element, and a character submits one per material.
			   ProcessSkinning's own "already skinned this frame" guard would
			   absorb the repeats -- except that bForceUpdate deliberately
			   bypasses that guard, so a forced update would re-skin the whole
			   mesh once per chunk.  Characters submit their chunks together, so
			   remembering just the last one caught is enough. */
			static IDeformableRenderMesh *s_pLastSkinned = NULL;
			static int s_nLastSkinnedFrame = -1;
			const int nFrame = GetFrameID(true);
			if (obj->m_pCharInstance != s_pLastSkinned || nFrame != s_nLastSkinnedFrame)
			{
				s_pLastSkinned = obj->m_pCharInstance;
				s_nLastSkinnedFrame = nFrame;
				CLeafBuffer *pVertexContainer = leaf->GetVertexContainer();
				const bool bForceUpdate = pVertexContainer &&
					pVertexContainer->m_UpdateFrame == (unsigned)nFrame;
				obj->m_pCharInstance->ProcessSkinning(obj->m_Matrix.GetTranslationOLD(),
					obj->m_Matrix, obj->m_nTemplId, obj->m_nLod, bForceUpdate);
			}
		}

		const char *vitaShaderName = (ef && ef->GetName()) ? ef->GetName() : "";
		// Collision-only proxy: has no texture, so drawing it painted white.
		if (VitaMaterial::IsNoDrawMaterial(*chunk))
			return;
		/* AI cover/hide-point meshes are editor helpers, not world art.  Their
		   intentionally loud cover_hard/cover_soft textures are the red/black
		   polygons visible in the Training hut when the desktop shader filter is
		   absent.  Reject the whole helper source before it costs a draw call. */
		if (leaf->m_sSource &&
			(VitaMaterial::NameContains(leaf->m_sSource, "objects/editor/cover/") ||
			 VitaMaterial::NameContains(leaf->m_sSource, "objects\\editor\\cover\\")))
			return;
		/* Objects/default.cgf is CryEngine's red "replace me" editor placeholder.
		   Training instantiates it for many logic/helper entities; desktop shaders
		   suppress those objects, while the compact path exposed the red lettered
		   shards in the world.  It is never shipping scene art, so reject it here. */
		if (leaf->m_sSource &&
			(VitaMaterial::NameContains(leaf->m_sSource, "objects/default.cgf") ||
			 VitaMaterial::NameContains(leaf->m_sSource, "objects\\default.cgf")))
			return;

		int renderState = GS_DEPTHWRITE;
		/* Cut-out foliage rule -- but only for static geometry.  The test is a
		   guess made from the shader's name, and character diffuse maps
		   routinely carry a specular/gloss mask in alpha rather than coverage.
		   Applying a >=0.5 alpha test to one of those discards every fragment
		   and the character vanishes completely, which is exactly the symptom.
		   Skinned meshes are never cut-out foliage, so exclude them. */
		const bool bSkinnedMesh = leaf->m_pVertexBuffer->m_bDynamic != 0;
		if (!bSkinnedMesh && VitaMaterial::NeedsAlphaTest(vitaShaderName))
			renderState |= GS_ALPHATEST_GEQUAL128;
		const bool bNameShadow = VitaMaterial::IsModulativeShadow(vitaShaderName);
		const bool bNameAdditive = VitaMaterial::NeedsAdditiveBlend(vitaShaderName);
		const bool bNameAlpha = VitaMaterial::NeedsAlphaBlend(vitaShaderName);
		if (sr)
		{
			// Same reasoning as above: never alpha-test a skinned mesh.
			if (!bSkinnedMesh)
			{
				if (sr->m_AlphaRef >= 0.5f) renderState |= GS_ALPHATEST_GEQUAL128;
				else if (sr->m_AlphaRef > 0.0f) renderState |= GS_ALPHATEST_GEQUAL64;
			}
			/* Additive is a property of the material, not of its opacity.  This
			   only reached for the blend at all when m_Opacity was below 1, so an
			   additive material authored at full opacity -- which is the normal
			   way to author a muzzle flash, a tracer, a glow or a light halo --
			   was drawn with blending switched off entirely.  Those effects are
			   painted on a black backing square that additive blending is meant
			   to make disappear, so instead of the effect you get the square:
			   the black boxes behind shots and lights.

			   The retail path takes its blend from the shader script (Blend=ONE
			   ONE), which likewise has nothing to do with opacity.  Test the flag
			   first, and keep the opacity test for ordinary translucency. */
			const bool bAdditive = (sr->m_ResFlags & (MTLFLAG_ADDITIVE | MTLFLAG_ADDITIVEDECAL)) != 0 || bNameAdditive;
			if (bNameShadow)
			{
				renderState &= ~(GS_DEPTHWRITE | GS_ALPHATEST_MASK);
				renderState |= GS_BLSRC_ZERO | GS_BLDST_SRCCOL;
			}
			else if (bAdditive)
			{
				renderState |= (GS_BLSRC_ONE | GS_BLDST_ONE);
				// A black source contributes nothing under additive blending, so an
				// alpha test would only punch holes in the parts that do glow.
				renderState &= ~GS_ALPHATEST_MASK;
				/* Effect cards must never write their rectangular silhouette into
				   depth; black texels add no colour but would still hide later draws. */
				renderState &= ~GS_DEPTHWRITE;
			}
			else if (sr->m_Opacity < 0.999f || bNameAlpha)
			{
				renderState &= ~GS_DEPTHWRITE;
				renderState |= (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
			}
			/* Several CE1 character submeshes have mirrored winding.  The desktop
			   character shader compensates for it; fixed back-face culling made
			   limbs and equipment blink out as animations changed pose. */
			SetCullMode((bSkinnedMesh || (sr->m_ResFlags & MTLFLAG_2SIDED)) ? R_CULL_NONE : R_CULL_BACK);
		}
		else
			SetCullMode(bSkinnedMesh ? R_CULL_NONE : R_CULL_BACK);
		SetState(renderState);

		SEfResTexture *diffuse = sr ? sr->m_Textures[EFTT_DIFFUSE] : NULL;
		/* No-draw AI cover volumes are also embedded as spare material slots in
		   ordinary props and vegetation.  Their source CGF is therefore not under
		   Objects/Editor, but their intentionally loud helper texture is unique. */
		if (diffuse &&
			(VitaMaterial::NameContains(diffuse->m_Name.c_str(), "cover_hard") ||
			 VitaMaterial::NameContains(diffuse->m_Name.c_str(), "cover_soft")))
			return;
		bool bVitaWaterTextureMatrix = false;
		/* A diffuse that has already been searched for and not found must not
		   come back here every frame.  The budget above was spent on those first
		   -- the load is attempted, fails, m_ITexPic stays null, and the whole
		   thing repeats on the next frame, forever.  Two absent textures in view
		   were therefore enough to consume the entire per-frame allowance on
		   nothing, which left every chunk whose texture WOULD have loaded stuck
		   in the skip path below permanently: geometry that never appears, and
		   geometry that appears and vanishes again as view order shifts which
		   chunks reach the front of the queue.  Record the ones that have been
		   given up on -- keyed on both halves of what the search actually uses,
		   since the fallback below composes the material's own folder with the
		   name -- and let them fall straight through to the untextured draw. */
		const bool bDiffusePending = diffuse && !diffuse->m_TU.m_ITexPic && !diffuse->m_Name.empty();
		std::string szGaveUpKey;
		bool bDiffuseGivenUp = false;
		if (bDiffusePending)
		{
			szGaveUpKey = sr->m_TexturePath.c_str();
			szGaveUpKey += '|';
			szGaveUpKey += diffuse->m_Name.c_str();
			bDiffuseGivenUp = m_LazyDiffuseGaveUp.find(szGaveUpKey) != m_LazyDiffuseGaveUp.end();
		}
		if (bDiffusePending && !bDiffuseGivenUp &&
			g_nLazyTextureLoadsThisFrame < g_nLazyTextureBudgetThisFrame)
		{
			++g_nLazyTextureLoadsThisFrame;
			std::string textureName = diffuse->m_Name.c_str();
			ITexPic *loaded = EF_LoadTexture(textureName.c_str(), diffuse->m_TU.GetTexFlags(),
				diffuse->m_TU.GetTexFlags2(), eTT_Base, 1.0f, 1.0f, -1, -1);
			/* Only prefix the material's folder onto a bare file name.  Many
			   materials already name the texture from the data root, and
			   prepending there built nonsense like
			   "Objects/Indoor/boxes/crates/Objects/Indoor/crates/crate1.dds",
			   which of course missed -- and a missed diffuse draws solid
			   white, which is where a lot of the white surfaces came from. */
			if (!loaded && !sr->m_TexturePath.empty())
			{
				std::string lowerName = textureName;
				for (size_t i = 0; i < lowerName.size(); ++i)
					lowerName[i] = (char)tolower((unsigned char)lowerName[i]);
				for (size_t i = 0; i < lowerName.size(); ++i)
					if (lowerName[i] == '\\') lowerName[i] = '/';
				const bool bAlreadyRooted =
					lowerName.compare(0, 8, "objects/") == 0 ||
					lowerName.compare(0, 9, "textures/") == 0 ||
					lowerName.compare(0, 7, "levels/") == 0 ||
					lowerName.compare(0, 10, "materials/") == 0;
				if (!bAlreadyRooted)
				{
					std::string fullName = sr->m_TexturePath.c_str();
					if (fullName[fullName.size()-1] != '/' && fullName[fullName.size()-1] != '\\')
						fullName += '/';
					fullName += textureName;
					loaded = EF_LoadTexture(fullName.c_str(), diffuse->m_TU.GetTexFlags(),
						diffuse->m_TU.GetTexFlags2(), eTT_Base, 1.0f, 1.0f, -1, -1);
				}
				/* A rooted name that misses is not the end of it.  The device log
				   has the crate material naming "Objects\Indoor\crates\crate2.dds"
				   while its own folder is "Objects\Indoor\boxes\crates\" -- the
				   name is rooted, so the composition above is skipped, and the
				   rooted path does not exist because the asset actually sits under
				   boxes/.  The folder recorded on the material is the reliable part
				   in that situation, so try it against the bare file name too.
				   Only after the name as authored has already failed, so a correct
				   rooted path still wins and this costs nothing for it. */
				if (!loaded)
				{
					const size_t nSlash = textureName.find_last_of("/\\");
					if (nSlash != std::string::npos)
					{
						std::string fullName = sr->m_TexturePath.c_str();
						if (!fullName.empty() &&
							fullName[fullName.size()-1] != '/' && fullName[fullName.size()-1] != '\\')
							fullName += '/';
						fullName += textureName.substr(nSlash + 1);
						loaded = EF_LoadTexture(fullName.c_str(), diffuse->m_TU.GetTexFlags(),
							diffuse->m_TU.GetTexFlags2(), eTT_Base, 1.0f, 1.0f, -1, -1);
					}
				}
			}
			diffuse->m_TU.m_ITexPic = loaded;
			if (!loaded)
				m_LazyDiffuseGaveUp.insert(szGaveUpKey);
		}
		else if (bDiffusePending && !bDiffuseGivenUp)
		{
			/* Its load was pushed to a later frame by the budget above.  Drawing
			   it now would paint the surface solid white until the texture
			   arrives, which is the white flashing seen while moving -- leave
			   the chunk out for a frame instead and let it appear textured.
			   Count it, so next frame's budget is sized to clear the backlog
			   rather than trickling through it.  Textures already given up on are
			   excluded, or those surfaces would never be drawn at all. */
			++g_nChunksWaitingOnTextureThisFrame;
			return;
		}
		if (diffuse && diffuse->m_TU.m_ITexPic)
		{
			/* Cut-out foliage often arrives through a generic fallback shader, so
			   the shader-name test above cannot identify it.  When an explicitly
			   alpha-bearing texture is attached to natural/foliage geometry, use
			   alpha test instead of drawing its pale rectangular backing.  Keep this
			   source-scoped: alpha on ordinary opaque materials is commonly gloss. */
			const char *szSource = leaf->m_sSource ? leaf->m_sSource : "";
			const bool bNaturalCutout = !bSkinnedMesh &&
				(VitaMaterial::NameContains(szSource, "objects/natural/") ||
				 VitaMaterial::NameContains(szSource, "objects\\natural\\") ||
				 VitaMaterial::NameContains(szSource, "foliage") ||
				 VitaMaterial::NameContains(szSource, "vegetation") ||
				 VitaMaterial::NameContains(szSource, "bush") ||
				 VitaMaterial::NameContains(szSource, "grass"));
			if (bNaturalCutout && (diffuse->m_TU.m_ITexPic->GetFlags() & FT_HASALPHA) &&
				!(renderState & (GS_ALPHATEST_MASK | GS_BLEND_MASK)))
			{
				renderState |= GS_ALPHATEST_GEQUAL128;
				SetState(renderState);
			}
			SetTexture(diffuse->m_TU.m_ITexPic->GetTextureID(), eTT_Base);
		}
		else if (leafElement->m_CustomTexBind[0] > 0 && leafElement->m_CustomTexBind[0] != 0x1000)
			SetTexture(leafElement->m_CustomTexBind[0], eTT_Base);
		else
		{
			/* Same white fallback CLeafBuffer::Draw has, and it needs the same
			   reporting: geometry submitted through EF_AddEf (terrain sectors,
			   the ocean plane, anything the 3D engine queues rather than draws
			   itself) was silently rendering pure white with nothing naming it. */
			const bool bWaterSurface = VitaMaterial::IsWaterSurface(vitaShaderName);
			const bool bCPUWaveWater = bWaterSurface && leaf && leaf->m_sSource &&
				(VitaMaterial::NameContains(leaf->m_sSource, "OutdoorWater") ||
				 VitaMaterial::NameContains(leaf->m_sSource, "WaterOcean"));
			static int s_nEfWaterTexture = 0;
			if (bWaterSurface && !s_nEfWaterTexture)
			{
				ITexPic *pWater = EF_LoadTexture("Textures/water_lm.dds",
					FT_NOREMOVE, 0, eTT_Base, 1.0f, 1.0f, -1, -1);
				if (pWater) s_nEfWaterTexture = pWater->GetTextureID();
			}
			if (bWaterSurface && s_nEfWaterTexture > 0)
				SetTexture(s_nEfWaterTexture, eTT_Base);
			else
				SetWhiteTexture();
			/* Except water, which legitimately has no diffuse to find.  The log
			   names TerrainWater_OnlySky, TerrainWaterBeach and the outdoor water
			   circle with diffuse=<null>, and those are big surfaces -- painted
			   white they are the sheets of blown-out white across the level.
			   Give them a translucent blue-green instead of leaving them at the
			   untextured default. */
			if (bWaterSurface)
			{
				/* The generated ocean UVs are world-sized (the trace starts around
				   75,228).  Sampling them 1:1 turns a 256px water map into obvious
				   hard bands.  Reduce their frequency and keep the animated UV drift;
				   the matrix is restored immediately after this water draw. */
				glActiveTexture(GL_TEXTURE0);
				glMatrixMode(GL_TEXTURE);
				glPushMatrix();
				glLoadIdentity();
				glScalef(0.04f, 0.04f, 1.0f);
				glMatrixMode(GL_MODELVIEW);
				bVitaWaterTextureMatrix = true;
				/* Keep the generated geometry/alpha but use a stable water tint.  The
				   former high-contrast crest vertex colours amplified the coarse radial
				   mesh into dark angular bands instead of the PC water surface. */
				g_bSurfaceTintActive = true;
				g_arrSurfaceTint[0] = 0.25f; g_arrSurfaceTint[1] = 0.47f;
				g_arrSurfaceTint[2] = 0.58f; g_arrSurfaceTint[3] = 0.68f;
				if (bCPUWaveWater)
					g_bUntexturedSurface = false;
				g_bForceVertexColours = false;
				/* Replace, do not OR into, the previous blend mode.  Combining two
				   encoded GS blend masks falls through the GL mapper and made water
				   opaque on the device. */
				renderState &= ~(GS_DEPTHWRITE | GS_BLEND_MASK);
				renderState |= GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
				SetState(renderState);
			}
			#if defined(VITA_PERF_TELEMETRY)
			static std::map<std::string, bool> s_reportedWhite;
			static unsigned int s_nWhiteReportChecks = 0;
			if (s_nWhiteReportChecks < 4096 && s_reportedWhite.size() < 256)
			{
				++s_nWhiteReportChecks;
				std::string key = (ef && ef->GetName()) ? ef->GetName() : "<null-shader>";
				key += "|";
				if (diffuse)
					key += diffuse->m_Name.c_str();
				if (s_reportedWhite.find(key) == s_reportedWhite.end())
				{
					s_reportedWhite[key] = true;
					if (iLog)
						iLog->LogToFile("\001[VITA][EFWHITE] shader=%s diffuse=%s tris=%d",
							(ef && ef->GetName()) ? ef->GetName() : "<null>",
							diffuse ? diffuse->m_Name.c_str() : "<none>",
							chunk->nNumIndices / 3);
				}
			}
			#endif
		}

		/* Baked lighting.  Far Cry stores the world's static lighting in
		   lightmaps, not in the vertex colours, so drawing only the diffuse
		   texture leaves everything evenly lit and flat -- no sun, no shadow,
		   no bounce.  CBrush::Render hands the colour/lerp lightmap and its own
		   texture-coordinate buffer down on the render object, so modulate it in
		   on texture unit 1.

		   The Vita leaf builder records the original unwelded-corner mapping and
		   CBrush::SetLightmap resamples that stream into the primary vertex order.
		   Brushes whose old data genuinely cannot be mapped are rejected there and
		   deliberately remain unlit rather than receiving corrupt coordinates. */
		bool bLightMapBound = false;
		g_bVitaLightMapActive = false;
		if (obj && obj->m_pLMTCBufferO && LightMapsEnabled())
		{
			/* Take the lightmap from the render object, not from the material.
			   sr->m_Textures[EFTT_LIGHTMAP] is the material's own lightmap slot
			   and Far Cry does not use it for baked brush lighting -- the baked
			   map is a per-brush texture the 3D engine hands down on the object
			   as m_nLMId (CStatObj::Render fills it from
			   RenderLMData::GetColorLerpTex).  Reading the material slot meant
			   lmTex was 0 for every brush in the level, so the whole pass below
			   was skipped every time and no baked lighting was ever applied,
			   however well the coordinates had been resampled to reach it.  Keep
			   the material slot as a fallback for anything that does use it. */
			SEfResTexture *lmTexture = sr ? sr->m_Textures[EFTT_LIGHTMAP] : NULL;
			int lmTex = (obj && obj->m_nLMId > 0) ? obj->m_nLMId : 0;
			if (!lmTex && lmTexture && lmTexture->m_TU.m_ITexPic)
				lmTex = lmTexture->m_TU.m_ITexPic->GetTextureID();
			CVertexBuffer *lmVB = obj->m_pLMTCBufferO->m_pVertexBuffer;
			const byte *lmData = lmVB ? (const byte *)lmVB->m_VS[VSF_GENERAL].m_VData : NULL;
			/* The coordinate array is indexed here by the primary buffer's
			   vertex numbering, but CBrush::SetLightmap sized it by the
			   SECONDARY buffer (m_SecVertCount).  Where those counts agree the
			   numbering lines up and the mapping is valid; where they do not,
			   binding it reads coordinates belonging to other vertices and
			   paints black and white blotches instead of shading.  Require the
			   array to cover every vertex we will index rather than trusting
			   that it does. */
			const bool bCoordsCoverMesh = lmVB && leaf->m_pVertexBuffer &&
				lmVB->m_NumVerts >= leaf->m_pVertexBuffer->m_NumVerts;
			if (lmTex > 0 && lmData && bCoordsCoverMesh)
			{
				/* VERTEX_FORMAT_TEX2F: two floats per vertex, nothing else.  The
				   cached helper retains the unit-1 combiner across adjacent brushes. */
				VitaEnableLightMapStage((GLuint)lmTex,
					m_VertexSize[lmVB->m_vertexformat], lmData);
				bLightMapBound = true;
			}
		}

		const bool hasObjectTransform = obj && (obj->m_ObjFlags & FOB_TRANS_MASK);
		if (hasObjectTransform)
		{
			PushMatrix();
			MultMatrix(obj->m_Matrix.GetData());
		}
		DrawBuffer(leaf->m_pVertexBuffer, &leaf->m_Indices, chunk->nNumIndices,
			chunk->nFirstIndexId, leaf->m_nPrimetiveType, chunk->nFirstVertId,
			chunk->nFirstVertId + chunk->nNumVerts, chunk);
		// Cleared immediately: a tint left set would recolour every later draw
		// that has no vertex colours of its own.
		g_bSurfaceTintActive = false;
		g_bForceVertexColours = false;
		if (bVitaWaterTextureMatrix)
		{
			glActiveTexture(GL_TEXTURE0);
			glMatrixMode(GL_TEXTURE);
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW);
		}
		if (hasObjectTransform)
			PopMatrix();

		if (bLightMapBound)
			g_bVitaLightMapActive = false;

		#if defined(VITA_PERF_TELEMETRY)
		static std::map<std::string, bool> reportedCharacterBuffers;
		static unsigned int s_nCharacterReportChecks = 0;
		if (s_nCharacterReportChecks < 2048 && reportedCharacterBuffers.size() < 16)
		{
			++s_nCharacterReportChecks;
			const std::string source = leaf->m_sSource ? leaf->m_sSource : "<null>";
			if (reportedCharacterBuffers.find(source) == reportedCharacterBuffers.end())
			{
				reportedCharacterBuffers[source] = true;
				sceClibPrintf("[VITARE] source=%s fmt=%d verts=%d inds=%d shader=%s tex=%d\n",
					source.c_str(), leaf->m_pVertexBuffer->m_vertexformat,
					leaf->m_pVertexBuffer->m_NumVerts, chunk->nNumIndices,
					ef ? ef->GetName() : "<null>",
					(diffuse && diffuse->m_TU.m_ITexPic) ? diffuse->m_TU.m_ITexPic->GetTextureID() : leafElement->m_CustomTexBind[0]);
				fflush(stdout);
			}
		}
		#endif
		return;
	}
	if (re->mfGetType() == eDATA_Sky)
	{
		static bool reportedSkySubmit = false;
		CVitaShader *vitaShader = dynamic_cast<CVitaShader *>(ef);
		if (!reportedSkySubmit && iLog)
		{
			reportedSkySubmit = true;
			iLog->LogToFile("\001[VITA][SKY] eDATA_Sky submitted with shader '%s'", ef ? ef->GetName() : "<null>");
		}
		DrawVitaSkyBox(this, static_cast<CRESky *>(re), vitaShader, obj);
	}
#endif
}
void CVitaRenderer::EF_EndEf3D(int nFlags)
{
	if (SRendItem::m_RecurseLevel > 0)
		--SRendItem::m_RecurseLevel;
}
bool CVitaRenderer::EF_IsFakeDLight(CDLight * Source) { return Source == NULL; }
void CVitaRenderer::EF_ADDDlight(CDLight * Source)
{
	/* C3DEngine uses the assigned ID to build per-object light masks even on
	   the low fixed-function path. Leaving it at -1 made the engine reject
	   every visible light and emit an assert for each one every frame. */
	if (!Source || m_nActiveLights >= 32)
		return;
	Source->m_Id = m_nActiveLights++;
}
void CVitaRenderer::EF_ClearLightsList() { m_nActiveLights = 0; }
bool CVitaRenderer::EF_UpdateDLight(CDLight * pDL) { return pDL != NULL; }
void CVitaRenderer::EF_EndEf2D(bool bSort)
{
	if (SRendItem::m_RecurseLevel > 0)
		--SRendItem::m_RecurseLevel;
}
bool CVitaRenderer::EF_DrawEfForName(char * name, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEf(IShader * ef, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawPartialEfForName(char * name, SVrect * vr, SVrect * pr, CFColor& col) { return false; }
bool CVitaRenderer::EF_DrawPartialEfForNum(int num, SVrect * vr, SVrect * pr, CFColor& col) { return false; }
bool CVitaRenderer::EF_DrawPartialEf(IShader * ef, SVrect * vr, SVrect * pr, CFColor& col, float iwdt, float ihgt) { return false; }
void * CVitaRenderer::EF_Query(int Query, int Param)
{
	switch (Query)
	{
		case EFQ_RecurseLevel:
			return (void *)(INT_PTR)SRendItem::m_RecurseLevel;
		case EFQ_Pointer2FrameID:
			return (void *)&m_nFrameId;
		default:
			return 0;
	}
}
void CVitaRenderer::EF_ConstructEf(IShader * Ef) { }
void CVitaRenderer::EF_SetWorldColor(float r, float g, float b, float a) { }
int CVitaRenderer::EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex, bool bCaustics) { return 0; }
int CVitaRenderer::GetPolyCount() { return 0; }
void CVitaRenderer::GetPolyCount(int & nPolygons, int & nShadowVolPolys) { }
void CVitaRenderer::SetClearColor(const Vec3 & vColor) { m_vClearColor = vColor; }
// Vita: real implementations in engine_port/LeafBufferVita.cpp, alongside
// the rest of CLeafBuffer's Vita-side support.
int CVitaRenderer::GetFrameID(bool bIncludeRecursiveCalls) { return m_nFrameId; }
void CVitaRenderer::MakeMatrix(const Vec3 & pos, const Vec3 & angles, const Vec3 & scale, Matrix44* mat) { }
void CVitaRenderer::DrawLabelImage(const Vec3 & vPos, float fSize, int nTextureId) { }
void CVitaRenderer::DrawLabel(Vec3 pos, float font_size, const char * label_text, ...) { }
void CVitaRenderer::DrawLabelEx(Vec3 pos, float font_size, float * pfColor, bool bFixedSize, bool bCenter, const char * label_text, ...) { }
void CVitaRenderer::Draw2dLabel(float x, float y, float font_size, float * pfColor, bool bCenter, const char * label_text, ...) { }
float CVitaRenderer::ScaleCoordX(float value) { return value * (float)m_nWidth / 800.0f; }
float CVitaRenderer::ScaleCoordY(float value) { return value * (float)m_nHeight / 600.0f; }
bool CVitaRenderer::EnableFog(bool enable)
{
	bool previous = s_vitaFogEnabled;
	s_vitaFogEnabled = enable;
#if defined(LINUX)
	if (enable)
		glEnable(GL_FOG);
	else
		glDisable(GL_FOG);
#endif
	return previous;
}
void CVitaRenderer::SetFog(float density, float fogstart, float fogend, const float * color, int fogmode)
{
	s_vitaFogDensity = density;
	s_vitaFogStart = fogstart;
	s_vitaFogEnd = fogend;
	s_vitaFogMode = fogmode;
	if (color)
		s_vitaFogColor = CFColor(color[0], color[1], color[2], 1.0f);
#if defined(LINUX)
	glFogi(GL_FOG_MODE, fogmode == R_FOGMODE_EXP2 ? GL_EXP2 : GL_LINEAR);
	glFogf(GL_FOG_DENSITY, density);
	glFogf(GL_FOG_START, fogstart);
	glFogf(GL_FOG_END, fogend);
	glFogfv(GL_FOG_COLOR, &s_vitaFogColor[0]);
#endif
}
void CVitaRenderer::EnableTexGen(bool enable) { }
void CVitaRenderer::SetTexgen(float scaleX, float scaleY, float translateX, float translateY) { }
void CVitaRenderer::SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2) { }
void CVitaRenderer::SetLodBias(float value) { }
void CVitaRenderer::SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa)
{
#if defined(LINUX)
	glActiveTexture(GL_TEXTURE0);
	VitaFixedFunction::ApplyColorOp(eCo, eAo, eCa, eAa, g_arrVitaMaterialColor);
	g_bVitaCustomColorOp = true;
#endif
}
void CVitaRenderer::EnableVSync(bool enable) { }
void CVitaRenderer::EnableTMU(bool enable) { }
void CVitaRenderer::SelectTMU(int tnum) { }
/* Real dynamic textures.  CUIVideoPanel uploads a decoded video frame through
   these every frame, so they had to stop being stubs before any cut scene could
   appear.  Only the 32-bit paths the video panel uses are handled; the source
   buffer is always four bytes per pixel here. */
unsigned int CVitaRenderer::DownLoadToVideoMemory(unsigned char * data, int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat, int filter, int Id, char * szCacheName, int flags)
{
#if defined(LINUX)
	/* Texture dimensions ultimately reach vitaGL/GXM allocation arithmetic.
	   Refuse corrupt or nonsensical requests here, before a wrapped byte count
	   can become an undersized allocation followed by a driver memcpy. */
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096 ||
		(size_t)w > ((size_t)-1) / (size_t)h)
		return 0;
	GLuint tex = 0;
	glGenTextures(1, &tex);
	if (!tex)
		return 0;
	glBindTexture(GL_TEXTURE_2D, tex);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	/* The source format was being ignored and everything uploaded as RGBA8888.
	   The terrain sector textures come through here as raw DXT1 straight out of
	   terrain/cover.ctc, so a 128x128 tile handed over 8 KB of blocks and this
	   read 64 KB of it as uncompressed pixels -- 56 KB past the end of the
	   buffer, with whatever it found becoming the "texture".  That is where the
	   white/garbage terrain came from, and it was corrupting the heap besides. */
	int nFourCC = DDS_FOURCC_NONE;
	switch (eTFSrc)
	{
		case eTF_DXT1: nFourCC = DDS_FOURCC_DXT1; break;
		case eTF_DXT3: nFourCC = DDS_FOURCC_DXT3; break;
		case eTF_DXT5: nFourCC = DDS_FOURCC_DXT5; break;
		default: break;
	}
	if (nFourCC != DDS_FOURCC_NONE && data)
	{
		GLenum ePixelType = GL_UNSIGNED_SHORT_4_4_4_4;
		byte *pDecoded = VitaDecodeDXTLevel16(data, w, h, w, h, nFourCC, &ePixelType);
		if (!pDecoded)
		{
			glDeleteTextures(1, &tex);
			return 0;
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
			GL_RGBA, ePixelType, pDecoded);
		delete [] pDecoded;
	}
	else
	{
		// data may be null: the caller then fills it with UpdateTextureInVideoMemory.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	}
	if (glGetError() != GL_NO_ERROR)
	{
		glDeleteTextures(1, &tex);
		return 0;
	}
	return (unsigned int)tex;
#else
	return 0;
#endif
}

void CVitaRenderer::UpdateTextureInVideoMemory(uint tnum, unsigned char * newdata, int posx, int posy, int w, int h, ETEX_Format eTFSrc)
{
#if defined(LINUX)
	if (!tnum || !newdata || posx < 0 || posy < 0 ||
		w <= 0 || h <= 0 || w > 4096 || h > 4096 ||
		(size_t)w > ((size_t)-1) / (size_t)h)
		return;
	glBindTexture(GL_TEXTURE_2D, (GLuint)tnum);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	// Same format trap as DownLoadToVideoMemory: the terrain texture pool feeds
	// DXT1 blocks through here, which must not be read as RGBA8888.
	int nFourCC = DDS_FOURCC_NONE;
	switch (eTFSrc)
	{
		case eTF_DXT1: nFourCC = DDS_FOURCC_DXT1; break;
		case eTF_DXT3: nFourCC = DDS_FOURCC_DXT3; break;
		case eTF_DXT5: nFourCC = DDS_FOURCC_DXT5; break;
		default: break;
	}
	if (nFourCC != DDS_FOURCC_NONE)
	{
		GLenum ePixelType = GL_UNSIGNED_SHORT_4_4_4_4;
		byte *pDecoded = VitaDecodeDXTLevel16(newdata, w, h, w, h, nFourCC, &ePixelType);
		if (pDecoded)
		{
			/* Terrain-pool updates replace an existing same-sized DXT tile.
			   Reallocating it with glTexImage2D on every LOD transition churned
			   vitaGL's mapped allocator and could temporarily double the texture's
			   footprint.  Update the resident storage in place, exactly like the
			   uncompressed/video path below. */
			glTexSubImage2D(GL_TEXTURE_2D, 0, posx, posy, w, h,
				GL_RGBA, ePixelType, pDecoded);
			delete [] pDecoded;
		}
	}
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, posx, posy, w, h, GL_RGBA, GL_UNSIGNED_BYTE, newdata);
#endif
}
unsigned int CVitaRenderer::LoadTexture(const char * filename, int * tex_type, unsigned int def_tid, bool compresstodisk, bool bWarn) { return 0; }
bool CVitaRenderer::DXTCompress(byte * raw_data, int nWidth, int nHeight, ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback) { return false; }
bool CVitaRenderer::DXTDecompress(byte * srcData, byte * dstData, int nWidth, int nHeight, ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix) { return false; }
void CVitaRenderer::RemoveTexture(unsigned int TextureId)
{
	std::map<int, CVitaTexPic *>::iterator it = m_TextureById.find((int)TextureId);
	if (it == m_TextureById.end())
	{
#if defined(LINUX)
		/* Not every texture id belongs to a CVitaTexPic.  DownLoadToVideoMemory
		   hands back a bare GL name, and the terrain texture pool -- which runs
		   with pooling disabled, so it allocates a fresh texture for every
		   sector LOD change and releases the old one through here -- is the
		   heaviest user of that path.  Returning without deleting leaked one
		   texture per sector per LOD step for the lifetime of the level, and
		   once video memory ran out new uploads started failing, which shows up
		   as surfaces turning white.  Nothing else owns these, so free it. */
		if (TextureId && glIsTexture((GLuint)TextureId))
		{
			GLuint nName = (GLuint)TextureId;
			glDeleteTextures(1, &nName);
		}
#endif
		return;
	}
	CVitaTexPic *pTexture = it->second;
	/* Script texture userdata is garbage-collected after its numeric ID has
	   been copied into long-lived UI widgets. The retail texture manager
	   honours FT_NOREMOVE here; deleting it made GL recycle the ID for a
	   font atlas, so the mouse cursor rendered arbitrary glyph blocks. */
	if (pTexture->GetFlags() & FT_NOREMOVE)
		return;
	m_TextureById.erase(it);
	pTexture->Release(true);
}
void CVitaRenderer::RemoveTexture(ITexPic * pTexPic)
{
	if (pTexPic)
		RemoveTexture((unsigned int)pTexPic->GetTextureID());
}
void CVitaRenderer::TextToScreen(float x, float y, const char * format, ...) { }
void CVitaRenderer::TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ...) { }
void CVitaRenderer::ResetToDefault()
{
#if defined(LINUX)
	VitaResetFixedFunctionMaterial();
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	VitaSetConstantColor(1, 1, 1, 1);
	VitaInvalidateRenderStateCache();
	SetViewport(0, 0, m_nWidth, m_nHeight);
#endif
}
int CVitaRenderer::GenerateAlphaGlowTexture(float k) { return 0; }
void CVitaRenderer::SetMaterialColor(float r, float g, float b, float a)
{
#if defined(LINUX)
	g_arrVitaMaterialColor[0] = r;
	g_arrVitaMaterialColor[1] = g;
	g_arrVitaMaterialColor[2] = b;
	g_arrVitaMaterialColor[3] = a;
	glActiveTexture(GL_TEXTURE0);
	glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, g_arrVitaMaterialColor);
	VitaSetConstantColor(r, g, b, a);
#endif
}
int CVitaRenderer::LoadAnimatedTexture(const char * format, const int nCount) { return 0; }
void CVitaRenderer::RemoveAnimatedTexture(AnimTexInfo * pInfo) { }
AnimTexInfo * CVitaRenderer::GetAnimTexInfoFromId(int nId) { return 0; }
void CVitaRenderer::Draw2dLine(float x1, float y1, float x2, float y2)
{
	DrawLineColor(Vec3(x1, y1, 0.0f), CFColor(1,1,1,1),
		Vec3(x2, y2, 0.0f), CFColor(1,1,1,1));
}
void CVitaRenderer::SetLineWidth(float fWidth)
{
#if defined(LINUX)
	glLineWidth(max(1.0f, fWidth));
#endif
}
void CVitaRenderer::DrawLine(const Vec3 & vPos1, const Vec3 & vPos2)
{
	DrawLineColor(vPos1, CFColor(1,1,1,1), vPos2, CFColor(1,1,1,1));
}
void CVitaRenderer::DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2)
{
#if defined(LINUX)
	if (!g_bVitaLightMapActive)
		VitaDisableLightMapStage();
	const float verts[6] = {
		vPos1.x, vPos1.y, vPos1.z,
		vPos2.x, vPos2.y, vPos2.z
	};
	const unsigned char colors[8] = {
		(unsigned char)(clamp_tpl(vColor1.r, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor1.g, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor1.b, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor1.a, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor2.r, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor2.g, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor2.b, 0.0f, 1.0f) * 255.0f),
		(unsigned char)(clamp_tpl(vColor2.a, 0.0f, 1.0f) * 255.0f)
	};
	SetTexture(0, eTT_Base);
	VitaBindArrayBuffer(0);
	VitaBindElementBuffer(0);
	VitaInvalidateVertexPointerCache();
	VitaSetClientArrayState(GL_VERTEX_ARRAY, true);
	VitaSetClientArrayState(GL_COLOR_ARRAY, true);
	VitaSetClientArrayState(GL_TEXTURE_COORD_ARRAY, false);
	glVertexPointer(3, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
	VITA_DRAW_INCREMENT();
	VITA_PERF_ADD(g_nVitaDrawIndices, 2);
	VITA_PERF_INCREMENT(g_nVitaClientDraws);
	glDrawArrays(GL_LINES, 0, 2);
#endif
}
void CVitaRenderer::Graph(byte * g, int x, int y, int wdt, int hgt, int nC, int type, char * text, CFColor& color, float fScale) { }
void CVitaRenderer::DrawBall(float x, float y, float z, float radius) { }
void CVitaRenderer::DrawBall(const Vec3 & pos, float radius) { }
void CVitaRenderer::DrawPoint(float x, float y, float z, float fSize) { }
void CVitaRenderer::FlushTextMessages() { }
#if defined(LINUX)
/* Crytek's DrawObjSprites_NoBend_Merge, reduced to the fixed-function pieces
   this backend actually supports.  Keeping one small record per visible
   instance and sorting it by texture turns hundreds of distant trees into a
   handful of DrawDynVB calls.  That is both the missing visual path and a much
   cheaper representation than keeping full tree meshes alive at distance. */
struct SVitaSpriteInfo
{
	int nTextureId;
	Vec3d vPos;
	float fDX, fDY, fScaleV;
	uchar ucLodAngle;
	UCol color;
};

static bool VitaSpriteTextureLess(const SVitaSpriteInfo &a, const SVitaSpriteInfo &b)
{
	return a.nTextureId < b.nTextureId;
}

static void VitaFlushSpriteBatch(CVitaRenderer *pRenderer,
	std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> &vertices, int nTextureId)
{
	if (!pRenderer || vertices.empty() || nTextureId <= 0)
		return;
	pRenderer->SetTexture(nTextureId, eTT_Base);
	pRenderer->DrawDynVB(&vertices[0], NULL, (int)vertices.size(), 0, R_PRIMV_TRIANGLES);
	vertices.clear();
}
#endif

void CVitaRenderer::DrawObjSprites(list2<CStatObjInst*> * pList, float fMaxViewDist, CObjManager * pObjMan)
{
#if defined(LINUX)
	if (!pList || !pObjMan || pList->Count() <= 0 || fMaxViewDist <= 0.0f)
		return;

	const Vec3d vCamPos = m_Camera.GetPos();
	const Vec3d vWorldColor = iSystem && iSystem->GetI3DEngine()
		? iSystem->GetI3DEngine()->GetWorldColor() : Vec3d(1.0f, 1.0f, 1.0f);
	const float fMaxSpriteViewDist = fMaxViewDist * 0.8f;
	const float fRadToDeg = 180.0f / gf_PI;
	int nRecurseIndex = SRendItem::m_RecurseLevel - 1;
	if (nRecurseIndex < 0) nRecurseIndex = 0;
	if (nRecurseIndex > 2) nRecurseIndex = 2;

	std::vector<SVitaSpriteInfo> sprites;
	sprites.reserve((size_t)pList->Count());
	for (int i = pList->Count() - 1; i >= 0; --i)
	{
		CStatObjInst *o = pList->GetAt(i);
		if (!o || o->m_nObjectTypeID >= pObjMan->m_lstStaticTypes.Count())
			continue;
		StatInstGroup &group = pObjMan->m_lstStaticTypes[o->m_nObjectTypeID];
		CStatObj *pLod0 = group.GetStatObj();
		if (!pLod0)
			continue;
		CStatObj *pLowest = pLod0;
		if (pLod0->m_nLoadedLodsNum > 0 &&
			pLod0->m_arrpLowLODs[pLod0->m_nLoadedLodsNum - 1])
			pLowest = pLod0->m_arrpLowLODs[pLod0->m_nLoadedLodsNum - 1];

		const float fDistance = ((IEntityRender *)o)->m_arrfDistance[nRecurseIndex];
		if (fDistance <= 0.001f)
			continue;
		float fObjectMaxDist = o->GetMaxViewDist();
		if (fObjectMaxDist > fMaxSpriteViewDist)
			fObjectMaxDist = fMaxSpriteViewDist;
		if (fObjectMaxDist <= 0.001f)
			continue;

		float fFade = 1.0f;
		if (group.bFadeSize)
		{
			fFade = (1.0f - (fDistance * pObjMan->m_fZoomFactor) / fObjectMaxDist) * 8.0f;
			if (fFade <= 0.0f)
				continue;
			if (fFade > 1.0f)
				fFade = 1.0f;
		}

		const Vec3d vCenter = pLowest->GetCenter() * o->m_fScale;
		const float dx = o->m_vPos.x - vCamPos.x;
		const float dy = o->m_vPos.y - vCamPos.y;
		float fAngle = fRadToDeg * cry_atan2f(vCenter.x + dx, vCenter.y + dy);
		while (fAngle < 0.0f) fAngle += 360.0f;
		const int nSlot = QRound(fAngle / (float)FAR_TEX_ANGLE + 0.5f) % FAR_TEX_COUNT;
		const int nTextureId = (int)pLod0->m_arrSpriteTexID[nSlot];
		if (nTextureId <= 0)
			continue;
		if (SRendItem::m_RecurseLevel == 1)
			o->m_ucAngleSlotId = (uchar)nSlot;

		SVitaSpriteInfo sp;
		sp.nTextureId = nTextureId;
		sp.vPos = o->m_vPos + vCenter * fFade;
		const float fScaleH = o->m_fScale * pLowest->GetRadiusHors() *
			pObjMan->m_fZoomFactor * fFade;
		sp.fScaleV = o->m_fScale * pLowest->GetRadiusVert() * fFade;
		sp.fDY = dx * fScaleH / fDistance;
		sp.fDX = dy * fScaleH / fDistance;
		sp.ucLodAngle = o->m_ucLodAngle;

		float fLight = (float)(o->m_ucBright > 32 ? o->m_ucBright : 32) /
			255.0f * group.fBrightness;
		sp.color.bcolor[0] = (byte)CLAMP((int)(vWorldColor.x * fLight * 255.0f), 0, 255);
		sp.color.bcolor[1] = (byte)CLAMP((int)(vWorldColor.y * fLight * 255.0f), 0, 255);
		sp.color.bcolor[2] = (byte)CLAMP((int)(vWorldColor.z * fLight * 255.0f), 0, 255);
		sp.color.bcolor[3] = 255;
		sprites.push_back(sp);
	}

	if (sprites.empty())
		return;
	std::sort(sprites.begin(), sprites.end(), VitaSpriteTextureLess);

	SetState(GS_ALPHATEST_GEQUAL128 | GS_DEPTHWRITE);
	SetCullMode(R_CULL_NONE);
	std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> vertices;
	vertices.reserve((size_t)min(DYNVB_CAPACITY, pList->Count() * 6));
	int nCurrentTexture = sprites[0].nTextureId;
	for (size_t i = 0; i < sprites.size(); ++i)
	{
		const SVitaSpriteInfo &sp = sprites[i];
		if (sp.nTextureId != nCurrentTexture || vertices.size() + 6 > DYNVB_CAPACITY)
		{
			VitaFlushSpriteBatch(this, vertices, nCurrentTexture);
			nCurrentTexture = sp.nTextureId;
		}

		const float x0 = sp.vPos.x + sp.fDX;
		const float x1 = sp.vPos.x - sp.fDX;
		const float y0 = sp.vPos.y + sp.fDY;
		const float y1 = sp.vPos.y - sp.fDY;
		Vec3d vUp(0.0f, 0.0f, -sp.fScaleV);
		Vec3d vAxis(-sp.fDX, sp.fDY, 0.0f);
		if (sp.ucLodAngle != 127 && vAxis.GetLengthSquared() > 0.000001f)
			vUp = vUp.rotated(vAxis.normalized(), sp.ucLodAngle / 255.0f - 0.5f);

		struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F v[6];
		v[0].xyz = Vec3(x0 + vUp.x, y1 + vUp.y, sp.vPos.z + vUp.z);
		v[1].xyz = Vec3(x1 + vUp.x, y0 + vUp.y, sp.vPos.z + vUp.z);
		v[2].xyz = Vec3(x0 - vUp.x, y1 - vUp.y, sp.vPos.z - vUp.z);
		v[3].xyz = v[1].xyz;
		v[4].xyz = Vec3(x1 - vUp.x, y0 - vUp.y, sp.vPos.z - vUp.z);
		v[5].xyz = v[2].xyz;
		/* Preserve Crytek's generated-sprite orientation.  The original GL path
		   uses 0..-1 with repeat wrapping, which is the horizontal mirror needed
		   after rendering the object into an OpenGL texture. */
		const float uv[6][2] = { {0,0}, {-1,0}, {0,1}, {-1,0}, {-1,1}, {0,1} };
		for (int n = 0; n < 6; ++n)
		{
			v[n].color = sp.color;
			v[n].st[0] = uv[n][0];
			v[n].st[1] = uv[n][1];
			vertices.push_back(v[n]);
		}
	}
	VitaFlushSpriteBatch(this, vertices, nCurrentTexture);
	SetCullMode(R_CULL_BACK);

	#if defined(VITA_PERF_TELEMETRY)
	static bool s_bReportedSprites = false;
	if (!s_bReportedSprites && iLog)
	{
		s_bReportedSprites = true;
		iLog->LogToFile("\001[VITA][SPRITES] far vegetation active instances=%u textures-generated-at-load",
			(unsigned)sprites.size());
	}
	#endif
#endif
}
void CVitaRenderer::DrawQuad(const Vec3 & right, const Vec3 & up, const Vec3 & origin, int nFlipMode) { }
void CVitaRenderer::DrawQuad(float dy, float dx, float dz, float x, float y, float z) { }
void CVitaRenderer::ClearDepthBuffer()
{
#if defined(LINUX)
	glClear(GL_DEPTH_BUFFER_BIT);
#endif
}
void CVitaRenderer::ClearColorBuffer(const Vec3 vColor)
{
#if defined(LINUX)
	glClearColor(vColor.x, vColor.y, vColor.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 1.0f);
#endif
}
void CVitaRenderer::ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX, int nScaledY) { }
void CVitaRenderer::SetFogColor(float * color)
{
	if (!color)
		return;
	s_vitaFogColor = CFColor(color[0], color[1], color[2], 1.0f);
#if defined(LINUX)
	glFogfv(GL_FOG_COLOR, &s_vitaFogColor[0]);
#endif
}
void CVitaRenderer::TransformTextureMatrix(float x, float y, float angle, float scale) { }
void CVitaRenderer::ResetTextureMatrix() { }
char* CVitaRenderer::GetVertexProfile(bool bSupportedProfile) { return 0; }
char* CVitaRenderer::GetPixelProfile(bool bSupportedProfile) { return 0; }
unsigned int CVitaRenderer::MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid)
{
#if defined(LINUX)
	if (!pStatObj || object_scale <= 0.0f || tex_size <= 0)
		return 0;
	/* FAR_TEX_SIZE is 64.  Do not let map data request an oversized render
	   target: 24 RGBA views are retained per vegetation type and Vita has only
	   128 MiB of graphics memory. */
	if (tex_size > FAR_TEX_SIZE) tex_size = FAR_TEX_SIZE;
	if (tex_size < 16) tex_size = 16;

	GLuint texture = 0;
	glGenTextures(1, &texture);
	if (!texture)
		return 0;
	glBindTexture(GL_TEXTURE_2D, texture);
	VitaInvalidateTextureCache();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_size, tex_size, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	if (glGetError() != GL_NO_ERROR)
	{
		glDeleteTextures(1, &texture);
		return 0;
	}

	static GLuint s_nSpriteFbo = 0;
	static GLuint s_nSpriteDepth = 0;
	static int s_nSpriteDepthSize = 0;
	if (!s_nSpriteFbo) glGenFramebuffers(1, &s_nSpriteFbo);
	if (!s_nSpriteDepth) glGenRenderbuffers(1, &s_nSpriteDepth);
	GLint nOldFbo = 0;
	GLint oldViewport[4] = { 0, 0, m_nWidth, m_nHeight };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &nOldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	GLboolean bOldFog = glIsEnabled(GL_FOG);
	GLboolean bOldScissor = glIsEnabled(GL_SCISSOR_TEST);

	glBindFramebuffer(GL_FRAMEBUFFER, s_nSpriteFbo);
	glBindRenderbuffer(GL_RENDERBUFFER, s_nSpriteDepth);
	if (s_nSpriteDepthSize != tex_size)
	{
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, tex_size, tex_size);
		s_nSpriteDepthSize = tex_size;
	}
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, s_nSpriteDepth);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)nOldFbo);
		glDeleteTextures(1, &texture);
		static bool s_bReportedFboFailure = false;
		if (!s_bReportedFboFailure && iLog)
		{
			s_bReportedFboFailure = true;
			iLog->LogToFile("\001[VITA][SPRITES] render target creation failed");
		}
		return 0;
	}

	glViewport(0, 0, tex_size, tex_size);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_FOG);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	CStatObj *pConcrete = static_cast<CStatObj *>(pStatObj);
	const float fRadiusH = max(pConcrete->GetRadiusHors(), 0.01f);
	const float fRadiusV = max(pConcrete->GetRadiusVert(), 0.01f);
	const float fDrawDistance = fRadiusV * object_scale;
	float fNear = fDrawDistance - fRadiusH;
	if (fNear < 0.05f) fNear = 0.05f;
	float fFar = fDrawDistance + fRadiusH;
	if (fFar <= fNear) fFar = fNear + 1.0f;
	const float fFovY = DEG2RAD(0.565f / object_scale * 200.0f);
	const float fTop = fNear * tanf(fFovY * 0.5f);
	const float fRight = fTop * (fRadiusH / fRadiusV);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glFrustum(-fRight, fRight, -fTop, fTop, fNear, fFar);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	/* gluLookAt(0,0,0, -1,0,0, 0,0,1), copied algebraically from
	   Crytek's GL MakeSprite so no GLU dependency is introduced. */
	const GLfloat lookAtMinusX[16] = {
		0,0,1,0,  1,0,0,0,  0,1,0,0,  0,0,0,1 };
	glLoadMatrixf(lookAtMinusX);
	glTranslatef(-fDrawDistance, 0.0f, 0.0f);
	glRotatef(angle, 0.0f, 0.0f, 1.0f);
	const Vec3d vCenter = (pConcrete->GetBoxMax() + pConcrete->GetBoxMin()) * 0.5f;
	glTranslatef(-vCenter.x, -vCenter.y, -vCenter.z);
	VitaSetConstantColor(1, 1, 1, 1);

	/* Sprite creation happens during level loading, outside the normal lazy
	   per-frame texture budget.  Let every material needed by this one object
	   resolve now; otherwise the first angle can be captured with missing
	   branches merely because its third material exceeded a budget of two. */
	const int nOldTextureBudget = g_nLazyTextureBudgetThisFrame;
	g_nLazyTextureBudgetThisFrame = 0x3fffffff;
	EF_StartEf();
	SRendParams rParams;
	pStatObj->Render(rParams, Vec3(zero), 0);
	EF_EndEf3D(true);
	g_nLazyTextureBudgetThisFrame = nOldTextureBudget;

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)nOldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	if (bOldFog) glEnable(GL_FOG); else glDisable(GL_FOG);
	if (bOldScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	glClearColor(m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 1.0f);
	VitaInvalidateRenderStateCache();
	VitaInvalidateTextureCache();

	char szName[256];
	snprintf(szName, sizeof(szName), "VitaSprite_%s_%d",
		pStatObj->GetFileName() ? pStatObj->GetFileName() : "object", (int)angle);
	szName[sizeof(szName) - 1] = 0;
	CVitaTexPic *pTexture = new CVitaTexPic(szName, (int)texture,
		tex_size, tex_size, NULL, FT_HASALPHA, 0);
	m_TextureById[(int)texture] = pTexture;

	/* MakeObjectPicture is editor-only in this port.  Avoid glReadPixels on
	   Vita: that path is known to fault in vitaGL and gameplay never supplies
	   pTmpBuffer here.  The generated GPU texture is the runtime contract. */
	(void)pTmpBuffer;
	(void)def_tid;
	static unsigned int s_nGeneratedSprites = 0;
	if (((++s_nGeneratedSprites) % FAR_TEX_COUNT) == 0 && iLog)
		iLog->LogToFile("\001[VITA][SPRITES] generated views=%u latest=%s size=%d",
			s_nGeneratedSprites, pStatObj->GetFileName(), tex_size);
	return (unsigned int)texture;
#else
	return 0;
#endif
}
unsigned int CVitaRenderer::Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj) { return 0; }
ShadowMapFrustum * CVitaRenderer::MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type) { return 0; }
void CVitaRenderer::Set2DMode(bool enable, int ortox, int ortoy)
{
#if defined(LINUX)
	/* ScriptObjectRenderer batches nearly the entire retail HUD into raw
	   800x600 vertices.  The desktop OpenGL backend brackets that batch with
	   an orthographic projection; leaving this function as a stub sent health,
	   ammo, stamina, stealth, weapon slots and the crosshair through the current
	   3D camera matrices, while the separately drawn compass still appeared. */
	static GLboolean s_oldFog = GL_FALSE;
	static GLboolean s_oldDepth = GL_FALSE;
	static GLboolean s_oldBlend = GL_FALSE;
	static GLboolean s_oldTexture = GL_FALSE;
	static float s_scaleStackX[16];
	static float s_scaleStackY[16];
	static int s_ignoredDepth = 0;
	if (enable)
	{
		if (g_nVita2DModeDepth >= 16)
		{
			++s_ignoredDepth;
			return;
		}
		const bool bOutermost = g_nVita2DModeDepth == 0;
		s_scaleStackX[g_nVita2DModeDepth] = g_fVita2DModeScaleX;
		s_scaleStackY[g_nVita2DModeDepth] = g_fVita2DModeScaleY;
		++g_nVita2DModeDepth;
		/* Record how this bracket's projection relates to the 800x600 canvas the
		   2D calls are authored in, so Draw2dImage can map onto it. */
		g_fVita2DModeScaleX = ortox > 0 ? (float)ortox / 800.0f : 1.0f;
		g_fVita2DModeScaleY = ortoy > 0 ? (float)ortoy / 600.0f : 1.0f;
		if (bOutermost)
		{
			VitaResetFixedFunctionMaterial();
			s_oldFog = glIsEnabled(GL_FOG);
			s_oldDepth = glIsEnabled(GL_DEPTH_TEST);
			s_oldBlend = glIsEnabled(GL_BLEND);
			s_oldTexture = glIsEnabled(GL_TEXTURE_2D);
			glDisable(GL_FOG);
			glDisable(GL_DEPTH_TEST);
		}
		glActiveTexture(GL_TEXTURE0);
		glClientActiveTexture(GL_TEXTURE0);
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glLoadIdentity();

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrthof(0.0f, (float)ortox, (float)ortoy, 0.0f, -1.0f, 1.0f);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		#if defined(VITA_PERF_TELEMETRY)
		static bool s_reportedHudBatch = false;
		if (!s_reportedHudBatch && iLog)
		{
			s_reportedHudBatch = true;
			iLog->LogToFile("\001[VITA][HUD2D] batch projection=%dx%d framebuffer=%dx%d",
				ortox, ortoy, m_nWidth, m_nHeight);
		}
		#endif
	}
	else
	{
		if (s_ignoredDepth > 0)
		{
			--s_ignoredDepth;
			return;
		}
		if (g_nVita2DModeDepth <= 0)
			return;
		glActiveTexture(GL_TEXTURE0);
		glClientActiveTexture(GL_TEXTURE0);
		glMatrixMode(GL_TEXTURE);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		--g_nVita2DModeDepth;
		g_fVita2DModeScaleX = s_scaleStackX[g_nVita2DModeDepth];
		g_fVita2DModeScaleY = s_scaleStackY[g_nVita2DModeDepth];
		if (g_nVita2DModeDepth > 0)
			return;
		if (s_oldFog) glEnable(GL_FOG); else glDisable(GL_FOG);
		if (s_oldDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		if (s_oldBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		if (s_oldTexture) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
		VitaInvalidateRenderStateCache();
		VitaInvalidateTextureCache();
	}
#endif
}
int CVitaRenderer::ScreenToTexture() { return 0; }
void CVitaRenderer::SetTexClampMode(bool clamp)
{
#if defined(LINUX)
	const GLint mode = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
#endif
}
void CVitaRenderer::EnableSwapBuffers(bool bEnable)
{
	m_bSwapBuffersEnabled = bEnable;
#if defined(LINUX)
	if (iLog)
		iLog->LogToFile("\001[VITA][PRECACHE] display swaps %s",
			bEnable ? "enabled" : "suppressed");
#endif
}
void CVitaRenderer::OnEntityDeleted(IEntityRender * pEntityRender) { }
void CVitaRenderer::SetGlobalShaderTemplateId(int nTemplateId) { }
int CVitaRenderer::GetGlobalShaderTemplateId() { return 0; }
int CVitaRenderer::EnumAAFormats(TArray<SAAFormat>& Formats, bool bReset) { return 0; }
int CVitaRenderer::CreateRenderTarget(int nWidth, int nHeight, ETEX_Format eTF) { return 0; }
bool CVitaRenderer::DestroyRenderTarget(int nHandle) { return false; }
bool CVitaRenderer::SetRenderTarget(int nHandle) { return false; }
float CVitaRenderer::EF_GetWaterZElevation(float fX, float fY) { return 0.0f; }

//////////////////////////////////////////////////////////////////////
extern "C" DLL_EXPORT IRenderer* PackageRenderConstructor(int argc, char* argv[], SCryRenderInterface *sp);
DLL_EXPORT IRenderer* PackageRenderConstructor(int argc, char* argv[], SCryRenderInterface *sp)
{
	sceClibPrintf("[BOOTTRACE] PackageRenderConstructor entered, sp=%p\n", (void*)sp);
	iConsole = sp->ipConsole;
	iLog = sp->ipLog;
	iSystem = sp->ipSystem;
	iTimer = sp->ipTimer;
	pTest_int = sp->ipTest_int;
	pIPhysicalWorld = sp->pIPhysicalWorld;
	sceClibPrintf("[BOOTTRACE] PackageRenderConstructor: before new CVitaRenderer\n");

	IRenderer *r = new CVitaRenderer();
	sceClibPrintf("[BOOTTRACE] PackageRenderConstructor: after new CVitaRenderer, r=%p\n", (void*)r);
	return r;
}
