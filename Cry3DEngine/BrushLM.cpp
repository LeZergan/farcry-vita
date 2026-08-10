////////////////////////////////////////////////////////////////////////////
//
//  Crytek Engine Source File.
//  Copyright (C), Crytek Studios, 2002.
// -------------------------------------------------------------------------
//  File name:   brushlm.cpp
//  Version:     v1.00
//  Created:     28/5/2001 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: 
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "StatObj.h"
#include "objman.h"
#include "visareas.h"
#include "terrain_sector.h"
#include "cbuffer.h"
#include "3DEngine.h"
#include "meshidx.h"
#include "watervolumes.h"
#include "LMCompStructures.h"
#include "brush.h"
#include "IEntitySystem.h"

void CBrush::SetLightmap(RenderLMData *pLMData, float *pTexCoords, UINT iNumTexCoords, const unsigned char cucOcclIDCount, const std::vector<std::pair<EntityId, EntityId> >& aIDs)
{
	assert(cucOcclIDCount <= 4);

	memset(m_arrOcclusionLightOwners,0,sizeof(m_arrOcclusionLightOwners));

	if(GetCVars()->e_light_maps_occlusion)
	{
		if(m_bEditorMode)
		{
			for(int i=0; i<4 && i<cucOcclIDCount; ++i)
			{
				EntityId nEntId = aIDs[i].first;
				IEntityRender * pEnt;
				if(nEntId == (EntityId)-1)
					pEnt = (IEntityRender*)-1; // sun
				else
					pEnt = GetSystem()->GetIEntitySystem()->GetEntity(nEntId);
				//			assert(pEnt);
				m_arrOcclusionLightOwners[i] = pEnt;
			}
		}
		else
		{
			const list2<CDLight*> * pStaticLights = Get3DEngine()->GetStaticLightSources();
			for(int i=0; (i<4) && (i<cucOcclIDCount) && (pStaticLights->Count()); ++i)
			{
				EntityId nEntId = aIDs[i].second;
				IEntityRender * pEnt;
				if(nEntId == (EntityId)-1)
					pEnt = (IEntityRender*)-1; // sun
				else
					pEnt = pStaticLights->GetAt(nEntId)->m_pOwner;
				assert(pEnt);
				m_arrOcclusionLightOwners[i] = pEnt;
			}
		}
	}

	SetLightmap(pLMData, pTexCoords, iNumTexCoords, 0);
}

void CBrush::SetLightmap(RenderLMData *pLMData, float *pTexCoords, UINT iNumTexCoords, int nLod)
{
	// ---------------------------------------------------------------------------------------------
	// Set a referenece of a DOT3 Lightmap object for this GLM
	// ---------------------------------------------------------------------------------------------

	/* This used to bail out on Vita because CVitaRenderer had no lightmap
	   stage, which meant no brush ever kept its RenderLMData or its lightmap
	   texture coordinates -- and with those thrown away the world had no baked
	   lighting at all, only the flat diffuse texture.  CVitaRenderer::EF_AddEf
	   now binds the colour/lerp lightmap on texture unit 1 (see r_lightmaps),
	   so the data is worth keeping. */
	IRenderer *pIRenderer = GetRenderer();

	assert(iNumTexCoords);
	assert(!IsBadReadPtr(pTexCoords, sizeof(float) * 2 * iNumTexCoords));

	m_arrLMData[nLod].m_pLMData = pLMData;

	if (m_arrLMData[nLod].m_pLMTCBuffer)
	{
		pIRenderer->DeleteLeafBuffer(m_arrLMData[nLod].m_pLMTCBuffer);
		m_arrLMData[nLod].m_pLMTCBuffer = NULL;
	}

	IStatObj *pIStatObj = GetEntityStatObj(0, NULL);									

	if (pIStatObj == NULL)
		return;

  if(!pIStatObj->EnableLightamapSupport())
    return;

	CLeafBuffer *pLeafBuffer = pIStatObj->GetLeafBuffer();			

	if (pLeafBuffer == NULL)
		return;

	// Renderer expect 2 floats
	std::vector<float> vTexCoord2;
	vTexCoord2.reserve(iNumTexCoords * 2);
	UINT i;
	for (i=0; i<iNumTexCoords; i++)
	{
		vTexCoord2.push_back(pTexCoords[i * 2 + 0]); // S
		vTexCoord2.push_back(pTexCoords[i * 2 + 1]); // T
	}

#if defined(LINUX)
	/* The baked UVs are one per unwelded triangle corner, which is what the
	   desktop vertex buffer held.  The Vita builder welds shared corners, so
	   its vertex count is smaller -- resample the UVs through the corner map
	   it recorded rather than rejecting the brush, which is what left every
	   lightmapped object in the level unlit. */
	if (pLeafBuffer->m_SecVertCount != (int)iNumTexCoords &&
		pLeafBuffer->m_nLMCornerCount == (int)iNumTexCoords &&
		(int)pLeafBuffer->m_arrLMCornerOfVertex.size() == pLeafBuffer->m_SecVertCount)
	{
		std::vector<float> vResampled;
		vResampled.reserve(pLeafBuffer->m_SecVertCount * 2);
		for (int nVert = 0; nVert < pLeafBuffer->m_SecVertCount; ++nVert)
		{
			const int nCorner = pLeafBuffer->m_arrLMCornerOfVertex[nVert];
			if (nCorner < 0 || nCorner >= (int)iNumTexCoords)
			{
				vResampled.clear();
				break;
			}
			vResampled.push_back(pTexCoords[nCorner * 2 + 0]);
			vResampled.push_back(pTexCoords[nCorner * 2 + 1]);
		}
		if (!vResampled.empty())
			vTexCoord2.swap(vResampled);
	}
#endif

	if (pLeafBuffer->m_SecVertCount != (int)(vTexCoord2.size() / 2))
	{
		/* One line per affected brush drowns the load log -- and every one of
		   these is the same finding repeated: this object's lightmap has fewer
		   corners than the leaf buffer has vertices, so it renders unlit.  Keep
		   the first few with their full detail, then report periodically with a
		   running total, which is the part that actually matters. */
		static int s_nLMMismatchCount = 0;
		++s_nLMMismatchCount;
		if (s_nLMMismatchCount <= 5 || (s_nLMMismatchCount % 250) == 0)
		{
			char szBuffer[1024];
			sprintf(szBuffer, "Error: CBrush::SetLightmap: Object at position (%f, %f, %f) has" \
				" texture mismatch (%i coordinates supplied, %i required) [%d objects unlit so far]\r\n",
				GetPos().x, GetPos().y, GetPos().z,
				iNumTexCoords, pLeafBuffer->m_SecVertCount, s_nLMMismatchCount);
			Warning(0,pIStatObj->GetFileName(),szBuffer);
		}
		return;
	}

	// Make leafbuffer and fill it with texture coordinates
	m_arrLMData[nLod].m_pLMTCBuffer = GetRenderer()->CreateLeafBufferInitialized(
		&vTexCoord2[0], pLeafBuffer->m_SecVertCount, VERTEX_FORMAT_TEX2F, 
		0/*pLeafBuffer->GetIndices(0)*/, 0/*pLeafBuffer->m_Indices.m_nItems*/, 
		R_PRIMV_TRIANGLES, "LMapTexCoords", eBT_Static, 1, 0, NULL, NULL, false, false);

  C3DEngine *pEng = (C3DEngine *)Get3DEngine();
	m_arrLMData[nLod].m_pLMTCBuffer->SetChunk(pEng->m_pSHDefault,
		0,pLeafBuffer->m_SecVertCount, 0,pLeafBuffer->m_Indices.m_nItems);
}
