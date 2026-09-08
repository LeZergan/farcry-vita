//////////////////////////////////////////////////////////////////////
// Vita: minimal real IRenderer implementation for the Vita port.
//
// CNULLRenderer (and every other RenderDll backend) derives from
// RenderDll/Common/Renderer.h's CRenderer, a ~57K-line shared base
// implementing the shader/effect/resource pipeline. Porting that is a
// large separate undertaking (tracked, not started). This class instead
// implements IRenderer directly -- mechanically stubbed (see
// engine_port/compat's ScriptStubs.h/.cpp for the established pattern
// used earlier this session for IScriptSystem) -- with the handful of
// lifecycle methods that matter for getting a real frame on screen
// (Init/BeginFrame/Update/ShutDown) doing genuine vitaGL work. Confirmed
// working standalone via vita_bringup/src/main.c before this was wired
// into the actual engine.
//////////////////////////////////////////////////////////////////////

#ifndef VITA_RENDERER_H
#define VITA_RENDERER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#if _MSC_VER > 1000
# pragma once
#endif

/* Vita: real ITexPic backed by a real vitaGL texture. Upload staging pixels
   are released after glTexImage2D; the Vita runtime no longer has a caller
   that needs a persistent GetData32 copy. No fabricated content:
   GetTextureID/GetWidth/GetHeight all reflect the actual decoded file. */
class CVitaTexPic : public ITexPic
{
public:
	CVitaTexPic(const char *pName, int nGLTexId, int nWidth, int nHeight, byte *pRGBA32, int nFlags, int nFlags2)
		: m_nRefs(1), m_nGLTexId(nGLTexId), m_nWidth(nWidth), m_nHeight(nHeight),
		  m_nFlags(nFlags), m_nFlags2(nFlags2), m_pRGBA32(pRGBA32)
	{
		strncpy(m_szName, pName ? pName : "", sizeof(m_szName)-1);
		m_szName[sizeof(m_szName)-1] = 0;
	}
	virtual void AddRef() { ++m_nRefs; }
	virtual void Release(int bForce=false) { if (--m_nRefs <= 0 || bForce) delete this; }
	virtual const char *GetName() { return m_szName; }
	virtual int GetWidth() { return m_nWidth; }
	virtual int GetHeight() { return m_nHeight; }
	virtual int GetOriginalWidth() { return m_nWidth; }
	virtual int GetOriginalHeight() { return m_nHeight; }
	virtual int GetTextureID() { return m_nGLTexId; }
	virtual int GetFlags() { return m_nFlags; }
	virtual int GetFlags2() { return m_nFlags2; }
	virtual void SetClamp(bool bEnable) { }
	virtual bool IsTextureLoaded() { return m_nGLTexId > 0; }
	virtual void PrecacheAsynchronously(float fDist, int Flags) { }
	virtual void Preload(int Flags) { }
	virtual byte *GetData32() { return m_pRGBA32; }
	virtual bool SetFilter(int nFilter) { return true; }

private:
	int m_nRefs;
	int m_nGLTexId;
	int m_nWidth, m_nHeight;
	int m_nFlags, m_nFlags2;
	byte *m_pRGBA32; // optional owned CPU pixels; normally null on Vita
	char m_szName[256];
public:
	~CVitaTexPic();
};

class CVitaShader;
extern class CVitaRenderer *gcpVitaRenderer;

class CVitaRenderer : public IRenderer
{
public:
	CVitaRenderer();
	virtual ~CVitaRenderer();

	virtual WIN_HWND Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit);
	virtual bool SetCurrentContext(WIN_HWND hWnd);
	virtual bool CreateContext(WIN_HWND hWnd, bool bAllowFSAA);
	virtual bool DeleteContext(WIN_HWND hWnd);
	virtual int GetFeatures();
	virtual int GetMaxTextureMemory();
	virtual void ShutDown(bool bReInit);
	virtual int EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset);
	virtual bool ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen);
	virtual void Release();
	virtual void FreeResources(int nFlags);
	virtual void RefreshResources(int nFlags);
	virtual void PreLoad();
	virtual void PostLoad();
	virtual void BeginFrame();
	virtual void Update();
	virtual void ShareResources(IRenderer * renderer);
	virtual void GetViewport(int * x, int * y, int * width, int * height);
	virtual void SetViewport(int x, int y, int width, int height);
	virtual void SetScissor(int x, int y, int width, int height);
	virtual void MakeCurrent();
	virtual void DrawTriStrip(CVertexBuffer * src, int vert_num);
	virtual void * GetDynVBPtr(int nVerts, int & nOffs, int Pool);
	virtual void DrawDynVB(int nOffs, int Pool, int nVerts);
	virtual void DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F * pBuf, ushort * pInds, int nVerts, int nInds, int nPrimType);
	virtual void SetFenceCompleted(CVertexBuffer * buffer);
	virtual CVertexBuffer	* CreateBuffer(int vertexcount, int vertexformat, const char * szSource, bool bDynamic);
	virtual void ReleaseBuffer(CVertexBuffer * bufptr);
	virtual void DrawBuffer(CVertexBuffer * src, SVertexStream * indicies, int numindices, int offsindex, int prmode, int vert_start, int vert_stop, CMatInfo * mi);
	virtual void UpdateBuffer(CVertexBuffer * dest, const void * src, int vertexcount, bool bUnLock, int nOffs, int Type);
	virtual void CreateIndexBuffer(SVertexStream * dest, const void * src, int indexcount);
	virtual void UpdateIndexBuffer(SVertexStream * dest, const void * src, int indexcount, bool bUnLock);
	virtual void ReleaseIndexBuffer(SVertexStream * dest);
	virtual void CheckError(const char * comment);
	virtual void Draw3dBBox(const Vec3 & mins, const Vec3 & maxs, int nPrimType);
	virtual void SetCamera(const CCamera & cam);
	virtual const CCamera& GetCamera();
	virtual bool SetGammaDelta(const float fGamma);
	virtual bool ChangeDisplay(unsigned int width, unsigned int height, unsigned int cbpp);
	virtual void ChangeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height);
	virtual bool SaveTga(unsigned char * sourcedata, int sourceformat, int w, int h, const char * filename, bool flip);
	virtual void SetTexture(int tnum, ETexType Type);
	virtual void SetWhiteTexture();
	virtual void WriteXY(CXFont * currfont, int x, int y, float xscale, float yscale, float r, float g, float b, float a, const char * message, ... );
	virtual void Draw2dText(float posX, float posY, const char * szText, SDrawTextInfo & info);
	virtual void Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z);
	virtual void DrawImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float r, float g, float b, float a);
	virtual int SetPolygonMode(int mode);
	virtual int GetWidth();
	virtual int GetHeight();
	virtual void GetMemoryUsage(ICrySizer* Sizer);
	virtual void ScreenShot(const char * filename);
	virtual int GetColorBpp();
	virtual int GetDepthBpp();
	virtual int GetStencilBpp();
	virtual void ProjectToScreen(float ptx, float pty, float ptz, float * sx, float * sy, float * sz);
	virtual int UnProject(float sx, float sy, float sz, float * px, float * py, float * pz, const float modelMatrix[16] , const float projMatrix[16] , const int    viewport[4] );
	virtual int UnProjectFromScreen(float sx, float sy, float sz, float * px, float * py, float * pz);
	virtual void GetModelViewMatrix(float * mat);
	virtual void GetModelViewMatrix(double * mat);
	virtual void GetProjectionMatrix(double * mat);
	virtual void GetProjectionMatrix(float * mat);
	virtual Vec3 GetUnProject(const Vec3 & WindowCoords, const CCamera & cam);
	virtual void RenderToViewport(const CCamera & cam, float x, float y, float width, float height);
	virtual void WriteDDS(byte * dat, int wdt, int hgt, int Size, const char * name, EImFormat eF, int NumMips);
	virtual void WriteTGA(byte * dat, int wdt, int hgt, const char * name, int bits);
	virtual void WriteJPG(byte * dat, int wdt, int hgt, char * name);
	virtual bool FontUploadTexture(class CFBitmap* , ETEX_Format eTF);
	virtual int FontCreateTexture(int Width, int Height, byte * pData, ETEX_Format eTF);
	virtual bool FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte * pData);
	virtual void FontReleaseTexture(class CFBitmap * pBmp);
	virtual void FontSetTexture(class CFBitmap* , int nFilterMode);
	virtual void FontSetTexture(int nTexId, int nFilterMode);
	virtual void FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight);
	virtual void FontSetBlending(int src, int dst);
	virtual void FontRestoreRenderingState();
	virtual bool EF_PrecacheResource(IShader * pSH, float fDist, float fTimeToReady, int Flags);
	virtual bool EF_PrecacheResource(ITexPic * pTP, float fDist, float fTimeToReady, int Flags);
	virtual bool EF_PrecacheResource(CLeafBuffer * pPB, float fDist, float fTimeToReady, int Flags);
	virtual bool EF_PrecacheResource(CDLight * pLS, float fDist, float fTimeToReady, int Flags);
	virtual void EF_EnableHeatVision(bool bEnable);
	virtual bool EF_GetHeatVision();
	virtual void EF_PolygonOffset(bool bEnable, float fFactor, float fUnits);
	virtual void EF_AddPolyToScene3D(int Ef, int numPts, SColorVert * verts, CCObject * obj, int nFogID);
	virtual CCObject * EF_AddSpriteToScene(int Ef, int numPts, SColorVert * verts, CCObject * obj, byte * inds, int ninds, int nFogID);
	virtual void EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D * verts);
	virtual void EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D * verts);
	virtual IShader * EF_LoadShader(const char * name, EShClass Class, int flags, uint64 nMaskGen);
	virtual SShaderItem EF_LoadShaderItem(const char * name, EShClass Class, bool bShare, const char * templName, int flags, SInputShaderResources * Res, uint64 nMaskGen);
	virtual bool EF_ReloadFile(const char * szFileName);
	virtual void EF_ReloadShaderFiles(int nCategory);
	virtual void EF_ReloadTextures();
	virtual IShader			* EF_CopyShader(IShader * ef);
	virtual ITexPic * EF_GetTextureByID(int Id);
	virtual ITexPic			* EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId);
	virtual int EF_LoadLightmap(const char * name);
	virtual bool EF_ScanEnvironmentCM(const char * name, int size, Vec3& Pos);
	virtual int EF_ReadAllImgFiles(IShader * ef, SShaderTexUnit * tl, STexAnim * ta, char * name);
	virtual char					** EF_GetShadersForFile(const char * File, int num);
	virtual SLightMaterial * EF_GetLightMaterial(char * Str);
	virtual bool EF_RegisterTemplate(int nTemplId, char * Name, bool bReplace);
	virtual void EF_AddSplash(Vec3 Pos, eSplashType eST, float fForce, int Id);
	virtual bool EF_HideTemplate(const char * name);
	virtual bool EF_UnhideTemplate(const char * name);
	virtual bool EF_UnhideAllTemplates();
	virtual bool EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale, bool bAdditive);
	virtual CRendElement * EF_CreateRE(EDataType edt);
	virtual void EF_StartEf();
	virtual CCObject * EF_GetObject(bool bTemp, int num);
	virtual void EF_AddEf(int NumFog, CRendElement * re, IShader * ef, SRenderShaderResources * sr, CCObject * obj, int nTempl, IShader * efState, int nSort);
	virtual void EF_EndEf3D(int nFlags);
	virtual bool EF_IsFakeDLight(CDLight * Source);
	virtual void EF_ADDDlight(CDLight * Source);
	virtual void EF_ClearLightsList();
	virtual bool EF_UpdateDLight(CDLight * pDL);
	virtual void EF_EndEf2D(bool bSort);
	virtual bool EF_DrawEfForName(char * name, float x, float y, float width, float height, CFColor& col, int nTempl);
	virtual bool EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl);
	virtual bool EF_DrawEf(IShader * ef, float x, float y, float width, float height, CFColor& col, int nTempl);
	virtual bool EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl);
	virtual bool EF_DrawPartialEfForName(char * name, SVrect * vr, SVrect * pr, CFColor& col);
	virtual bool EF_DrawPartialEfForNum(int num, SVrect * vr, SVrect * pr, CFColor& col);
	virtual bool EF_DrawPartialEf(IShader * ef, SVrect * vr, SVrect * pr, CFColor& col, float iwdt, float ihgt);
	virtual void * EF_Query(int Query, int Param);
	virtual void EF_ConstructEf(IShader * Ef);
	virtual void EF_SetWorldColor(float r, float g, float b, float a);
	virtual int EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex, bool bCaustics);
	virtual int GetPolyCount();
	virtual void GetPolyCount(int & nPolygons, int & nShadowVolPolys);
	virtual void SetClearColor(const Vec3 & vColor);
	virtual CLeafBuffer * CreateLeafBuffer(bool bDynamic, const char * szSource, class CIndexedMesh * pIndexedMesh);
	virtual CLeafBuffer * CreateLeafBufferInitialized(void * pVertBuffer, int nVertCount, int nVertFormat, ushort* pIndices, int nIndices, int nPrimetiveType, const char * szSource, EBufferType eBufType, int nMatInfoCount, int nClientTextureBindID, bool (*PrepareBufferCallback)(CLeafBuffer *, bool) , void * CustomData, bool bOnlyVideoBuffer, bool bPrecache);
	virtual void DeleteLeafBuffer(CLeafBuffer * pLBuffer);
	virtual int GetFrameID(bool bIncludeRecursiveCalls);
	virtual void MakeMatrix(const Vec3 & pos, const Vec3 & angles, const Vec3 & scale, Matrix44* mat);
	virtual void DrawLabelImage(const Vec3 & vPos, float fSize, int nTextureId);
	virtual void DrawLabel(Vec3 pos, float font_size, const char * label_text, ... );
	virtual void DrawLabelEx(Vec3 pos, float font_size, float * pfColor, bool bFixedSize, bool bCenter, const char * label_text, ... );
	virtual void Draw2dLabel(float x, float y, float font_size, float * pfColor, bool bCenter, const char * label_text, ... );
	virtual float ScaleCoordX(float value);
	virtual float ScaleCoordY(float value);
	virtual void SetState(int State);
	virtual void SetCullMode(int mode);
	virtual bool EnableFog(bool enable);
	virtual void SetFog(float density, float fogstart, float fogend, const float * color, int fogmode);
	virtual void EnableTexGen(bool enable);
	virtual void SetTexgen(float scaleX, float scaleY, float translateX, float translateY);
	virtual void SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2);
	virtual void SetLodBias(float value);
	virtual void SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa);
	virtual void EnableVSync(bool enable);
	virtual void PushMatrix();
	virtual void RotateMatrix(float a, float x, float y, float z);
	virtual void RotateMatrix(const Vec3 & angels);
	virtual void TranslateMatrix(float x, float y, float z);
	virtual void ScaleMatrix(float x, float y, float z);
	virtual void TranslateMatrix(const Vec3 & pos);
	virtual void MultMatrix(float * mat);
	virtual void LoadMatrix(const Matrix44 * src);
	virtual void PopMatrix();
	virtual void EnableTMU(bool enable);
	virtual void SelectTMU(int tnum);
	virtual unsigned int DownLoadToVideoMemory(unsigned char * data, int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat, int filter, int Id, char * szCacheName, int flags);
	virtual void UpdateTextureInVideoMemory(uint tnum, unsigned char * newdata, int posx, int posy, int w, int h, ETEX_Format eTFSrc);
	virtual unsigned int LoadTexture(const char * filename, int * tex_type, unsigned int def_tid, bool compresstodisk, bool bWarn);
	virtual bool DXTCompress(byte * raw_data, int nWidth, int nHeight, ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback);
	virtual bool DXTDecompress(byte * srcData, byte * dstData, int nWidth, int nHeight, ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix);
	virtual void RemoveTexture(unsigned int TextureId);
	virtual void RemoveTexture(ITexPic * pTexPic);
	virtual void TextToScreen(float x, float y, const char * format, ... );
	virtual void TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ... );
	virtual void ResetToDefault();
	virtual int GenerateAlphaGlowTexture(float k);
	virtual void SetMaterialColor(float r, float g, float b, float a);
	virtual int LoadAnimatedTexture(const char * format, const int nCount);
	virtual void RemoveAnimatedTexture(AnimTexInfo * pInfo);
	virtual AnimTexInfo * GetAnimTexInfoFromId(int nId);
	virtual void Draw2dLine(float x1, float y1, float x2, float y2);
	virtual void SetLineWidth(float fWidth);
	virtual void DrawLine(const Vec3 & vPos1, const Vec3 & vPos2);
	virtual void DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2);
	virtual void Graph(byte * g, int x, int y, int wdt, int hgt, int nC, int type, char * text, CFColor& color, float fScale);
	virtual void DrawBall(float x, float y, float z, float radius);
	virtual void DrawBall(const Vec3 & pos, float radius);
	virtual void DrawPoint(float x, float y, float z, float fSize);
	virtual void FlushTextMessages();
	virtual void DrawObjSprites(list2<CStatObjInst*> * pList, float fMaxViewDist, CObjManager * pObjMan);
	virtual void DrawQuad(const Vec3 & right, const Vec3 & up, const Vec3 & origin, int nFlipMode);
	virtual void DrawQuad(float dy, float dx, float dz, float x, float y, float z);
	virtual void ClearDepthBuffer();
	virtual void ClearColorBuffer(const Vec3 vColor);
	virtual void ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX, int nScaledY);
	virtual void SetFogColor(float * color);
	virtual void TransformTextureMatrix(float x, float y, float angle, float scale);
	virtual void ResetTextureMatrix();
	virtual char GetType();
	virtual char* GetVertexProfile(bool bSupportedProfile);
	virtual char* GetPixelProfile(bool bSupportedProfile);
	virtual void SetType(char type);
	virtual unsigned int MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid);
	virtual unsigned int Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj);
	virtual ShadowMapFrustum * MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type);
	virtual void Set2DMode(bool enable, int ortox, int ortoy);
	virtual int ScreenToTexture();
	virtual void SetTexClampMode(bool clamp);
	virtual void EnableSwapBuffers(bool bEnable);
	virtual WIN_HWND GetHWND();
	virtual void OnEntityDeleted(IEntityRender * pEntityRender);
	virtual void SetGlobalShaderTemplateId(int nTemplateId);
	virtual int GetGlobalShaderTemplateId();
	virtual int EnumAAFormats(TArray<SAAFormat>& Formats, bool bReset);
	virtual int CreateRenderTarget(int nWidth, int nHeight, ETEX_Format eTF);
	virtual bool DestroyRenderTarget(int nHandle);
	virtual bool SetRenderTarget(int nHandle);
	virtual float EF_GetWaterZElevation(float fX, float fY);

	// Called by CVitaTexPic when an external Release destroys a texture.
	void UnregisterTexture(CVitaTexPic *pTexture);
	/* Static brushes enter through CLeafBuffer's immediate Vita path rather
	   than EF_AddEf.  Keep the baked-lightmap fixed-function stage here so both
	   paths use the same unit-1 setup and, crucially, the static world does not
	   silently fall back to diffuse-only rendering. */
	bool BeginVitaLightMap(CCObject *pObject, CVertexBuffer *pGeometry);
	void EndVitaLightMap();

private:
	int m_nWidth;
	int m_nHeight;
	int m_nColorBpp;
	int m_nDepthBpp;
	int m_nStencilBpp;
	char m_cType;
	int m_nViewportX;
	int m_nViewportY;
	int m_nViewportWidth;
	int m_nViewportHeight;
	int m_nFrameId;
	Vec3 m_vClearColor;
	int m_nActiveLights;
	bool m_bSwapBuffersEnabled;

	// Backing store for GetDynVBPtr/DrawDynVB -- CFFont::DrawStringW (see
	// CryFont/FFont.cpp) fills this via GetDynVBPtr and submits it via
	// DrawDynVB(offset, pool, count); real vitaGL draw, not a stub.
	static const int DYNVB_CAPACITY = 16384;
	struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F m_DynVB[DYNVB_CAPACITY];
	int m_nDynVBCursor;

	// Vita: real 3D draw state -- see SetCamera/PushMatrix/etc and
	// DrawBuffer in VitaRenderer.cpp.
	CCamera m_Camera;
	int m_nCullMode;
	std::map<int, CVitaTexPic *> m_TextureById;
public:
	//! True once a texture name has been searched for and proven absent.
	//! Lets the draw paths tell "not loaded yet" apart from "will never load".
	bool IsTextureKnownMissing(const char *szName);
private:
	std::map<std::string, CVitaTexPic *> m_TextureByName;
	/* Textures already proven absent.  Without this, every material that names
	   a missing texture re-runs the whole candidate-path search and its failed
	   CryPak opens on every draw, every frame, for the life of the level. */
	std::set<std::string> m_FailedTextureNames;
	/* Material diffuses whose whole lazy-load search -- the name as authored and
	   the material-folder fallback built from it -- has already come back empty.
	   Keyed on both, because either one alone can succeed where the other fails.
	   Without it a missing diffuse spends one of the per-frame lazy-load slots
	   on every frame for the life of the level, and the geometry that was
	   waiting behind it in the queue never gets drawn at all. */
	std::set<std::string> m_LazyDiffuseGaveUp;
	std::map<std::string, CVitaShader *> m_ShaderByName;
	int m_nNextShaderId;

	// CryEngine builds per-draw CCObjects through EF_GetObject.  Reuse the
	// temporary pool every EF_StartEf instead of allocating thousands of
	// 256-byte objects per frame on Vita's CPU heap.
	std::vector<CCObject *> m_TempRenderObjects;
	std::vector<CCObject *> m_PermanentRenderObjects;
	size_t m_nTempRenderObjectCursor;
};

/* Far Cry's cut-out transparency lives in the shader template, not in the
   material's alpha reference: a leaf card is "TemplPlants1" with m_AlphaRef 0,
   and the real backends knew from the template that it needs an alpha test.
   Without that, every leaf, frond, grass blade, chain-link fence and decal
   draws as a solid rectangle of its texture including the parts meant to be
   invisible -- which is what "the transparencies are broken" looks like.
   Recognise those templates by name so the fixed-function alpha test can do
   the job the missing shader would have.

   "no_draw" is Crytek's explicitly non-rendering material (collision-only
   proxies and similar); it has no texture at all, so on this port it fell
   through to the white fallback and drew solid white boxes in the world. */
namespace VitaMaterial
{
	inline bool NameContains(const char *haystack, const char *needle)
	{
		if (!haystack || !needle)
			return false;
		const size_t hLen = strlen(haystack), nLen = strlen(needle);
		if (nLen > hLen)
			return false;
		for (size_t i = 0; i + nLen <= hLen; ++i)
			if (strnicmp(haystack + i, needle, nLen) == 0)
				return true;
		return false;
	}

	//! Material that must not be rendered at all.
	inline bool IsNoDraw(const char *shaderName)
	{
		return NameContains(shaderName, "no_draw") || NameContains(shaderName, "nodraw");
	}

	//! Collision/editor proxy slots which the desktop shader pipeline suppresses.
	inline bool IsNoDrawMaterial(const CMatInfo &material)
	{
		const char *shaderName = material.shaderItem.m_pShader ?
			material.shaderItem.m_pShader->GetName() : "";
		if (IsNoDraw(shaderName) || IsNoDraw(material.sMaterialName) ||
			IsNoDraw(material.sScriptMaterial))
			return true;
		/* These script suffixes are consumed by the physics/visibility systems;
		   they are not visible surface shaders. */
		if (NameContains(material.sScriptMaterial, "mat_phys") ||
			NameContains(material.sScriptMaterial, "mat_obstruct") ||
			NameContains(material.sScriptMaterial, "mat_occl"))
			return true;
		/* Far Cry's AI cover helpers use these exact material slots.  Their
		   source path is not retained by every static-object submission, so
		   filter the slots as well as the CGF path in EF_AddEf. */
		if (stricmp(material.sMaterialName, "s_hard") == 0 ||
			stricmp(material.sMaterialName, "s_soft") == 0)
			return true;
		/* A few old CGFs use a decal template for their proxy slot, so their only
		   surviving marker is the material name. */
		return NameContains(material.sMaterialName, "collision") ||
			NameContains(material.sMaterialName, "colllision") ||
			NameContains(material.sMaterialName, "hullproxy");
	}

	/*! Water surfaces carry no diffuse map at all -- the retail water shaders
	    (TerrainWater_OnlySky, TerrainWaterBeach, the ocean circle) build their
	    colour from a reflection and a sky sample, neither of which exists on
	    this fixed-function path.  With no texture the untextured fallback paints
	    them solid white, and they are large surfaces, so the result is the sheets
	    of blown-out white across the level rather than water.  Tint them instead:
	    still not real water, but the right colour and translucent, which reads as
	    water instead of as a hole in the world. */
	inline bool IsWaterSurface(const char *shaderName)
	{
		if (!shaderName || !shaderName[0])
			return false;
		return NameContains(shaderName, "water") || NameContains(shaderName, "ocean");
	}

	//! Shader templates whose geometry is alpha-cut-out foliage or similar.
	inline bool NeedsAlphaTest(const char *shaderName)
	{
		if (!shaderName || !shaderName[0])
			return false;
		static const char *kCutoutTemplates[] = {
			"plant", "leaf", "leaves", "grass", "tree", "veget", "bush", "frond",
			"palm", "fence", "wire", "net", "grid", "ladder", "alphatest",
			"hair", "detailbend"
		};
		for (size_t i = 0; i < sizeof(kCutoutTemplates) / sizeof(kCutoutTemplates[0]); ++i)
			if (NameContains(shaderName, kCutoutTemplates[i]))
				return true;
		return false;
	}

	/* Fixed-function replacements for the blend directives normally supplied by
	   CryEngine's shader scripts.  These effects are commonly authored at opacity
	   1, so material opacity alone cannot identify them. */
	inline bool IsModulativeShadow(const char *shaderName)
	{
		return NameContains(shaderName, "shadow") || NameContains(shaderName, "blob");
	}

	inline bool NeedsAdditiveBlend(const char *shaderName)
	{
		static const char *kAdditive[] = {
			"muzzle", "flash", "flare", "corona", "glow", "halo", "tracer",
			"beam", "laser", "fire", "explosion", "spark"
		};
		for (size_t i = 0; i < sizeof(kAdditive) / sizeof(kAdditive[0]); ++i)
			if (NameContains(shaderName, kAdditive[i]))
				return true;
		return false;
	}

	inline bool NeedsAlphaBlend(const char *shaderName)
	{
		static const char *kTranslucent[] = {
			"particle", "sprite", "smoke", "cloud", "dust", "steam", "fog",
			"decal", "soft", "impact"
		};
		for (size_t i = 0; i < sizeof(kTranslucent) / sizeof(kTranslucent[0]); ++i)
			if (NameContains(shaderName, kTranslucent[i]))
				return true;
		return false;
	}
}

#endif // VITA_RENDERER_H
