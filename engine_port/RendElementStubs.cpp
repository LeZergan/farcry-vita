/* Vita: real, honest no-op overrides for the handful of CRendElement
   subclass virtual methods that C3DEngine's constructor needs a complete
   vtable for (CRESky, CRE2DQuad), standing in for their REAL bodies
   (RenderDll/Common/RendElements/CRESky.cpp, CRE2DQuad.cpp) -- those real
   bodies reach into gRenDev/CPShader/CVProgram, i.e. the real CRenderer's
   internal shader pipeline, which doesn't exist for CVitaRenderer (same
   "large separate undertaking" gap already documented at the EF_LoadShader/
   EF_CreateRE call sites in VitaRenderer.cpp). This file exists purely so
   C3DEngine's constructor can hold real, valid, non-null CRESky/CRE2DQuad
   objects (needed because it casts EF_CreateRE's result to the concrete
   type and, for CRESky, dereferences it immediately) without pulling in
   that whole undone subsystem. Nothing here is ever actually called yet
   -- the render loop doesn't invoke 3D-engine rendering at all so far. */
#include "RenderPCH.h"
#if defined(LINUX)
#include <CRESky.h>
#include <CRE2DQuad.h>
#include <CREDummy.h>
#include <CRETerrainSector.h>
#include <CREOcLeaf.h>
#include <CREScreenProcess.h>
#include "../RenderDll/Common/RendElements/CREScreenCommon.h"

/* These definitions are copied from the stock Common/Shaders backend.  The
   Vita renderer does not compile ShaderCore/ShaderTemplate yet, but engine
   materials still own SRenderShaderResources and must release them normally. */
TArray<SRenderShaderResources *> SShader::m_ShaderResources_known;
SLightMaterial *SLightMaterial::current_material = NULL;
int SLightMaterial::m_ObjFrame = 0;
TArray<SLightMaterial *> SLightMaterial::known_materials;

void SLightMaterial::Release()
{
	--m_nRefCounter;
	if (m_nRefCounter <= 0)
	{
		if (Id >= 0 && Id < known_materials.Num())
			known_materials[Id] = NULL;
		delete this;
	}
}

SRenderShaderResources::~SRenderShaderResources()
{
	for (int i = 0; i < EFTT_MAX; ++i)
	{
		if (m_Textures[i])
		{
			delete m_Textures[i];
			m_Textures[i] = NULL;
		}
	}
	SAFE_RELEASE(m_LMaterial);
	if (m_Id >= 0 && m_Id < SShader::m_ShaderResources_known.Num())
		SShader::m_ShaderResources_known[m_Id] = NULL;
}

/* Vita: CRendElement itself (the base every RE subclass derives from,
   CryCommon/RendElement.h) needs the same treatment -- its real body is
   RenderDll/Common/RendElements/RendElement.cpp, ~1500 lines and 177
   gRenDev references, the same undone-CRenderer-pipeline gap as above.
   Its destructor and a few accessors are inline in the header (so don't
   need a body here), but mfPrepare is the first non-inline virtual it
   declares, making it the Itanium-ABI "key function" whose translation
   unit is where the vtable/RTTI get emitted -- without a real definition
   somewhere, every CRendElement-derived object (including CRESky/CREDummy/
   etc above) fails to link with "undefined reference to typeinfo for
   CRendElement". Honest no-op bodies for every other out-of-line virtual
   too, so any future caller that reaches the base class directly (instead
   of a subclass's override) gets a real, harmless no-op instead of a
   fresh undefined-reference hunt. */
CRendElement CRendElement::m_RootGlobal;

void CRendElement::mfPrepare() {}
bool CRendElement::mfCullByClipPlane(CCObject *pObj) { return false; }
CMatInfo *CRendElement::mfGetMatInfo() { return NULL; }
list2<CMatInfo> *CRendElement::mfGetMatInfoList() { return NULL; }
int CRendElement::mfGetMatId() { return 0; }
bool CRendElement::mfCull(CCObject *obj) { return true; }
bool CRendElement::mfCull(CCObject *obj, SShader *ef) { return true; }
void CRendElement::mfReset() {}
CRendElement *CRendElement::mfCopyConstruct(void) { return NULL; }
void CRendElement::mfCenter(Vec3& centr, CCObject *pObj) { centr.Set(0,0,0); }
void CRendElement::mfGetPlane(Plane& pl) {}
float CRendElement::mfDistanceToCameraSquared(const CCObject & thisObject) { return 0.0f; }
void CRendElement::mfEndFlush() {}
void CRendElement::Release() {}
int CRendElement::mfTransform(Matrix44& ViewMatr, Matrix44& ProjMatr, vec4_t *verts, vec4_t *vertsp, int Num) { return 0; }
bool CRendElement::mfIsValidTime(SShader *ef, CCObject *obj, float curtime) { return false; }
void CRendElement::mfBuildGeometry(SShader *ef) {}
bool CRendElement::mfCompile(SShader *ef, char *scr) { return false; }
CRendElement *CRendElement::mfCreateWorldRE(SShader *ef, SInpData *ds) { return NULL; }
bool CRendElement::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }
void *CRendElement::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags) { return NULL; }

CRESky::~CRESky() {}
void CRESky::mfPrepare() {}
bool CRESky::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }

void CRE2DQuad::mfPrepare() {}
bool CRE2DQuad::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }
void *CRE2DQuad::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags) { return NULL; }

/* Static meshes keep one OcLeaf render element per material chunk.  Vita's
   leaf-buffer path submits those chunks directly, so these desktop deferred-
   pipeline hooks are deliberately inert, but the real concrete object and
   metadata must exist for Cry3DEngine's ownership/culling code. */
CREOcLeaf *CREOcLeaf::m_pLastRE = NULL;
SLightIndicies *CREOcLeaf::mfGetIndiciesForLight(CDLight *) { return NULL; }
void CREOcLeaf::mfGenerateIndicesInsideFrustrum(SLightIndicies *, CDLight *) {}
void CREOcLeaf::mfGenerateIndicesForAttenuation(SLightIndicies *, CDLight *) {}
void CREOcLeaf::mfFillRB(CCObject *) {}
CMatInfo *CREOcLeaf::mfGetMatInfo() { return m_pChunk; }
list2<CMatInfo> *CREOcLeaf::mfGetMatInfoList() { return m_pBuffer ? m_pBuffer->m_pMats : NULL; }
int CREOcLeaf::mfGetMatId() { return m_pChunk ? m_pChunk->m_Id : 0; }
bool CREOcLeaf::mfPreDraw(SShaderPass *) { return true; }
void CREOcLeaf::mfGetPlane(Plane&) {}
void CREOcLeaf::mfPrepare() {}
bool CREOcLeaf::mfCullByClipPlane(CCObject *) { return false; }
void CREOcLeaf::mfCenter(Vec3& pos, CCObject *) { pos = m_pChunk ? m_pChunk->m_vCenter : Vec3(0,0,0); }
bool CREOcLeaf::mfDraw(SShader *, SShaderPass *) { return false; }
void *CREOcLeaf::mfGetPointer(ESrcPointer, int *, int, ESrcPointer, int) { return NULL; }
void CREOcLeaf::mfEndFlush() {}
float CREOcLeaf::mfMinDistanceToCamera(CCObject *) { return 0.0f; }
float CREOcLeaf::mfDistanceToCameraSquared(const CCObject &) { return 0.0f; }
bool CREOcLeaf::mfCheckUpdate(int, int) { return true; }
void CREOcLeaf::mfGetBBox(Vec3& mins, Vec3& maxs)
{
	mins = m_pBuffer ? m_pBuffer->m_vBoxMin : Vec3(0,0,0);
	maxs = m_pBuffer ? m_pBuffer->m_vBoxMax : Vec3(0,0,0);
}

void CREDummy::mfPrepare() {}
bool CREDummy::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }

/* Lightweight Vita implementation of Crytek's real screen-process state
   contract.  The desktop implementation also compiles a large Cg post-FX
   pipeline in its constructor; Vita keeps the same parameter semantics so
   game/script code can safely enable and query effects while the draw hook
   remains a no-op until equivalent vitaGL post-process shaders are added. */
void CScreenVars::Create() {}
void CScreenVars::Release() {}
void CScreenVars::Reset()
{
	m_bFadeActive = m_bBlurActive = m_bColorTransferActive = false;
	m_bMotionBlurActive = m_bGlareActive = m_bFlashBangActive = false;
	m_bCartoonActive = m_bDofActive = m_bScreenTexActive = false;
	m_iNightVisionActive = m_iHeatVisionActive = 0;
	m_fFadeCurrTime = m_fFadeTime;
	m_fFadeCurrPreTime = m_fFadePreTime;
}

CREScreenProcess::CREScreenProcess()
{
	mfSetType(eDATA_ScreenProcess);
	mfUpdateFlags(FCEF_TRANSFORM);
	m_pVars = new CScreenVars;
}

CREScreenProcess::~CREScreenProcess() { delete m_pVars; m_pVars = NULL; }
void CREScreenProcess::mfPrepare() {}
bool CREScreenProcess::mfDraw(SShader *, SShaderPass *) { return false; }
bool CREScreenProcess::mfDrawLowSpec(SShader *, SShaderPass *) { return false; }
void CREScreenProcess::mfReset() { if(m_pVars) m_pVars->Reset(); }

void CREScreenProcess::mfActivate(int process)
{
	if(!m_pVars) return;
	switch(process)
	{
	case SCREENPROCESS_FADE:          m_pVars->m_bFadeActive = true; break;
	case SCREENPROCESS_BLUR:          m_pVars->m_bBlurActive = true; break;
	case SCREENPROCESS_COLORTRANSFER: m_pVars->m_bColorTransferActive = true; break;
	case SCREENPROCESS_MOTIONBLUR:    m_pVars->m_bMotionBlurActive = true; break;
	case SCREENPROCESS_GLARE:         m_pVars->m_bGlareActive = true; break;
	case SCREENPROCESS_NIGHTVISION:   m_pVars->m_iNightVisionActive = 1; break;
	case SCREENPROCESS_HEATVISION:    m_pVars->m_iHeatVisionActive = 1; break;
	case SCREENPROCESS_FLASHBANG:     m_pVars->m_bFlashBangActive = true; break;
	case SCREENPROCESS_CARTOON:       m_pVars->m_bCartoonActive = true; break;
	case SCREENPROCESS_DOF:           m_pVars->m_bDofActive = true; break;
	case SCREENPROCESS_SCREENTEX:     m_pVars->m_bScreenTexActive = true; break;
	default: break;
	}
}

int CREScreenProcess::mfSetParameter(int process, int param, void *value)
{
	if(!m_pVars || !value) return 0;
	if(param == SCREENPROCESS_ACTIVE)
	{
		const bool active = (process == SCREENPROCESS_NIGHTVISION || process == SCREENPROCESS_HEATVISION)
			? (*(const int *)value != 0) : (*(const bool *)value != 0);
		switch(process)
		{
		case SCREENPROCESS_FADE:          m_pVars->m_bFadeActive = active; break;
		case SCREENPROCESS_BLUR:          m_pVars->m_bBlurActive = active; break;
		case SCREENPROCESS_COLORTRANSFER: m_pVars->m_bColorTransferActive = active; break;
		case SCREENPROCESS_MOTIONBLUR:    m_pVars->m_bMotionBlurActive = active; break;
		case SCREENPROCESS_GLARE:         m_pVars->m_bGlareActive = active; break;
		case SCREENPROCESS_NIGHTVISION:   m_pVars->m_iNightVisionActive = active; break;
		case SCREENPROCESS_HEATVISION:    m_pVars->m_iHeatVisionActive = active; break;
		case SCREENPROCESS_FLASHBANG:     m_pVars->m_bFlashBangActive = active; break;
		case SCREENPROCESS_CARTOON:       m_pVars->m_bCartoonActive = active; break;
		case SCREENPROCESS_DOF:           m_pVars->m_bDofActive = active; break;
		case SCREENPROCESS_SCREENTEX:     m_pVars->m_bScreenTexActive = active; break;
		default: break;
		}
		return 1;
	}

	switch(process)
	{
	case SCREENPROCESS_FADE:
		if(param == SCREENPROCESS_TRANSITIONTIME) m_pVars->m_fFadeTime = *(float *)value;
		else if(param == SCREENPROCESS_PRETRANSITIONTIME) m_pVars->m_fFadePreTime = *(float *)value;
		else if(param == SCREENPROCESS_FADECOLOR) m_pVars->m_pFadeColor = *(color4f *)value;
		break;
	case SCREENPROCESS_BLUR:
		if(param == SCREENPROCESS_BLURAMOUNT) m_pVars->m_fBlurAmount = *(float *)value;
		else if(param == SCREENPROCESS_BLURCOLORRED) m_pVars->m_pBlurColor.r = *(float *)value;
		else if(param == SCREENPROCESS_BLURCOLORGREEN) m_pVars->m_pBlurColor.g = *(float *)value;
		else if(param == SCREENPROCESS_BLURCOLORBLUE) m_pVars->m_pBlurColor.b = *(float *)value;
		break;
	case SCREENPROCESS_FLASHBANG:
		if(param == SCREENPROCESS_FLASHBANGTIMESCALE) m_pVars->m_fFlashBangTimeScale = *(float *)value;
		else if(param == SCREENPROCESS_FLASHBANGFLASHPOSX) m_pVars->m_fFlashBangFlashPosX = *(float *)value;
		else if(param == SCREENPROCESS_FLASHBANGFLASHPOSY) m_pVars->m_fFlashBangFlashPosY = *(float *)value;
		else if(param == SCREENPROCESS_FLASHBANGFLASHSIZEX) m_pVars->m_fFlashBangFlashSizeX = *(float *)value;
		else if(param == SCREENPROCESS_FLASHBANGFLASHSIZEY) m_pVars->m_fFlashBangFlashSizeY = *(float *)value;
		else if(param == SCREENPROCESS_FLASHBANGFORCEAFTERIMAGE) m_pVars->m_iFlashBangForce = *(int *)value;
		break;
	case SCREENPROCESS_DOF:
		if(param == SCREENPROCESS_DOFFOCALDISTANCE) m_pVars->m_fDofFocalDistance = *(float *)value;
		break;
	default: break;
	}
	return 1;
}

void *CREScreenProcess::mfGetParameter(int process, int param)
{
	static int zero = 0;
	if(!m_pVars) return &zero;
	if(param == SCREENPROCESS_ACTIVE)
	{
		switch(process)
		{
		case SCREENPROCESS_FADE:          return &m_pVars->m_bFadeActive;
		case SCREENPROCESS_BLUR:          return &m_pVars->m_bBlurActive;
		case SCREENPROCESS_COLORTRANSFER: return &m_pVars->m_bColorTransferActive;
		case SCREENPROCESS_MOTIONBLUR:    return &m_pVars->m_bMotionBlurActive;
		case SCREENPROCESS_GLARE:         return &m_pVars->m_bGlareActive;
		case SCREENPROCESS_NIGHTVISION:   return &m_pVars->m_iNightVisionActive;
		case SCREENPROCESS_HEATVISION:    return &m_pVars->m_iHeatVisionActive;
		case SCREENPROCESS_FLASHBANG:     return &m_pVars->m_bFlashBangActive;
		case SCREENPROCESS_CARTOON:       return &m_pVars->m_bCartoonActive;
		case SCREENPROCESS_DOF:           return &m_pVars->m_bDofActive;
		case SCREENPROCESS_SCREENTEX:     return &m_pVars->m_bScreenTexActive;
		default: return &zero;
		}
	}
	switch(process)
	{
	case SCREENPROCESS_FADE:
		if(param == SCREENPROCESS_TRANSITIONTIME) return &m_pVars->m_fFadeTime;
		if(param == SCREENPROCESS_PRETRANSITIONTIME) return &m_pVars->m_fFadePreTime;
		if(param == SCREENPROCESS_FADECOLOR) return &m_pVars->m_pFadeColor;
		break;
	case SCREENPROCESS_BLUR:
		if(param == SCREENPROCESS_BLURAMOUNT) return &m_pVars->m_fBlurAmount;
		if(param == SCREENPROCESS_BLURCOLORRED) return &m_pVars->m_pBlurColor.r;
		if(param == SCREENPROCESS_BLURCOLORGREEN) return &m_pVars->m_pBlurColor.g;
		if(param == SCREENPROCESS_BLURCOLORBLUE) return &m_pVars->m_pBlurColor.b;
		break;
	case SCREENPROCESS_FLASHBANG:
		if(param == SCREENPROCESS_FLASHBANGTIMESCALE) return &m_pVars->m_fFlashBangTimeScale;
		if(param == SCREENPROCESS_FLASHBANGFLASHPOSX) return &m_pVars->m_fFlashBangFlashPosX;
		if(param == SCREENPROCESS_FLASHBANGFLASHPOSY) return &m_pVars->m_fFlashBangFlashPosY;
		if(param == SCREENPROCESS_FLASHBANGFLASHSIZEX) return &m_pVars->m_fFlashBangFlashSizeX;
		if(param == SCREENPROCESS_FLASHBANGFLASHSIZEY) return &m_pVars->m_fFlashBangFlashSizeY;
		if(param == SCREENPROCESS_FLASHBANGTIMEOUT) return &m_pVars->m_fFlashBangTimeOut;
		if(param == SCREENPROCESS_FLASHBANGFORCEAFTERIMAGE) return &m_pVars->m_iFlashBangForce;
		break;
	case SCREENPROCESS_DOF:
		if(param == SCREENPROCESS_DOFFOCALDISTANCE) return &m_pVars->m_fDofFocalDistance;
		break;
	default: break;
	}
	return &zero;
}

void CRECommon::mfPrepare() {}
bool CRETerrainParticles::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }
#endif
