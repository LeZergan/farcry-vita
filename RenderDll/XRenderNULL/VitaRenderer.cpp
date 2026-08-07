//////////////////////////////////////////////////////////////////////
// Vita: see VitaRenderer.h for why this exists instead of a real
// CRenderer-derived backend.
//////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "VitaRenderer.h"

#if defined(LINUX)
#include <vitaGL.h>
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

/* Vita: deliberately NOT defining GetISystem() here -- CrySystem/System.cpp
   owns the one real definition (see its comment); a second one here would
   silently win under -Wl,--allow-multiple-definition and reintroduce the
   exact bug fixed earlier this session (see git log). */

CVitaRenderer::CVitaRenderer()
	: m_nWidth(0), m_nHeight(0), m_nColorBpp(32), m_nDepthBpp(24), m_nStencilBpp(8), m_cType(0)
{
	gcpVitaRenderer = this;
}

CVitaRenderer::~CVitaRenderer()
{
	ShutDown(false);
}

WIN_HWND CVitaRenderer::Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit)
{
	m_nWidth = width;
	m_nHeight = height;
	m_nColorBpp = cbpp;
	m_nDepthBpp = zbpp;
	m_nStencilBpp = sbits;
#if defined(LINUX)
	/* Vita: real vitaGL context + framebuffer -- same call validated
	   standalone by vita_bringup/src/main.c. */
	vglInit(0x400000);
	glViewport(0, 0, width, height);
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

void CVitaRenderer::BeginFrame()
{
#if defined(LINUX)
	/* Vita: real frame clear. This and Update()'s swap below are the
	   first genuine on-screen output driven by the actual engine, not a
	   standalone test -- everything else in this class is a mechanical
	   IRenderer stub (see VitaRenderer.h). */
	glClearColor(0.05f, 0.05f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
#endif
}

void CVitaRenderer::Update()
{
#if defined(LINUX)
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

bool CVitaRenderer::SetCurrentContext(WIN_HWND hWnd) { return false; }
bool CVitaRenderer::CreateContext(WIN_HWND hWnd, bool bAllowFSAA) { return false; }
bool CVitaRenderer::DeleteContext(WIN_HWND hWnd) { return false; }
int CVitaRenderer::GetFeatures() { return 0; }
int CVitaRenderer::GetMaxTextureMemory() { return 0; }
int CVitaRenderer::EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset) { return 0; }
bool CVitaRenderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen) { return false; }
void CVitaRenderer::FreeResources(int nFlags) { }
void CVitaRenderer::RefreshResources(int nFlags) { }
void CVitaRenderer::ShareResources(IRenderer * renderer) { }
void CVitaRenderer::GetViewport(int * x, int * y, int * width, int * height) { }
void CVitaRenderer::SetViewport(int x, int y, int width, int height) { }
void CVitaRenderer::SetScissor(int x, int y, int width, int height) { }
void CVitaRenderer::MakeCurrent() { }
void CVitaRenderer::DrawTriStrip(CVertexBuffer * src, int vert_num) { }
void * CVitaRenderer::GetDynVBPtr(int nVerts, int & nOffs, int Pool) { return 0; }
void CVitaRenderer::DrawDynVB(int nOffs, int Pool, int nVerts) { }
void CVitaRenderer::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F * pBuf, ushort * pInds, int nVerts, int nInds, int nPrimType) { }
void CVitaRenderer::SetFenceCompleted(CVertexBuffer * buffer) { }
CVertexBuffer	* CVitaRenderer::CreateBuffer(int vertexcount, int vertexformat, const char * szSource, bool bDynamic) { return 0; }
void CVitaRenderer::ReleaseBuffer(CVertexBuffer * bufptr) { }
void CVitaRenderer::DrawBuffer(CVertexBuffer * src, SVertexStream * indicies, int numindices, int offsindex, int prmode, int vert_start, int vert_stop, CMatInfo * mi) { }
void CVitaRenderer::UpdateBuffer(CVertexBuffer * dest, const void * src, int vertexcount, bool bUnLock, int nOffs, int Type) { }
void CVitaRenderer::CreateIndexBuffer(SVertexStream * dest, const void * src, int indexcount) { }
void CVitaRenderer::UpdateIndexBuffer(SVertexStream * dest, const void * src, int indexcount, bool bUnLock) { }
void CVitaRenderer::ReleaseIndexBuffer(SVertexStream * dest) { }
void CVitaRenderer::CheckError(const char * comment) { }
void CVitaRenderer::Draw3dBBox(const Vec3 & mins, const Vec3 & maxs, int nPrimType) { }
void CVitaRenderer::SetCamera(const CCamera & cam) { }
static CCamera s_defaultCamera; const CCamera& CVitaRenderer::GetCamera() { return s_defaultCamera; }
bool CVitaRenderer::SetGammaDelta(const float fGamma) { return false; }
bool CVitaRenderer::ChangeDisplay(unsigned int width, unsigned int height, unsigned int cbpp) { return false; }
void CVitaRenderer::ChangeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height) { }
bool CVitaRenderer::SaveTga(unsigned char * sourcedata, int sourceformat, int w, int h, const char * filename, bool flip) { return false; }
void CVitaRenderer::SetTexture(int tnum, ETexType Type) { }
void CVitaRenderer::SetWhiteTexture() { }
void CVitaRenderer::WriteXY(CXFont * currfont, int x, int y, float xscale, float yscale, float r, float g, float b, float a, const char * message, ...) { }
void CVitaRenderer::Draw2dText(float posX, float posY, const char * szText, SDrawTextInfo & info) { }
void CVitaRenderer::Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z) { }
void CVitaRenderer::DrawImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float r, float g, float b, float a) { }
int CVitaRenderer::SetPolygonMode(int mode) { return 0; }
void CVitaRenderer::GetMemoryUsage(ICrySizer* Sizer) { }
void CVitaRenderer::ScreenShot(const char * filename) { }
void CVitaRenderer::ProjectToScreen(float ptx, float pty, float ptz, float * sx, float * sy, float * sz) { }
int CVitaRenderer::UnProject(float sx, float sy, float sz, float * px, float * py, float * pz, const float modelMatrix[16], const float projMatrix[16], const int viewport[4]) { return 0; }
int CVitaRenderer::UnProjectFromScreen(float sx, float sy, float sz, float * px, float * py, float * pz) { return 0; }
void CVitaRenderer::GetModelViewMatrix(float * mat) { }
void CVitaRenderer::GetModelViewMatrix(double * mat) { }
void CVitaRenderer::GetProjectionMatrix(double * mat) { }
void CVitaRenderer::GetProjectionMatrix(float * mat) { }
Vec3 CVitaRenderer::GetUnProject(const Vec3 & WindowCoords, const CCamera & cam) { return Vec3(0,0,0); }
void CVitaRenderer::RenderToViewport(const CCamera & cam, float x, float y, float width, float height) { }
void CVitaRenderer::WriteDDS(byte * dat, int wdt, int hgt, int Size, const char * name, EImFormat eF, int NumMips) { }
void CVitaRenderer::WriteTGA(byte * dat, int wdt, int hgt, const char * name, int bits) { }
void CVitaRenderer::WriteJPG(byte * dat, int wdt, int hgt, char * name) { }
bool CVitaRenderer::FontUploadTexture(class CFBitmap* a0, ETEX_Format eTF) { return false; }
int CVitaRenderer::FontCreateTexture(int Width, int Height, byte * pData, ETEX_Format eTF) { return 0; }
bool CVitaRenderer::FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte * pData) { return false; }
void CVitaRenderer::FontReleaseTexture(class CFBitmap * pBmp) { }
void CVitaRenderer::FontSetTexture(class CFBitmap* a0, int nFilterMode) { }
void CVitaRenderer::FontSetTexture(int nTexId, int nFilterMode) { }
void CVitaRenderer::FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight) { }
void CVitaRenderer::FontSetBlending(int src, int dst) { }
void CVitaRenderer::FontRestoreRenderingState() { }
bool CVitaRenderer::EF_PrecacheResource(IShader * pSH, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(ITexPic * pTP, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(CLeafBuffer * pPB, float fDist, float fTimeToReady, int Flags) { return false; }
bool CVitaRenderer::EF_PrecacheResource(CDLight * pLS, float fDist, float fTimeToReady, int Flags) { return false; }
void CVitaRenderer::EF_EnableHeatVision(bool bEnable) { }
bool CVitaRenderer::EF_GetHeatVision() { return false; }
void CVitaRenderer::EF_PolygonOffset(bool bEnable, float fFactor, float fUnits) { }
void CVitaRenderer::EF_AddPolyToScene3D(int Ef, int numPts, SColorVert * verts, CCObject * obj, int nFogID) { }
CCObject * CVitaRenderer::EF_AddSpriteToScene(int Ef, int numPts, SColorVert * verts, CCObject * obj, byte * inds, int ninds, int nFogID) { return 0; }
void CVitaRenderer::EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D * verts) { }
void CVitaRenderer::EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D * verts) { }
IShader * CVitaRenderer::EF_LoadShader(const char * name, EShClass Class, int flags, uint64 nMaskGen) { return 0; }
SShaderItem CVitaRenderer::EF_LoadShaderItem(const char * name, EShClass Class, bool bShare, const char * templName, int flags, SInputShaderResources * Res, uint64 nMaskGen) { return SShaderItem(); }
bool CVitaRenderer::EF_ReloadFile(const char * szFileName) { return false; }
void CVitaRenderer::EF_ReloadShaderFiles(int nCategory) { }
void CVitaRenderer::EF_ReloadTextures() { }
IShader			* CVitaRenderer::EF_CopyShader(IShader * ef) { return 0; }
ITexPic * CVitaRenderer::EF_GetTextureByID(int Id) { return 0; }
ITexPic			* CVitaRenderer::EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId) { return 0; }
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
CRendElement * CVitaRenderer::EF_CreateRE(EDataType edt) { return 0; }
void CVitaRenderer::EF_StartEf() { }
CCObject * CVitaRenderer::EF_GetObject(bool bTemp, int num) { return 0; }
void CVitaRenderer::EF_AddEf(int NumFog, CRendElement * re, IShader * ef, SRenderShaderResources * sr, CCObject * obj, int nTempl, IShader * efState, int nSort) { }
void CVitaRenderer::EF_EndEf3D(int nFlags) { }
bool CVitaRenderer::EF_IsFakeDLight(CDLight * Source) { return false; }
void CVitaRenderer::EF_ADDDlight(CDLight * Source) { }
void CVitaRenderer::EF_ClearLightsList() { }
bool CVitaRenderer::EF_UpdateDLight(CDLight * pDL) { return false; }
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
void CVitaRenderer::SetClearColor(const Vec3 & vColor) { }
CLeafBuffer * CVitaRenderer::CreateLeafBuffer(bool bDynamic, const char * szSource, class CIndexedMesh * pIndexedMesh) { return 0; }
CLeafBuffer * CVitaRenderer::CreateLeafBufferInitialized(void * pVertBuffer, int nVertCount, int nVertFormat, ushort* pIndices, int nIndices, int nPrimetiveType, const char * szSource, EBufferType eBufType, int nMatInfoCount, int nClientTextureBindID, bool (*PrepareBufferCallback)(CLeafBuffer *, bool), void * CustomData, bool bOnlyVideoBuffer, bool bPrecache) { return 0; }
void CVitaRenderer::DeleteLeafBuffer(CLeafBuffer * pLBuffer) { }
int CVitaRenderer::GetFrameID(bool bIncludeRecursiveCalls) { return 0; }
void CVitaRenderer::MakeMatrix(const Vec3 & pos, const Vec3 & angles, const Vec3 & scale, Matrix44* mat) { }
void CVitaRenderer::DrawLabelImage(const Vec3 & vPos, float fSize, int nTextureId) { }
void CVitaRenderer::DrawLabel(Vec3 pos, float font_size, const char * label_text, ...) { }
void CVitaRenderer::DrawLabelEx(Vec3 pos, float font_size, float * pfColor, bool bFixedSize, bool bCenter, const char * label_text, ...) { }
void CVitaRenderer::Draw2dLabel(float x, float y, float font_size, float * pfColor, bool bCenter, const char * label_text, ...) { }
float CVitaRenderer::ScaleCoordX(float value) { return 0.0f; }
float CVitaRenderer::ScaleCoordY(float value) { return 0.0f; }
void CVitaRenderer::SetState(int State) { }
void CVitaRenderer::SetCullMode(int mode) { }
bool CVitaRenderer::EnableFog(bool enable) { return false; }
void CVitaRenderer::SetFog(float density, float fogstart, float fogend, const float * color, int fogmode) { }
void CVitaRenderer::EnableTexGen(bool enable) { }
void CVitaRenderer::SetTexgen(float scaleX, float scaleY, float translateX, float translateY) { }
void CVitaRenderer::SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2) { }
void CVitaRenderer::SetLodBias(float value) { }
void CVitaRenderer::SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa) { }
void CVitaRenderer::EnableVSync(bool enable) { }
void CVitaRenderer::PushMatrix() { }
void CVitaRenderer::RotateMatrix(float a, float x, float y, float z) { }
void CVitaRenderer::RotateMatrix(const Vec3 & angels) { }
void CVitaRenderer::TranslateMatrix(float x, float y, float z) { }
void CVitaRenderer::ScaleMatrix(float x, float y, float z) { }
void CVitaRenderer::TranslateMatrix(const Vec3 & pos) { }
void CVitaRenderer::MultMatrix(float * mat) { }
void CVitaRenderer::LoadMatrix(const Matrix44 * src) { }
void CVitaRenderer::PopMatrix() { }
void CVitaRenderer::EnableTMU(bool enable) { }
void CVitaRenderer::SelectTMU(int tnum) { }
unsigned int CVitaRenderer::DownLoadToVideoMemory(unsigned char * data, int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat, int filter, int Id, char * szCacheName, int flags) { return 0; }
void CVitaRenderer::UpdateTextureInVideoMemory(uint tnum, unsigned char * newdata, int posx, int posy, int w, int h, ETEX_Format eTFSrc) { }
unsigned int CVitaRenderer::LoadTexture(const char * filename, int * tex_type, unsigned int def_tid, bool compresstodisk, bool bWarn) { return 0; }
bool CVitaRenderer::DXTCompress(byte * raw_data, int nWidth, int nHeight, ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback) { return false; }
bool CVitaRenderer::DXTDecompress(byte * srcData, byte * dstData, int nWidth, int nHeight, ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix) { return false; }
void CVitaRenderer::RemoveTexture(unsigned int TextureId) { }
void CVitaRenderer::RemoveTexture(ITexPic * pTexPic) { }
void CVitaRenderer::TextToScreen(float x, float y, const char * format, ...) { }
void CVitaRenderer::TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ...) { }
void CVitaRenderer::ResetToDefault() { }
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
void CVitaRenderer::ClearDepthBuffer() { }
void CVitaRenderer::ClearColorBuffer(const Vec3 vColor) { }
void CVitaRenderer::ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX, int nScaledY) { }
void CVitaRenderer::SetFogColor(float * color) { }
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
	iConsole = sp->ipConsole;
	iLog = sp->ipLog;
	iSystem = sp->ipSystem;
	iTimer = sp->ipTimer;
	pTest_int = sp->ipTest_int;
	pIPhysicalWorld = sp->pIPhysicalWorld;

	return new CVitaRenderer();
}
