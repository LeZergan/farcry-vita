#include "stdafx.h"
#include "MathUtils.h"
#include "CrySkinBuilderBase.h"
#include "CrySkinFull.h"
#include "platform.h"

#if defined(__vita__) || defined(LINUX)
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#endif

#define FOR_TEST 0

#if defined(__vita__) || defined(LINUX)
namespace
{
	const unsigned VITA_SKIN_RIGID_BIT = 0x8000u;
	const unsigned VITA_SKIN_BONE_MASK = 0x7fffu;
	const unsigned VITA_SKIN_MIN_DESTS = 256;
	const unsigned VITA_SKIN_MIN_LINKS = 512;

	struct VitaSkinJob
	{
		const Matrix44 *pBones;
		const CrySkinVertexAligned *pVertices;
		const VitaSkinGatherLink *pLinks;
		const unsigned *pOffsets;
		void *pDest;
		unsigned nDestStride;
		unsigned nBegin;
		unsigned nEnd;
		bool bTranslate;
	};

	void RunVitaSkinRange(const VitaSkinJob &job)
	{
		for(unsigned nDest=job.nBegin; nDest<job.nEnd; ++nDest)
		{
			Vec3d result(0.0f, 0.0f, 0.0f);
			const unsigned nLinkEnd = job.pOffsets[nDest+1];
			for(unsigned nLink=job.pOffsets[nDest]; nLink<nLinkEnd; ++nLink)
			{
				const VitaSkinGatherLink &link = job.pLinks[nLink];
				const CrySkinVertexAligned &vertex = job.pVertices[link.nVertex];
				const Matrix44 &bone = job.pBones[link.nBoneAndRigid & VITA_SKIN_BONE_MASK];
				const float weight = (link.nBoneAndRigid & VITA_SKIN_RIGID_BIT) ? 1.0f : vertex.fWeight;
				const float tx = job.bTranslate ? bone[3][0] : 0.0f;
				const float ty = job.bTranslate ? bone[3][1] : 0.0f;
				const float tz = job.bTranslate ? bone[3][2] : 0.0f;
				result.x += ((bone[0][0]*vertex.pt.x) + (bone[1][0]*vertex.pt.y) + (bone[2][0]*vertex.pt.z) + tx) * weight;
				result.y += ((bone[0][1]*vertex.pt.x) + (bone[1][1]*vertex.pt.y) + (bone[2][1]*vertex.pt.z) + ty) * weight;
				result.z += ((bone[0][2]*vertex.pt.x) + (bone[1][2]*vertex.pt.y) + (bone[2][2]*vertex.pt.z) + tz) * weight;
			}
			*(Vec3d *)((char *)job.pDest + nDest*job.nDestStride) = result;
		}
	}

	class VitaSkinWorkers
	{
	public:
		VitaSkinWorkers(): m_done(-1), m_ready(false), m_attempted(false), m_running(true)
		{
			for(int i=0; i<2; ++i)
			{
				m_thread[i] = -1;
				m_start[i] = -1;
				m_index[i] = i;
			}
		}

		~VitaSkinWorkers()
		{
			m_running = false;
			for(int i=0; i<2; ++i)
				if(m_start[i] >= 0)
					sceKernelSignalSema(m_start[i], 1);
			for(int i=0; i<2; ++i)
			{
				if(m_thread[i] >= 0)
				{
					sceKernelWaitThreadEnd(m_thread[i], 0, 0);
					sceKernelDeleteThread(m_thread[i]);
				}
				if(m_start[i] >= 0)
					sceKernelDeleteSema(m_start[i]);
			}
			if(m_done >= 0)
				sceKernelDeleteSema(m_done);
		}

		bool initialize()
		{
			if(m_ready)
				return true;
			/* Do not leak another partial pool every frame if a kernel rejects a
			   semaphore, affinity, or thread request.  Serial skinning remains the
			   safe fallback for the rest of that run. */
			if(m_attempted)
				return false;
			m_attempted = true;

			m_done = sceKernelCreateSema("fc_skin_done", 0, 0, 2, 0);
			if(m_done < 0)
			{
				if(g_GetLog())
					g_GetLog()->LogToFile("\001[VITA][SKINMT] disabled: done semaphore failed 0x%08x", (unsigned)m_done);
				return false;
			}

			/* CapUnlock removes the game-process check on the normally reserved
			   0x80000/core-3 mask.  Main owns core 0 and the audio mixer owns core
			   2, so skinning gets the otherwise idle cores 1 and 3. */
			const int cpuMasks[2] = { SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_SYSTEM };
			for(int i=0; i<2; ++i)
			{
				char semName[32], threadName[32];
				sprintf(semName, "fc_skin_start_%d", i);
				sprintf(threadName, "fc_skin_cpu_%d", i ? 3 : 1);
				m_start[i] = sceKernelCreateSema(semName, 0, 0, 1, 0);
				if(m_start[i] < 0)
				{
					if(g_GetLog())
						g_GetLog()->LogToFile("\001[VITA][SKINMT] disabled: start semaphore %d failed 0x%08x", i, (unsigned)m_start[i]);
					return false;
				}
				m_thread[i] = sceKernelCreateThread(threadName, WorkerEntry,
					0x10000100, 0x10000, 0, cpuMasks[i], 0);
				if(m_thread[i] < 0)
				{
					if(g_GetLog())
						g_GetLog()->LogToFile("\001[VITA][SKINMT] disabled: worker %d create failed 0x%08x", i, (unsigned)m_thread[i]);
					return false;
				}
				const int startResult = sceKernelStartThread(m_thread[i], sizeof(int), &m_index[i]);
				if(startResult < 0)
				{
					if(g_GetLog())
						g_GetLog()->LogToFile("\001[VITA][SKINMT] disabled: worker %d start failed 0x%08x", i, (unsigned)startResult);
					sceKernelDeleteThread(m_thread[i]);
					m_thread[i] = -1;
					return false;
				}
			}

			m_ready = true;
			if(g_GetLog())
				g_GetLog()->LogToFile("\001[VITA][SKINMT] gather workers active on CPU 1 and unlocked CPU 3");
			return true;
		}

		void run(VitaSkinJob jobs[3])
		{
#if defined(VITA_PERF_TELEMETRY)
			const SceUInt64 nStartUs = sceKernelGetProcessTimeWide();
#endif
			m_jobs[0] = jobs[0];
			m_jobs[1] = jobs[1];
			__sync_synchronize();
			sceKernelSignalSema(m_start[0], 1);
			sceKernelSignalSema(m_start[1], 1);
			RunVitaSkinRange(jobs[2]);
			sceKernelWaitSema(m_done, 2, 0);

			/* Prove that the workers are receiving substantial jobs, not merely
			   that their threads were created.  Report an amortised wall time so
			   semaphore overhead and poor thresholds are visible on hardware. */
#if defined(VITA_PERF_TELEMETRY)
			static unsigned s_nJobs = 0;
			static unsigned s_nBatchJobs = 0;
			static SceUInt64 s_nTotalUs = 0;
			static SceUInt64 s_nTotalDests = 0;
			static SceUInt64 s_nTotalLinks = 0;
			s_nTotalUs += sceKernelGetProcessTimeWide() - nStartUs;
			s_nTotalDests += jobs[2].nEnd;
			s_nTotalLinks += jobs[2].pOffsets[jobs[2].nEnd];
			++s_nJobs;
			++s_nBatchJobs;
			if ((s_nJobs == 1 || s_nBatchJobs >= 120) && g_GetLog())
			{
				g_GetLog()->LogToFile("\001[VITA][SKINMT] jobs=%u total=%u avgWallUs=%u avgDests=%u avgLinks=%u cores=1,3",
					s_nBatchJobs, s_nJobs, (unsigned)(s_nTotalUs / s_nBatchJobs),
					(unsigned)(s_nTotalDests / s_nBatchJobs),
					(unsigned)(s_nTotalLinks / s_nBatchJobs));
				s_nTotalUs = s_nTotalDests = s_nTotalLinks = 0;
				s_nBatchJobs = 0;
			}
#endif
		}

	private:
		static int WorkerEntry(SceSize args, void *argp)
		{
			int index = 0;
			if(argp && args >= sizeof(int))
				index = *(int *)argp;
			VitaSkinWorkers &workers = Instance();
			while(workers.m_running)
			{
				sceKernelWaitSema(workers.m_start[index], 1, 0);
				if(!workers.m_running)
					break;
				__sync_synchronize();
				RunVitaSkinRange(workers.m_jobs[index]);
				sceKernelSignalSema(workers.m_done, 1);
			}
			return 0;
		}

	public:
		static VitaSkinWorkers &Instance()
		{
			static VitaSkinWorkers workers;
			return workers;
		}

	private:
		SceUID m_thread[2];
		SceUID m_start[2];
		SceUID m_done;
		int m_index[2];
		VitaSkinJob m_jobs[2];
		bool m_ready;
		bool m_attempted;
		volatile bool m_running;
	};
}

void CrySkinFull::buildVitaGather()
{
	if(m_vitaGatherOffsets.size() == m_numDests+1 &&
	   m_vitaGatherLinks.size() == m_arrVertices.size())
		return;

	std::vector<unsigned> counts(m_numDests, 0);
	u32 s = 0, t = 0;
	for(unsigned nBone=m_numSkipBones; nBone<m_numBones; ++nBone)
	{
		const u32 rigid = m_arrAux[t++];
		for(u32 i=0; i<rigid; ++i, ++s)
			++counts[m_arrVertices[s].nDest];
		const u32 smoothFirst = m_arrAux[t++];
		for(u32 i=0; i<smoothFirst; ++i, ++s)
			++counts[m_arrAux[t++]];
		const u32 smoothMore = m_arrAux[t++];
		for(u32 i=0; i<smoothMore; ++i, ++s)
			++counts[m_arrAux[t++]];
	}

	m_vitaGatherOffsets.resize(m_numDests+1);
	m_vitaGatherOffsets[0] = 0;
	for(unsigned i=0; i<m_numDests; ++i)
		m_vitaGatherOffsets[i+1] = m_vitaGatherOffsets[i] + counts[i];
	m_vitaGatherLinks.resize(m_arrVertices.size());
	std::vector<unsigned> cursor(m_vitaGatherOffsets.begin(), m_vitaGatherOffsets.end()-1);

	s = 0; t = 0;
	for(unsigned nBone=m_numSkipBones; nBone<m_numBones; ++nBone)
	{
		const u32 rigid = m_arrAux[t++];
		for(u32 i=0; i<rigid; ++i, ++s)
		{
			const unsigned nDest = m_arrVertices[s].nDest;
			VitaSkinGatherLink &link = m_vitaGatherLinks[cursor[nDest]++];
			link.nVertex = s;
			link.nBoneAndRigid = (u16)(nBone | VITA_SKIN_RIGID_BIT);
		}
		const u32 smoothFirst = m_arrAux[t++];
		for(u32 i=0; i<smoothFirst; ++i, ++s)
		{
			const unsigned nDest = m_arrAux[t++];
			VitaSkinGatherLink &link = m_vitaGatherLinks[cursor[nDest]++];
			link.nVertex = s;
			link.nBoneAndRigid = (u16)nBone;
		}
		const u32 smoothMore = m_arrAux[t++];
		for(u32 i=0; i<smoothMore; ++i, ++s)
		{
			const unsigned nDest = m_arrAux[t++];
			VitaSkinGatherLink &link = m_vitaGatherLinks[cursor[nDest]++];
			link.nVertex = s;
			link.nBoneAndRigid = (u16)nBone;
		}
	}
}
#endif

// takes each offset and includes it into the bbox of corresponding bone
/*void CrySkinFull::computeBoneBBoxes(CryBBoxA16* pBBoxes)
{
	CrySkinAuxInt* pAux = &m_arrAux[0];
	Vertex* pVertex = &m_arrVertices[0];
	CryBBoxA16* pBBox = pBBoxes + m_numSkipBones, *pBBoxEnd = pBBoxes + m_numBones;

	for (; pBBox!= pBBoxEnd; ++pBBox)
	{
		// each bone has a group of vertices

		// first process the rigid vertices
		Vertex* pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex)
			pBBox->include(pVertex->pt);

		// process the smooth1 vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
			pBBox->include(pVertex->pt);

		// process the smooth vertices that were the second time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
			pBBox->include(pVertex->pt);
	}
}*/

//////////////////////////////////////////////////////////////////////////
// does the skinning out of the given array of global matrices
void CrySkinFull::skin (const Matrix44* pBones, Vec3d* pDest)
{
#ifdef DEFINE_PROFILER_FUNCTION
	DEFINE_PROFILER_FUNCTION();
#endif

#if defined(__vita__) || defined(LINUX)
	/* Bone-major skinning cannot be sliced safely: smooth links accumulate into
	   shared destinations and both packed streams have serial cursors.  The
	   cached gather map reverses that relation, so each worker exclusively owns
	   a destination range and produces byte-for-byte compatible weighted sums. */
	if(m_numDests >= VITA_SKIN_MIN_DESTS && m_arrVertices.size() >= VITA_SKIN_MIN_LINKS)
	{
		buildVitaGather();
		VitaSkinWorkers &workers = VitaSkinWorkers::Instance();
		if(workers.initialize())
		{
			const unsigned split1 = m_numDests/3;
			const unsigned split2 = (m_numDests*2)/3;
			VitaSkinJob jobs[3];
			const unsigned begins[3] = { 0, split1, split2 };
			const unsigned ends[3] = { split1, split2, m_numDests };
			for(int i=0; i<3; ++i)
			{
				jobs[i].pBones = pBones;
				jobs[i].pVertices = &m_arrVertices[0];
				jobs[i].pLinks = &m_vitaGatherLinks[0];
				jobs[i].pOffsets = &m_vitaGatherOffsets[0];
				jobs[i].pDest = pDest;
				jobs[i].nDestStride = sizeof(Vec3d);
				jobs[i].nBegin = begins[i];
				jobs[i].nEnd = ends[i];
				jobs[i].bTranslate = true;
			}
			workers.run(jobs);
			return;
		}
	}
#endif

	//PROFILE_FRAME_SELF(PureSkin);
#if FOR_TEST
	for (int i = 0; i < g_GetCVars()->ca_TestSkinningRepeats(); ++i)
#endif
	{
	
	const Matrix44* pBone			= pBones + m_numSkipBones;
	const Matrix44* pBonesEnd = pBones + m_numBones;

	u32 s = 0;
	u32 t = 0;

#ifdef _DEBUG
	TFixedArray<float> arrW;
	arrW.reinit(m_numDests, 0);
#endif

	for (; pBone!= pBonesEnd; ++pBone)
	{

		Matrix34 m34 = Matrix34( GetTransposed44(*pBone) );

		// first process the rigid vertices
		u32 a0=m_arrAux[t];
		for (u32 i=0; i<a0; i++ )
		{
			//_mm_prefetch( (char*)&m_arrVertices[s+20].pt, _MM_HINT_T0 );
			pDest[m_arrVertices[s].nDest] = m34 * m_arrVertices[s].pt;

			#ifdef _DEBUG
				assert (arrW[m_arrVertices[s].nDest] == 0);
				arrW[m_arrVertices[s].nDest] = 1;
			#endif
			s++;
		}
		t++;

		// process the smooth1 vertices that were the first time met
		u32 a1=m_arrAux[t]; t++;
		for (u32 i=0; i<a1; i++ )
		{
			//_mm_prefetch( (char*)&m_arrVertices[s+20].pt, _MM_HINT_T0 );
			pDest[m_arrAux[t]]= (m34*m_arrVertices[s].pt) * m_arrVertices[s].fWeight;
			
			#ifdef _DEBUG
				assert (arrW[m_arrAux[t]] == 0);
				arrW[m_arrAux[t]] = m_arrVertices[s].fWeight;
			#endif
			s++;
			t++;
		}

		// process the smooth vertices that were the first time met
		u32 a2=m_arrAux[t]; t++;
		for (u32 i=0; i<a2; i++)
		{
			//_mm_prefetch( (char*)&m_arrVertices[s+20].pt, _MM_HINT_T0 );
			pDest[m_arrAux[t]] += (m34*m_arrVertices[s].pt) * m_arrVertices[s].fWeight;
			
			#ifdef _DEBUG
				assert (arrW[m_arrAux[t]] > 0 && arrW[m_arrAux[t]] < 1.005f);
				arrW[m_arrAux[t]] += m_arrVertices[s].fWeight;
				assert (arrW[m_arrAux[t]] > 0 && arrW[m_arrAux[t]] < 1.005f);
			#endif
		 s++;
		 t++;
		}
	}
	
	/*#ifdef _DEBUG
	for (unsigned i = 0; i < m_numDests; ++i)
		assert (arrW[i] > 0.995f && arrW[i] < 1.005f);
	#endif
	*/

	}
}

//////////////////////////////////////////////////////////////////////////
// does the skinning out of the given array of global matrices
void CrySkinFull::skinAsVec3d16 (const Matrix44* pBones, Vec3dA16* pDest)
{

#if defined(__vita__) || defined(LINUX)
	if(m_numDests >= VITA_SKIN_MIN_DESTS && m_arrVertices.size() >= VITA_SKIN_MIN_LINKS)
	{
		buildVitaGather();
		VitaSkinWorkers &workers = VitaSkinWorkers::Instance();
		if(workers.initialize())
		{
			const unsigned split1 = m_numDests/3;
			const unsigned split2 = (m_numDests*2)/3;
			VitaSkinJob jobs[3];
			const unsigned begins[3] = { 0, split1, split2 };
			const unsigned ends[3] = { split1, split2, m_numDests };
			for(int i=0; i<3; ++i)
			{
				jobs[i].pBones = pBones;
				jobs[i].pVertices = &m_arrVertices[0];
				jobs[i].pLinks = &m_vitaGatherLinks[0];
				jobs[i].pOffsets = &m_vitaGatherOffsets[0];
				jobs[i].pDest = pDest;
				jobs[i].nDestStride = sizeof(Vec3dA16);
				jobs[i].nBegin = begins[i];
				jobs[i].nEnd = ends[i];
				jobs[i].bTranslate = false;
			}
			workers.run(jobs);
			return;
		}
	}
#endif

	//PROFILE_FRAME_SELF(PureSkin);
#if FOR_TEST
	for (int i = 0; i < g_GetCVars()->ca_TestSkinningRepeats(); ++i)
#endif
	{
	const Matrix44* pBone = pBones + m_numSkipBones, *pBonesEnd = pBones + m_numBones;
	CrySkinAuxInt* pAux = &m_arrAux[0];
	Vertex* pVertex = &m_arrVertices[0];


#ifdef _DEBUG
	TFixedArray<float> arrW;
	arrW.reinit(m_numDests, 0);
#endif

	for (; pBone!= pBonesEnd; ++pBone)
	{
		// each bone has a group of vertices

		// first process the rigid vertices
		Vertex* pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex)
		{
			//CHANGED_BY_IVO  - INVALID CHANGE, PLEASE REVISE
			pDest[pVertex->nDest].v = pBone->TransformVectorOLD(pVertex->pt);
			// Temporary fixed by Sergiy. A new operation in the Matrix must be made
			//pDest[pVertex->nDest].v = GetTransposed44(*pBone) * (pVertex->pt);
			//transformVectorNoTrans (pDest[pVertex->nDest].v, pVertex->pt, *pBone);
#ifdef _DEBUG
			assert (arrW[pVertex->nDest] == 0);
			arrW[pVertex->nDest] = 1;
#endif
		}

		// process the smooth1 vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			transformWVector (pDest[*pAux].v, *pBone, *pVertex);
#ifdef _DEBUG
			assert (arrW[*pAux] == 0);
			arrW[*pAux] = pVertex->fWeight;
#endif
		}

		// process the smooth vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			addWVector (pDest[*pAux].v, *pBone, *pVertex);
#ifdef _DEBUG
			assert (arrW[*pAux] > 0 && arrW[*pAux] < 1.005f);
			arrW[*pAux] += pVertex->fWeight;
			assert (arrW[*pAux] > 0 && arrW[*pAux] < 1.005f);
#endif
		}
	}
#ifdef _DEBUG
	for (unsigned i = 0; i < m_numDests; ++i)
		assert (arrW[i] > 0.995f && arrW[i] < 1.005f);
#endif
	}
}

void CrySkinFull::CStatistics::addDest(unsigned nDest)
{
	if (arrNumLinks.size() < nDest+1)
		arrNumLinks.resize (nDest+1,0);
	++arrNumLinks[nDest];
	setDests.insert (nDest);
}

void CrySkinFull::CStatistics::initSetDests (const CrySkinFull* pSkin)
{
	const CrySkinAuxInt* pAux = &pSkin->m_arrAux[0];
	const Vertex* pVertex = &pSkin->m_arrVertices[0];

	arrNumLinks.clear();

	for (unsigned nBone = pSkin->m_numSkipBones; nBone < pSkin->m_numBones; ++nBone)
	{
		// each bone has a group of vertices

		// first process the rigid vertices
		const Vertex* pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex)
		{
			unsigned nDest = pVertex->nDest;
			addDest (nDest);
			assert (arrNumLinks[nDest] == 1);
		}

		// process the smooth1 vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			unsigned nDest = *pAux;
			addDest (nDest);
			assert (arrNumLinks[nDest] == 1);
			//pVertex->fWeight is the weight of the vertex
		}

		// process the smooth vertices that were the second/etc time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			unsigned nDest = *pAux;
			addDest (nDest);
			assert (arrNumLinks[nDest] > 1);
			// pVertex->fWeight contains the weight of the vertex
		}
	}	
}

//////////////////////////////////////////////////////////////////////////
// validates the skin against the given geom info
#if defined (_DEBUG)
void CrySkinFull::validate (const ICrySkinSource* pGeometry)
{
	TElementaryArray<unsigned> arrNumLinks ("CrySkinFull::validate.arrNumLinks");
	arrNumLinks.reinit (pGeometry->numVertices(), 0);

	CrySkinAuxInt* pAux = &m_arrAux[0];
	Vertex* pVertex = &m_arrVertices[0];

	for (unsigned nBone = m_numSkipBones; nBone < m_numBones; ++nBone)
	{
		// each bone has a group of vertices

		// first process the rigid vertices
		Vertex* pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex)
		{
			unsigned nDest = pVertex->nDest;
			const CryVertexBinding& rLink = pGeometry->getLink(nDest);
			assert (arrNumLinks[nDest] == 0);
			arrNumLinks[nDest] = 1;
			assert (rLink.size()==1);
			assert (rLink[0].Blending == 1);
			assert (rLink[0].BoneID == nBone);
		}

		// process the smooth1 vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			unsigned nDest = *pAux;
			const CryVertexBinding& rLink = pGeometry->getLink(nDest);
			assert (arrNumLinks[nDest]++ == 0);
			assert (rLink.size()>1);
			float fLegacyWeight = rLink.getBoneWeight(nBone);
			assert (pVertex->fWeight == fLegacyWeight);
		}

		// process the smooth vertices that were the first time met
		pGroupEnd = pVertex + *pAux++;
		for (;pVertex < pGroupEnd; ++pVertex, ++pAux)
		{
			unsigned nDest = *pAux;
			const CryVertexBinding& rLink = pGeometry->getLink(nDest);
			assert (arrNumLinks[nDest]++ > 0);
			assert (arrNumLinks[nDest] <= rLink.size());
			assert (rLink.size()>1);
			float fLegacyWeight = rLink.getBoneWeight(nBone);
			assert (rLink.hasBoneWeight(nBone,pVertex->fWeight));
		}
	}

	for (unsigned nVert = 0; nVert < pGeometry->numVertices(); ++nVert)
		assert (arrNumLinks[nVert] == pGeometry->getLink(nVert).size());
}
#endif

#if ( defined (_CPU_X86) || defined (_CPU_AMD64) ) & !defined(LINUX)

DEFINE_ALIGNED_DATA( CryBBoxA16, CrySkinFull::g_BBox, 32 ); // align by cache line boundaries

#if defined (_CPU_AMD64)
extern "C" void Amd64Skinner(CrySkinAuxInt* pAux, CrySkinVertexAligned* pVertex, Vec3dA16* pDest, const Matrix44* pBone, Vec3dA16* pvMin,const Matrix44* pBoneEnd);
#endif


void CrySkinFull::skinSSE (const Matrix44* pBones, Vec3dA16* pDest)
{	

#ifdef DEFINE_PROFILER_FUNCTION
	DEFINE_PROFILER_FUNCTION();
#endif

	//PROFILE_FRAME_SELF(PureSkin);
	const Matrix44* pBone = pBones + m_numSkipBones, *pBoneEnd = pBones + m_numBones;
	CrySkinAuxInt* pAux = &m_arrAux[0];
	Vertex* pVertex = &m_arrVertices[0];

	// set the bbox to the negative volume to make sure the bbox will calculate starting from the first vertex
	g_BBox.vMin.v = Vec3d(1e6,1e6,1e6);// = pBone->GetTranslation();
	g_BBox.vMax.v = Vec3d(-1e6,-1e6,-1e6);// = pBone->GetTranslation();

#if FOR_TEST
	for (int i = 0; i < g_GetCVars()->ca_TestSkinningRepeats(); ++i)
#endif

#if defined(_CPU_AMD64)
	Amd64Skinner(pAux, pVertex, pDest, pBone, &g_BBox.vMin, pBoneEnd);
#else
		_asm
		{	
			mov EDX, pAux
			mov EBX, pVertex
			mov EDI, pDest
			mov ESI, pBone

			// load the current matrix; we don't need the move component
	startLoop:
			cmp ESI, pBoneEnd
			jz endLoop
			movaps xmm0, [ESI]
			movaps xmm1, [ESI+0x10]
			movaps xmm2, [ESI+0x20]
			movaps xmm3, [ESI+0x30]
			add ESI, 0x40

			// load the counter for the number of non-flipped tangets for this bone
	#if CRY_SKIN_AUX_INT_SIZE==2
			xor ECX,ECX
			mov CX, word ptr [EDX]
			add EDX, 2
	#else
			mov ECX, dword ptr [EDX]
			add EDX, 4
	#endif
			test ECX, ECX
			jz endLoopRigid

	startLoopRigid:
			// load the offset
			movaps xmm7, [EBX]
			// calculate the destination pointer
			mov EAX, [EBX+0xC]
			and EAX, 0xFFFFFF
			add EAX, EAX
			// EDI+EAX*8 points to the destination vector now
			add EBX, 0x10

			// transform the vertex
			movss xmm6, xmm7
			shufps xmm6, xmm6, 0 // xmm6 = 4 copies of offset.x
			mulps xmm6, xmm0
			movaps xmm5, xmm7
			shufps xmm5, xmm5, 0x55 // xmm5 = 4 copies of offset.y
			mulps xmm5, xmm1
			shufps xmm7, xmm7, 0xAA // xmm7 = 4 copies of offset.z
			mulps xmm7, xmm2
			addps xmm7, xmm5
			addps xmm7, xmm6
			addps xmm7, xmm3        // xmm7 = fully transformed vertex, store it
			// xmm7 = transformed vertex
			movaps [EDI+EAX*8], xmm7

			//----------------------
			// Calculation of BBox
			// xmm5 will be the min, xmm6 will be the max of bbox
			movaps xmm5, xmm7
			movaps xmm6, xmm7
			minps xmm5, g_BBox.vMin
			maxps xmm6, g_BBox.vMax
			movaps g_BBox.vMin, xmm5
			movaps g_BBox.vMax, xmm6

			loop startLoopRigid
	endLoopRigid:


//////////////////////////////////////////////////////////
// Smooth-1 loop
// load the counter for the number of smooth vertices met for the first time
//////////////////////////////////////////////////////////
	#if CRY_SKIN_AUX_INT_SIZE==2
			xor ECX,ECX
			mov CX, word ptr [EDX]
			add EDX, 2
	#else
			mov ECX, dword ptr [EDX]
			add EDX, 4
	#endif
			test ECX, ECX
			jz endLoopSmooth1

	startLoopSmooth1:
			// load the offset & blending
			movaps xmm7, [EBX]
			// calculate the destination pointer
	#if CRY_SKIN_AUX_INT_SIZE==2
			xor EAX,EAX
			mov AX, word ptr [EDX]
			add EDX, 2
	#else
			mov EAX, dword ptr [EDX]
			add EDX, 4
	#endif
			add EAX, EAX
			// EDI+EAX*8 points to the destination vector now
			add EBX, 0x10

			// transform the vertex
			movss xmm6, xmm7
			shufps xmm6, xmm6, 0 // xmm6 = 4 copies of offset.x
			mulps xmm6, xmm0
			movaps xmm5, xmm7
			shufps xmm5, xmm5, 0x55 // xmm5 = 4 copies of offset.y
			mulps xmm5, xmm1
			movaps xmm4, xmm7
			shufps xmm4, xmm4, 0xAA // xmm4 = 4 copies of offset.z
			mulps xmm4, xmm2
			addps xmm4, xmm5
			addps xmm4, xmm6
			addps xmm4, xmm3        // xmm4 = fully transformed vertex, blend it
			shufps xmm7, xmm7, 0xFF // xmm7 = 4 copies of blending
			mulps xmm7, xmm4
			// xmm7 = transformed and blended vertex
			movaps [EDI+EAX*8], xmm7

			loop startLoopSmooth1
			//loop startLoopNonflipped
	endLoopSmooth1:



//////////////////////////////////////////////////////////
// Smooth-2 loop
// load the counter for the number of smooth vertices met for the second time
//////////////////////////////////////////////////////////
	#if CRY_SKIN_AUX_INT_SIZE==2
			xor ECX,ECX
			mov CX, word ptr [EDX]
			add EDX, 2
	#else
			mov ECX, dword ptr [EDX]
			add EDX, 4
	#endif
			test ECX, ECX
			jz endLoopSmooth2

	startLoopSmooth2:
			// load the offset & blending
			movaps xmm7, [EBX]
			// calculate the destination pointer
	#if CRY_SKIN_AUX_INT_SIZE==2
			xor EAX,EAX
			mov AX, word ptr [EDX]
			add EDX, 2
	#else
			mov EAX, dword ptr [EDX]
			add EDX, 4
	#endif
			shl EAX, 4
			add EAX, EDI
			// EAX points to the destination vector now
			add EBX, 0x10

			// transform the vertex
			movss xmm6, xmm7
			shufps xmm6, xmm6, 0 // xmm6 = 4 copies of offset.x
			mulps xmm6, xmm0
			movaps xmm5, xmm7
			shufps xmm5, xmm5, 0x55 // xmm5 = 4 copies of offset.y
			mulps xmm5, xmm1
			movaps xmm4, xmm7
			shufps xmm4, xmm4, 0xAA // xmm4 = 4 copies of offset.z
			mulps xmm4, xmm2
			addps xmm4, xmm5
			addps xmm4, xmm6
			addps xmm4, xmm3        // xmm4 = fully transformed vertex, blend it
			shufps xmm7, xmm7, 0xFF // xmm7 = 4 copies of blending
			mulps xmm7, xmm4
			// xmm7 = transformed and blended vertex
			addps xmm7, [EAX]
			movaps [EAX], xmm7

			loop startLoopSmooth2
			//loop startLoopNonflipped
	endLoopSmooth2:

			jmp startLoop
	endLoop:
		}
#endif		// _CPU_AMD64
}
#endif
