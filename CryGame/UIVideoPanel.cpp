
//////////////////////////////////////////////////////////////////////
//
//	Crytek Source code 
//	Copyright (c) Crytek 2001-2004
//
//  File: UIVideoPanel.cpp
//  Description: UI Video Panel Manager
//
//  History:
//  - [9/7/2003]: File created by Márcio Martins
//	- February 2005: Modified by Marco Corbetta for SDK release
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "UIVideoPanel.h"
#include "UISystem.h"

#if !defined(NOT_USE_DIVX_SDK)
#include "UIDivX_Video.h"
#endif

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
#pragma comment(lib, "binkw32.lib")
#endif

_DECLARE_SCRIPTABLEEX(CUIVideoPanel)

//////////////////////////////////////////////////////////////////////
static bool g_bBinkInit = 0;

////////////////////////////////////////////////////////////////////// 
CUIVideoPanel::CUIVideoPanel()
:
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	m_hBink(0),
#endif
	m_bLooping(1), m_bPlaying(0), m_bPaused(0), m_iTextureID(-1), m_pSwapBuffer(0), m_szVideoFile(""), m_bKeepAspect(1)
#if defined(__vita__)
	, m_nVitaSourceWidth(0), m_nVitaSourceHeight(0), m_nVitaWidth(0), m_nVitaHeight(0),
	  m_nVitaNumFrames(0), m_fVitaFrameRate(30.0f), m_fVitaNextFrameTime(0.0f),
	  m_bVitaFinishPending(false), m_pVitaDecodeBuffer(0), m_nVitaDecodeThread(-1),
	  m_nVitaWorkerRun(0), m_nVitaWorkerPlay(0), m_nVitaVideoRead(0), m_nVitaVideoWrite(0),
	  m_nVitaLastDecodeUs(0), m_nVitaLastConvertUs(0), m_nVitaPresentedFrames(0),
	  m_bVitaPresentationStarted(false), m_nVitaWorkerEOF(0),
	  m_pVitaAudioStream(0), m_nVitaAudioChannel(-1), m_bVitaAudioEnabled(false),
	  m_nVitaAudioTrack(0), m_nVitaAudioChannels(0), m_nVitaAudioScratchBytes(0),
	  m_pVitaAudioScratch(0), m_pVitaAudioRing(0), m_nVitaAudioRingBytes(0),
	  m_nVitaAudioPrebufferBytes(0), m_nVitaAudioRead(0), m_nVitaAudioWrite(0),
	  m_nVitaAudioUnderruns(0), m_nVitaAvHandle(-1), m_nVitaAvFrameIndex(0),
	  m_nVitaAvIdleTicks(0), m_nVitaAvAudioThread(-1), m_nVitaAvAudioPort(-1),
	  m_nVitaAvVolume(32768),
	  m_nVitaAvAudioRun(0), m_bVitaAvActive(false), m_bVitaAvStarted(false),
	  m_bVitaAvFirstFrame(false), m_bVitaAvSound(false)
#endif
{
	m_DivX_Active=0;
#if defined(__vita__)
	m_VitaBink.isValid = false;
	m_VitaBink.instanceIndex = -1;
	memset(m_arrVitaAvTextureIds, 0, sizeof(m_arrVitaAvTextureIds));
	memset(m_arrVitaAvGxmTextures, 0, sizeof(m_arrVitaAvGxmTextures));
	memset(m_arrVitaAvOwnedData, 0, sizeof(m_arrVitaAvOwnedData));
#endif
}

#if defined(__vita__)
/* NOTE for anyone tempted to add path fallbacks here: do not call Bink_Open
   repeatedly to try alternative paths.  libbinkdec allocates from a fixed
   instance pool and a failed open still consumes a slot, so probing several
   candidates exhausts the pool and takes the whole app down.  Resolve the path
   before opening, and open exactly once. */

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>
#include <crysound.h>

//! Cheap existence test used to locate a video before opening it.  Deliberately
//! not Bink_Open: a failed open still consumes one of libbinkdec's fixed
//! instance-pool slots, so searching with it exhausts the pool.
static bool VitaFileExists(const char *szPath)
{
	struct stat st;
	return szPath && stat(szPath, &st) == 0;
}

/* Hardware cutscene playback follows the upstream vitaGL video_playback
   sample: sceAvPlayer decodes H.264 into GPU-ready YVU420 buffers, which are
   attached directly to vitaGL texture objects.  There is no CPU colourswizzle,
   full-frame upload, or software codec work in the game process. */
static void *VitaAvAllocCPU(void *, uint32_t nAlign, uint32_t nSize)
{
	if (nAlign < 16)
		nAlign = 16;
	return memalign(nAlign, nSize);
}

static void VitaAvFreeCPU(void *, void *pMemory)
{
	free(pMemory);
}

/* Physically contiguous memory is a small pool of its own, and vitaGL has
   already reserved a slice of it, so the decoder's frame buffers cannot rely on
   it being there.  Returning 0 to sceAvPlayer is not a recoverable error for
   the caller either: the player stays "active" and simply never produces a
   frame, which presents as a black screen rather than a failed open.  Try
   phycont first, since it is what the video decoder prefers, then fall back to
   ordinary GPU-mappable memory. */
static void *VitaAvAllocGPUFrom(SceKernelMemBlockType nType, uint32_t nAlign,
	uint32_t nSize)
{
	nSize = (nSize + nAlign - 1) & ~(nAlign - 1);
	SceUID nBlock = sceKernelAllocMemBlock("fc_av_frame", nType, nSize, 0);
	if (nBlock < 0)
		return 0;
	void *pBase = 0;
	if (sceKernelGetMemBlockBase(nBlock, &pBase) < 0 || !pBase)
	{
		sceKernelFreeMemBlock(nBlock);
		return 0;
	}
	if (sceGxmMapMemory(pBase, nSize, SCE_GXM_MEMORY_ATTRIB_RW) < 0)
	{
		sceKernelFreeMemBlock(nBlock);
		return 0;
	}
	return pBase;
}

static void *VitaAvAllocGPU(void *, uint32_t, uint32_t nSize)
{
	void *pMemory = VitaAvAllocGPUFrom(
		SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, 1024 * 1024, nSize);
	if (!pMemory)
		pMemory = VitaAvAllocGPUFrom(
			SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 256 * 1024, nSize);
	return pMemory;
}

static void VitaAvFreeGPU(void *, void *pMemory)
{
	if (!pMemory)
		return;
	glFinish();
	SceUID nBlock = sceKernelFindMemBlockByAddr(pMemory, 0);
	sceGxmUnmapMemory(pMemory);
	if (nBlock >= 0)
		sceKernelFreeMemBlock(nBlock);
}

int CUIVideoPanel::VitaAvAudioThread(unsigned int, void *pArgs)
{
	CUIVideoPanel *pThis = *(CUIVideoPanel **)pArgs;
	int nConfiguredRate = 0;
	int nConfiguredMode = -1;
	while (pThis && pThis->m_nVitaAvAudioRun)
	{
		if (!pThis->m_bPlaying || pThis->m_bPaused ||
			!pThis->m_bVitaAvStarted || !sceAvPlayerIsActive(pThis->m_nVitaAvHandle))
		{
			sceKernelDelayThread(1000);
			continue;
		}
		SceAvPlayerFrameInfo frame;
		memset(&frame, 0, sizeof(frame));
		if (!sceAvPlayerGetAudioData(pThis->m_nVitaAvHandle, &frame))
		{
			sceKernelDelayThread(1000);
			continue;
		}
		const int nMode = frame.details.audio.channelCount == 1 ?
			SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;
		const int nRate = (int)frame.details.audio.sampleRate;
		if (nRate != nConfiguredRate || nMode != nConfiguredMode)
		{
			sceAudioOutSetConfig(pThis->m_nVitaAvAudioPort, 1024, nRate, nMode);
			nConfiguredRate = nRate;
			nConfiguredMode = nMode;
		}
		sceAudioOutOutput(pThis->m_nVitaAvAudioPort, frame.pData);
	}
	return 0;
}

/* sceAvPlayerInit returns an opaque handle that is in practice a pointer into
   user memory (0x8296c340 on this device), and SceAvPlayerHandle is typed int --
   so the sign bit is set on every successful call and the obvious "handle < 0"
   error test rejects all of them.  That single mistake is why no cutscene ever
   reached the hardware decoder, and why the players leaked: VitaAvPlayerClose
   guarded on "handle >= 0" too, so it never closed the one it had just made.
   Sony's own failures are SCE_AVPLAYER_ERROR_* in the 0x806Axxxx block, below
   the user memory window, and the not-open sentinel is -1; both fall outside
   the range checked here. */
static inline bool VitaAvHandleValid(int nHandle)
{
	const unsigned int nValue = (unsigned int)nHandle;
	return nValue >= 0x81000000u && nValue < 0xF0000000u;
}

/* Converted H.264/AAC sidecars are shipped with the Vita data kit.  Prefer the
   hardware decoder: the software Bink path takes 45-145 ms per frame on retail
   hardware and consequently starves both its video queue and audio ring.  Keep
   the cvar so a user can explicitly select the software fallback for diagnosis. */
static ICVar *s_pVitaHardwareVideo = 0;
static bool VitaHardwareVideoEnabled(CUISystem *pUISystem)
{
	if (!s_pVitaHardwareVideo && pUISystem && pUISystem->GetISystem() &&
		pUISystem->GetISystem()->GetIConsole())
		s_pVitaHardwareVideo = pUISystem->GetISystem()->GetIConsole()->CreateVariable(
			"v_vita_hardware_video", "1", 0,
			"Play .mp4 sidecars through sceAvPlayer instead of the software Bink decoder");
	return s_pVitaHardwareVideo && s_pVitaHardwareVideo->GetIVal() != 0;
}

void CUIVideoPanel::VitaAvLog(const char *szFormat, ...)
{
	if (!m_pUISystem || !m_pUISystem->GetISystem() ||
		!m_pUISystem->GetISystem()->GetILog())
		return;
	char szText[512];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szText, sizeof(szText), szFormat, args);
	va_end(args);
	m_pUISystem->GetISystem()->GetILog()->LogToFile("\001[VITA][VIDEO] %s", szText);
}

bool CUIVideoPanel::VitaAvPlayerOpen(const char *szPath, bool bSound)
{
	if (!szPath || !VitaFileExists(szPath))
		return false;
	/* m_nVitaAvHandle is the only reference to a live player, so initialising a
	   second one over the top strands the first: its controller thread keeps
	   running and its GPU frame buffers are never returned.  Stop() cannot undo
	   that either, so close here before taking a new handle. */
	if (m_bVitaAvActive || VitaAvHandleValid(m_nVitaAvHandle))
		VitaAvPlayerClose();
	static bool s_bAvModuleLoaded = false;
	if (!s_bAvModuleLoaded)
	{
		const int nLoad = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
		if (nLoad < 0)
			return false;
		s_bAvModuleLoaded = true;
	}

	SceAvPlayerInitData init;
	memset(&init, 0, sizeof(init));
	init.memoryReplacement.allocate = VitaAvAllocCPU;
	init.memoryReplacement.deallocate = VitaAvFreeCPU;
	init.memoryReplacement.allocateTexture = VitaAvAllocGPU;
	init.memoryReplacement.deallocateTexture = VitaAvFreeGPU;
	init.basePriority = 0xA0;
	init.numOutputVideoFrameBuffers = kVitaAvFrameBuffers;
	init.autoStart = SCE_FALSE;
	m_nVitaAvHandle = sceAvPlayerInit(&init);
	if (!VitaAvHandleValid(m_nVitaAvHandle))
	{
		VitaAvLog("sceAvPlayerInit failed 0x%08x for '%s'",
			(unsigned)m_nVitaAvHandle, szPath);
		return false;
	}
	const int nSourceResult = sceAvPlayerAddSource(m_nVitaAvHandle, szPath);
	if (nSourceResult < 0)
	{
		/* Init already spawned the controller thread, so this has to go through
		   the full close, not just sceAvPlayerClose -- otherwise every rejected
		   sidecar strands a thread and its buffers for the session. */
		VitaAvLog("sceAvPlayerAddSource failed 0x%08x for '%s' -- falling back to Bink",
			(unsigned)nSourceResult, szPath);
		VitaAvPlayerClose();
		return false;
	}

	glGenTextures(kVitaAvFrameBuffers, (GLuint *)m_arrVitaAvTextureIds);
	for (unsigned int i = 0; i < kVitaAvFrameBuffers; ++i)
	{
		if (!m_arrVitaAvTextureIds[i])
		{
			VitaAvLog("glGenTextures gave no name for frame buffer %u -- "
				"falling back to Bink", i);
			VitaAvPlayerClose();
			return false;
		}
		glBindTexture(GL_TEXTURE_2D, (GLuint)m_arrVitaAvTextureIds[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, 0);
		m_arrVitaAvGxmTextures[i] = vglGetGxmTexture(GL_TEXTURE_2D);
		/* Keep this 8x8 allocation rather than releasing it here.  It is only
		   256 bytes, and holding it means the texture always has a vitaGL-owned
		   buffer to hand back at close time -- both while no frame has arrived
		   yet and after the frame loop has re-pointed it at the player. */
		m_arrVitaAvOwnedData[i] = vglGetTexDataPointer(GL_TEXTURE_2D);
		if (!m_arrVitaAvGxmTextures[i])
		{
			VitaAvLog("vglGetGxmTexture returned NULL for frame buffer %u -- "
				"falling back to Bink", i);
			VitaAvPlayerClose();
			return false;
		}
	}

	m_iTextureID = -1;
	m_nVitaWidth = 720;
	m_nVitaHeight = 408;
	m_nVitaSourceWidth = m_nVitaWidth;
	m_nVitaSourceHeight = m_nVitaHeight;
	m_nVitaAvFrameIndex = kVitaAvFrameBuffers - 1;
	m_nVitaAvIdleTicks = 0;
	m_nVitaPresentedFrames = 0;
	m_nVitaAvAudioThread = -1;
	m_nVitaAvAudioPort = -1;
	m_nVitaAvAudioRun = 0;
	m_bVitaAvActive = true;
	m_bVitaAvStarted = false;
	m_bVitaAvFirstFrame = false;
	m_bVitaAvSound = bSound;
	m_bVitaAudioEnabled = bSound;
	if (m_pUISystem && m_pUISystem->GetISystem() && m_pUISystem->GetISystem()->GetILog())
		m_pUISystem->GetISystem()->GetILog()->LogToFile(
			"\001[VITA][VIDEO] hardware H264/AAC source='%s' buffers=%u",
			szPath, kVitaAvFrameBuffers);
	return true;
}

bool CUIVideoPanel::VitaAvPlayerAdvanceFrame()
{
	if (!m_bVitaAvActive || !VitaAvHandleValid(m_nVitaAvHandle))
		return false;
	if (sceAvPlayerIsActive(m_nVitaAvHandle))
	{
		SceAvPlayerFrameInfo frame;
		memset(&frame, 0, sizeof(frame));
		if (!sceAvPlayerGetVideoData(m_nVitaAvHandle, &frame))
		{
			/* Active but starving.  The idle counter used to be cleared here on
			   every tick, which made this branch an unbounded wait: if the
			   decoder could not allocate its buffers it stayed active forever,
			   produced nothing, and the panel sat black with the menu behind it
			   never coming up.  Only a real frame clears the counter now. */
			if (++m_nVitaAvIdleTicks >= 150)
			{
				VitaAvLog("no video frame after %u ticks -- giving up on the "
					"hardware path for this clip", (unsigned)m_nVitaAvIdleTicks);
				return false;
			}
			return true;
		}
		m_nVitaAvIdleTicks = 0;
		{
			m_nVitaAvFrameIndex = (m_nVitaAvFrameIndex + 1) % kVitaAvFrameBuffers;
			SceGxmTexture *pTexture =
				(SceGxmTexture *)m_arrVitaAvGxmTextures[m_nVitaAvFrameIndex];
			sceGxmTextureInitLinear(pTexture, frame.pData,
				SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
				frame.details.video.width, frame.details.video.height, 0);
			sceGxmTextureSetMinFilter(pTexture, SCE_GXM_TEXTURE_FILTER_LINEAR);
			sceGxmTextureSetMagFilter(pTexture, SCE_GXM_TEXTURE_FILTER_LINEAR);
			m_nVitaWidth = (int)frame.details.video.width;
			m_nVitaHeight = (int)frame.details.video.height;
			m_nVitaSourceWidth = m_nVitaWidth;
			m_nVitaSourceHeight = m_nVitaHeight;
			m_iTextureID = (int)m_arrVitaAvTextureIds[m_nVitaAvFrameIndex];
			m_bVitaAvFirstFrame = true;
			++m_nVitaPresentedFrames;
			if ((m_nVitaPresentedFrames % 120) == 0 && m_pUISystem &&
				m_pUISystem->GetISystem() && m_pUISystem->GetISystem()->GetILog())
				m_pUISystem->GetISystem()->GetILog()->LogToFile(
					"\001[VITA][VIDEO] hardware frame=%u time=%llums %dx%d",
					m_nVitaPresentedFrames,
					(unsigned long long)sceAvPlayerCurrentTime(m_nVitaAvHandle),
					m_nVitaWidth, m_nVitaHeight);
		}
		return true;
	}
	if (m_bVitaAvFirstFrame)
		return false;
	/* The player reports inactive briefly while its worker opens and probes the
	   MP4.  Bound that grace period so a corrupt sidecar cannot hang the UI. */
	return ++m_nVitaAvIdleTicks < 300;
}

void CUIVideoPanel::VitaAvPlayerClose()
{
	m_nVitaAvAudioRun = 0;
	if (m_nVitaAvAudioThread >= 0)
	{
		sceKernelWaitThreadEnd(m_nVitaAvAudioThread, 0, 0);
		sceKernelDeleteThread(m_nVitaAvAudioThread);
		m_nVitaAvAudioThread = -1;
	}
	if (m_nVitaAvAudioPort >= 0)
	{
		sceAudioOutReleasePort(m_nVitaAvAudioPort);
		m_nVitaAvAudioPort = -1;
	}
	if (VitaAvHandleValid(m_nVitaAvHandle))
	{
		if (m_bVitaAvStarted)
			sceAvPlayerStop(m_nVitaAvHandle);
		sceAvPlayerClose(m_nVitaAvHandle);
		m_nVitaAvHandle = -1;
	}
	/* Point every texture back at the buffer vitaGL allocated for it before
	   deleting it.  By now sceAvPlayerClose has released the decoder's frame
	   buffers, and vitaGL frees whatever sceGxmTextureGetData reports -- so
	   without this it frees the player's memory instead of its own and corrupts
	   the pool for everything that allocates afterwards. */
	for (unsigned int i = 0; i < kVitaAvFrameBuffers; ++i)
	{
		if (m_arrVitaAvGxmTextures[i] && m_arrVitaAvOwnedData[i])
			sceGxmTextureInitLinear((SceGxmTexture *)m_arrVitaAvGxmTextures[i],
				m_arrVitaAvOwnedData[i], SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
				8, 8, 0);
	}
	if (m_arrVitaAvTextureIds[0])
		glDeleteTextures(kVitaAvFrameBuffers, (GLuint *)m_arrVitaAvTextureIds);
	memset(m_arrVitaAvTextureIds, 0, sizeof(m_arrVitaAvTextureIds));
	memset(m_arrVitaAvGxmTextures, 0, sizeof(m_arrVitaAvGxmTextures));
	memset(m_arrVitaAvOwnedData, 0, sizeof(m_arrVitaAvOwnedData));
	m_iTextureID = -1;
	m_bVitaAvActive = false;
	m_bVitaAvStarted = false;
	m_bVitaAvFirstFrame = false;
	m_bVitaAvSound = false;
}

//! Bink hands frames back as planar YUV with the chroma planes at half
//! resolution; the renderer wants packed RGBA, so convert as we copy.
static void VitaBinkYUVToRGBA(const YUVbuffer yuv, int *pDest,
	int srcWidth, int srcHeight, int dstWidth, int dstHeight)
{
	const ImagePlane &planeY = yuv[0];
	const ImagePlane &planeU = yuv[1];
	const ImagePlane &planeV = yuv[2];
	if (!planeY.data || !planeU.data || !planeV.data)
		return;

	/* Saturating lookup, built once.  The three clamps were six unpredictable
	   branches per pixel; at video resolution that is millions of mispredicts a
	   second on an in-order ARM core. */
	static uint8_t s_arrClamp[1024];
	static bool s_bClampReady = false;
	if (!s_bClampReady)
	{
		for (int i = 0; i < 1024; ++i)
		{
			int v = i - 384;
			s_arrClamp[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
		}
		s_bClampReady = true;
	}

	for (int y = 0; y < dstHeight; ++y)
	{
		const int sy = (y * srcHeight) / dstHeight;
		const uint8_t *rowY = planeY.data + (size_t)sy * planeY.pitch;
		const uint8_t *rowU = planeU.data + (size_t)(sy >> 1) * planeU.pitch;
		const uint8_t *rowV = planeV.data + (size_t)(sy >> 1) * planeV.pitch;
		int *rowDest = pDest + (size_t)y * dstWidth;

		for (int x = 0; x < dstWidth; ++x)
		{
			const int sx = (x * srcWidth) / dstWidth;
			const int nChroma = sx >> 1;
			const int d = (int)rowU[nChroma] - 128;
			const int e = (int)rowV[nChroma] - 128;

			const int rTerm =  409 * e + 128;
			const int gTerm = -100 * d - 208 * e + 128;
			const int bTerm =  516 * d + 128;

			const int nLuma = 298 * ((int)rowY[sx] - 16);
			const unsigned r = s_arrClamp[((nLuma + rTerm) >> 8) + 384];
			const unsigned g = s_arrClamp[((nLuma + gTerm) >> 8) + 384];
			const unsigned b = s_arrClamp[((nLuma + bTerm) >> 8) + 384];
			rowDest[x] = (int)(0xFF000000u | (b << 16) | (g << 8) | r);
		}
	}
}

signed char CUIVideoPanel::VitaAudioCallback(CS_STREAM *, void *pBuffer, int nBytes, int nParam)
{
	CUIVideoPanel *pThis = (CUIVideoPanel *)nParam;
	if (!pThis || !pBuffer || nBytes <= 0 || !pThis->m_pVitaAudioRing)
		return 0;
	const unsigned int nRead = pThis->m_nVitaAudioRead;
	if (pThis->m_nVitaAudioWrite - nRead < (unsigned int)nBytes)
	{
		/* Keep the CrySound stream clock alive during a rare decoder miss.  The
		   old zero return caused the stream itself to repeatedly stop/start and
		   made a short underrun sound like the entire cutscene audio had broken. */
		memset(pBuffer, 0, nBytes);
		++pThis->m_nVitaAudioUnderruns;
		return 1;
	}
	unsigned char *pDst = (unsigned char *)pBuffer;
	const unsigned int nOffset = nRead % pThis->m_nVitaAudioRingBytes;
	const unsigned int nFirst = min((unsigned int)nBytes,
		pThis->m_nVitaAudioRingBytes - nOffset);
	memcpy(pDst, pThis->m_pVitaAudioRing + nOffset, nFirst);
	if (nFirst < (unsigned int)nBytes)
		memcpy(pDst + nFirst, pThis->m_pVitaAudioRing, (unsigned int)nBytes - nFirst);
	__sync_synchronize();
	pThis->m_nVitaAudioRead = nRead + (unsigned int)nBytes;
	return 1;
}

int CUIVideoPanel::VitaDecodeThread(unsigned int, void *pArgs)
{
	CUIVideoPanel *pThis = *(CUIVideoPanel **)pArgs;
	while (pThis && pThis->m_nVitaWorkerRun)
	{
		if (!pThis->m_nVitaWorkerPlay ||
			pThis->m_nVitaVideoWrite - pThis->m_nVitaVideoRead >= kVitaVideoQueueFrames)
		{
			sceKernelDelayThread(1000);
			continue;
		}
		const uint32_t nCurrent = Bink_GetCurrentFrameNum(pThis->m_VitaBink);
		if (pThis->m_nVitaNumFrames > 0 && (int)nCurrent >= pThis->m_nVitaNumFrames)
		{
			if (pThis->m_bLooping)
				Bink_GotoFrame(pThis->m_VitaBink, 0);
			else
			{
				pThis->m_nVitaWorkerEOF = 1;
				pThis->m_nVitaWorkerPlay = 0;
				continue;
			}
		}

		YUVbuffer yuv;
		memset(yuv, 0, sizeof(yuv));
		const SceUInt64 nDecodeStartUs = sceKernelGetProcessTimeWide();
		if (pThis->m_bVitaAudioEnabled && pThis->m_nVitaAudioChannels)
			Bink_GetNextFrame(pThis->m_VitaBink, yuv);
		else
			Bink_GetNextFrameVideoOnly(pThis->m_VitaBink, yuv);
		const SceUInt64 nConvertStartUs = sceKernelGetProcessTimeWide();

		const unsigned int nVideoWrite = pThis->m_nVitaVideoWrite;
		const unsigned int nFramePixels = pThis->m_nVitaWidth * pThis->m_nVitaHeight;
		int *pFrame = pThis->m_pVitaDecodeBuffer +
			(nVideoWrite % kVitaVideoQueueFrames) * nFramePixels;
		VitaBinkYUVToRGBA(yuv, pFrame,
			pThis->m_nVitaSourceWidth, pThis->m_nVitaSourceHeight,
			pThis->m_nVitaWidth, pThis->m_nVitaHeight);
		const SceUInt64 nConvertEndUs = sceKernelGetProcessTimeWide();
		pThis->m_nVitaLastDecodeUs = (unsigned int)(nConvertStartUs - nDecodeStartUs);
		pThis->m_nVitaLastConvertUs = (unsigned int)(nConvertEndUs - nConvertStartUs);

		if (pThis->m_bVitaAudioEnabled && pThis->m_pVitaAudioScratch && pThis->m_pVitaAudioRing)
		{
			unsigned int nBytes = Bink_GetAudioData(pThis->m_VitaBink,
				pThis->m_nVitaAudioTrack, (int16_t *)pThis->m_pVitaAudioScratch);
			const unsigned int nUsed = pThis->m_nVitaAudioWrite - pThis->m_nVitaAudioRead;
			if (nBytes > pThis->m_nVitaAudioRingBytes - nUsed)
				nBytes = pThis->m_nVitaAudioRingBytes - nUsed;
			nBytes -= nBytes % (pThis->m_nVitaAudioChannels * 2);
			const unsigned int nWrite = pThis->m_nVitaAudioWrite;
			const unsigned int nOffset = nWrite % pThis->m_nVitaAudioRingBytes;
			const unsigned int nFirst = min(nBytes, pThis->m_nVitaAudioRingBytes - nOffset);
			memcpy(pThis->m_pVitaAudioRing + nOffset, pThis->m_pVitaAudioScratch, nFirst);
			if (nFirst < nBytes)
				memcpy(pThis->m_pVitaAudioRing, pThis->m_pVitaAudioScratch + nFirst, nBytes - nFirst);
			__sync_synchronize();
			pThis->m_nVitaAudioWrite = nWrite + nBytes;
		}
		__sync_synchronize();
		pThis->m_nVitaVideoWrite = nVideoWrite + 1;
	}
	return 0;
}

void CUIVideoPanel::VitaStopDecodeThread()
{
	if (m_nVitaDecodeThread < 0)
		return;
	m_nVitaWorkerRun = 0;
	sceKernelWaitThreadEnd(m_nVitaDecodeThread, 0, 0);
	sceKernelDeleteThread(m_nVitaDecodeThread);
	m_nVitaDecodeThread = -1;
}

//! Decodes the next frame if one is due. Returns false when the video ended.
bool CUIVideoPanel::VitaAdvanceFrame()
{
	if (!m_VitaBink.isValid || !m_pSwapBuffer || m_iTextureID <= 0)
		return false;

	ITimer *pTimer = m_pUISystem->GetISystem()->GetITimer();
	const float fNow = pTimer ? pTimer->GetAsyncCurTime() : 0.0f;
	/* Decode and colour conversion happen on core 3.  The render thread only
	   uploads an already finished, Vita-sized frame and never waits for the
	   decoder. */
	if (m_nVitaDecodeThread >= 0)
	{
		const unsigned int nQueued = m_nVitaVideoWrite - m_nVitaVideoRead;
		if (!m_bVitaPresentationStarted)
		{
			const bool bVideoReady = nQueued >= 3 || (m_nVitaWorkerEOF && nQueued > 0);
			const unsigned int nPCM = m_nVitaAudioWrite - m_nVitaAudioRead;
			const bool bAudioReady = !m_bVitaAudioEnabled ||
				nPCM >= m_nVitaAudioPrebufferBytes || m_nVitaWorkerEOF;
			if (!bVideoReady || !bAudioReady)
				return true;
			m_bVitaPresentationStarted = true;
			m_fVitaNextFrameTime = fNow;
			if (m_pVitaAudioStream && m_nVitaAudioChannel < 0)
				m_nVitaAudioChannel = CS_Stream_Play(CS_FREE, m_pVitaAudioStream);
		}
		if (fNow < m_fVitaNextFrameTime)
			return true;
		if (m_nVitaVideoRead != m_nVitaVideoWrite)
		{
			const unsigned int nRead = m_nVitaVideoRead;
			const unsigned int nFramePixels = m_nVitaWidth * m_nVitaHeight;
			const int *pFrame = m_pVitaDecodeBuffer +
				(nRead % kVitaVideoQueueFrames) * nFramePixels;
			m_pUISystem->GetIRenderer()->UpdateTextureInVideoMemory(m_iTextureID,
				(unsigned char *)pFrame, 0, 0, m_nVitaWidth, m_nVitaHeight, eTF_8888);
			__sync_synchronize();
			m_nVitaVideoRead = nRead + 1;
			++m_nVitaPresentedFrames;
			if ((m_nVitaPresentedFrames % 60) == 0 && m_pUISystem &&
				m_pUISystem->GetISystem() && m_pUISystem->GetISystem()->GetILog())
				m_pUISystem->GetISystem()->GetILog()->LogToFile(
					"\001[VITA][VIDEO] frame=%u decode=%uus convert=%uus queued=%u pcm=%u underruns=%u",
					m_nVitaPresentedFrames, m_nVitaLastDecodeUs, m_nVitaLastConvertUs,
					m_nVitaVideoWrite - m_nVitaVideoRead,
					m_nVitaAudioWrite - m_nVitaAudioRead, m_nVitaAudioUnderruns);
			/* Add the period rather than scheduling from 'now': this prevents a
			   late UI tick from permanently slowing the rest of the movie. */
			m_fVitaNextFrameTime += 1.0f / m_fVitaFrameRate;
			if (m_fVitaNextFrameTime < fNow - 1.0f / m_fVitaFrameRate)
				m_fVitaNextFrameTime = fNow + 1.0f / m_fVitaFrameRate;
			return true;
		}
		if (!m_nVitaWorkerEOF)
			return true;
		/* Let the prebuffered PCM tail drain instead of closing its stream the
		   instant the final video frame is uploaded. */
		if (m_bVitaAudioEnabled && m_nVitaAudioChannel >= 0 &&
			m_nVitaAudioWrite - m_nVitaAudioRead > 4096)
			return true;
		if (m_nVitaAudioUnderruns && m_pUISystem && m_pUISystem->GetISystem() &&
			m_pUISystem->GetISystem()->GetILog())
			m_pUISystem->GetISystem()->GetILog()->LogToFile(
				"\001[VITA][VIDEO] completed audio_underruns=%u", m_nVitaAudioUnderruns);
		return false;
	}
	if (fNow < m_fVitaNextFrameTime)
		return true;

	m_fVitaNextFrameTime = fNow + (1.0f / m_fVitaFrameRate);

	const uint32_t nCurrent = Bink_GetCurrentFrameNum(m_VitaBink);
	if (m_nVitaNumFrames > 0 && (int)nCurrent >= m_nVitaNumFrames)
	{
		if (!m_bLooping)
			return false;
		Bink_GotoFrame(m_VitaBink, 0);
	}

	YUVbuffer yuv;
	memset(yuv, 0, sizeof(yuv));
	/* libbinkdec returns the decoded frame INDEX, not a success boolean.
	   Frame zero therefore returns zero even though it decoded correctly.  The
	   old test treated that first valid frame as failure: non-looping story
	   videos ended immediately and looping menu videos remained a blank texture
	   forever.  Bounds are checked above, so reaching here means the frame is
	   valid and must be copied. */
	/* There is no Bink PCM output wired into CrySound yet.  Use the decoder's
	   video-only path so compressed audio is skipped, rather than decoded on
	   core 0 and immediately discarded. */
	Bink_GetNextFrameVideoOnly(m_VitaBink, yuv);

	VitaBinkYUVToRGBA(yuv, m_pSwapBuffer, m_nVitaSourceWidth, m_nVitaSourceHeight,
		m_nVitaWidth, m_nVitaHeight);
	m_pUISystem->GetIRenderer()->UpdateTextureInVideoMemory(m_iTextureID,
		(unsigned char *)m_pSwapBuffer, 0, 0, m_nVitaWidth, m_nVitaHeight, eTF_8888);
	return true;
}
#endif

////////////////////////////////////////////////////////////////////// 
CUIVideoPanel::~CUIVideoPanel()
{
	ReleaseVideo();
}

////////////////////////////////////////////////////////////////////// 
string CUIVideoPanel::GetClassName()
{
	return UICLASSNAME_VIDEOPANEL;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::InitBink()
{
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (g_bBinkInit)
	{
		return 1;
	}

	if (!BinkSoundUseDirectSound(0))
	{
		m_pUISystem->GetISystem()->GetILog()->Log("$4Bink Error$1: %s", BinkGetError());
		m_pUISystem->GetISystem()->GetILog()->Log("  Trying WaveOut");

		if (!BinkSoundUseWaveOut())
		{
			char *szError = BinkGetError();
			m_pUISystem->GetISystem()->GetILog()->Log("$4Bink Error$1: %s", szError);
			m_pUISystem->GetISystem()->GetILog()->Log("  No sound will be played!");
		}
	}

#endif
	g_bBinkInit = 1;
	return 1;
}


////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::LoadVideo(const string &szFileName, bool bSound)
{
	
	m_DivX_Active=1; //activate DivX

#if !defined(WIN64) && !defined(NOT_USE_BINK_SDK)
	//check if a BINK-file exists 
	HBINK hBink = BinkOpen(szFileName.c_str(), BINKSNDTRACK);
	if (hBink) {
		m_DivX_Active=0; //deactivate DivX
		BinkClose(hBink);
	}
#endif
#if !defined(NOT_USE_DIVX_SDK)	
	if (m_DivX_Active){
		m_DivX_Active = g_DivXPlayer.Load_DivX( this, szFileName );
		return m_DivX_Active;
	}
#endif
#if !defined(WIN64) && !defined(NOT_USE_BINK_SDK)

	if (m_hBink)
	{
		BinkClose(m_hBink);

		m_hBink = 0;
	}

	if (m_pSwapBuffer)
	{
		delete[] m_pSwapBuffer;
		m_pSwapBuffer = 0;
	}

	if (m_iTextureID > -1)
	{
		m_pUISystem->GetIRenderer()->RemoveTexture(m_iTextureID);
		m_iTextureID = -1;
	}

	if (szFileName.empty())
	{
		return 0;
	}

	if (!InitBink())
	{
		return 0;
	}

	if (!bSound)
	{
		unsigned long dwTrack = 0;

		BinkSetSoundTrack(0, &dwTrack);
	}
	else
	{
		unsigned long dwTrack = 0;

		BinkSetSoundTrack(1, &dwTrack);
	}

	// load bink file
	if (stricmp(szFileName.c_str(), "binklogo") == 0)
	{
		m_hBink = BinkOpen((const char *)BinkLogoAddress(), BINKFROMMEMORY | BINKSNDTRACK);
	}
	else
	{
		// check for a MOD first
		const char *szPrefix=NULL;
		IGameMods *pMods=m_pUISystem->GetISystem()->GetIGame()->GetModsInterface();
		if (pMods)		
			szPrefix=pMods->GetModPath(szFileName.c_str());
				
		if(szPrefix)
		{
			m_hBink = BinkOpen(szPrefix, BINKSNDTRACK);
		}

		// try in the original folder
		if(!m_hBink)
		{
			m_hBink = BinkOpen(szFileName.c_str(), BINKSNDTRACK);
		}
	}

	if (!m_hBink)
	{
		char *szError = BinkGetError();

		OnError(szError);

		return 0;
	}

	// create swap buffer
	m_pSwapBuffer = new int[m_hBink->Width * m_hBink->Height];

	if (!m_pSwapBuffer)
	{
		assert(!"Failed to create video swap buffer for blitting video!");

		BinkClose(m_hBink);
		m_hBink = 0;

		OnError("");

		return 0;
	}

	// create texture for blitting

  // WORKAROUND: NVidia driver bug during playing of video file
  // Solution: Never remove video texture (non-power-of-two)
  if (m_hBink->Width==640 && m_hBink->Height==480)
	  m_iTextureID = 	m_pUISystem->GetIRenderer()->DownLoadToVideoMemory((unsigned char *)m_pSwapBuffer, m_hBink->Width, m_hBink->Height, eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, "$VideoPanel", FT_DYNAMIC);
  else
    m_iTextureID = 	m_pUISystem->GetIRenderer()->DownLoadToVideoMemory((unsigned char *)m_pSwapBuffer, m_hBink->Width, m_hBink->Height, eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, NULL, FT_DYNAMIC);

	if (m_iTextureID == -1)
	{
		assert(!"Failed to create video memory surface for blitting video!");

		delete[] m_pSwapBuffer;
		m_pSwapBuffer = 0;

		BinkClose(m_hBink);
		m_hBink = 0;
		
		OnError("");

		return 0;
	}
	return 1;
#else
#if defined(__vita__)
	/* Real Bink playback through libbinkdec (third_party/libbinkdec), the
	   LGPL decoder vendored for this port -- the licensed RAD SDK the desktop
	   build links is not available and the Vita's hardware decoder only does
	   H.264.  Same shape as the BinkOpen path above: decode into an RGBA swap
	   buffer, hand it to a dynamic texture, and let Draw() present it. */
	/* Vita never uses the DivX singleton.  Leaving this flag set made
	   ReleaseVideo return before joining/freeing the Bink worker, leaking an
	   entire decoder and two frame buffers every time a menu/cutscene changed. */
	m_DivX_Active = 0;
	ReleaseVideo();

	m_szVideoFile = szFileName;

	/* Resolve to an absolute device path before opening.  The scripts build
	   these as "./languages/movies/<lang>/<name>.bik" -- UI.szCutSceneDrive is
	   "./" and the folder is relative (UISystemCfg.lua).  main() does chdir to
	   the data root, but the Vita's file layer does not resolve relative paths
	   against a working directory the way the desktop CRT does, so the open
	   fails and the panel covers the screen with nothing.  The videos are
	   present -- ux0:data/farcry/languages/Movies/Russian/training_begin.bik is
	   82 MB on the device -- so this is purely path resolution.

	   Resolved once and opened once, deliberately: libbinkdec allocates from a
	   fixed instance pool and a failed open still consumes a slot, so probing
	   candidate paths in a loop exhausts the pool and takes the app down (see
	   the note at the top of this file). */
	string szResolved = szFileName;
	if (szResolved.size() > 2 && szResolved[0] == '.' &&
		(szResolved[1] == '/' || szResolved[1] == '\\'))
		szResolved = szResolved.substr(2);
	if (szResolved.find(':') == string::npos)
		szResolved = string("ux0:data/farcry/") + szResolved;

	/* Find the language folder that is actually installed.  The scripts build
	   the path from g_language, which resolves to "english" here, but this
	   install ships only languages/Movies/Russian -- so the open failed on
	   ".../movies/english/Governmental_Message.bik" while the file sat in
	   .../Movies/Russian.  Rather than hard-code a language, look for the
	   basename in whichever subdirectories of languages/Movies exist.

	   Probing with stat() is safe; probing with Bink_Open is not.  libbinkdec
	   allocates from a fixed instance pool and a failed open still consumes a
	   slot, so the search happens entirely on the filesystem and Bink_Open is
	   still called exactly once, on the winner. */
	if (!VitaFileExists(szResolved.c_str()))
	{
		const size_t nSlash = szResolved.find_last_of('/');
		if (nSlash != string::npos)
		{
			const string szBase = szResolved.substr(nSlash + 1);
			static const char *kMovieRoots[] = {
				"ux0:data/farcry/languages/Movies/",
				"ux0:data/farcry/languages/movies/"
			};
			bool bFound = false;
			for (int nRoot = 0; nRoot < 2 && !bFound; ++nRoot)
			{
				DIR *pDir = opendir(kMovieRoots[nRoot]);
				if (!pDir)
					continue;
				while (struct dirent *pEntry = readdir(pDir))
				{
					if (pEntry->d_name[0] == '.')
						continue;
					string szTry = string(kMovieRoots[nRoot]) + pEntry->d_name + "/" + szBase;
					if (VitaFileExists(szTry.c_str()))
					{
						szResolved = szTry;
						bFound = true;
						break;
					}
				}
				closedir(pDir);
			}
		}
	}
	m_szVideoFile = szResolved;

	/* Prefer a Vita-ready sidecar generated from the user's own Bink asset.
	   Sony's AvPlayer performs H.264 video decode in hardware and returns a
	   GPU-native YUV texture, eliminating both measured hot spots from the
	   software path (50-141 ms decode plus 23-32 ms RGB conversion). */
	string szHardwareVideo = szResolved;
	const size_t nExtension = szHardwareVideo.find_last_of('.');
	if (nExtension != string::npos)
		szHardwareVideo = szHardwareVideo.substr(0, nExtension) + ".mp4";
	else
		szHardwareVideo += ".mp4";
	if (VitaHardwareVideoEnabled(m_pUISystem) &&
		VitaFileExists(szHardwareVideo.c_str()) &&
		VitaAvPlayerOpen(szHardwareVideo.c_str(), bSound))
	{
		m_szVideoFile = szHardwareVideo;
		m_bPaused = 0;
		m_bPlaying = 0;
		m_bVitaFinishPending = false;
		return 1;
	}

	m_VitaBink = Bink_Open(szResolved.c_str());
	if (!m_VitaBink.isValid)
	{
		// Missing or unreadable: the normal unsupported-media contract. Do not
		// call OnError synchronously -- BackScreen.OnError calls OnFinished,
		// which calls LoadVideo again and overflows Lua's stack during menu
		// creation.
		/* Say which file, once per name.  A video that silently refuses to open
		   leaves whatever the panel was covering on screen instead -- the world
		   flashing through before the panel settles, and then an empty panel --
		   and that is indistinguishable from a rendering fault unless the open
		   failure is reported. */
		{
			static std::set<std::string> s_reportedMissingVideos;
			if (s_reportedMissingVideos.size() < 32 &&
				s_reportedMissingVideos.find(szFileName) == s_reportedMissingVideos.end())
			{
				s_reportedMissingVideos.insert(szFileName);
				if (m_pUISystem && m_pUISystem->GetISystem() && m_pUISystem->GetISystem()->GetILog())
					m_pUISystem->GetISystem()->GetILog()->LogToFile(
						"\001[VITA][VIDEO] cannot open '%s' -- nothing will be drawn where this video should be",
						szFileName.c_str());
			}
		}
		m_bPlaying = 0;
		m_bPaused = 0;
		m_bVitaFinishPending = true;
		return 0;
	}

	uint32_t nWidth = 0, nHeight = 0;
	Bink_GetFrameSize(m_VitaBink, nWidth, nHeight);
	if (!nWidth || !nHeight)
	{
		/* Mark the handle dead as well as closing it.  ReleaseVideo() closes
		   whenever isValid is set, so leaving it set here means the next
		   LoadVideo closes this same instance a second time -- which is what
		   crashed the game when the in-game menu opened a video after the
		   main menu had already used one. */
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
		m_bPlaying = 0;
		return 0;
	}

	m_nVitaSourceWidth = (int)nWidth;
	m_nVitaSourceHeight = (int)nHeight;
	/* The Vita panel is 960x544, but decoding a 640/720p movie into an equally
	   large RGBA texture wastes conversion, upload bandwidth and memory.  A
	   480x272 movie texture maps cleanly to the screen and cuts that work to a
	   quarter while libbinkdec retains the source planes for decoding. */
	float fScale = 1.0f;
	if (nWidth > 480)
		fScale = 480.0f / (float)nWidth;
	if ((float)nHeight * fScale > 272.0f)
		fScale = 272.0f / (float)nHeight;
	m_nVitaWidth = ((int)((float)nWidth * fScale)) & ~1;
	m_nVitaHeight = ((int)((float)nHeight * fScale)) & ~1;
	if (m_nVitaWidth < 2) m_nVitaWidth = 2;
	if (m_nVitaHeight < 2) m_nVitaHeight = 2;
	m_fVitaFrameRate = Bink_GetFrameRate(m_VitaBink);
	if (m_fVitaFrameRate <= 0.0f)
		m_fVitaFrameRate = 30.0f;
	m_nVitaNumFrames = (int)Bink_GetNumFrames(m_VitaBink);
	m_fVitaNextFrameTime = 0.0f;

	/* A full-size RGBA frame is a couple of megabytes, and by the time the
	   in-game menu opens the heap can be too full to give it up.  new returns
	   null here rather than throwing, and the memset below would then write
	   through it -- that is what crashed the game on pressing Start.  Skip the
	   video instead; the panel just stays blank. */
	m_pSwapBuffer = new int[m_nVitaWidth * m_nVitaHeight];
	m_pVitaDecodeBuffer = new int[m_nVitaWidth * m_nVitaHeight * kVitaVideoQueueFrames];
	if (!m_pSwapBuffer || !m_pVitaDecodeBuffer)
	{
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
		delete [] m_pSwapBuffer;
		delete [] m_pVitaDecodeBuffer;
		m_pSwapBuffer = 0;
		m_pVitaDecodeBuffer = 0;
		m_bPlaying = 0;
		return 0;
	}
	memset(m_pSwapBuffer, 0, sizeof(int) * m_nVitaWidth * m_nVitaHeight);
	memset(m_pVitaDecodeBuffer, 0, sizeof(int) * m_nVitaWidth * m_nVitaHeight * kVitaVideoQueueFrames);

	m_iTextureID = m_pUISystem->GetIRenderer()->DownLoadToVideoMemory(
		(unsigned char *)m_pSwapBuffer, m_nVitaWidth, m_nVitaHeight,
		eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, "$VideoPanel", FT_DYNAMIC);

	if (m_iTextureID == 0 || m_iTextureID == -1)
	{
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
		delete [] m_pSwapBuffer;
		delete [] m_pVitaDecodeBuffer;
		m_pSwapBuffer = 0;
		m_pVitaDecodeBuffer = 0;
		m_iTextureID = -1;
		m_bPlaying = 0;
		return 0;
	}

	m_bVitaAudioEnabled = bSound && Bink_GetNumAudioTracks(m_VitaBink) > 0;
	if (m_bVitaAudioEnabled)
	{
		const AudioInfo info = Bink_GetAudioTrackDetails(m_VitaBink, 0);
		m_nVitaAudioTrack = 0;
		m_nVitaAudioChannels = info.nChannels > 1 ? 2 : 1;
		m_nVitaAudioScratchBytes = info.idealBufferSize;
		m_nVitaAudioRingBytes = info.sampleRate * m_nVitaAudioChannels * 2;
		m_nVitaAudioPrebufferBytes = info.sampleRate * m_nVitaAudioChannels * 2 / 8;
		if (m_nVitaAudioRingBytes < info.idealBufferSize * 4)
			m_nVitaAudioRingBytes = info.idealBufferSize * 4;
		m_pVitaAudioScratch = new unsigned char[m_nVitaAudioScratchBytes];
		m_pVitaAudioRing = new unsigned char[m_nVitaAudioRingBytes];
		if (!m_pVitaAudioScratch || !m_pVitaAudioRing)
		{
			delete [] m_pVitaAudioScratch;
			delete [] m_pVitaAudioRing;
			m_pVitaAudioScratch = 0;
			m_pVitaAudioRing = 0;
			m_bVitaAudioEnabled = false;
			m_nVitaAudioChannels = 0;
		}
		else
		{
			memset(m_pVitaAudioRing, 0, m_nVitaAudioRingBytes);
			const unsigned int nMode = CS_16BITS | CS_SIGNED |
				(m_nVitaAudioChannels == 2 ? CS_STEREO : CS_MONO);
			m_pVitaAudioStream = CS_Stream_Create(VitaAudioCallback, 4096, nMode,
				(int)info.sampleRate, (int)(intptr_t)this);
			if (!m_pVitaAudioStream)
				m_bVitaAudioEnabled = false;
		}
	}

	m_nVitaWorkerRun = 1;
	m_nVitaWorkerPlay = 0;
	m_nVitaVideoRead = m_nVitaVideoWrite = 0;
	m_nVitaLastDecodeUs = m_nVitaLastConvertUs = 0;
	m_nVitaPresentedFrames = 0;
	m_bVitaPresentationStarted = false;
	m_nVitaAudioRead = m_nVitaAudioWrite = 0;
	m_nVitaAudioUnderruns = 0;
	m_nVitaWorkerEOF = 0;
	/* Audio mixing owns core 2.  Keep software Bink decode on unlocked core 3:
	   skinning only wakes there for short gameplay jobs and is idle while these
	   full-screen movies run, whereas sharing core 2 caused decode/audio stalls. */
	m_nVitaDecodeThread = sceKernelCreateThread("fc_bink_decode", VitaDecodeThread,
		0x10000100, 0x20000, 0, SCE_KERNEL_CPU_MASK_SYSTEM, 0);
	if (m_nVitaDecodeThread >= 0)
	{
		CUIVideoPanel *pThis = this;
		const int nStart = sceKernelStartThread(m_nVitaDecodeThread, sizeof(pThis), &pThis);
		if (nStart < 0)
		{
			sceKernelDeleteThread(m_nVitaDecodeThread);
			m_nVitaDecodeThread = -1;
			m_nVitaWorkerRun = 0;
		}
	}
	else
		m_nVitaWorkerRun = 0;
	if (m_pUISystem && m_pUISystem->GetISystem() && m_pUISystem->GetISystem()->GetILog())
		m_pUISystem->GetISystem()->GetILog()->LogToFile(
			"\001[VITA][VIDEO] %dx%d -> %dx%d %.2ffps decode_thread=%d audio=%uHz/%uch",
			m_nVitaSourceWidth, m_nVitaSourceHeight, m_nVitaWidth, m_nVitaHeight,
			m_fVitaFrameRate, m_nVitaDecodeThread >= 0 ? 1 : 0,
			m_bVitaAudioEnabled ? Bink_GetAudioTrackDetails(m_VitaBink, 0).sampleRate : 0,
			m_bVitaAudioEnabled ? m_nVitaAudioChannels : 0);

	m_bPaused = 0;
	m_bPlaying = 0;	// Play() starts it, matching the desktop contract
	m_bVitaFinishPending = false;
	return 1;
#else
	m_bPlaying = 0;
	m_bPaused = 0;
	return 0;
#endif
#endif

	return 0;
}

////////////////////////////////////////////////////////////////////// 
LRESULT CUIVideoPanel::Update(unsigned int iMessage, WPARAM wParam, LPARAM lParam)	//AMD Port
{

	FUNCTION_PROFILER( m_pUISystem->GetISystem(), PROFILE_GAME );
#if defined(__vita__)
	if ((iMessage == UIM_DRAW) && (wParam == 0))
	{
		/* A video that could not be opened has to finish, not hang.  The panel
		   exists to cover the screen while it plays, so if it just sits there
		   the game shows an empty panel and never advances past it -- which is
		   the blank screen after the level's assets flash through.  OnFinished
		   cannot be called from LoadVideo itself (BackScreen.OnError calls
		   OnFinished, which calls LoadVideo again, and Lua's stack overflows
		   during menu creation), so it is deferred to here, one frame later,
		   where that recursion cannot form. */
		if (m_bVitaFinishPending)
		{
			m_bVitaFinishPending = false;
			Stop();
			OnFinished();
			return CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);
		}
		if (m_bPlaying && !m_bPaused && (m_bVitaAvActive || m_VitaBink.isValid))
		{
			const bool bContinue = m_bVitaAvActive ?
				VitaAvPlayerAdvanceFrame() : VitaAdvanceFrame();
			if (!bContinue)
			{
				Stop();
				OnFinished();
			}
		}
		return CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);
	}
	return CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);
#endif
#if !defined(NOT_USE_DIVX_SDK)
	if (m_DivX_Active){
		g_DivXPlayer.Update_DivX(this);
		return CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);
	}
#endif
#if !defined(WIN64) && !defined(NOT_USE_BINK_SDK)

	if ((iMessage == UIM_DRAW) && (wParam == 0))
	{
		// stream the frame here
		if (m_bPlaying && m_hBink && m_pSwapBuffer)
		{
			if (!BinkWait(m_hBink))
			{
				{
					FRAME_PROFILER("CUIVideoPanel::Update:BinkDoFrame", m_pUISystem->GetISystem(), PROFILE_GAME);
					BinkDoFrame(m_hBink);		
				}

				if ((m_iTextureID > -1) && (m_pSwapBuffer))
				{
					{
						FRAME_PROFILER("CUIVideoPanel::Update:BinkCopyToBuffer", m_pUISystem->GetISystem(), PROFILE_GAME);
						BinkCopyToBuffer(m_hBink, m_pSwapBuffer, m_hBink->Width * 4, m_hBink->Height, 0, 0, BINKCOPYALL | BINKSURFACE32);
					}

					{
						FRAME_PROFILER("Renderer::UpdateTextureInVideoMemory", m_pUISystem->GetISystem(), PROFILE_GAME);
						m_pUISystem->GetIRenderer()->UpdateTextureInVideoMemory(m_iTextureID, (unsigned char *)m_pSwapBuffer, 0, 0, m_hBink->Width, m_hBink->Height, eTF_8888);
					}
				}

				if ((m_hBink->FrameNum < m_hBink->Frames) || m_bLooping)
				{
					FRAME_PROFILER("CUIVideoPanel::Update:BinkNextFrame", m_pUISystem->GetISystem(), PROFILE_GAME);
					BinkNextFrame(m_hBink);
				}
			}
		}

		int iResult = CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);

		if (m_hBink && ((m_hBink->FrameNum == m_hBink->Frames) && !m_bLooping))
		{
			Stop();

			OnFinished();
		}

		return iResult;
	}

	return CUISystem::DefaultUpdate(this, iMessage, wParam, lParam);

#endif

	return 0;
}


////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Play()
{
	if (m_DivX_Active){
		m_bPlaying = 1;
		m_bPaused = 0;
		return 1;
	}	

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		if (m_szVideoFile.empty())
		{
			return 0;
		}

		if (!LoadVideo(m_szVideoFile, 1))
		{
			return 0;
		}
	}

	BinkPause(m_hBink, 0);
 	m_bPlaying = 1;
	m_bPaused = 0;
	return 1;
#elif defined(__vita__)
	if (m_bVitaAvActive)
	{
		if (!VitaAvHandleValid(m_nVitaAvHandle))
		{
			VitaAvLog("Play() reached with no valid handle (0x%08x)",
				(unsigned)m_nVitaAvHandle);
			return 0;
		}
		sceAvPlayerSetLooping(m_nVitaAvHandle, m_bLooping ? SCE_TRUE : SCE_FALSE);
		const int nStart = sceAvPlayerStart(m_nVitaAvHandle);
		if (nStart < 0)
		{
			VitaAvLog("sceAvPlayerStart failed 0x%08x", (unsigned)nStart);
			return 0;
		}
		VitaAvLog("started, advancing frames");
		m_bVitaAvStarted = true;
		m_bPlaying = 1;
		m_bPaused = 0;
		m_nVitaAvIdleTicks = 0;
		if (m_bVitaAvSound && m_nVitaAvAudioThread < 0)
		{
			/* CrySound owns the MAIN port.  Using BGM for movie audio prevents two
			   independent mixers from fighting for the same hardware path. */
			m_nVitaAvAudioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
				1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
			if (m_nVitaAvAudioPort >= 0)
			{
				int arrVolume[2] = { m_nVitaAvVolume, m_nVitaAvVolume };
				sceAudioOutSetVolume(m_nVitaAvAudioPort,
					(SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH |
					SCE_AUDIO_VOLUME_FLAG_R_CH), arrVolume);
				m_nVitaAvAudioRun = 1;
				m_nVitaAvAudioThread = sceKernelCreateThread("fc_av_audio",
					VitaAvAudioThread, 0x10000100 - 10, 0x4000, 0,
					SCE_KERNEL_CPU_MASK_USER_2, 0);
				if (m_nVitaAvAudioThread >= 0)
				{
					CUIVideoPanel *pThis = this;
					if (sceKernelStartThread(m_nVitaAvAudioThread,
						sizeof(pThis), &pThis) < 0)
					{
						VitaAvLog("audio thread start failed");
						sceKernelDeleteThread(m_nVitaAvAudioThread);
						m_nVitaAvAudioThread = -1;
						m_nVitaAvAudioRun = 0;
					}
				}
				else
					VitaAvLog("audio thread create failed 0x%08x", (unsigned)m_nVitaAvAudioThread);
			}
			else
				VitaAvLog("BGM audio port open failed 0x%08x", (unsigned)m_nVitaAvAudioPort);
		}
		return 1;
	}
	if (!m_VitaBink.isValid)
	{
		if (m_szVideoFile.empty() || !LoadVideo(m_szVideoFile, 1))
			return 0;
	}
	m_bPlaying = 1;
	m_bPaused = 0;
	m_nVitaWorkerEOF = 0;
	m_nVitaWorkerPlay = 1;
	if (m_nVitaAudioChannel >= 0)
		CS_SetPaused(m_nVitaAudioChannel, 0);
	m_fVitaNextFrameTime = 0.0f;	// decode the first frame immediately
	return 1;
#else
	return 0;
#endif

	return 0;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Stop()
{
#if !defined(NOT_USE_DIVX_SDK)
	if (m_DivX_Active){
		g_DivXPlayer.StopSound();
		m_bPaused = 0;
		m_bPlaying = 0;
		return 1;
	}
#endif
#if !defined(WIN64) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}
	BinkPause(m_hBink, 1);
	BinkClose(m_hBink);
	m_hBink = 0;
	m_bPaused = 0;
	m_bPlaying = 0;
#if defined(__vita__)
	if (m_bVitaAvActive && VitaAvHandleValid(m_nVitaAvHandle) && m_bVitaAvStarted)
		sceAvPlayerStop(m_nVitaAvHandle);
	m_nVitaWorkerPlay = 0;
	if (m_nVitaAudioChannel >= 0)
		CS_SetPaused(m_nVitaAudioChannel, 1);
#endif
	return 1;
#endif

#if defined(__vita__)
	/* This is the path Stop() actually takes on Vita: the Bink branch above is
	   inside !NOT_USE_BINK_SDK, and ProjectDefines.h defines that for LINUX, so
	   the sceAvPlayerStop it contains never compiles.  Without this the decoder
	   keeps running after the panel has stopped. */
	if (m_bVitaAvActive && VitaAvHandleValid(m_nVitaAvHandle) && m_bVitaAvStarted)
		sceAvPlayerStop(m_nVitaAvHandle);
	m_nVitaWorkerPlay = 0;
	if (m_nVitaAudioChannel >= 0)
		CS_SetPaused(m_nVitaAudioChannel, 1);
#endif
	m_bPaused = 0;
	m_bPlaying = 0;
	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::ReleaseVideo()
{
	if (m_DivX_Active){
		return 1;
	}

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (m_hBink)
	{
		BinkClose(m_hBink);
	}

	if (m_iTextureID > -1)
	{
		m_pUISystem->GetIRenderer()->RemoveTexture(m_iTextureID);
		m_iTextureID = -1;
	}

	m_hBink = 0;
	m_szVideoFile = "";

	if (m_pSwapBuffer)
	{
		delete[] m_pSwapBuffer;
		m_pSwapBuffer = 0;
	}
	return 1;
#elif defined(__vita__)
	/* The decoder owns libbinkdec from core 3.  Join it before closing the
	   handle or freeing any frame/audio storage it can still touch. */
	VitaAvPlayerClose();
	VitaStopDecodeThread();
	if (m_pVitaAudioStream)
	{
		CS_Stream_Stop(m_pVitaAudioStream);
		CS_Stream_Close(m_pVitaAudioStream);
		m_pVitaAudioStream = 0;
	}
	m_nVitaAudioChannel = -1;
	if (m_VitaBink.isValid)
	{
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
	}
	if (m_iTextureID > 0)
	{
		m_pUISystem->GetIRenderer()->RemoveTexture(m_iTextureID);
		m_iTextureID = -1;
	}
	if (m_pSwapBuffer)
	{
		delete[] m_pSwapBuffer;
		m_pSwapBuffer = 0;
	}
	delete [] m_pVitaDecodeBuffer;
	m_pVitaDecodeBuffer = 0;
	delete [] m_pVitaAudioScratch;
	m_pVitaAudioScratch = 0;
	delete [] m_pVitaAudioRing;
	m_pVitaAudioRing = 0;
	m_nVitaAudioRingBytes = 0;
	m_nVitaAudioScratchBytes = 0;
	m_nVitaAudioPrebufferBytes = 0;
	m_nVitaAudioRead = m_nVitaAudioWrite = 0;
	m_nVitaAudioChannels = 0;
	m_bVitaAudioEnabled = false;
	m_nVitaVideoRead = m_nVitaVideoWrite = 0;
	m_bVitaPresentationStarted = false;
	m_nVitaWorkerEOF = 0;
	m_bPlaying = 0;
	m_bPaused = 0;
	m_bVitaFinishPending = false;
	m_szVideoFile = "";
	return 1;
#else
	return 0;
#endif

	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Pause(bool bPause)
{
	if (m_DivX_Active){
		return 1;
	}


#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}

	if (bPause)
	{
		if (!m_bPaused)
		{
			m_bPaused = 1;
			BinkPause(m_hBink, 1);
		}
	}
	else
	{
		if (m_bPaused)
		{
			m_bPaused = 0;
			BinkPause(m_hBink, 0);
		}
	}
	return 1;
#elif defined(__vita__)
	if (m_bVitaAvActive)
	{
		if (!VitaAvHandleValid(m_nVitaAvHandle))
			return 0;
		m_bPaused = bPause;
		if (bPause)
			sceAvPlayerPause(m_nVitaAvHandle);
		else
			sceAvPlayerResume(m_nVitaAvHandle);
		return 1;
	}
	if (!m_VitaBink.isValid)
		return 0;
	m_bPaused = bPause;
	m_nVitaWorkerPlay = (m_bPlaying && !m_bPaused) ? 1 : 0;
	if (m_nVitaAudioChannel >= 0)
		CS_SetPaused(m_nVitaAudioChannel, bPause ? 1 : 0);
	return 1;
#else
	return 0;
#endif

	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::IsPlaying()
{
	if (m_DivX_Active){
		return (m_bPlaying ? 1 : 0);
	}

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}
	return (m_bPlaying ? 1 : 0);
#elif defined(__vita__)
	return ((m_bVitaAvActive || m_VitaBink.isValid) && m_bPlaying) ? 1 : 0;
#else
	return 0;
#endif

	return (0);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::IsPaused()
{
	if (m_DivX_Active){
		return (m_bPaused ? 1 : 0);
	}
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}
	return (m_bPaused ? 1 : 0);
#elif defined(__vita__)
	return ((m_bVitaAvActive || m_VitaBink.isValid) && m_bPaused) ? 1 : 0;
#else
	return 0;
#endif

	return 0;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::SetVolume(int iTrackID, float fVolume)
{

	if (m_DivX_Active){
		return 1;
	}

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}

	if (fVolume < 0.0f)
	{
		fVolume = 0.0f;
	}

	BinkSetVolume(m_hBink, iTrackID, (int)(fVolume * 32768));

	return 1;
#elif defined(__vita__)
	if (fVolume < 0.0f) fVolume = 0.0f;
	if (fVolume > 1.0f) fVolume = 1.0f;
	m_nVitaAvVolume = (int)(fVolume * (float)SCE_AUDIO_OUT_MAX_VOL);
	if (m_bVitaAvActive)
	{
		if (m_nVitaAvAudioPort >= 0)
		{
			int arrVolume[2] = { m_nVitaAvVolume, m_nVitaAvVolume };
			sceAudioOutSetVolume(m_nVitaAvAudioPort,
				(SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH |
				SCE_AUDIO_VOLUME_FLAG_R_CH), arrVolume);
		}
		return 1;
	}
	if (m_nVitaAudioChannel >= 0)
		CS_SetVolume(m_nVitaAudioChannel, (int)(fVolume * 255.0f));
	return m_pVitaAudioStream ? 1 : 0;
#else
	return 0;
#endif

	return 1;
}

//////////////////////////////////////////////////////////////////////
int CUIVideoPanel::SetPan(int iTrackID, float fPan)
{
	if (m_DivX_Active){
		return 1;
	}

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}

	if (fPan > 1.0f)
	{
		fPan = 1.0f;
	}
	else if (fPan < -1.0f)
	{
		fPan = -1.0f;
	}

	BinkSetPan(m_hBink, 1, 32768 + (int)(fPan * 32767));
	return 1;
#else
	return 0;
#endif

	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::SetFrameRate(int iFrameRate)
{
	if (m_DivX_Active){
		return 1;
	}

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)	{
		return 0;
	}

	BinkSetFrameRate(iFrameRate, 1);

	return 1;
#else
	return 0;
#endif

	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Draw(int iPass)
{
	if (iPass != 0)
	{
		return 1;
	}

	m_pUISystem->BeginDraw(this);

	// get the absolute widget rect
	UIRect pAbsoluteRect(m_pRect);

	m_pUISystem->GetAbsoluteXY(&pAbsoluteRect.fLeft, &pAbsoluteRect.fTop, m_pRect.fLeft, m_pRect.fTop, m_pParent);

	// if transparent, draw only the clipped text
	if ((GetStyle() & UISTYLE_TRANSPARENT) == 0)
	{
		// if shadowed, draw the shadow
		if (GetStyle() & UISTYLE_SHADOWED)
		{
			m_pUISystem->DrawShadow(pAbsoluteRect, UI_DEFAULT_SHADOW_COLOR, UI_DEFAULT_SHADOW_BORDER_SIZE, this);
		}
	}

	// if border is large enough to be visible, draw it
	if (m_pBorder.fSize > 0.125f)
	{
		m_pUISystem->DrawBorder(pAbsoluteRect, m_pBorder);
		m_pUISystem->AdjustRect(&pAbsoluteRect, pAbsoluteRect, m_pBorder.fSize);
	}

	// save the client area without the border,
	// to draw a greyed quad later, if disabled
	UIRect pGreyedRect = pAbsoluteRect;

	// video
	if (m_iTextureID > -1)
	{
		float fWidth = pAbsoluteRect.fWidth;
		float fHeight = pAbsoluteRect.fHeight;

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
		if (m_bKeepAspect && m_hBink)
		{
			float fAspect = m_hBink->Width / (float)m_hBink->Height;

			if (fAspect < 1.0f)
			{
				fWidth = fHeight * fAspect;
			}
			else
			{
				fHeight = fWidth / fAspect;
			}
		}
#elif defined(__vita__)
		if (m_bKeepAspect && m_nVitaWidth > 0 && m_nVitaHeight > 0)
		{
			const float fAspect = m_nVitaWidth / (float)m_nVitaHeight;
			if (fAspect < 1.0f)
				fWidth = fHeight * fAspect;
			else
				fHeight = fWidth / fAspect;
		}
#endif

		if (fWidth > pAbsoluteRect.fWidth)
		{
			float fRatio = pAbsoluteRect.fWidth / fWidth;

			fWidth *= fRatio;
			fHeight *= fRatio;
		}
		if (fHeight > pAbsoluteRect.fHeight)
		{
			float fRatio = pAbsoluteRect.fHeight / fHeight;

			fWidth *= fRatio;
			fHeight *= fRatio;
		}

		UIRect pRect;

		pRect.fLeft = pAbsoluteRect.fLeft + (pAbsoluteRect.fWidth - fWidth) * 0.5f;
		pRect.fTop = pAbsoluteRect.fTop + (pAbsoluteRect.fHeight - fHeight) * 0.5f;
		pRect.fWidth = fWidth;
		pRect.fHeight = fHeight;

		if (m_bKeepAspect)
		{
			m_pUISystem->DrawQuad(pAbsoluteRect, m_cColor);
		}

		m_pUISystem->DrawImage(pRect, m_iTextureID, 0, color4f(1.0f, 1.0f, 1.0f, 1.0f));
	}

	// draw overlay
	if (m_pOverlay.iTextureID > -1)
	{
		m_pUISystem->DrawSkin(pAbsoluteRect, m_pOverlay, color4f(1.0f, 1.0f, 1.0f, 1.0f), UISTATE_UP);
	}

	// draw a greyed quad ontop, if disabled
	if ((m_iFlags & UIFLAG_ENABLED) == 0)
	{
		m_pUISystem->ResetDraw();
		m_pUISystem->DrawGreyedQuad(pGreyedRect, m_cGreyedColor, m_iGreyedBlend);
	}

	m_pUISystem->EndDraw();

	// draw the children
	if (m_pUISystem->ShouldSortByZ())
	{
		SortChildrenByZ();
	}

	DrawChildren();

	return 1;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::EnableVideo(bool bEnable)
{
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	if (!m_hBink)
	{
		return 0;
	}

	return BinkSetVideoOnOff(m_hBink, bEnable ? 1 : 0);
#else
	return 0;
#endif

	return 0;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::EnableAudio(bool bEnable)
{
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)

	if (!m_hBink)
	{
		return 0;
	}

	return BinkSetSoundOnOff(m_hBink, bEnable ? 1 : 0);
#elif defined(__vita__)
	if (m_bVitaAvActive)
	{
		m_bVitaAvSound = bEnable;
		if (m_nVitaAvAudioPort >= 0)
		{
			const int nVolume = bEnable ? m_nVitaAvVolume : 0;
			int arrVolume[2] = { nVolume, nVolume };
			sceAudioOutSetVolume(m_nVitaAvAudioPort,
				(SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH |
				SCE_AUDIO_VOLUME_FLAG_R_CH), arrVolume);
		}
		return 1;
	}
	m_bVitaAudioEnabled = bEnable && m_pVitaAudioStream != 0;
	if (m_nVitaAudioChannel >= 0)
		CS_SetMute(m_nVitaAudioChannel, bEnable ? 0 : 1);
	return m_pVitaAudioStream ? 1 : 0;
#else
	return 0;
#endif

	return 0;
}

////////////////////////////////////////////////////////////////////// 
void CUIVideoPanel::InitializeTemplate(IScriptSystem *pScriptSystem)
{
	_ScriptableEx<CUIVideoPanel>::InitializeTemplate(pScriptSystem);

	REGISTER_COMMON_MEMBERS(pScriptSystem, CUIVideoPanel);

	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, LoadVideo);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, ReleaseVideo);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, Play);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, Stop);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, Pause);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, IsPlaying);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, IsPaused);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, SetVolume);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, SetPan);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, SetFrameRate);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, EnableVideo);
	REGISTER_SCRIPTOBJECT_MEMBER(pScriptSystem, CUIVideoPanel, EnableAudio);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::OnError(const char *szError)
{
	IScriptSystem *pScriptSystem = m_pUISystem->GetIScriptSystem();
	IScriptObject *pScriptObject = m_pUISystem->GetWidgetScriptObject(this);

	if (!pScriptObject)
	{
		return 1;
	}

	HSCRIPTFUNCTION pScriptFunction = pScriptSystem->GetFunctionPtr(GetName().c_str(), "OnError");

	if (!pScriptFunction)
	{
		if (!pScriptObject->GetValue("OnError", pScriptFunction))
		{
			return 1;
		}
	}

	int iResult = 1;

	pScriptSystem->BeginCall(pScriptFunction);
	pScriptSystem->PushFuncParam(pScriptObject);
	pScriptSystem->PushFuncParam(szError);
	pScriptSystem->EndCall(iResult);

	pScriptSystem->ReleaseFunc(pScriptFunction);

	return iResult;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::OnFinished()
{
	IScriptSystem *pScriptSystem = m_pUISystem->GetIScriptSystem();
	IScriptObject *pScriptObject = m_pUISystem->GetWidgetScriptObject(this);

	if (!pScriptObject)
	{
		return 1;
	}

	HSCRIPTFUNCTION pScriptFunction = pScriptSystem->GetFunctionPtr(GetName().c_str(), "OnFinished");

	if (!pScriptFunction)
	{
		if (!pScriptObject->GetValue("OnFinished", pScriptFunction))
		{
			return 1;
		}
	}

	int iResult = 1;

	pScriptSystem->BeginCall(pScriptFunction);
	pScriptSystem->PushFuncParam(pScriptObject);
	pScriptSystem->EndCall(iResult);

	pScriptSystem->ReleaseFunc(pScriptFunction);

	return iResult;
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::LoadVideo(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT2(m_pScriptSystem, GetName().c_str(), LoadVideo, 1, 2);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), LoadVideo, 1, svtString);

	if (pH->GetParamCount() == 2)
	{
		CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), LoadVideo, 2, svtNumber);
	}
	
	char *pszFileName;
	int iSound = 0;

	pH->GetParam(1, pszFileName);

	if (pH->GetParamCount() == 2)
	{
		pH->GetParam(2, iSound);
	}

	if (!LoadVideo(pszFileName, iSound != 0))
	{
		return pH->EndFunctionNull();
	}

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::ReleaseVideo(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), ReleaseVideo, 0);

	ReleaseVideo();

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Play(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), Play, 0);

	Play();

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Stop(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), Stop, 0);

	Stop();

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::Pause(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), Pause, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), Pause, 1, svtNumber);

	int iPause;

	pH->GetParam(1, iPause);

	Pause(iPause != 0);

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::IsPlaying(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), IsPlaying, 0);

	if (m_bPlaying)
	{
		return pH->EndFunction(1);
	}
	else
	{
		return pH->EndFunctionNull();
	}
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::IsPaused(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), IsPaused, 0);

	if (m_bPaused)
	{
		return pH->EndFunction(1);
	}
	else
	{
		return pH->EndFunctionNull();
	}
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::SetVolume(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), SetVolume, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), SetVolume, 1, svtNumber);

	float fVolume;

	pH->GetParam(1, fVolume);

	for (int i = 0; i < 16; i++)
	{
		SetVolume(i, fVolume);
	}

	return pH->EndFunction(1);
}

//////////////////////////////////////////////////////////////////////
int CUIVideoPanel::SetPan(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), SetPan, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), SetPan, 1, svtNumber);

	float fPan;

	pH->GetParam(1, fPan);

	for (int i = 0; i < 16; i++)
	{
		SetPan(i, fPan);
	}

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::SetFrameRate(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), SetFrameRate, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), SetFrameRate, 1, svtNumber);

	int iFrameRate;

	pH->GetParam(1, iFrameRate);

	SetFrameRate(iFrameRate);

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::EnableVideo(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), EnableVideo, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), EnableVideo, 1, svtNumber);

	int iEnable;

	pH->GetParam(1, iEnable);

	EnableVideo(iEnable != 0);

	return pH->EndFunction(1);
}

////////////////////////////////////////////////////////////////////// 
int CUIVideoPanel::EnableAudio(IFunctionHandler *pH)
{
	CHECK_SCRIPT_FUNCTION_PARAMCOUNT(m_pScriptSystem, GetName().c_str(), EnableAudio, 1);
	CHECK_SCRIPT_FUNCTION_PARAMTYPE(m_pScriptSystem, GetName().c_str(), EnableAudio, 1, svtNumber);

	int iEnable;

	pH->GetParam(1, iEnable);

	EnableAudio(iEnable != 0);
	
	return pH->EndFunction(1);
}
