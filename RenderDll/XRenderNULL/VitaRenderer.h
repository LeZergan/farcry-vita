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

#if _MSC_VER > 1000
# pragma once
#endif

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

private:
	int m_nWidth;
	int m_nHeight;
	int m_nColorBpp;
	int m_nDepthBpp;
	int m_nStencilBpp;
	char m_cType;

	// Backing store for GetDynVBPtr/DrawDynVB -- CFFont::DrawStringW (see
	// CryFont/FFont.cpp) fills this via GetDynVBPtr and submits it via
	// DrawDynVB(offset, pool, count); real vitaGL draw, not a stub.
	static const int DYNVB_CAPACITY = 16384;
	struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F m_DynVB[DYNVB_CAPACITY];
	int m_nDynVBCursor;
};

#endif // VITA_RENDERER_H
