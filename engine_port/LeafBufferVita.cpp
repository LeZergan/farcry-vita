/* Vita: real (non-shim) CLeafBuffer support for CVitaRenderer.
   CLeafBuffer (CryCommon/LeafBuffer.h) is the real, unmodified engine type
   Cry3DEngine's terrain/static-object code uses to hold geometry -- its
   real method bodies live in RenderDll/Common/LeafBufferRender.cpp/
   LeafBufferCreate.cpp, ~1500 combined lines built entirely around the
   shader-sorted deferred render-element pipeline (EF_AddEf_NotVirtual,
   CRendElement::mfDraw dispatch across GL/D3D-specific subclasses) that
   belongs to RenderDll/Common's CRenderer base -- the same "large separate
   undertaking" CVitaRenderer was written to bypass (see VitaRenderer.h).

   This file is the Vita-side replacement, following the same pattern as
   RendElementStubs.cpp: real bodies, not stubs, but a deliberately smaller
   real pipeline. Instead of enqueuing into a sorted render list for a later
   flush, AddRenderElements below draws immediately, straight through
   CVitaRenderer::DrawBuffer -- no shader sorting, no multi-pass, no
   transparency ordering. Correct enough to get real submitted geometry on
   screen; a real drop-in for RenderDll/Common's pipeline is future work if
   sorting/materials ever turn out to matter. */
#include "RenderPCH.h"
#include "VitaRenderer.h"
#include <MeshIdx.h>
#include <CREOcLeaf.h>
#include <CryCompiledFile.h>
#include <VertexBufferSource.h>

#if defined(LINUX)

CLeafBuffer::CLeafBuffer(const char *szSource)
{
	m_Indices.Reset();
	m_Next = m_Prev = NULL;
	m_NextGlobal = m_PrevGlobal = NULL;
	m_sSource = (char*)szSource;

	m_TempNormals = NULL;
	m_TempTexCoords = NULL;
	m_TempColors = NULL;
	m_TempSecColors = NULL;
	m_pCustomData = NULL;
	PrepareBufferCallback = NULL;

	m_pVertexBuffer = NULL;
	m_pVertexContainer = NULL;
	m_bMaterialsWasCreatedInRenderer = 0;
	m_bOnlyVideoBuffer = 0;
	m_bDynamic = 0;
	m_UpdateVBufferMask = 0;
	m_UpdateFrame = 0;
	m_SortFrame = 0;
	m_SecVertCount = 0;
	m_nLMCornerCount = 0;
	m_pSecVertBuffer = NULL;

	m_NumIndices = 0;
	m_pIndicesPreStrip = NULL;
	m_arrVertStripMap = NULL;

	m_nVertexFormat = 0;
	m_nPrimetiveType = R_PRIMV_TRIANGLES;

	m_pMats = NULL;

	m_arrVtxMap = NULL;
	m_fMinU = m_fMinV = m_fMaxU = m_fMaxV = 0.0f;

	m_nClientTextureBindID = 0;

	m_vBoxMin.Set(0, 0, 0);
	m_vBoxMax.Set(0, 0, 0);
	m_pLoadedColors = NULL;
}

CLeafBuffer::~CLeafBuffer()
{
	ReleaseShaders();
	if (gcpVitaRenderer)
	{
		gcpVitaRenderer->ReleaseBuffer(m_pVertexBuffer);
		if (m_pSecVertBuffer != m_pVertexBuffer)
			gcpVitaRenderer->ReleaseBuffer(m_pSecVertBuffer);
		gcpVitaRenderer->ReleaseIndexBuffer(&m_Indices);
	}
	delete m_pIndicesPreStrip;
	m_pIndicesPreStrip = NULL;
	delete m_pMats;
}

/* Vita: real immediate draw -- the payoff. Each material range in m_pMats
   (set up by CreateLeafBufferInitialized's caller via SetChunk, see
   CryCommon/LeafBuffer.h) becomes one direct DrawBuffer call against the
   video-memory buffer, right now instead of queued for later. */
void CLeafBuffer::AddRenderElements(CCObject *pObj, int DLightMask, int nTemplate, int nFogVolumeID, int nSortId, IMatInfo *pIMatInfo)
{
	if (!gcpVitaRenderer || !m_pVertexBuffer)
		return;
	/* VolFogTopCircle is a shader-generated translucent volume cap.  Drawing
	   its bare geometry through the white fallback produces the opaque white
	   disc/sheet reported in gameplay.  Distance fog remains active; omit only
	   this unsupported auxiliary pass until it has a real fixed-function
	   equivalent. */
	if (m_sSource && !stricmp(m_sSource, "VolFogTopCircle"))
	{
		static bool s_bReportedFogCapSkip = false;
		if (!s_bReportedFogCapSkip && iLog)
		{
			s_bReportedFogCapSkip = true;
			iLog->LogToFile("\001[VITA][FOG] suppressed shader-only volume top cap");
		}
		return;
	}
	#if defined(VITA_PERF_TELEMETRY)
	{
		static std::map<std::string, bool> reportedSources;
		/* Build the key only while the report can still fire.  This runs on
		   every draw call of every frame, and constructing (and hashing) a
		   std::string per draw just to discover the cap was reached long ago is
		   pure overhead in the hottest path in the renderer. */
		if (reportedSources.size() < 96)
		{
		std::string sourceKey = m_sSource ? m_sSource : "<null>";
		if (reportedSources.find(sourceKey) == reportedSources.end())
		{
			reportedSources[sourceKey] = true;
			sceClibPrintf("[VITADRAW] source=%s prim=%d fmt=%d verts=%d inds=%d mats=%d clientTex=%d\n",
				sourceKey.c_str(), m_nPrimetiveType, m_pVertexBuffer->m_vertexformat,
				m_pVertexBuffer->m_NumVerts, m_NumIndices,
				m_pMats ? m_pMats->Count() : -1, m_nClientTextureBindID);
			fflush(stdout);
		}
		}
	}
	#endif

	/* The terrain source format has no UV field because the desktop shaders
	   derive coordinates from position.  Supplying a separate generated client
	   array made vitaGL unbind the resident vertex VBO for every terrain draw.
	   Upgrade each sector once to the otherwise identical format with TEX2F,
	   then only touch those two floats when the engine switches between the
	   near sector texture and the far cover-map mapping. */
	if (m_sSource && !stricmp(m_sSource, "TerrainSector") && m_pMats &&
		m_pMats->Count() > 0 && (*m_pMats)[0].pRE &&
		(*m_pMats)[0].pRE->m_CustomData && m_pVertexBuffer)
	{
		const float *pTexGen = (const float *)(*m_pMats)[0].pRE->m_CustomData;
		if (m_pVertexBuffer->m_vertexformat == VERTEX_FORMAT_P3F_N_COL4UB_COL4UB)
		{
			const int nVerts = m_pVertexBuffer->m_NumVerts;
			const struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB *pSrc =
				(const struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB *)m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData;
			std::vector<struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F> baked;
			baked.resize((size_t)nVerts);
			for (int n = 0; pSrc && n < nVerts; ++n)
			{
				baked[n].xyz = pSrc[n].xyz;
				baked[n].normal = pSrc[n].normal;
				baked[n].color = pSrc[n].color;
				baked[n].seccolor = pSrc[n].seccolor;
				baked[n].st[0] = pSrc[n].xyz.y * pTexGen[2] + pTexGen[0];
				baked[n].st[1] = pSrc[n].xyz.x * pTexGen[2] + pTexGen[1];
			}

			CVertexBuffer *pBakedBuffer = (pSrc && nVerts > 0) ?
				gcpVitaRenderer->CreateBuffer(nVerts,
					VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, m_sSource, false) : NULL;
			if (pBakedBuffer)
			{
				gcpVitaRenderer->UpdateBuffer(pBakedBuffer, &baked[0], nVerts, true, 0, VSF_GENERAL);
				CVertexBuffer *pOldVideo = m_pVertexBuffer;
				CVertexBuffer *pOldSystem = m_pSecVertBuffer;
				m_pVertexBuffer = pBakedBuffer;
				m_pSecVertBuffer = pBakedBuffer;
				m_nVertexFormat = VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F;
				gcpVitaRenderer->ReleaseBuffer(pOldVideo);
				if (pOldSystem && pOldSystem != pOldVideo)
					gcpVitaRenderer->ReleaseBuffer(pOldSystem);
				m_fMinU = pTexGen[0];
				m_fMinV = pTexGen[1];
				m_fMaxU = pTexGen[2];
				static bool s_bReportedBakedTerrainUV = false;
				if (!s_bReportedBakedTerrainUV && iLog)
				{
					s_bReportedBakedTerrainUV = true;
					iLog->LogToFile("\001[VITA][PERF] terrain UVs baked into resident vertex buffers");
				}
			}
		}
		else if (m_pVertexBuffer->m_vertexformat == VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F &&
			(m_fMinU != pTexGen[0] || m_fMinV != pTexGen[1] || m_fMaxU != pTexGen[2]))
		{
			struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F *pVerts =
				(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F *)m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData;
			for (int n = 0; pVerts && n < m_pVertexBuffer->m_NumVerts; ++n)
			{
				pVerts[n].st[0] = pVerts[n].xyz.y * pTexGen[2] + pTexGen[0];
				pVerts[n].st[1] = pVerts[n].xyz.x * pTexGen[2] + pTexGen[1];
			}
			m_pVertexBuffer->m_bGLDirty = true;
			m_fMinU = pTexGen[0];
			m_fMinV = pTexGen[1];
			m_fMaxU = pTexGen[2];
		}
	}

	const bool bLightMapBound = gcpVitaRenderer->BeginVitaLightMap(pObj, m_pVertexBuffer);

	/* Match Crytek's EF_SetObjectTransform contract: m_Matrix is only valid
	   when one of FOB_TRANS_* is set. CCObject::Init intentionally does not
	   clear it, so multiplying it unconditionally reuses a prior object's
	   transform and explodes untransformed terrain/water into giant polys. */
	const bool hasObjectTransform = pObj && (pObj->m_ObjFlags & FOB_TRANS_MASK);
	if (hasObjectTransform)
	{
		gcpVitaRenderer->PushMatrix();
		gcpVitaRenderer->MultMatrix(pObj->m_Matrix.GetData());
	}

	if (m_pMats && m_pMats->Count() > 0)
	{
		/* Terrain sectors split into dozens of strip chunks that all share one
		   vertex buffer and one texture-coordinate mapping, but only chunk 0
		   owns a render element and therefore the texgen offsets.  Publish them
		   for the whole buffer so DrawBuffer can generate coordinates for every
		   chunk rather than just the first. */
		extern const float *g_pVitaTerrainTexGen;
		g_pVitaTerrainTexGen = (*m_pMats)[0].pRE ? (*m_pMats)[0].pRE->m_CustomData : NULL;
		extern int g_nVitaTerrainDetailTexture;
		extern float g_arrVitaTerrainDetailTransform[4];
		g_nVitaTerrainDetailTexture = 0;
		g_arrVitaTerrainDetailTransform[0] = 12.0f;
		g_arrVitaTerrainDetailTransform[1] = 12.0f;
		g_arrVitaTerrainDetailTransform[2] = 0.0f;
		g_arrVitaTerrainDetailTransform[3] = 0.0f;
		if (m_sSource && !stricmp(m_sSource, "TerrainSector") && (*m_pMats)[0].pRE)
		{
			/* Prefer a Z-projected authored layer: its UVs derive from world X/Y
			   and can therefore be reconstructed exactly from the sector base UVs.
			   Side-projected cliff layers need world Z, which is not present in the
			   fixed-function UV stream. */
			const float *pTerrainData = (const float *)(*m_pMats)[0].pRE->m_CustomData;
			int nChosenLayer = 0;
			if (pTerrainData)
			{
				for (int nLayer = 1; nLayer < MAX_CUSTOM_TEX_BINDS_NUM; ++nLayer)
				{
					const int nDetail = nLayer - 1;
					const float *pProjection = pTerrainData + 4 + nDetail * 8;
					if ((*m_pMats)[0].pRE->m_CustomTexBind[nLayer] > 0 &&
						pProjection[1] != 0.0f && pProjection[4] != 0.0f)
					{
						nChosenLayer = nLayer;
						break;
					}
				}
			}
			if (!nChosenLayer)
				for (int nLayer = 1; nLayer < MAX_CUSTOM_TEX_BINDS_NUM; ++nLayer)
					if ((*m_pMats)[0].pRE->m_CustomTexBind[nLayer] > 0)
					{
						nChosenLayer = nLayer;
						break;
					}
			if (nChosenLayer)
			{
				g_nVitaTerrainDetailTexture = (*m_pMats)[0].pRE->m_CustomTexBind[nChosenLayer];
				if (pTerrainData && pTerrainData[2] != 0.0f)
				{
					const float *pProjection = pTerrainData + 4 + (nChosenLayer - 1) * 8;
					if (pProjection[1] != 0.0f && pProjection[4] != 0.0f)
					{
						/* baseU = worldY*baseScale+biasU and baseV =
						   worldX*baseScale+biasV.  Convert those back into
						   the exact authored Z-projection without another UV stream. */
						g_arrVitaTerrainDetailTransform[0] = pProjection[1] / pTerrainData[2];
						g_arrVitaTerrainDetailTransform[1] = pProjection[4] / pTerrainData[2];
						g_arrVitaTerrainDetailTransform[2] = -pTerrainData[0] * g_arrVitaTerrainDetailTransform[0];
						g_arrVitaTerrainDetailTransform[3] = -pTerrainData[1] * g_arrVitaTerrainDetailTransform[1];
					}
				}
			}
		}
		struct SVitaTexGenScope
		{
			~SVitaTexGenScope()
			{
				extern const float *g_pVitaTerrainTexGen;
				extern int g_nVitaTerrainDetailTexture;
				extern float g_arrVitaTerrainDetailTransform[4];
				g_pVitaTerrainTexGen = NULL;
				g_nVitaTerrainDetailTexture = 0;
				g_arrVitaTerrainDetailTransform[0] = g_arrVitaTerrainDetailTransform[1] = 12.0f;
				g_arrVitaTerrainDetailTransform[2] = g_arrVitaTerrainDetailTransform[3] = 0.0f;
			}
		} texGenScope;

		/* Accumulated run of adjacent chunks sharing state/cull/texture; see the
		   batching comment at the end of the loop. */
		bool bRunActive = false;
		int nRunState = 0, nRunCull = R_CULL_BACK, nRunTexture = 0;
		int nRunFirstIndex = 0, nRunIndexCount = 0;
		int nRunFirstVert = 0, nRunLastVertEnd = 0;
		CMatInfo *pRunChunk = NULL;
		CLeafBuffer *pThisBuffer = this;
		auto VitaFlushChunkRun = [&]()
		{
			if (!bRunActive || nRunIndexCount <= 0)
				return;
			gcpVitaRenderer->SetCullMode(nRunCull);
			gcpVitaRenderer->SetState(nRunState);
			if (nRunTexture > 0)
				gcpVitaRenderer->SetTexture(nRunTexture, eTT_Base);
			else
				gcpVitaRenderer->SetWhiteTexture();
			gcpVitaRenderer->DrawBuffer(pThisBuffer->m_pVertexBuffer, &pThisBuffer->m_Indices,
				nRunIndexCount, nRunFirstIndex, pThisBuffer->m_nPrimetiveType,
				nRunFirstVert, nRunLastVertEnd, pRunChunk);
			bRunActive = false;
			nRunIndexCount = 0;
		};

		/* Hoisted out of the chunk loop: these depend only on the buffer's source
		   name, not on the chunk, so running two case-insensitive string
		   comparisons per chunk per draw per frame re-derived the same answer
		   dozens of times for every buffer.  Terrain sectors alone carry ~34
		   chunks each. */
		const bool isOutdoorWater = m_sSource && strnicmp(m_sSource, "OutdoorWater", 12) == 0;
		const bool isWaterVolume = m_sSource && strnicmp(m_sSource, "WaterVolume", 11) == 0;
		const bool isWaterSurface = isOutdoorWater || isWaterVolume;
		/* Indoor water arrives opaque white because its colour was authored in a
		   shader pass.  This is static geometry, so write the low-spec fallback
		   colour once per buffer instead of walking every vertex once per material
		   chunk on every frame. */
		if (isWaterVolume && m_fMaxV != -9876.0f)
		{
			const SBufInfoTable &format = gBufInfoTable[m_pVertexBuffer->m_vertexformat];
			if (format.OffsColor > 0)
			{
				byte *vertices = (byte *)m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData;
				const int stride = m_VertexSize[m_pVertexBuffer->m_vertexformat];
				for (int vertex = 0; vertices && vertex < m_SecVertCount; ++vertex)
				{
					byte *color = vertices + vertex * stride + format.OffsColor;
					color[0] = 112;
					color[1] = 158;
					color[2] = 184;
					color[3] = 96;
				}
				m_pVertexBuffer->m_bGLDirty = true;
			}
			m_fMaxV = -9876.0f;
		}

		for (int i = 0; i < m_pMats->Count(); i++)
		{
			CMatInfo &mi = (*m_pMats)[i];
			if (mi.nNumIndices <= 0)
				continue;
			/* The retail low-spec indoor-water shader uses the animated caustic
			   map and a fixed translucent water colour. The compact renderer has
			   no CSL multipass interpreter, so preserve that authored fallback in
			   the vertex stream instead of drawing the shaderless volume as opaque
			   white (its source vertices intentionally arrive as 0xffffffff). */
			#if defined(VITA_PERF_TELEMETRY)
			if (isWaterSurface)
			{
				static std::map<std::string, bool> reportedWaterBuffers;
				std::string waterKey = m_sSource ? m_sSource : "<null>";
				if (reportedWaterBuffers.find(waterKey) == reportedWaterBuffers.end())
				{
					reportedWaterBuffers[waterKey] = true;
					int minAlpha = 255, maxAlpha = 0;
					const SBufInfoTable &waterFormat = gBufInfoTable[m_pVertexBuffer->m_vertexformat];
					if (waterFormat.OffsColor > 0)
					{
						const byte *v = (const byte *)m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData;
						const int stride = m_VertexSize[m_pVertexBuffer->m_vertexformat];
						for (int n = 0; v && n < m_SecVertCount; ++n)
						{
							const int alpha = v[n * stride + waterFormat.OffsColor + 3];
							minAlpha = min(minAlpha, alpha);
							maxAlpha = max(maxAlpha, alpha);
						}
					}
					if (iLog)
						iLog->LogToFile("\001[VITA][WATER] source=%s fmt=%d verts=%d inds=%d chunks=%d clientTex=%d alpha=%d..%d",
							waterKey.c_str(), m_pVertexBuffer->m_vertexformat, m_SecVertCount,
							m_NumIndices, m_pMats->Count(), m_nClientTextureBindID, minAlpha, maxAlpha);
					sceClibPrintf("[VITAWATER] source=%s fmt=%d verts=%d inds=%d chunks=%d clientTex=%d alpha=%d..%d\n",
						waterKey.c_str(), m_pVertexBuffer->m_vertexformat, m_SecVertCount,
						m_NumIndices, m_pMats->Count(), m_nClientTextureBindID, minAlpha, maxAlpha);
					fflush(stdout);
				}
			}
			#endif

			/* Load and bind the material's real diffuse asset on first draw.
			   Crytek's desktop shader-template compiler normally performs this
			   step; the compact Vita shader object intentionally omits that
			   desktop multi-pass machinery. */
			SRenderShaderResources *resources = mi.shaderItem.m_pShaderResources;
				const char *vitaShaderName = (mi.shaderItem.m_pShader && mi.shaderItem.m_pShader->GetName())
					? mi.shaderItem.m_pShader->GetName() : "";
				/* Crytek's collision-only proxy material. It carries no texture,
				   so on this port it reached the white fallback and painted solid
				   white boxes over the world instead of not drawing at all. */
				if (VitaMaterial::IsNoDrawMaterial(mi))
					continue;
			int renderState = isWaterSurface
				? (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA)
				: GS_DEPTHWRITE;
			const bool bDynamicMesh = m_pVertexBuffer && m_pVertexBuffer->m_bDynamic != 0;
			int nChunkCull = (isWaterSurface || bDynamicMesh) ? R_CULL_NONE : R_CULL_BACK;
				/* Cut-out foliage keeps its transparency in the texture with
				   m_AlphaRef left at 0, so only the shader template says it needs
				   an alpha test. Without one, every leaf and frond draws as a
				   solid rectangle. See VitaMaterial::NeedsAlphaTest. */
				if (VitaMaterial::NeedsAlphaTest(vitaShaderName))
					renderState |= GS_ALPHATEST_GEQUAL128;
			const bool bNameShadow = VitaMaterial::IsModulativeShadow(vitaShaderName);
			const bool bNameAdditive = VitaMaterial::NeedsAdditiveBlend(vitaShaderName);
			const bool bNameAlpha = VitaMaterial::NeedsAlphaBlend(vitaShaderName);
			if (resources)
			{
				if (resources->m_AlphaRef >= 0.5f)
					renderState |= GS_ALPHATEST_GEQUAL128;
				else if (resources->m_AlphaRef > 0.0f)
					renderState |= GS_ALPHATEST_GEQUAL64;
				const bool bAdditive =
					(resources->m_ResFlags & (MTLFLAG_ADDITIVE | MTLFLAG_ADDITIVEDECAL)) != 0 ||
					bNameAdditive;
				if (bNameShadow)
				{
					renderState &= ~(GS_DEPTHWRITE | GS_ALPHATEST_MASK);
					renderState |= GS_BLSRC_ZERO | GS_BLDST_SRCCOL;
				}
				else if (bAdditive)
				{
					renderState &= ~(GS_DEPTHWRITE | GS_ALPHATEST_MASK);
					renderState |= GS_BLSRC_ONE | GS_BLDST_ONE;
				}
				else if (resources->m_Opacity < 0.999f || bNameAlpha)
				{
					renderState &= ~GS_DEPTHWRITE;
					renderState |= GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
				}
				if (bDynamicMesh || isWaterSurface || (resources->m_ResFlags & MTLFLAG_2SIDED))
					nChunkCull = R_CULL_NONE;
			}
			else if (bNameShadow)
			{
				renderState &= ~(GS_DEPTHWRITE | GS_ALPHATEST_MASK);
				renderState |= GS_BLSRC_ZERO | GS_BLDST_SRCCOL;
			}
			else if (bNameAdditive)
			{
				renderState &= ~(GS_DEPTHWRITE | GS_ALPHATEST_MASK);
				renderState |= GS_BLSRC_ONE | GS_BLDST_ONE;
			}
			else if (bNameAlpha)
			{
				renderState &= ~GS_DEPTHWRITE;
				renderState |= GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
			}
			SEfResTexture *diffuse = resources ? resources->m_Textures[EFTT_DIFFUSE] : NULL;
			if (diffuse && !diffuse->m_TU.m_ITexPic && !diffuse->m_Name.empty())
			{
				std::string textureName = diffuse->m_Name.c_str();
				ITexPic *loaded = gcpVitaRenderer->EF_LoadTexture(textureName.c_str(),
					diffuse->m_TU.GetTexFlags(), diffuse->m_TU.GetTexFlags2(),
					eTT_Base, 1.0f, 1.0f, -1, -1);
				/* Only prefix the material's folder onto a bare file name -- see the
				   matching comment in CVitaRenderer::EF_AddEf.  Prefixing a name
				   that is already rooted at the data root built paths like
				   "Objects/Indoor/boxes/crates/Objects/Indoor/crates/crate1.dds",
				   which missed, and a missed diffuse draws solid white. */
				if (!loaded && resources && !resources->m_TexturePath.empty())
				{
					std::string lowerName = textureName;
					for (size_t i = 0; i < lowerName.size(); ++i)
					{
						lowerName[i] = (char)tolower((unsigned char)lowerName[i]);
						if (lowerName[i] == '\\') lowerName[i] = '/';
					}
					const bool bAlreadyRooted =
						lowerName.compare(0, 8, "objects/") == 0 ||
						lowerName.compare(0, 9, "textures/") == 0 ||
						lowerName.compare(0, 7, "levels/") == 0 ||
						lowerName.compare(0, 10, "materials/") == 0;
					if (!bAlreadyRooted)
					{
						std::string fullName = resources->m_TexturePath.c_str();
						if (!fullName.empty() && fullName[fullName.size()-1] != '/' && fullName[fullName.size()-1] != '\\')
							fullName += '/';
						fullName += textureName;
						loaded = gcpVitaRenderer->EF_LoadTexture(fullName.c_str(),
							diffuse->m_TU.GetTexFlags(), diffuse->m_TU.GetTexFlags2(),
							eTT_Base, 1.0f, 1.0f, -1, -1);
					}
					/* Some retail materials contain a stale rooted folder but retain
					   the correct material texture path.  Carrier's crate2 is the
					   concrete device-log case: the authored name omits "boxes/",
					   while the asset exists beside the material.  After the authored
					   path has failed, resolve its basename against that authoritative
					   folder just as EF_AddEf already does. */
					if (!loaded)
					{
						const size_t nSlash = textureName.find_last_of("/\\");
						if (nSlash != std::string::npos)
						{
							std::string fullName = resources->m_TexturePath.c_str();
							if (!fullName.empty() && fullName[fullName.size()-1] != '/' && fullName[fullName.size()-1] != '\\')
								fullName += '/';
							fullName += textureName.substr(nSlash + 1);
							loaded = gcpVitaRenderer->EF_LoadTexture(fullName.c_str(),
								diffuse->m_TU.GetTexFlags(), diffuse->m_TU.GetTexFlags2(),
								eTT_Base, 1.0f, 1.0f, -1, -1);
						}
					}
				}
				diffuse->m_TU.m_ITexPic = loaded;
			}
			/* Resolve the texture instead of binding it here, so adjacent
			   chunks that end up on the same texture and state can be merged
			   into one draw below. 0 means the untextured/white fallback. */
			int nChunkTexture = 0;
			if (diffuse && diffuse->m_TU.m_ITexPic)
				nChunkTexture = diffuse->m_TU.m_ITexPic->GetTextureID();
			/* TerrainWater normally supplies its animated maps through the desktop
			   shader-template passes.  On Vita both ocean and indoor water now carry
			   valid UVs, so use the retail low-spec caustic asset instead of the white
			   fallback that also disables the vertex colour/alpha array. */
			else if (isWaterSurface)
			{
				static int s_waterTexture = 0;
				if (!s_waterTexture)
				{
					ITexPic *waterTexture = gcpVitaRenderer->EF_LoadTexture(
						"Textures/Animated/Water/CAUST_XB_00", FT_NOREMOVE, 0,
						eTT_Base, 1.0f, 1.0f, -1, -1);
					if (waterTexture)
						s_waterTexture = waterTexture->GetTextureID();
				}
				nChunkTexture = s_waterTexture;
			}
			else if (m_nClientTextureBindID > 0 && m_nClientTextureBindID != 0x1000)
				nChunkTexture = m_nClientTextureBindID;
			else
			{
				#if defined(VITA_PERF_TELEMETRY)
				static std::map<std::string, bool> reportedWhiteMaterials;
				std::string key = m_sSource ? m_sSource : "<unknown-buffer>";
				key += "|";
				if (mi.shaderItem.m_pShader && mi.shaderItem.m_pShader->GetName())
					key += mi.shaderItem.m_pShader->GetName();
				key += "|";
				if (diffuse)
					key += diffuse->m_Name.c_str();
				if (reportedWhiteMaterials.size() < 256 && reportedWhiteMaterials.find(key) == reportedWhiteMaterials.end())
				{
					reportedWhiteMaterials[key] = true;
					if (iLog)
						iLog->LogToFile("\001[VITA][MATWHITE] source=%s shader=%s diffuse=%s path=%s",
							m_sSource ? m_sSource : "<null>",
							(mi.shaderItem.m_pShader && mi.shaderItem.m_pShader->GetName()) ? mi.shaderItem.m_pShader->GetName() : "<null>",
							diffuse ? diffuse->m_Name.c_str() : "<null>",
							resources ? resources->m_TexturePath.c_str() : "<null>");
				}
				#endif
			}
			#if defined(VITA_PERF_TELEMETRY)
			if (isWaterSurface && i == 0)
			{
				static std::map<std::string, bool> reportedWaterMaterials;
				const std::string waterMaterialKey = m_sSource ? m_sSource : "<null>";
				if (reportedWaterMaterials.find(waterMaterialKey) == reportedWaterMaterials.end())
				{
					reportedWaterMaterials[waterMaterialKey] = true;
					sceClibPrintf("[VITAWATERMAT] source=%s state=0x%x resources=%p opacity=%.3f flags=0x%x diffuse=%s path=%s tex=%d\n",
						waterMaterialKey.c_str(),
						renderState, resources,
						resources ? resources->m_Opacity : 1.0f,
						resources ? resources->m_ResFlags : 0,
						diffuse ? diffuse->m_Name.c_str() : "<null>",
						resources ? resources->m_TexturePath.c_str() : "<null>",
						(diffuse && diffuse->m_TU.m_ITexPic) ? diffuse->m_TU.m_ITexPic->GetTextureID() : 0);
					fflush(stdout);
				}
			}
			#endif
			/* Batch adjacent chunks.  A terrain sector carries ~34 material
			   chunks and the ocean ~32, each of which used to be its own state
			   change plus draw call.  Where consecutive chunks agree on state,
			   cull mode and texture, and their index ranges are contiguous,
			   they describe one run of triangles and can be drawn in a single
			   call.  With terrain detail texturing off, most neighbours now do
			   agree, so this collapses the heaviest objects in the scene. */
			/* Only independent triangles may be concatenated: joining two
			   triangle strips or fans end to end invents triangles that span
			   the seam, so those keep one draw per chunk. */
			const bool bCanExtendRun = bRunActive &&
				m_nPrimetiveType == R_PRIMV_TRIANGLES &&
				nRunState == renderState && nRunCull == nChunkCull &&
				nRunTexture == nChunkTexture &&
				mi.nFirstIndexId == nRunFirstIndex + nRunIndexCount;
			if (bCanExtendRun)
			{
				nRunIndexCount += mi.nNumIndices;
				nRunLastVertEnd = max(nRunLastVertEnd, mi.nFirstVertId + mi.nNumVerts);
				nRunFirstVert   = min(nRunFirstVert, mi.nFirstVertId);
				continue;
			}
			VitaFlushChunkRun();
			bRunActive      = true;
			nRunState       = renderState;
			nRunCull        = nChunkCull;
			nRunTexture     = nChunkTexture;
			nRunFirstIndex  = mi.nFirstIndexId;
			nRunIndexCount  = mi.nNumIndices;
			nRunFirstVert   = mi.nFirstVertId;
			nRunLastVertEnd = mi.nFirstVertId + mi.nNumVerts;
			pRunChunk       = &mi;
		}
		VitaFlushChunkRun();
	}
	else if (m_NumIndices > 0)
	{
		gcpVitaRenderer->DrawBuffer(m_pVertexBuffer, &m_Indices, m_NumIndices, 0,
			m_nPrimetiveType, 0, m_pVertexBuffer->m_NumVerts, NULL);
	}
	if (hasObjectTransform)
		gcpVitaRenderer->PopMatrix();
	if (bLightMapBound)
		gcpVitaRenderer->EndVitaLightMap();
}

/* Vita: every other CLeafBuffer virtual -- honest no-ops/safe defaults, the
   same "not yet implemented, not silently pretending to work" pattern as
   the rest of this port (see VitaRenderer.cpp's own header comment). None
   of these are on the terrain-render path above; they cover mesh
   editing/streaming/tangent-space/debug-light features no caller in this
   build's compiled source set currently reaches (confirmed by linking
   without them until this file added AddRenderElements/the ctor/dtor). */
void CLeafBuffer::CalcFaceNormals(void) {}
void CLeafBuffer::AddRE(CCObject *pObj, IShader *pEf, int nNumSort, IShader *pStateEff)
{
	/* The desktop renderer queues this for a sorted flush.  Vita's compact
	   backend draws leaf buffers immediately, so keep the same geometry path
	   used by AddRenderElements instead of silently dropping beaches and
	   other callers that still use the older AddRE entry point. */
	AddRenderElements(pObj, 0, -1, 0, nNumSort, NULL);
}
bool CLeafBuffer::CheckUpdate(int VertFormat, int Flags, bool bNeedAddNormals) { return true; }
bool CLeafBuffer::CreateTangBuffer()
{
	if (!m_pSecVertBuffer || m_SecVertCount <= 0)
		return false;

	SPipTangents *tangents = new SPipTangents[m_SecVertCount];
	if (!tangents)
		return false;

	/* The Vita fixed-function path does not consume tangent space, but the
	   original character setup always copies a basis out of this stream.
	   Build a small, valid orthonormal basis from the normal instead of
	   retaining the desktop renderer's unconditional-false stub. */
	const struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F *vertices =
		m_pSecVertBuffer->m_vertexformat == VERTEX_FORMAT_P3F_N_COL4UB_TEX2F
		? (const struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F *)m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData
		: NULL;
	for (int i = 0; i < m_SecVertCount; ++i)
	{
		Vec3 n = vertices ? vertices[i].normal : Vec3(0.0f, 0.0f, 1.0f);
		float nLen2 = n.x*n.x + n.y*n.y + n.z*n.z;
		if (nLen2 > 1.0e-12f)
			n *= 1.0f / sqrtf(nLen2);
		else
			n = Vec3(0.0f, 0.0f, 1.0f);

		Vec3 tangent = fabsf(n.z) < 0.999f
			? Vec3(-n.y, n.x, 0.0f)
			: Vec3(1.0f, 0.0f, 0.0f);
		float tLen2 = tangent.x*tangent.x + tangent.y*tangent.y + tangent.z*tangent.z;
		if (tLen2 > 1.0e-12f)
			tangent *= 1.0f / sqrtf(tLen2);
		Vec3 binormal(
			n.y*tangent.z - n.z*tangent.y,
			n.z*tangent.x - n.x*tangent.z,
			n.x*tangent.y - n.y*tangent.x);

		tangents[i].m_Tangent = tangent;
		tangents[i].m_Binormal = binormal;
		tangents[i].m_TNormal = n;
	}

	delete [] (SPipTangents *)m_pSecVertBuffer->m_VS[VSF_TANGENTS].m_VData;
	m_pSecVertBuffer->m_VS[VSF_TANGENTS].m_VData = tangents;
	return true;
}
void CLeafBuffer::ReleaseShaders()
{
	if (!m_pMats)
		return;
	for (int i = 0; i < m_pMats->Count(); ++i)
	{
		CMatInfo &mi = (*m_pMats)[i];
		if (mi.pRE)
		{
			delete mi.pRE;
			mi.pRE = NULL;
		}
		if (mi.shaderItem.m_pShader)
		{
			mi.shaderItem.m_pShader->Release();
			mi.shaderItem.m_pShader = NULL;
		}
		if (mi.shaderItem.m_pShaderResources)
		{
			mi.shaderItem.m_pShaderResources->Release();
			mi.shaderItem.m_pShaderResources = NULL;
		}
	}
}
/* THE reason characters were invisible.  CryAnimation builds with
   UNIQUE_VERT_BUFF_PER_INSTANCE (CryAnimation/stdafx.h), so every character
   instance creates an empty leaf buffer and calls this to clone the model's
   one into it.  While this was a stub, every instance kept that empty buffer:
   no materials, no geometry, no indices -- CryModelSubmesh then found a null
   m_pMats and drew nothing at all, silently.  The model itself loaded fine,
   which is why nothing ever showed up in the logs.

   Same contract as the desktop backend's CopyTo: duplicate the material table
   with its own render elements pointing at the destination buffer, and share
   the geometry, since the instance re-uploads skinned vertices every frame
   through UpdateSysVertices anyway. */
void CLeafBuffer::CopyTo(CLeafBuffer *pDst, bool bUseSysBuf)
{
	if (!pDst || pDst == this)
		return;

	pDst->m_nVertexFormat   = m_nVertexFormat;
	pDst->m_nPrimetiveType  = m_nPrimetiveType;
	pDst->m_vBoxMin         = m_vBoxMin;
	pDst->m_vBoxMax         = m_vBoxMax;
	pDst->m_SecVertCount    = m_SecVertCount;
	pDst->m_NumIndices      = m_NumIndices;
	pDst->m_nClientTextureBindID = m_nClientTextureBindID;
#if defined(LINUX)
	pDst->m_nLMCornerCount  = m_nLMCornerCount;
	pDst->m_arrLMCornerOfVertex = m_arrLMCornerOfVertex;
#endif

	/* Geometry has to be per instance, not shared.  Each instance skins into
	   its own vertex buffer every frame, so a shared one would have every
	   character wearing the last one's pose -- and the destructor frees these,
	   so sharing would double-free them as well.  This is the whole point of
	   UNIQUE_VERT_BUFF_PER_INSTANCE. */
	pDst->m_pVertexBuffer  = NULL;
	pDst->m_pSecVertBuffer = NULL;
	pDst->m_NumIndices     = 0;
	if (m_pVertexBuffer && m_pVertexBuffer->m_NumVerts > 0)
	{
		pDst->m_pVertexBuffer = gcpVitaRenderer->CreateBuffer(
			m_pVertexBuffer->m_NumVerts, m_nVertexFormat, m_sSource, true);
		if (pDst->m_pVertexBuffer && m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData)
			gcpVitaRenderer->UpdateBuffer(pDst->m_pVertexBuffer,
				m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData,
				m_pVertexBuffer->m_NumVerts, true, 0, VSF_GENERAL);
		pDst->m_pSecVertBuffer = pDst->m_pVertexBuffer;
	}
	if (m_Indices.m_VData && m_Indices.m_nItems > 0)
	{
		gcpVitaRenderer->CreateIndexBuffer(&pDst->m_Indices, m_Indices.m_VData, m_Indices.m_nItems);
		pDst->m_NumIndices = m_NumIndices;
	}

	if (!m_pMats)
		return;

	// The material table must be per instance: each one owns render elements
	// that point back at their own buffer.
	if (!pDst->m_pMats)
		pDst->m_pMats = new list2<CMatInfo>;
	pDst->m_pMats->Clear();
	for (int nMat = 0; nMat < m_pMats->Count(); ++nMat)
	{
		CMatInfo &rSrc = (*m_pMats)[nMat];
		CMatInfo newMat = rSrc;
		if (newMat.shaderItem.m_pShader)
			newMat.shaderItem.m_pShader->AddRef();
		if (newMat.shaderItem.m_pShaderResources)
			newMat.shaderItem.m_pShaderResources->AddRef();
		newMat.m_pPrimitiveGroups = NULL;
		newMat.pRE = NULL;
		pDst->m_pMats->Add(newMat);
	}
	for (int nMat = 0; nMat < pDst->m_pMats->Count(); ++nMat)
	{
		CMatInfo &rDst = (*pDst->m_pMats)[nMat];
		CMatInfo &rSrcMat = (*m_pMats)[nMat];
		/* list2 hands out raw memory and copies elements by assignment, and
		   assignment does not carry the vtable pointer in C++.  CMatInfo is
		   polymorphic (GetShaderItem is virtual), so without this the entry
		   keeps whatever happened to be in that memory and the first virtual
		   call jumps into the heap -- which is exactly what crashed in
		   DeleteLeafBuffers.  Copying the vptr explicitly is the same hack the
		   engine already uses wherever it fills a list2<CMatInfo>. */
		*(unsigned int*)&rDst = *(unsigned int*)&rSrcMat;
		if (!rSrcMat.pRE)
			continue;
		rDst.pRE = (CREOcLeaf *)gcpVitaRenderer->EF_CreateRE(eDATA_OcLeaf);
		if (rDst.pRE)
		{
			rDst.pRE->m_pBuffer = pDst;
			rDst.pRE->m_pChunk  = &rDst;
			rDst.pRE->m_Flags  |= (*m_pMats)[nMat].pRE->m_Flags;
		}
	}
}
void CLeafBuffer::CreateBuffer(CIndexedMesh *pTriData, bool bStripifyAndShareVerts, bool bRemoveNoDrawFaces, bool bKeepRemapTable)
{
	if (!gcpVitaRenderer || !pTriData || pTriData->m_nFaceCount <= 0)
		return;

	/* Characters die a long way downstream of an empty material list.  If this
	   returns without building anything, m_SecVertCount stays 0; the model's
	   external-to-internal vertex map is then sized 0, so the per-instance
	   buffer bails too, m_pMats is never assigned, and CryModelSubmesh has
	   nothing to draw -- the character is simply absent, with no error.

	   Build the geometry anyway against one catch-all material.  An untextured
	   character is a bug worth seeing; an invisible one hides the cause. */
	bool bSingleFallbackMaterial = false;
	if (!m_pMats || m_pMats->Count() <= 0)
	{
		if (!m_pMats)
			m_pMats = new list2<CMatInfo>;
		if (m_pMats->Count() <= 0)
		{
			CMatInfo fallbackMaterial;
			m_pMats->Add(fallbackMaterial);
		}
		bSingleFallbackMaterial = true;
		if (iLog)
		{
			static int s_nReportedFallbacks = 0;
			if (s_nReportedFallbacks < 16)
			{
				++s_nReportedFallbacks;
				iLog->LogToFile("\001[VITA][MESH] no materials for '%s' -- building with a catch-all material",
					m_sSource ? m_sSource : "<unnamed>");
			}
		}
	}

	struct VertexKey
	{
		int v, n, t, material;
		bool operator<(const VertexKey& other) const
		{
			if (material != other.material) return material < other.material;
			if (v != other.v) return v < other.v;
			if (n != other.n) return n < other.n;
			return t < other.t;
		}
	};

	typedef struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F VitaMeshVertex;

	/* Vertex dedup used a std::map, which cost a red-black tree walk and a node
	   allocation for every triangle corner in the mesh.  Across a whole level
	   that is hundreds of thousands of allocations on a 500 MHz ARM, and it was
	   a large part of the level load time.  An open-addressed table sized up
	   front does the same job with no allocation per corner and preserves the
	   insertion order the vertex numbering (and the lightmap corner map) rely
	   on, because a miss still appends to `vertices` in exactly the same
	   sequence. */
	struct VertexHashSlot { int v, n, t, material; int index; };
	int nHashSize = 64;
	while (nHashSize < (pTriData->m_nFaceCount * 3 * 2) && nHashSize < (1 << 22))
		nHashSize <<= 1;
	const unsigned nHashMask = (unsigned)(nHashSize - 1);
	std::vector<VertexHashSlot> vertexHash((size_t)nHashSize);
	for (int nSlot = 0; nSlot < nHashSize; ++nSlot)
		vertexHash[nSlot].index = -1;

	std::vector<VitaMeshVertex> vertices;
	std::vector<ushort> indices;
	// See CLeafBuffer::m_nLMCornerCount -- lightmap UV resampling data.
	std::vector<int> arrLMCornerOfVertex;
	int nCornerCounter = 0;
	/* evs_NoSharing is required by baked lightmaps: the lightmap UV stream has
	   one entry per triangle corner, including separate UVs on geometry seams.
	   Welding those corners and retaining only the first UV produces the visible
	   smears/seams.  Preserve them whenever they fit in Vita's 16-bit index
	   range; only oversized merged geometry uses the compatibility fallback. */
	const bool bPreserveUnsharedCorners = !bStripifyAndShareVerts &&
		pTriData->m_nFaceCount <= 21845;
	if (!bStripifyAndShareVerts && !bPreserveUnsharedCorners)
	{
		static bool s_bReportedOversizedUnsharedMesh = false;
		if (!s_bReportedOversizedUnsharedMesh && iLog)
		{
			s_bReportedOversizedUnsharedMesh = true;
			iLog->LogToFile("\001[VITA][LIGHTMAP] oversized no-sharing mesh (%d faces); welding 16-bit fallback",
				pTriData->m_nFaceCount);
		}
	}
	vertices.reserve(pTriData->m_nFaceCount * 2);
	indices.reserve(pTriData->m_nFaceCount * 3);
	m_nVertexFormat = VERTEX_FORMAT_P3F_N_COL4UB_TEX2F;
	m_nPrimetiveType = R_PRIMV_TRIANGLES;

	for (int material = 0; material < m_pMats->Count(); ++material)
	{
		CMatInfo &mi = (*m_pMats)[material];
		mi.nFirstIndexId = (int)indices.size();
		int minVertex = 0x7fffffff;
		int maxVertex = -1;

		IShader *templateShader = mi.shaderItem.m_pShader ? mi.shaderItem.m_pShader->GetTemplate(-1) : NULL;
		const bool noDraw = VitaMaterial::IsNoDrawMaterial(mi) ||
			(templateShader && (templateShader->GetFlags3() & EF3_NODRAW));
		for (int faceIndex = 0; faceIndex < pTriData->m_nFaceCount; ++faceIndex)
		{
			CObjFace &face = pTriData->m_pFaces[faceIndex];
			// With the catch-all material every face belongs to it, whatever
			// shader id the mesh claims.
			if ((!bSingleFallbackMaterial && face.shader_id != material) ||
				(noDraw && bRemoveNoDrawFaces))
				continue;

			for (int corner = 0; corner < 3; ++corner)
			{
				/* Mirror the desktop builder's buff_vert_count: one slot per
				   unwelded corner, walked material-major then face then corner.
				   Baked lightmap UVs are indexed by exactly this number. */
				const int nSourceCorner = nCornerCounter++;
				const int keyV = face.v[corner];
				const int keyN = face.n[corner];
				const int keyT = face.t[corner];
				unsigned nSlot = ((unsigned)keyV * 73856093u) ^ ((unsigned)keyN * 19349663u) ^
					((unsigned)keyT * 83492791u) ^ ((unsigned)material * 2654435761u);
				nSlot &= nHashMask;
				while (!bPreserveUnsharedCorners && vertexHash[nSlot].index >= 0 &&
					!(vertexHash[nSlot].v == keyV && vertexHash[nSlot].n == keyN &&
					  vertexHash[nSlot].t == keyT && vertexHash[nSlot].material == material))
					nSlot = (nSlot + 1) & nHashMask;

				ushort index;
				if (!bPreserveUnsharedCorners && vertexHash[nSlot].index >= 0)
				{
					index = (ushort)vertexHash[nSlot].index;
				}
				else
				{
					if (vertices.size() >= 65535)
					{
						iLog->Log("CLeafBuffer::CreateBuffer: object exceeds Vita 16-bit vertex limit");
						return;
					}
					VitaMeshVertex vertex;
					memset(&vertex, 0, sizeof(vertex));
					vertex.xyz = pTriData->m_pVerts[face.v[corner]];
					if (pTriData->m_pNorms && face.n[corner] >= 0)
						vertex.normal = pTriData->m_pNorms[face.n[corner]];
					if (pTriData->m_pCoors && face.t[corner] >= 0 && face.t[corner] < pTriData->m_nCoorCount)
					{
						vertex.st[0] = pTriData->m_pCoors[face.t[corner]].s;
						vertex.st[1] = pTriData->m_pCoors[face.t[corner]].t;
					}
					if (pTriData->m_pColor)
					{
						vertex.color.bcolor[0] = pTriData->m_pColor[face.v[corner]].r;
						vertex.color.bcolor[1] = pTriData->m_pColor[face.v[corner]].g;
						vertex.color.bcolor[2] = pTriData->m_pColor[face.v[corner]].b;
						vertex.color.bcolor[3] = pTriData->m_pColor[face.v[corner]].a;
					}
					else
						vertex.color.dcolor = 0xffffffff;

					index = (ushort)vertices.size();
					vertices.push_back(vertex);
					arrLMCornerOfVertex.push_back(nSourceCorner);
					if (!bPreserveUnsharedCorners)
					{
						vertexHash[nSlot].v = keyV;
						vertexHash[nSlot].n = keyN;
						vertexHash[nSlot].t = keyT;
						vertexHash[nSlot].material = material;
						vertexHash[nSlot].index = (int)index;
					}
				}
				indices.push_back(index);
				minVertex = min(minVertex, (int)index);
				maxVertex = max(maxVertex, (int)index);
			}
		}

		mi.nNumIndices = (int)indices.size() - mi.nFirstIndexId;
		if (mi.nNumIndices > 0)
		{
			mi.nFirstVertId = minVertex;
			mi.nNumVerts = maxVertex - minVertex + 1;
			CREOcLeaf *re = (CREOcLeaf *)gcpVitaRenderer->EF_CreateRE(eDATA_OcLeaf);
			if (re)
			{
				re->m_pChunk = &mi;
				re->m_pBuffer = this;
				mi.pRE = re;
			}
		}
	}

	if (vertices.empty() || indices.empty())
		return;
	m_pVertexBuffer = gcpVitaRenderer->CreateBuffer((int)vertices.size(), m_nVertexFormat, m_sSource, m_bDynamic != 0);
	gcpVitaRenderer->UpdateBuffer(m_pVertexBuffer, &vertices[0], (int)vertices.size(), true, 0, VSF_GENERAL);
	/* Vita client arrays already live in CPU-addressable memory; aliasing the
	   system/video views avoids the desktop backend's duplicate mesh copy. */
	m_pSecVertBuffer = m_pVertexBuffer;
	m_SecVertCount = (int)vertices.size();
	m_nLMCornerCount = nCornerCounter;
	m_arrLMCornerOfVertex.swap(arrLMCornerOfVertex);
	gcpVitaRenderer->CreateIndexBuffer(&m_Indices, &indices[0], (int)indices.size());
	m_NumIndices = (int)indices.size();
	/* CryAnimation builds its external-to-internal skinning map from the
	   original triangle list immediately after CreateBuffer.  The desktop
	   renderer keeps this alongside the potentially stripified video index
	   stream; Vita does not stripify, so retain one compact copy verbatim. */
	delete m_pIndicesPreStrip;
	m_pIndicesPreStrip = new list2<ushort>;
	m_pIndicesPreStrip->AddList(&indices[0], (int)indices.size());
	m_vBoxMin = pTriData->m_vBoxMin;
	m_vBoxMax = pTriData->m_vBoxMax;
}
bool CLeafBuffer::CreateBuffer(struct VertexBufferSource *pSource)
{
	/* Exact compact equivalent of Crytek's LeafBufferCreate.cpp CCG path.
	   CCG characters already contain indexed primitive groups and skinned
	   external vertices, so no re-indexing/stripification is required. */
	if (!gcpVitaRenderer || !pSource || !pSource->pIndices || !pSource->pVertices ||
		pSource->numVertices == 0 || pSource->numVertices > 65535)
	{
		/* This is the per-instance (skinned) character buffer.  Bailing here
		   leaves m_pMats null and the character invisible with no other trace,
		   so say which model and which field was wrong. */
		if (iLog)
		{
			static int s_nReportedInstanceFails = 0;
			if (s_nReportedInstanceFails < 16)
			{
				++s_nReportedInstanceFails;
				iLog->LogToFile("\001[VITA][MESH] instance buffer rejected for '%s': inds=%p verts=%p numVerts=%u",
					m_sSource ? m_sSource : "<unnamed>",
					pSource ? (void *)pSource->pIndices : NULL,
					pSource ? (void *)pSource->pVertices : NULL,
					pSource ? pSource->numVertices : 0u);
			}
		}
		return false;
	}

	if (!m_pMats)
	{
		if (pSource->pMats)
		{
			m_pMats = pSource->pMats;
			pSource->pMats = NULL;
		}
		else
		{
			m_pMats = new list2<CMatInfo>;
			for (unsigned i = 0; i < pSource->numMaterials; ++i)
			{
				CMatInfo mi;
				m_pMats->Add(mi);
			}
		}
	}
	if (!m_pMats)
		return false;

	while ((unsigned)m_pMats->Count() < pSource->numMaterials)
	{
		CMatInfo mi;
		m_pMats->Add(mi);
	}

	std::vector<int> minVertex((size_t)m_pMats->Count(), 0x7fffffff);
	std::vector<int> maxVertex((size_t)m_pMats->Count(), -1);
	std::vector<Vec3> materialMin((size_t)m_pMats->Count(),
		Vec3(1.0e20f, 1.0e20f, 1.0e20f));
	std::vector<Vec3> materialMax((size_t)m_pMats->Count(),
		Vec3(-1.0e20f, -1.0e20f, -1.0e20f));
	std::vector< std::vector<ushort> > materialIndices((size_t)m_pMats->Count());
	bool boxInitialized = false;

	for (unsigned groupIndex = 0; groupIndex < pSource->numPrimGroups; ++groupIndex)
	{
		const CCFMaterialGroup &group = pSource->pPrimGroups[groupIndex];
		if (group.nMaterial >= (unsigned)m_pMats->Count() ||
			group.nIndexBase >= pSource->numIndices)
			continue;
		unsigned end = min(pSource->numIndices, group.nIndexBase + group.numIndices);
		CMatInfo &mi = (*m_pMats)[group.nMaterial];
		mi.m_Id = group.nMaterial;
		if (pSource->pShaders)
			mi.shaderItem = pSource->pShaders[group.nMaterial];
		if (pSource->pOrFlags)
			mi.m_Flags |= pSource->pOrFlags[group.nMaterial];
		if (pSource->pAndFlags)
			mi.m_Flags &= pSource->pAndFlags[group.nMaterial];

		if (!mi.pRE)
			mi.pRE = (CREOcLeaf *)gcpVitaRenderer->EF_CreateRE(eDATA_OcLeaf);
		if (mi.pRE)
		{
			mi.pRE->m_pBuffer = this;
			mi.pRE->m_pChunk = &mi;
			mi.pRE->m_Flags |= pSource->nREFlags;
		}

		/* A material may own several primitive groups, and those groups are not
		   required to be adjacent in the source index stream.  Taking the minimum
		   group start and maximum group end (the old code) swallowed every group
		   belonging to other materials between them.  CryModelSubmesh then
		   submitted that oversized range once per primitive group: the first-person
		   weapon could redraw the same geometry dozens of times and shade some of
		   it with the wrong material.

		   Preserve every triangle exactly once by collecting each group's exact
		   indices into its material bucket.  Below, the buckets are flattened into
		   one contiguous range per CMatInfo, which is the layout AddRenderElements
		   and its one-submit-per-material path actually require. */
		std::vector<ushort> &grouped = materialIndices[group.nMaterial];
		grouped.reserve(grouped.size() + (size_t)(end - group.nIndexBase));
		for (unsigned index = group.nIndexBase; index < end; ++index)
		{
			unsigned vertexIndex = pSource->pIndices[index];
			if (vertexIndex >= pSource->numVertices)
				continue;
			grouped.push_back((ushort)vertexIndex);
			const Vec3 &position = pSource->pVertices[vertexIndex];
			materialMin[group.nMaterial].CheckMin(position);
			materialMax[group.nMaterial].CheckMax(position);
			minVertex[group.nMaterial] = min(minVertex[group.nMaterial], (int)vertexIndex);
			maxVertex[group.nMaterial] = max(maxVertex[group.nMaterial], (int)vertexIndex);
			if (!boxInitialized)
			{
				m_vBoxMin = m_vBoxMax = position;
				boxInitialized = true;
			}
			else
			{
				m_vBoxMin.CheckMin(position);
				m_vBoxMax.CheckMax(position);
			}
		}
	}

	std::vector<ushort> drawIndices;
	drawIndices.reserve((size_t)pSource->numIndices);
	for (int material = 0; material < m_pMats->Count(); ++material)
	{
		CMatInfo &mi = (*m_pMats)[material];
		const std::vector<ushort> &grouped = materialIndices[(size_t)material];
		if (!grouped.empty() && maxVertex[material] >= minVertex[material])
		{
			mi.nFirstIndexId = (int)drawIndices.size();
			mi.nNumIndices = (int)grouped.size();
			mi.nFirstVertId = minVertex[material];
			mi.nNumVerts = maxVertex[material] - minVertex[material] + 1;
			mi.m_vCenter = (materialMin[material] + materialMax[material]) * 0.5f;
			mi.m_fRadius = (materialMax[material] - mi.m_vCenter).GetLength();
			drawIndices.insert(drawIndices.end(), grouped.begin(), grouped.end());
		}
		else
		{
			mi.nFirstIndexId = mi.nNumIndices = mi.nFirstVertId = mi.nNumVerts = 0;
		}
	}

	m_nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
	m_nPrimetiveType = R_PRIMV_TRIANGLES;
	std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> vertices(pSource->numVertices);
	for (unsigned i = 0; i < pSource->numVertices; ++i)
	{
		vertices[i].xyz = pSource->pVertices[i];
		vertices[i].color.dcolor = 0xffffffff;
		vertices[i].st[0] = pSource->pUVs ? pSource->pUVs[i].u : 0.0f;
		vertices[i].st[1] = pSource->pUVs ? pSource->pUVs[i].v : 0.0f;
	}
	m_pVertexBuffer = gcpVitaRenderer->CreateBuffer((int)pSource->numVertices, m_nVertexFormat, m_sSource, true);
	if (!m_pVertexBuffer)
		return false;
	gcpVitaRenderer->UpdateBuffer(m_pVertexBuffer, &vertices[0], (int)pSource->numVertices, true, 0, VSF_GENERAL);
	m_pSecVertBuffer = m_pVertexBuffer;
	m_SecVertCount = (int)pSource->numVertices;
	if (drawIndices.empty())
	{
		/* Defensive old-CCG fallback.  The caller normally takes a separate path
		   when there are no primitive groups, but retaining the source stream is
		   safer than producing a valid vertex buffer with nothing drawable. */
		drawIndices.assign(pSource->pIndices, pSource->pIndices + pSource->numIndices);
		if (m_pMats->Count() > 0)
		{
			CMatInfo &mi = (*m_pMats)[0];
			mi.nFirstIndexId = 0;
			mi.nNumIndices = (int)drawIndices.size();
			mi.nFirstVertId = 0;
			mi.nNumVerts = (int)pSource->numVertices;
		}
	}
	#if defined(VITA_PERF_TELEMETRY)
	if (iLog)
	{
		unsigned nDrawMaterials = 0;
		for (int nMaterial = 0; nMaterial < m_pMats->Count(); ++nMaterial)
			if ((*m_pMats)[nMaterial].nNumIndices > 0)
				++nDrawMaterials;
		static unsigned s_nCharacterLayoutReports = 0;
		if (s_nCharacterLayoutReports < 24 && pSource->numPrimGroups > nDrawMaterials)
		{
			++s_nCharacterLayoutReports;
			iLog->LogToFile("\001[VITA][CHARBUF] source=%s primGroups=%u drawMaterials=%u "
				"sourceIndices=%u drawIndices=%u verts=%u ringKB=%u",
				m_sSource ? m_sSource : "<unnamed>", pSource->numPrimGroups,
				nDrawMaterials, pSource->numIndices, (unsigned)drawIndices.size(),
				pSource->numVertices,
				(unsigned)(pSource->numVertices * sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F) * 5 / 1024));
		}
	}
	#endif
	gcpVitaRenderer->CreateIndexBuffer(&m_Indices, &drawIndices[0], (int)drawIndices.size());
	m_NumIndices = (int)drawIndices.size();
	return true;
}
int CLeafBuffer::GetAllocatedBytes(bool bVideoMem)
{
	int bytes = sizeof(*this) + m_NumIndices * (int)sizeof(ushort);
	if (m_pVertexBuffer)
		bytes += m_pVertexBuffer->m_NumVerts * m_VertexSize[m_pVertexBuffer->m_vertexformat];
	return bytes;
}
void CLeafBuffer::Unload() {}
void CLeafBuffer::UpdateCustomLighting(float fBackSideLevel, Vec3 vStatObjAmbientColor, const Vec3 &vLight, bool bCalcLighting) {}
unsigned short *CLeafBuffer::GetIndices(int *pIndicesCount) { if (pIndicesCount) *pIndicesCount = m_Indices.m_nItems; return (ushort*)m_Indices.m_VData; }
void CLeafBuffer::DestroyIndices() { if (gcpVitaRenderer) gcpVitaRenderer->ReleaseIndexBuffer(&m_Indices); m_NumIndices = 0; }
void CLeafBuffer::UpdateVidIndices(const ushort *pNewInds, int nInds)
{
	if (!gcpVitaRenderer || !pNewInds || nInds <= 0)
		return;
	gcpVitaRenderer->UpdateIndexBuffer(&m_Indices, pNewInds, nInds, true);
	m_NumIndices = nInds;
}
void CLeafBuffer::UpdateSysIndices(const ushort *pNewInds, int nInds)
{
	/* Vita keeps client-side indices as the draw buffer, so a system update
	   must reach it immediately.  Dynamic ocean/particle buffers use this
	   path every frame and otherwise stay frozen at their creation point. */
	UpdateVidIndices(pNewInds, nInds);
}
void CLeafBuffer::UpdateSysVertices(void *pNewVertices, int nNewVerticesCount)
{
	if (!gcpVitaRenderer || !pNewVertices || nNewVerticesCount <= 0)
		return;
	if (!m_pVertexBuffer || m_pVertexBuffer->m_NumVerts < nNewVerticesCount)
	{
		CVertexBuffer *oldVideo = m_pVertexBuffer;
		CVertexBuffer *oldSystem = m_pSecVertBuffer;
		if (oldVideo)
			gcpVitaRenderer->ReleaseBuffer(oldVideo);
		if (oldSystem && oldSystem != oldVideo)
			gcpVitaRenderer->ReleaseBuffer(oldSystem);
		m_pVertexBuffer = gcpVitaRenderer->CreateBuffer(nNewVerticesCount, m_nVertexFormat, m_sSource, true);
		m_pSecVertBuffer = m_pVertexBuffer;
	}
	else if (m_pSecVertBuffer != m_pVertexBuffer)
	{
		/* Collapse old desktop-style duplicate system storage when a dynamic
		   buffer is next touched. The compact renderer never uploads to a
		   separate GPU VBO, so maintaining both copies only burns main RAM. */
		if (m_pSecVertBuffer)
			gcpVitaRenderer->ReleaseBuffer(m_pSecVertBuffer);
		m_pSecVertBuffer = m_pVertexBuffer;
	}
	gcpVitaRenderer->UpdateBuffer(m_pVertexBuffer, pNewVertices, nNewVerticesCount, true, 0, VSF_GENERAL);
	m_SecVertCount = nNewVerticesCount;
}
void CLeafBuffer::UpdateVidVertices(void *pNewVertices, int nNewVerticesCount)
{
	UpdateSysVertices(pNewVertices, nNewVerticesCount);
}
bool CLeafBuffer::CreateSysVertices(int nVerts, int VertFormat)
{
	if (!gcpVitaRenderer || nVerts <= 0)
		return false;
	CVertexBuffer *oldVideo = m_pVertexBuffer;
	CVertexBuffer *oldSystem = m_pSecVertBuffer;
	if (oldVideo)
		gcpVitaRenderer->ReleaseBuffer(oldVideo);
	if (oldSystem && oldSystem != oldVideo)
		gcpVitaRenderer->ReleaseBuffer(oldSystem);
	m_nVertexFormat = VertFormat;
	m_pSecVertBuffer = gcpVitaRenderer->CreateBuffer(nVerts, VertFormat, m_sSource, true);
	m_pVertexBuffer = m_pSecVertBuffer;
	m_SecVertCount = nVerts;
	return m_pSecVertBuffer != NULL;
}
void CLeafBuffer::CreateVidVertices(int nVerts, int VertFormat)
{
	if (!gcpVitaRenderer || nVerts <= 0)
		return;
	CVertexBuffer *oldVideo = m_pVertexBuffer;
	CVertexBuffer *oldSystem = m_pSecVertBuffer;
	if (oldVideo)
		gcpVitaRenderer->ReleaseBuffer(oldVideo);
	if (oldSystem && oldSystem != oldVideo)
		gcpVitaRenderer->ReleaseBuffer(oldSystem);
	m_nVertexFormat = VertFormat;
	m_pVertexBuffer = gcpVitaRenderer->CreateBuffer(nVerts, VertFormat, m_sSource, m_bDynamic != 0);
	m_pSecVertBuffer = m_pVertexBuffer;
	m_SecVertCount = nVerts;
}
void CLeafBuffer::SetRECustomData(float *pfCustomData, float fFogScale, float fAlpha) {}
void CLeafBuffer::SetChunk(IShader *pShader, int nFirstVertId, int nVertCount, int nFirstIndexId, int nIndexCount, int nMatID, bool bForceInitChunk)
{
	if (!m_pMats || nIndexCount <= 0 || nVertCount <= 0)
		return;
	if (nMatID < 0)
		nMatID = m_pMats->Count();
	while (m_pMats->Count() <= nMatID)
	{
		CMatInfo mi;
		m_pMats->Add(mi);
	}
	CMatInfo &mi = (*m_pMats)[nMatID];
	mi.shaderItem.m_pShader = pShader;
	if (!mi.pRE && (nMatID == 0 || bForceInitChunk))
	{
		CREOcLeaf *re = (CREOcLeaf *)gcpVitaRenderer->EF_CreateRE(eDATA_OcLeaf);
		if (re)
		{
			re->m_CustomTexBind[0] = m_nClientTextureBindID;
			re->m_pChunk = &mi;
			re->m_pBuffer = this;
			mi.pRE = re;
		}
	}
	mi.nFirstVertId = nFirstVertId;
	mi.nNumVerts = nVertCount;
	mi.nFirstIndexId = nFirstIndexId;
	mi.nNumIndices = nIndexCount;
}
void CLeafBuffer::SetShader(IShader *pShader, int nCustomTID)
{
	if (!m_pMats)
		return;
	for (int i = 0; i < m_pMats->Count(); ++i)
	{
		CMatInfo &mi = (*m_pMats)[i];
		mi.shaderItem.m_pShader = pShader;
		if (mi.pRE)
			mi.pRE->m_CustomTexBind[0] = nCustomTID;
	}
	m_nClientTextureBindID = nCustomTID;
}
void *CLeafBuffer::GetSecVerticesPtr(int *pVerticesCount)
{
	if (pVerticesCount) *pVerticesCount = m_pSecVertBuffer ? m_pSecVertBuffer->m_NumVerts : 0;
	return m_pSecVertBuffer ? m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData : NULL;
}
void CLeafBuffer::AllocateSystemBuffer(int nVertCount) {}
void CLeafBuffer::FreeSystemBuffer(void) {}
bool CLeafBuffer::Serialize(int &nPos, uchar *pSaveBuffer, bool bSaveToBuffer, char *szFolderName, char *szFileName, double &dCIndexedMesh__LoadMaterial) { return false; }
void CLeafBuffer::DrawImmediately(void) {}
bool CLeafBuffer::UpdateTangBuffer(SPipTangents *pBasis)
{
	if (!m_pSecVertBuffer || !pBasis || m_SecVertCount <= 0)
		return false;
	SPipTangents *copy = new SPipTangents[m_SecVertCount];
	if (!copy)
		return false;
	memcpy(copy, pBasis, sizeof(SPipTangents) * m_SecVertCount);
	delete [] (SPipTangents *)m_pSecVertBuffer->m_VS[VSF_TANGENTS].m_VData;
	m_pSecVertBuffer->m_VS[VSF_TANGENTS].m_VData = copy;
	return true;
}
void CLeafBuffer::RenderDebugLightPass(const Matrix44 &mat, int nLightMask, float fAlpha) {}
void CLeafBuffer::Render(const struct SRendParams &rParams, CCObject *pObj, TArray<int> &ShaderTemplates, int e_overlay_geometry, bool bNotCurArea, IMatInfo *pMaterial, bool bSupportDefaultShaderTemplates)
{
	/* Static objects and characters enter through Render(), while terrain
	   calls AddRenderElements() directly.  Route both into the same immediate
	   Vita draw path; the former no-op was why a fully loaded retail level
	   still displayed no world meshes. */
	AddRenderElements(pObj, rParams.nDLightMask, rParams.nShaderTemplate,
		rParams.nFogVolumeID, rParams.nSortValue, pMaterial);
}

CLeafBuffer *CVitaRenderer::CreateLeafBuffer(bool bDynamic, const char *szSource, class CIndexedMesh *pIndexedMesh)
{
	CLeafBuffer *lb = new CLeafBuffer(szSource);
	lb->m_bDynamic = bDynamic;
	return lb;
}

CLeafBuffer *CVitaRenderer::CreateLeafBufferInitialized(void *pVertBuffer, int nVertCount, int nVertFormat,
	ushort *pIndices, int nIndices, int nPrimetiveType, const char *szSource, EBufferType eBufType,
	int nMatInfoCount, int nClientTextureBindID, bool (*PrepareBufferCallback)(CLeafBuffer *, bool),
	void *CustomData, bool bOnlyVideoBuffer, bool bPrecache)
{
	CLeafBuffer *lb = new CLeafBuffer(szSource);
	lb->m_nVertexFormat = nVertFormat;
	lb->m_nPrimetiveType = nPrimetiveType;
	lb->m_nClientTextureBindID = nClientTextureBindID;
	lb->PrepareBufferCallback = PrepareBufferCallback;
	lb->m_pCustomData = CustomData;
	lb->m_bOnlyVideoBuffer = bOnlyVideoBuffer;
	lb->m_bDynamic = (eBufType == eBT_Dynamic);
	lb->m_Indices.m_bDynamic = lb->m_bDynamic;
	lb->m_bMaterialsWasCreatedInRenderer = 1;

	lb->m_pVertexBuffer = CreateBuffer(nVertCount, nVertFormat, szSource, lb->m_bDynamic != 0);
	UpdateBuffer(lb->m_pVertexBuffer, pVertBuffer, nVertCount, true, 0, VSF_GENERAL);

	/* vitaGL consumes CPU-addressable client arrays directly, so a second
	   byte-for-byte "video" copy wastes scarce Vita main RAM. Keep CryEngine's
	   two logical views but alias the same allocation; update methods already
	   preserve this alias and the destructor explicitly handles it. */
	lb->m_pSecVertBuffer = lb->m_pVertexBuffer;
	lb->m_SecVertCount = nVertCount;

	CreateIndexBuffer(&lb->m_Indices, pIndices, nIndices);
	lb->m_NumIndices = nIndices;

	lb->m_pMats = new list2<CMatInfo>;
	if (nMatInfoCount > 0)
	{
		for (int i = 0; i < nMatInfoCount; i++)
		{
			CMatInfo mi;
			mi.nFirstVertId = 0;
			mi.nNumVerts = nVertCount;
			mi.nFirstIndexId = 0;
			mi.nNumIndices = nIndices;
			lb->m_pMats->Add(mi);
		}
	}

	return lb;
}

void CVitaRenderer::DeleteLeafBuffer(CLeafBuffer *pLBuffer)
{
	delete pLBuffer;
}

#endif // defined(LINUX)
