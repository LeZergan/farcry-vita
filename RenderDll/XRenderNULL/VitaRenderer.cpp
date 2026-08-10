//////////////////////////////////////////////////////////////////////
// Vita: see VitaRenderer.h for why this exists instead of a real
// CRenderer-derived backend.
//////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "VitaRenderer.h"
#include <CryHeaders.h>

#if defined(LINUX)
#include <vitaGL.h>
#include <CRESky.h>
#include <CREDummy.h>
#include <CRE2DQuad.h>
#include <CREScreenProcess.h>
#include <CRETerrainSector.h>
#include <CREOcLeaf.h>
/* For IDeformableRenderMesh::ProcessSkinning -- IShader.h only forward-declares
   the interface, and the render pipeline is what drives character skinning. */
#include <ICryAnimation.h>
#endif

CVitaRenderer *gcpVitaRenderer = NULL;

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
static int s_vitaTexBumpResolution = 2;
static int s_vitaTexSkyResolution = 2;
static int s_vitaTexAnisotropy = 1;
static int s_vitaScopeLensFx = 0;
static int s_vitaFsaa = 0;
static int s_vitaFsaaSamples = 0;
static int s_vitaFsaaQuality = 0;
static int s_vitaVsync = 1;
/* Render at the panel's native 960x544.  Rendering at 640x368 and letting the
   display controller scale up is the cheapest frame-rate lever available, but
   that scaler is a plain bilinear filter: everything picks up a soft, grainy
   cast and the HUD text stops being readable, which is too high a price.  With
   static geometry now living in GPU buffer objects the fill-rate saving is no
   longer the deciding factor.  Override with -DFARCRY_VITA_RENDER_SCALE=66 to
   trade sharpness back for speed. */
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
	  m_nActiveLights(0),
	  m_nCullMode(R_CULL_BACK), m_nNextShaderId(1), m_nTempRenderObjectCursor(0)
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
	/* Render at 640x368 and let the display controller scale to the panel.
	   FARCRY_VITA_RENDER_SCALE (percent of the native 960x544) overrides it if
	   the CMake option is set; 100 gives the full panel resolution. */
	width = VITA_RENDER_WIDTH;
	height = VITA_RENDER_HEIGHT;
#if defined(FARCRY_VITA_RENDER_SCALE_PERCENT)
	{
		const int nScale = FARCRY_VITA_RENDER_SCALE_PERCENT;
		if (nScale > 0 && nScale <= 100)
		{
			// Keep both dimensions even: GXM dislikes odd render-target extents.
			width = ((960 * nScale / 100) + 1) & ~1;
			height = ((544 * nScale / 100) + 1) & ~1;
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
			"Modulate baked lightmaps onto world geometry");

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
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: after glViewport\n");
	return (WIN_HWND)this; // just checked against NULL by callers
#else
	return 0;
#endif
}

void CVitaRenderer::ShutDown(bool bReInit)
{
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
   frame: anything over budget draws white for that frame and is picked up on
   the next, so the cost spreads instead of spiking. */
static int g_nLazyTextureLoadsThisFrame = 0;
static const int kMaxLazyTextureLoadsPerFrame = 2;

void CVitaRenderer::BeginFrame()
{
#if defined(LINUX)
	/* Vita: real frame clear. This and Update()'s swap below are the
	   first genuine on-screen output driven by the actual engine, not a
	   standalone test -- everything else in this class is a mechanical
	   IRenderer stub (see VitaRenderer.h). */
	++m_nFrameId;
	m_nDynVBCursor = 0;
	g_nLazyTextureLoadsThisFrame = 0;
	glViewport(m_nViewportX, m_nViewportY, m_nViewportWidth, m_nViewportHeight);
	glDepthMask(GL_TRUE);
	glClearColor(m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 1.0f);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
	// Vita: TEMPORARY verification capture -- one real screenshot of frame
	// 90 (skips past the first couple of still-loading frames), written to
	// a real, host-visible path. Reverted once no longer needed.

	// Vita: present the frame cleared in BeginFrame() above.
	vglSwapBuffers(GL_FALSE);
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
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, width, height);
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

static void SetupVertexArraysForFormat(const byte *pBase, int nVertexFormat)
{
	const SBufInfoTable &tbl = gBufInfoTable[nVertexFormat];
	int nStride = m_VertexSize[nVertexFormat];

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, nStride, pBase);

	if (tbl.OffsColor && !g_bUntexturedSurface)
	{
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, nStride, pBase + tbl.OffsColor);
	}
	else
	{
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
	}

	if (tbl.OffsTC)
	{
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, nStride, pBase + tbl.OffsTC);
	}
	else
	{
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
}

/* Published by CLeafBuffer::AddRenderElements for the duration of one buffer's
   draw, so every chunk of a terrain sector can reach the texgen offsets that
   only the first chunk's render element actually carries. */
const float *g_pVitaTerrainTexGen = NULL;

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
	static std::vector<float> s_arrTerrainUVs;
	s_arrTerrainUVs.resize((size_t)nNumVerts * 2);
	const int nStride = m_VertexSize[nVertexFormat];
	for (int i = 0; i < nNumVerts; ++i)
	{
		const float *pPos = (const float *)(pData + (size_t)i * nStride);
		s_arrTerrainUVs[(size_t)i * 2 + 0] = pPos[1] * fScale + pTexGenOffsets[0];
		s_arrTerrainUVs[(size_t)i * 2 + 1] = pPos[0] * fScale + pTexGenOffsets[1];
	}
	return &s_arrTerrainUVs[0];
}

/* Attach generated coordinates as a client-side array.  Binding buffer zero
   first matters: the gl*Pointer call captures whatever GL_ARRAY_BUFFER is bound
   at the time, so on the VBO path the pointer would otherwise be read as an
   offset into the vertex buffer object. */
static void VitaApplyGeneratedTexCoords(const float *pUVs)
{
	if (!pUVs)
		return;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, pUVs);
}

static void TeardownVertexArrays()
{
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
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
static const int kGLBufferMinVerts = 64;
static const unsigned int kGLBufferBudgetBytes = 20u * 1024u * 1024u;
static unsigned int g_nGLBufferBytesUsed = 0;

static bool EnsureGLVertexBuffer(CVertexBuffer *src)
{
	if (!src || src->m_bDynamic || src->m_NumVerts < kGLBufferMinVerts)
		return false;
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	if (!pData)
		return false;
	const unsigned int nBytes = (unsigned int)(m_VertexSize[src->m_vertexformat] * src->m_NumVerts);
	if (src->m_nGLVBO && !src->m_bGLDirty && src->m_nGLVBOVerts == src->m_NumVerts)
		return true;
	if (!src->m_nGLVBO)
	{
		if (g_nGLBufferBytesUsed + nBytes > kGLBufferBudgetBytes)
			return false;
		GLuint nName = 0;
		glGenBuffers(1, &nName);
		if (!nName)
			return false;
		src->m_nGLVBO = nName;
		g_nGLBufferBytesUsed += nBytes;
	}
	glBindBuffer(GL_ARRAY_BUFFER, src->m_nGLVBO);
	glBufferData(GL_ARRAY_BUFFER, nBytes, pData, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	src->m_nGLVBOVerts = src->m_NumVerts;
	src->m_bGLDirty = false;
	return true;
}

static bool EnsureGLIndexBuffer(SVertexStream *inds)
{
	if (!inds || !inds->m_VData || inds->m_nItems <= 0)
		return false;
	const unsigned int nBytes = (unsigned int)(inds->m_nItems * sizeof(ushort));
	if (inds->m_nGLIBO && !inds->m_bGLDirty && inds->m_nGLIBOItems == inds->m_nItems)
		return true;
	if (!inds->m_nGLIBO)
	{
		if (g_nGLBufferBytesUsed + nBytes > kGLBufferBudgetBytes)
			return false;
		GLuint nName = 0;
		glGenBuffers(1, &nName);
		if (!nName)
			return false;
		inds->m_nGLIBO = nName;
		g_nGLBufferBytesUsed += nBytes;
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, inds->m_nGLIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, nBytes, inds->m_VData, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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
	if (!src || vert_num <= 0)
		return;
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	if (!pData)
		return;
	SetupVertexArraysForFormat(pData, src->m_vertexformat);
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
	if (nOffs < 0 || nVerts <= 0 || nOffs + nVerts > DYNVB_CAPACITY)
		return;
	struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pV = &m_DynVB[nOffs];
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->xyz);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->color);
	glTexCoordPointer(2, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->st);
	glDrawArrays(GL_TRIANGLES, 0, nVerts);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
}

void CVitaRenderer::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F * pBuf, ushort * pInds, int nVerts, int nInds, int nPrimType)
{
#if defined(LINUX)
	if (!pBuf || nVerts <= 0)
		return;
	SetupVertexArraysForFormat((const byte *)pBuf, VERTEX_FORMAT_P3F_COL4UB_TEX2F);
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
	CVertexBuffer *vb = new CVertexBuffer;
	vb->m_bDynamic = bDynamic;
	vb->m_vertexformat = vertexformat;
	vb->m_NumVerts = vertexcount;
	int nSize = m_VertexSize[vertexformat] * vertexcount;
	vb->m_VS[VSF_GENERAL].m_VData = nSize > 0 ? new byte[nSize] : NULL;
	return vb;
}
void CVitaRenderer::ReleaseBuffer(CVertexBuffer * bufptr)
{
	if (!bufptr)
		return;
#if defined(LINUX)
	if (bufptr->m_nGLVBO)
	{
		GLuint nName = bufptr->m_nGLVBO;
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
	if (!dest || !src || vertexcount <= 0)
		return;
	byte *pDst = (byte*)dest->m_VS[Type].m_VData;
	if (!pDst)
		return;
#if defined(LINUX)
	// CPU-side contents changed: any GPU copy is now stale.
	dest->m_bGLDirty = true;
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
#if defined(LINUX)
	dest->m_bGLDirty = true;
#endif
	if (dest->m_nItems < indexcount)
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
	if (dest->m_nGLIBO)
	{
		GLuint nName = dest->m_nGLIBO;
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
	if (!src || !indicies)
	{
		sceClibPrintf("[BOOTTRACE] DrawBuffer: null src/indicies, src=%p indicies=%p\n", (void*)src, (void*)indicies);
		return;
	}
	const byte *pData = (const byte *)src->m_VS[VSF_GENERAL].m_VData;
	const ushort *pInds = (const ushort *)indicies->m_VData;
	if (!pData || !pInds || numindices <= 0)
	{
		sceClibPrintf("[BOOTTRACE] DrawBuffer: bail pData=%p pInds=%p numindices=%d\n", (void*)pData, (void*)pInds, numindices);
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
	static int s_nDrawCalls = 0;
	const bool bTrace = ((++s_nDrawCalls % 512) == 0);
	if (bTrace)
		sceClibPrintf("[BOOTTRACE] DrawBuffer: call#%d fmt=%d numindices=%d offsindex=%d prmode=%d vert_start=%d vert_stop=%d\n",
			s_nDrawCalls, src->m_vertexformat, numindices, offsindex, prmode, vert_start, vert_stop);
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

	const bool bGPUResident = EnsureGLVertexBuffer(src) && EnsureGLIndexBuffer(indicies);
	if (bGPUResident)
	{
		glBindBuffer(GL_ARRAY_BUFFER, src->m_nGLVBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indicies->m_nGLIBO);
		SetupVertexArraysForFormat(NULL, src->m_vertexformat);
		VitaApplyGeneratedTexCoords(pGeneratedUVs);
		glDrawElements(PrimTypeToGL(prmode), numindices, GL_UNSIGNED_SHORT,
			(const void *)(size_t)(offsindex * sizeof(ushort)));
		TeardownVertexArrays();
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		return;
	}

	SetupVertexArraysForFormat(pData, src->m_vertexformat);
	VitaApplyGeneratedTexCoords(pGeneratedUVs);
	if (src->m_vertexformat == VERTEX_FORMAT_P3F_COL4UB)
	{
		static bool s_reportedColorOnlyDraw = false;
		if (!s_reportedColorOnlyDraw)
		{
			s_reportedColorOnlyDraw = true;
			const struct_VERTEX_FORMAT_P3F_COL4UB *v =
				(const struct_VERTEX_FORMAT_P3F_COL4UB *)pData;
			GLint blendSrc = 0, blendDst = 0;
			glGetIntegerv(GL_BLEND_SRC, &blendSrc);
			glGetIntegerv(GL_BLEND_DST, &blendDst);
			sceClibPrintf("[VITAGL] fmt2 rgba=%u,%u,%u,%u blend=%d src=0x%x dst=0x%x err=0x%x\n",
				(unsigned)v[0].color.bcolor[0], (unsigned)v[0].color.bcolor[1],
				(unsigned)v[0].color.bcolor[2], (unsigned)v[0].color.bcolor[3],
				(int)glIsEnabled(GL_BLEND), (unsigned)blendSrc, (unsigned)blendDst,
				(unsigned)glGetError());
			fflush(stdout);
		}
	}
	glDrawElements(PrimTypeToGL(prmode), numindices, GL_UNSIGNED_SHORT, pInds + offsindex);
	if (bTrace)
		sceClibPrintf("[BOOTTRACE] DrawBuffer: after glDrawElements err=%d viewport w=%d h=%d\n", glGetError(), m_nWidth, m_nHeight);
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
	glColor4f(1, 1, 1, 1);
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
   ignored: SetColorOp/SetTexgen/EnableFog etc remain their own separate,
   still-stubbed entry points). */
void CVitaRenderer::SetState(int State)
{
#if defined(LINUX)
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

	if (nBlend)
	{
		static bool s_reportedBlendState = false;
		if (!s_reportedBlendState)
		{
			s_reportedBlendState = true;
			GLint blendSrc = 0, blendDst = 0;
			glGetIntegerv(GL_BLEND_SRC, &blendSrc);
			glGetIntegerv(GL_BLEND_DST, &blendDst);
			sceClibPrintf("[VITAGL] SetState state=0x%x blend=%d src=0x%x dst=0x%x depthWrite=%d err=0x%x\n",
				State, (int)glIsEnabled(GL_BLEND), (unsigned)blendSrc, (unsigned)blendDst,
				(State & GS_DEPTHWRITE) != 0, (unsigned)glGetError());
			fflush(stdout);
		}
	}
#endif
}
void CVitaRenderer::WriteXY(CXFont * currfont, int x, int y, float xscale, float yscale, float r, float g, float b, float a, const char * message, ...) { }
void CVitaRenderer::Draw2dText(float posX, float posY, const char * szText, SDrawTextInfo & info) { }
void CVitaRenderer::Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z)
{
#if defined(LINUX)
	/* Vita: retain Crytek's OpenGL/D3D Draw2dImage texture-coordinate
	   contract.  DDS and the rest of the retail UI use a top-left image
	   origin, so both original backends submit (1-t), not t, even though the
	   screen-space projection itself also has y increasing downwards. */
	/* Far Cry's 2D API is authored in a virtual 800x600 canvas.  This is
	   copied from the real Crytek OpenGL backend's Draw2dImage contract. */
	xpos = ScaleCoordX(xpos);
	ypos = ScaleCoordY(ypos) - 1.0f;
	w = ScaleCoordX(w) + 1.0f;
	h = ScaleCoordY(h) + 2.0f;

	GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
	GLboolean wasBlend = glIsEnabled(GL_BLEND);
	GLboolean wasTexture = glIsEnabled(GL_TEXTURE_2D);

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

	bool bTextured = texture_id > 0;
	if (bTextured)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)texture_id);
		VitaInvalidateTextureCache(); // bound outside SetTexture
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
	else
	{
		glDisable(GL_TEXTURE_2D);
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
		for (int i = 0; i < 4; ++i)
		{
			const float x = verts[i*2+0] - cx;
			const float y = verts[i*2+1] - cy;
			verts[i*2+0] = x*c - y*s + cx;
			verts[i*2+1] = x*s + y*c + cy;
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

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, verts);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
	if (bTextured)
	{
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, uvs);
	}
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	if (bTextured)
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	if (wasDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (wasTexture) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
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
	if (!verts || numPts < 3)
		return obj;
	std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> vitaVerts((size_t)numPts);
	for (int i = 0; i < numPts; ++i)
	{
		vitaVerts[i].xyz = verts[i].vert;
		vitaVerts[i].color = verts[i].color;
		vitaVerts[i].st[0] = verts[i].dTC[0];
		vitaVerts[i].st[1] = verts[i].dTC[1];
	}
	std::vector<ushort> vitaIndices;
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

	if (bpp != 24 || compression != 0 || width <= 0 || height == 0)
	{
		sceClibPrintf("[BOOTTRACE] LoadBMP_RGBA32: unsupported BMP variant (only uncompressed 24bpp handled) %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	bool bBottomUp = height > 0;
	int absHeight = bBottomUp ? height : -height;
	int rowSizeSrc = ((width * 3 + 3) / 4) * 4; // BMP rows are padded to 4 bytes

	/* Vita: read the WHOLE pixel block in a single FRead instead of one
	   FRead per row. Each ICryPak::FRead call was going through a real
	   critical section plus the underlying sceIoRead round-trip -- for a
	   640x480 image that's 480 separate locked I/O calls, ~40 seconds
	   wall-clock on real hardware/emulation overhead. One big read is
	   the same bytes off the same real file, just without 479 redundant
	   lock/syscall round-trips. */
	long srcSize = (long)rowSizeSrc * absHeight;
	byte *pSrcBuf = new byte[srcSize];
	byte *pRGBA = new byte[width * absHeight * 4];

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
		byte *pRow = pSrcBuf + (long)y * rowSizeSrc;
		// BMP stores rows bottom-to-top by default (positive height).
		int destRow = bBottomUp ? (absHeight - 1 - y) : y;
		byte *pDest = pRGBA + (long)destRow * width * 4;
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
static void DecompressBlockDXT1(int x, int y, int width, const byte *blockStorage, byte *image)
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
			if (x+i < width)
			{
				byte *pDest = image + ((y+j)*width + (x+i)) * 4;
				pDest[0]=r; pDest[1]=g; pDest[2]=b; pDest[3]=a;
			}
		}
	}
}

// Shared by DXT3/DXT5: the color block (last 8 bytes) is always the plain
// 4-color interpolation, never DXT1's 3-color+transparent special case.
static void DecompressColorBlock4(int x, int y, int width, const byte *blockStorage, byte *image, const byte *alphas)
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
			if (x+i < width)
			{
				byte *pDest = image + ((y+j)*width + (x+i)) * 4;
				pDest[0]=r; pDest[1]=g; pDest[2]=b; pDest[3]=alphas[j*4+i];
			}
		}
	}
}

static void DecompressBlockDXT3(int x, int y, int width, const byte *blockStorage, byte *image)
{
	byte alphas[16];
	for (int j = 0; j < 4; j++)
		for (int i = 0; i < 2; i++)
		{
			byte packed = blockStorage[j*2+i];
			alphas[j*4+i*2+0] = (packed & 0x0F) * 17;
			alphas[j*4+i*2+1] = (packed >> 4) * 17;
		}
	DecompressColorBlock4(x, y, width, blockStorage + 8, image, alphas);
}

static void DecompressBlockDXT5(int x, int y, int width, const byte *blockStorage, byte *image)
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
	DecompressColorBlock4(x, y, width, blockStorage + 8, image, alphas);
}

enum EDdsFourCC { DDS_FOURCC_NONE=0, DDS_FOURCC_DXT1, DDS_FOURCC_DXT3, DDS_FOURCC_DXT5 };

/* Baked lightmaps can be turned off at runtime with "r_lightmaps 0" -- useful
   for telling a lighting problem apart from a texture one without a rebuild. */
static bool LightMapsEnabled()
{
	static ICVar *s_pLightMaps = NULL;
	static bool s_bLookedUp = false;
	if (!s_bLookedUp && iConsole)
	{
		s_bLookedUp = true;
		s_pLightMaps = iConsole->GetCVar("r_lightmaps");
	}
	// On unless explicitly disabled -- see CVitaRenderer::Init.
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
	GLenum *pOutInternalFormat, int *pOutDataSize, bool *pOutCompressed, int *pOutMipCount)
{
	*pOutInternalFormat = GL_RGBA;
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

	if (width <= 0 || height <= 0 || (fourCC == DDS_FOURCC_NONE && !bUncompressed))
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
		long srcSize = (long)width * height * srcBytesPerPixel;
		long dstSize = (long)width * height * 4;
		byte *pSrcBuf = new byte[srcSize];
		byte *pRGBA = new byte[dstSize];
		if (FReadChunked(pPak, pSrcBuf, srcSize, fp) != (size_t)srcSize)
		{
			sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: uncompressed pixel read failed for %s\n", path);
			delete [] pSrcBuf;
			delete [] pRGBA;
			pPak->FClose(fp);
			return NULL;
		}
		for (long i = 0; i < (long)width * height; i++)
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

	/* PowerVR SGX543MP4+ supports BC1/BC2/BC3 directly and upstream vitaGL
	   maps these enums to SCE_GXM_TEXTURE_FORMAT_UBC1/2/3.  Keep the retail
	   DDS blocks compressed instead of expanding every texture to RGBA8888
	   on the ARM CPU and retaining the 4-byte-per-pixel copy in memory. */
	switch (fourCC)
	{
		case DDS_FOURCC_DXT1: *pOutInternalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
		case DDS_FOURCC_DXT3: *pOutInternalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
		case DDS_FOURCC_DXT5: *pOutInternalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
		default:
			delete [] pCompressed;
			return NULL;
	}
	*pOutW = width;
	*pOutH = height;
	*pOutDataSize = (int)compressedSize;
	*pOutCompressed = true;
	return pCompressed;
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
		static bool s_reportedTextureReuse = false;
		if (!s_reportedTextureReuse && iLog)
		{
			iLog->LogToFile("[VITA] texture cache reuse active");
			s_reportedTextureReuse = true;
		}
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
		bCompressed = false;
		nMipCount = 1;
		if (candidateBmp)
		{
			pTextureData = LoadBMP_RGBA32(pPak, path.c_str(), &w, &h);
			if (pTextureData)
				dataSize = w * h * 4;
		}
		else
			pTextureData = LoadDDSForVita(pPak, path.c_str(), &w, &h, &internalFormat, &dataSize, &bCompressed, &nMipCount);
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
	const bool bUseMips = bCompressed && nMipCount > 1;
	/* Trilinear once there is a chain to filter between: without it, minified
	   surfaces alias badly (the grainy shimmer at distance) and the GPU keeps
	   sampling full-size textures for a few pixels. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		bUseMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (bCompressed)
	{
		const byte *pLevel = pTextureData;
		int lw = w, lh = h;
		for (int nLevel = 0; nLevel < nMipCount; ++nLevel)
		{
			const int nBlockSize = (internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
				internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ? 8 : 16;
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
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pTextureData);
	GLenum uploadError = glGetError();
	if (uploadError != GL_NO_ERROR)
	{
		glDeleteTextures(1, &tex);
		delete [] pTextureData;
		return 0;
	}

	// Compressed upload data has been copied into vitaGL's CDRAM pool. Do
	// not retain a second CPU-side copy. Raw RGBA remains available for the
	// few legacy GetData32 paths.
	byte *pCPUData = bCompressed ? NULL : pTextureData;
	if (bCompressed)
		delete [] pTextureData;
	CVitaTexPic *pResult = new CVitaTexPic(nameTex, (int)tex, w, h, pCPUData, (int)flags, (int)flags2);
	m_TextureById[(int)tex] = pResult;
	m_TextureByName[cacheKey] = pResult;
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
	glColor4f(1.0f, 1.0f, 1.0f, sky->m_fAlpha);
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
	renderer->DrawTriStrip(&(CVertexBuffer(top, VERTEX_FORMAT_P3F_TEX2F)), 4);

	struct_VERTEX_FORMAT_P3F_TEX2F south[] = {
		{ Vec3(-size,-size, size), 1, 0 }, { Vec3( size,-size, size), 0, 0 },
		{ Vec3(-size,-size,-p),   1, .5f }, { Vec3( size,-size,-p),   0, .5f },
		{ Vec3(-size,-size,-d),   1, .5f }, { Vec3( size,-size,-d),   0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(1), eTT_Base);
	renderer->DrawTriStrip(&(CVertexBuffer(south, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F east[] = {
		{ Vec3(-size, size, size), 1, 1 }, { Vec3(-size,-size, size), 0, 1 },
		{ Vec3(-size, size,-p),    1, .5f }, { Vec3(-size,-size,-p),    0, .5f },
		{ Vec3(-size, size,-d),    1, .5f }, { Vec3(-size,-size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(1), eTT_Base);
	renderer->DrawTriStrip(&(CVertexBuffer(east, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F north[] = {
		{ Vec3( size, size, size), 1, 0 }, { Vec3(-size, size, size), 0, 0 },
		{ Vec3( size, size,-p),    1, .5f }, { Vec3(-size, size,-p),    0, .5f },
		{ Vec3( size, size,-d),    1, .5f }, { Vec3(-size, size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(0), eTT_Base);
	renderer->DrawTriStrip(&(CVertexBuffer(north, VERTEX_FORMAT_P3F_TEX2F)), 6);

	struct_VERTEX_FORMAT_P3F_TEX2F west[] = {
		{ Vec3( size,-size, size), 1, 1 }, { Vec3( size, size, size), 0, 1 },
		{ Vec3( size,-size,-p),    1, .5f }, { Vec3( size, size,-p),    0, .5f },
		{ Vec3( size,-size,-d),    1, .5f }, { Vec3( size, size,-d),    0, .5f }
	};
	renderer->SetTexture(shader->GetSkyTextureId(0), eTT_Base);
	renderer->DrawTriStrip(&(CVertexBuffer(west, VERTEX_FORMAT_P3F_TEX2F)), 6);

	if (hasObjectTransform)
		renderer->PopMatrix();
	glDepthRangef(0.0f, 1.0f);
	renderer->SetState(GS_DEPTHWRITE);
	renderer->SetCullMode(R_CULL_BACK);
	glColor4f(1, 1, 1, 1);
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
		CREOcLeaf *leafElement = static_cast<CREOcLeaf *>(re);
		CLeafBuffer *leaf = leafElement->m_pBuffer;
		CMatInfo *chunk = leafElement->m_pChunk;

		/* Record what actually arrives here and whether it survives to the
		   draw.  Invisible geometry is otherwise indistinguishable from
		   geometry that was never submitted, and guessing between those two
		   has cost several round trips already. */
		{
			static std::map<std::string, bool> s_reportedSubmit;
			const std::string src = (leaf && leaf->m_sSource) ? leaf->m_sSource : "<null-leaf>";
			if (iLog && s_reportedSubmit.size() < 96 &&
				s_reportedSubmit.find(src) == s_reportedSubmit.end())
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
			CLeafBuffer *pVertexContainer = leaf->GetVertexContainer();
			const bool bForceUpdate = pVertexContainer &&
				pVertexContainer->m_UpdateFrame == (unsigned)GetFrameID(true);
			obj->m_pCharInstance->ProcessSkinning(obj->m_Matrix.GetTranslationOLD(),
				obj->m_Matrix, obj->m_nTemplId, obj->m_nLod, bForceUpdate);
		}

		const char *vitaShaderName = (ef && ef->GetName()) ? ef->GetName() : "";
		// Collision-only proxy: has no texture, so drawing it painted white.
		if (VitaMaterial::IsNoDraw(vitaShaderName))
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
		if (sr)
		{
			// Same reasoning as above: never alpha-test a skinned mesh.
			if (!bSkinnedMesh)
			{
				if (sr->m_AlphaRef >= 0.5f) renderState |= GS_ALPHATEST_GEQUAL128;
				else if (sr->m_AlphaRef > 0.0f) renderState |= GS_ALPHATEST_GEQUAL64;
			}
			if (sr->m_Opacity < 0.999f)
			{
				renderState &= ~GS_DEPTHWRITE;
				renderState |= (sr->m_ResFlags & MTLFLAG_ADDITIVE)
					? (GS_BLSRC_ONE | GS_BLDST_ONE)
					: (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
			}
			SetCullMode((sr->m_ResFlags & MTLFLAG_2SIDED) ? R_CULL_NONE : R_CULL_BACK);
		}
		else
			SetCullMode(R_CULL_BACK);
		SetState(renderState);

		SEfResTexture *diffuse = sr ? sr->m_Textures[EFTT_DIFFUSE] : NULL;
		if (diffuse && !diffuse->m_TU.m_ITexPic && !diffuse->m_Name.empty() &&
			g_nLazyTextureLoadsThisFrame < kMaxLazyTextureLoadsPerFrame)
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
			}
			diffuse->m_TU.m_ITexPic = loaded;
		}
		else if (diffuse && !diffuse->m_TU.m_ITexPic && !diffuse->m_Name.empty() &&
			!IsTextureKnownMissing(diffuse->m_Name.c_str()))
		{
			/* Its load was pushed to a later frame by the budget above.  Drawing
			   it now would paint the surface solid white until the texture
			   arrives, which is the white flashing seen while moving -- leave
			   the chunk out for a frame instead and let it appear textured.
			   Textures already proven absent are excluded, or those surfaces
			   would never be drawn at all. */
			return;
		}
		if (diffuse && diffuse->m_TU.m_ITexPic)
			SetTexture(diffuse->m_TU.m_ITexPic->GetTextureID(), eTT_Base);
		else if (leafElement->m_CustomTexBind[0] > 0 && leafElement->m_CustomTexBind[0] != 0x1000)
			SetTexture(leafElement->m_CustomTexBind[0], eTT_Base);
		else
		{
			/* Same white fallback CLeafBuffer::Draw has, and it needs the same
			   reporting: geometry submitted through EF_AddEf (terrain sectors,
			   the ocean plane, anything the 3D engine queues rather than draws
			   itself) was silently rendering pure white with nothing naming it. */
			SetWhiteTexture();
			static std::map<std::string, bool> s_reportedWhite;
			std::string key = (ef && ef->GetName()) ? ef->GetName() : "<null-shader>";
			key += "|";
			if (diffuse)
				key += diffuse->m_Name.c_str();
			if (s_reportedWhite.size() < 256 && s_reportedWhite.find(key) == s_reportedWhite.end())
			{
				s_reportedWhite[key] = true;
				if (iLog)
					iLog->LogToFile("\001[VITA][EFWHITE] shader=%s diffuse=%s tris=%d",
						(ef && ef->GetName()) ? ef->GetName() : "<null>",
						diffuse ? diffuse->m_Name.c_str() : "<none>",
						chunk->nNumIndices / 3);
			}
		}

		/* Baked lighting.  Far Cry stores the world's static lighting in
		   lightmaps, not in the vertex colours, so drawing only the diffuse
		   texture leaves everything evenly lit and flat -- no sun, no shadow,
		   no bounce.  CBrush::Render hands the colour/lerp lightmap and its own
		   texture-coordinate buffer down on the render object, so modulate it in
		   on texture unit 1.

		   KNOWN WRONG, which is why r_lightmaps defaults to 0.  The UV stream
		   below is bound alongside the PRIMARY vertex buffer, but
		   CBrush::SetLightmap builds it with one entry per vertex of the
		   SECONDARY buffer (it sizes the array by pLeafBuffer->m_SecVertCount
		   and errors out when the counts disagree).  The two buffers differ in
		   both length and ordering, so every coordinate lands on the wrong
		   vertex and the result is blotches of black and white instead of
		   shading.  Fixing this means either sourcing the LM coordinates
		   through the secondary buffer's own index mapping, or drawing this
		   pass from m_pSecVertBuffer -- not simply re-binding the same array. */
		bool bLightMapBound = false;
		if (obj && obj->m_pLMTCBufferO && LightMapsEnabled())
		{
			SEfResTexture *lmTexture = sr ? sr->m_Textures[EFTT_LIGHTMAP] : NULL;
			const int lmTex = (lmTexture && lmTexture->m_TU.m_ITexPic)
				? lmTexture->m_TU.m_ITexPic->GetTextureID() : 0;
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
				glActiveTexture(GL_TEXTURE1);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, (GLuint)lmTex);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glClientActiveTexture(GL_TEXTURE1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				// VERTEX_FORMAT_TEX2F: two floats per vertex, nothing else.
				glTexCoordPointer(2, GL_FLOAT, m_VertexSize[lmVB->m_vertexformat], lmData);
				glClientActiveTexture(GL_TEXTURE0);
				glActiveTexture(GL_TEXTURE0);
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
		if (hasObjectTransform)
			PopMatrix();

		if (bLightMapBound)
		{
			// Leave unit 1 off, or every later draw inherits this lightmap.
			glClientActiveTexture(GL_TEXTURE1);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glActiveTexture(GL_TEXTURE1);
			glDisable(GL_TEXTURE_2D);
			glActiveTexture(GL_TEXTURE0);
			glClientActiveTexture(GL_TEXTURE0);
		}

		static std::map<std::string, bool> reportedCharacterBuffers;
		const std::string source = leaf->m_sSource ? leaf->m_sSource : "<null>";
		if (reportedCharacterBuffers.size() < 16 && reportedCharacterBuffers.find(source) == reportedCharacterBuffers.end())
		{
			reportedCharacterBuffers[source] = true;
			sceClibPrintf("[VITARE] source=%s fmt=%d verts=%d inds=%d shader=%s tex=%d\n",
				source.c_str(), leaf->m_pVertexBuffer->m_vertexformat,
				leaf->m_pVertexBuffer->m_NumVerts, chunk->nNumIndices,
				ef ? ef->GetName() : "<null>",
				(diffuse && diffuse->m_TU.m_ITexPic) ? diffuse->m_TU.m_ITexPic->GetTextureID() : leafElement->m_CustomTexBind[0]);
			fflush(stdout);
		}
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
void CVitaRenderer::EF_EndEf3D(int nFlags) { }
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
void CVitaRenderer::EF_EndEf2D(bool bSort) { }
bool CVitaRenderer::EF_DrawEfForName(char * name, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEf(IShader * ef, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl) { return false; }
bool CVitaRenderer::EF_DrawPartialEfForName(char * name, SVrect * vr, SVrect * pr, CFColor& col) { return false; }
bool CVitaRenderer::EF_DrawPartialEfForNum(int num, SVrect * vr, SVrect * pr, CFColor& col) { return false; }
bool CVitaRenderer::EF_DrawPartialEf(IShader * ef, SVrect * vr, SVrect * pr, CFColor& col, float iwdt, float ihgt) { return false; }
void * CVitaRenderer::EF_Query(int Query, int Param) { return 0; }
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
void CVitaRenderer::SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa) { }
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
	if (w <= 0 || h <= 0)
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
	GLenum eCompressedFormat = 0;
	int nBlockBytes = 0;
	switch (eTFSrc)
	{
		case eTF_DXT1: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; nBlockBytes = 8;  break;
		case eTF_DXT3: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; nBlockBytes = 16; break;
		case eTF_DXT5: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; nBlockBytes = 16; break;
		default: break;
	}
	if (eCompressedFormat && data)
	{
		const int nSize = ((w + 3) / 4) * ((h + 3) / 4) * nBlockBytes;
		glCompressedTexImage2D(GL_TEXTURE_2D, 0, eCompressedFormat, w, h, 0, nSize, data);
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
	if (!tnum || !newdata || w <= 0 || h <= 0)
		return;
	glBindTexture(GL_TEXTURE_2D, (GLuint)tnum);
	VitaInvalidateTextureCache(); // bound outside SetTexture
	// Same format trap as DownLoadToVideoMemory: the terrain texture pool feeds
	// DXT1 blocks through here, which must not be read as RGBA8888.
	GLenum eCompressedFormat = 0;
	int nBlockBytes = 0;
	switch (eTFSrc)
	{
		case eTF_DXT1: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; nBlockBytes = 8;  break;
		case eTF_DXT3: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; nBlockBytes = 16; break;
		case eTF_DXT5: eCompressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; nBlockBytes = 16; break;
		default: break;
	}
	if (eCompressedFormat)
	{
		/* A compressed sub-image has to land on block boundaries; the pool
		   always replaces the whole texture, so upload it as a fresh level
		   rather than risking a partial block update. */
		const int nSize = ((w + 3) / 4) * ((h + 3) / 4) * nBlockBytes;
		while (glGetError() != GL_NO_ERROR) {} // discard anything already pending
		glCompressedTexImage2D(GL_TEXTURE_2D, 0, eCompressedFormat, w, h, 0, nSize, newdata);
		/* This is the only path that ever fills a terrain sector texture: the
		   pool creates its slots empty and every sector's content arrives
		   through here.  If the driver rejects the compressed format the call
		   fails silently, the slot keeps the blank RGBA image it was created
		   with, and the terrain draws untextured -- which is white once the
		   vertex colour carries brightness rather than the old detail mask.
		   Say so once rather than leaving it to be inferred from the screen. */
		GLenum eUploadError = glGetError();
		if (eUploadError != GL_NO_ERROR)
		{
			static bool s_bReportedCompressedUploadFail = false;
			if (!s_bReportedCompressedUploadFail && iLog)
			{
				s_bReportedCompressedUploadFail = true;
				iLog->LogToFile("\001[VITA][TEXUP] compressed upload rejected: fmt=0x%x %dx%d bytes=%d err=0x%x",
					(unsigned)eCompressedFormat, w, h, nSize, (unsigned)eUploadError);
			}
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
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glColor4f(1, 1, 1, 1);
	SetViewport(0, 0, m_nWidth, m_nHeight);
#endif
}
int CVitaRenderer::GenerateAlphaGlowTexture(float k) { return 0; }
void CVitaRenderer::SetMaterialColor(float r, float g, float b, float a) { }
int CVitaRenderer::LoadAnimatedTexture(const char * format, const int nCount) { return 0; }
void CVitaRenderer::RemoveAnimatedTexture(AnimTexInfo * pInfo) { }
AnimTexInfo * CVitaRenderer::GetAnimTexInfoFromId(int nId) { return 0; }
void CVitaRenderer::Draw2dLine(float x1, float y1, float x2, float y2) { }
void CVitaRenderer::SetLineWidth(float fWidth) { }
void CVitaRenderer::DrawLine(const Vec3 & vPos1, const Vec3 & vPos2) { }
void CVitaRenderer::DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2) { }
void CVitaRenderer::Graph(byte * g, int x, int y, int wdt, int hgt, int nC, int type, char * text, CFColor& color, float fScale) { }
void CVitaRenderer::DrawBall(float x, float y, float z, float radius) { }
void CVitaRenderer::DrawBall(const Vec3 & pos, float radius) { }
void CVitaRenderer::DrawPoint(float x, float y, float z, float fSize) { }
void CVitaRenderer::FlushTextMessages() { }
void CVitaRenderer::DrawObjSprites(list2<CStatObjInst*> * pList, float fMaxViewDist, CObjManager * pObjMan) { }
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
unsigned int CVitaRenderer::MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid) { return 0; }
unsigned int CVitaRenderer::Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj) { return 0; }
ShadowMapFrustum * CVitaRenderer::MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type) { return 0; }
void CVitaRenderer::Set2DMode(bool enable, int ortox, int ortoy) { }
int CVitaRenderer::ScreenToTexture() { return 0; }
void CVitaRenderer::SetTexClampMode(bool clamp) { }
void CVitaRenderer::EnableSwapBuffers(bool bEnable) { }
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
