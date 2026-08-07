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

CRESky::~CRESky() {}
void CRESky::mfPrepare() {}
bool CRESky::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }

void CRE2DQuad::mfPrepare() {}
bool CRE2DQuad::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }
void *CRE2DQuad::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags) { return NULL; }

void CREDummy::mfPrepare() {}
bool CREDummy::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }

void CRECommon::mfPrepare() {}
bool CRETerrainParticles::mfDraw(SShader *ef, SShaderPass *sfm) { return false; }
#endif
