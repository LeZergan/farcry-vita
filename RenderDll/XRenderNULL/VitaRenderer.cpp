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
	: m_nWidth(0), m_nHeight(0), m_nColorBpp(32), m_nDepthBpp(24), m_nStencilBpp(8), m_cType(0), m_nDynVBCursor(0)
{
	sceClibPrintf("[BOOTTRACE] CVitaRenderer ctor entered\n");
	gcpVitaRenderer = this;
	sceClibPrintf("[BOOTTRACE] CVitaRenderer ctor done\n");
}

CVitaRenderer::~CVitaRenderer()
{
	ShutDown(false);
}

WIN_HWND CVitaRenderer::Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit)
{
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init entered, width=%d height=%d cbpp=%u\n", width, height, cbpp);
	m_nWidth = width;
	m_nHeight = height;
	m_nColorBpp = cbpp;
	m_nDepthBpp = zbpp;
	m_nStencilBpp = sbits;
#if defined(LINUX)
	/* Vita: real vitaGL context + framebuffer -- same call validated
	   standalone by vita_bringup/src/main.c. */
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: before vglInit\n");
	vglInit(0x400000);
	sceClibPrintf("[BOOTTRACE] CVitaRenderer::Init: after vglInit, before glViewport\n");
	glViewport(0, 0, width, height);
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
int CVitaRenderer::GetFeatures() { return RFT_RGBA; }
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
	sceClibPrintf("[BOOTTRACE] DrawDynVB: nOffs=%d nVerts=%d v0.xyz=(%f,%f,%f) v0.color=%08x v0.st=(%f,%f)\n",
		nOffs, nVerts, pV->xyz.x, pV->xyz.y, pV->xyz.z, pV->color.dcolor, pV->st[0], pV->st[1]);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->xyz);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->color);
	glTexCoordPointer(2, GL_FLOAT, sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), &pV->st);
	glDrawArrays(GL_TRIANGLES, 0, nVerts);
	sceClibPrintf("[BOOTTRACE] DrawDynVB: after glDrawArrays err=%d\n", glGetError());
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
}

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
void CVitaRenderer::Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z)
{
#if defined(LINUX)
	/* Vita: real basic-shape/2D-image primitive -- solid-colored filled
	   rectangle when texture_id<=0 (no real ITexPic pipeline exists yet,
	   see EF_LoadTexture's stub), or a textured quad otherwise (e.g. the
	   font atlas, or any texture id obtained via FontCreateTexture).
	   angle is ignored (axis-aligned only) -- not needed for menu boxes. */
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
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
	else
	{
		glDisable(GL_TEXTURE_2D);
	}

	float x0 = xpos, y0 = ypos, x1 = xpos + w, y1 = ypos + h;
	const float verts[8] = { x0,y0, x1,y0, x1,y1, x0,y1 };
	const float uvs[8]   = { s0,t0, s1,t0, s1,t1, s0,t1 };
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
#endif
}
void CVitaRenderer::DrawImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float r, float g, float b, float a)
{
	Draw2dImage(xpos, ypos, w, h, texture_id, s0, t0, s1, t1, 0.0f, r, g, b, a, 1.0f);
}
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
	sceClibPrintf("[BOOTTRACE] FontUpdateTexture entered, nTexId=%d USize=%d VSize=%d pData=%p bytes=[%02x %02x %02x %02x]\n",
		nTexId, USize, VSize, (void*)pData, pData?pData[0]:0, pData?pData[1]:0, pData?pData[2]:0, pData?pData[3]:0);
	if (pData) {
		long total = (long)USize * VSize * 4;
		long nonWhiteFF = 0, nonZeroAlpha = 0;
		unsigned char minB = 255, maxB = 0;
		for (long i = 0; i < total; i += 4) {
			unsigned char a = pData[i+3];
			if (a != 0) nonZeroAlpha++;
			if (a < minB) minB = a;
			if (a > maxB) maxB = a;
		}
		sceClibPrintf("[BOOTTRACE] FontUpdateTexture: buffer scan total=%ld nonZeroAlpha=%ld minAlpha=%d maxAlpha=%d\n",
			total/4, nonZeroAlpha, (int)minB, (int)maxB);
	}
	glBindTexture(GL_TEXTURE_2D, (GLuint)nTexId);
	glTexSubImage2D(GL_TEXTURE_2D, 0, X, Y, USize, VSize, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	sceClibPrintf("[BOOTTRACE] FontUpdateTexture: after glTexSubImage2D err=%d\n", glGetError());
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
	sceClibPrintf("[BOOTTRACE] FontSetTexture: nTexId=%d\n", nTexId);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, (GLuint)nTexId);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	sceClibPrintf("[BOOTTRACE] FontSetTexture: err=%d\n", glGetError());
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
	/* Vita: src/dst are CryEngine's GS_BLSRC_x / GS_BLDST_x flags (see
	   CryCommon/IRenderer.h); CFFont almost always passes the default
	   src-alpha/one-minus-src-alpha pair already set by
	   FontSetRenderingState above, so a full flag-to-GLenum mapping
	   isn't needed for legible text yet -- left as a follow-up. */
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

CVitaTexPic::~CVitaTexPic()
{
	if (m_pRGBA32)
		delete [] m_pRGBA32;
}

#if defined(LINUX)
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

	if (pPak->FRead(pSrcBuf, 1, srcSize, fp) != (size_t)srcSize)
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

static byte *LoadDDS_RGBA32(ICryPak *pPak, const char *path, int *pOutW, int *pOutH)
{
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

	sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: %s width=%d height=%d fourCC=%c%c%c%c\n",
		path, width, height, fourCCBytes[0], fourCCBytes[1], fourCCBytes[2], fourCCBytes[3]);

	if (fourCC == DDS_FOURCC_NONE || width <= 0 || height <= 0)
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: unsupported DDS variant (only DXT1/3/5 handled) %s\n", path);
		pPak->FClose(fp);
		return NULL;
	}

	int blockCountX = (width + 3) / 4;
	int blockCountY = (height + 3) / 4;
	long compressedSize = (long)blockCountX * blockCountY * blockSize;

	sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: %s before compressed FRead, compressedSize=%ld\n", path, compressedSize);
	byte *pCompressed = new byte[compressedSize];
	size_t nReadGot = pPak->FRead(pCompressed, 1, compressedSize, fp);
	sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: %s after compressed FRead, got=%u\n", path, (unsigned)nReadGot);
	if (nReadGot != (size_t)compressedSize)
	{
		sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: compressed data read failed for %s\n", path);
		delete [] pCompressed;
		pPak->FClose(fp);
		return NULL;
	}
	pPak->FClose(fp);

	sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: %s before block decompress loop, blocks=%dx%d\n", path, blockCountX, blockCountY);
	byte *pRGBA = new byte[width * height * 4];
	for (int by = 0; by < blockCountY; by++)
	{
		for (int bx = 0; bx < blockCountX; bx++)
		{
			const byte *pBlock = pCompressed + (long)(by * blockCountX + bx) * blockSize;
			switch (fourCC)
			{
				case DDS_FOURCC_DXT1: DecompressBlockDXT1(bx*4, by*4, width, pBlock, pRGBA); break;
				case DDS_FOURCC_DXT3: DecompressBlockDXT3(bx*4, by*4, width, pBlock, pRGBA); break;
				case DDS_FOURCC_DXT5: DecompressBlockDXT5(bx*4, by*4, width, pBlock, pRGBA); break;
			}
		}
	}
	sceClibPrintf("[BOOTTRACE] LoadDDS_RGBA32: %s after block decompress loop\n", path);
	delete [] pCompressed;

	*pOutW = width;
	*pOutH = height;
	return pRGBA;
}
#endif

ITexPic			* CVitaRenderer::EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId)
{
#if defined(LINUX)
	sceClibPrintf("[BOOTTRACE] EF_LoadTexture entered, nameTex=%s\n", nameTex ? nameTex : "(null)");
	if (!nameTex || !iSystem || !iSystem->GetIPak())
		return 0;

	ICryPak *pPak = iSystem->GetIPak();
	int w = 0, h = 0;
	byte *pRGBA = NULL;

	size_t nameLen = strlen(nameTex);
	bool bIsBmp = nameLen >= 4 && stricmp(nameTex + nameLen - 4, ".bmp") == 0;
	bool bIsDds = nameLen >= 4 && stricmp(nameTex + nameLen - 4, ".dds") == 0;

	if (bIsBmp)
		pRGBA = LoadBMP_RGBA32(pPak, nameTex, &w, &h);
	else if (bIsDds)
		pRGBA = LoadDDS_RGBA32(pPak, nameTex, &w, &h);
	else
	{
		/* Vita: real Far Cry asset names are frequently the *source*
		   extension (.tga) or bare, but the compiled data on disk is
		   always .dds -- swap the extension and try that real file
		   before giving up. */
		char ddsPath[512];
		size_t baseLen = nameLen;
		const char *pDot = strrchr(nameTex, '.');
		if (pDot) baseLen = pDot - nameTex;
		if (baseLen > sizeof(ddsPath) - 5) baseLen = sizeof(ddsPath) - 5;
		memcpy(ddsPath, nameTex, baseLen);
		memcpy(ddsPath + baseLen, ".dds", 5);
		pRGBA = LoadDDS_RGBA32(pPak, ddsPath, &w, &h);
	}

	if (!pRGBA)
	{
		/* Honest failure, no fake pixels: no ITexPic is fabricated when
		   the real file can't be found or decoded. */
		return 0;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pRGBA);
	sceClibPrintf("[BOOTTRACE] EF_LoadTexture: uploaded, tex=%u w=%d h=%d glErr=%d\n", tex, w, h, glGetError());

	return new CVitaTexPic(nameTex, (int)tex, w, h, pRGBA);
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
float CVitaRenderer::ScaleCoordX(float value) { return value; }
float CVitaRenderer::ScaleCoordY(float value) { return value; }
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
