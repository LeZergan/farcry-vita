
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
	, m_nVitaWidth(0), m_nVitaHeight(0), m_nVitaNumFrames(0), m_fVitaFrameRate(30.0f), m_fVitaNextFrameTime(0.0f), m_bVitaFinishPending(false)
#endif
{
	m_DivX_Active=0;
#if defined(__vita__)
	m_VitaBink.isValid = false;
	m_VitaBink.instanceIndex = -1;
#endif
}

#if defined(__vita__)
/* NOTE for anyone tempted to add path fallbacks here: do not call Bink_Open
   repeatedly to try alternative paths.  libbinkdec allocates from a fixed
   instance pool and a failed open still consumes a slot, so probing several
   candidates exhausts the pool and takes the whole app down.  Resolve the path
   before opening, and open exactly once. */

//! Bink hands frames back as planar YUV with the chroma planes at half
//! resolution; the renderer wants packed RGBA, so convert as we copy.
static void VitaBinkYUVToRGBA(const YUVbuffer yuv, int *pDest, int width, int height)
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

	for (int y = 0; y < height; ++y)
	{
		const uint8_t *rowY = planeY.data + (size_t)y * planeY.pitch;
		const uint8_t *rowU = planeU.data + (size_t)(y >> 1) * planeU.pitch;
		const uint8_t *rowV = planeV.data + (size_t)(y >> 1) * planeV.pitch;
		int *rowDest = pDest + (size_t)y * width;

		/* Chroma is subsampled 2:1 across, so both pixels of a pair share d and
		   e -- and therefore share every term derived from them.  Computing
		   those once per pair instead of once per pixel removes four of the six
		   multiplies per pixel; only the luma term stays per pixel. */
		for (int x = 0; x < width; x += 2)
		{
			const int nChroma = x >> 1;
			const int d = (int)rowU[nChroma] - 128;
			const int e = (int)rowV[nChroma] - 128;

			const int rTerm =  409 * e + 128;
			const int gTerm = -100 * d - 208 * e + 128;
			const int bTerm =  516 * d + 128;

			const int nPair = (x + 1 < width) ? 2 : 1;
			for (int i = 0; i < nPair; ++i)
			{
				const int nLuma = 298 * ((int)rowY[x + i] - 16);
				// +384 biases into the clamp table, which covers [-384, 639].
				const unsigned r = s_arrClamp[((nLuma + rTerm) >> 8) + 384];
				const unsigned g = s_arrClamp[((nLuma + gTerm) >> 8) + 384];
				const unsigned b = s_arrClamp[((nLuma + bTerm) >> 8) + 384];

				// Matches the GL_RGBA byte order the dynamic texture is uploaded with.
				rowDest[x + i] = (int)(0xFF000000u | (b << 16) | (g << 8) | r);
			}
		}
	}
}

//! Decodes the next frame if one is due. Returns false when the video ended.
bool CUIVideoPanel::VitaAdvanceFrame()
{
	if (!m_VitaBink.isValid || !m_pSwapBuffer || m_iTextureID <= 0)
		return false;

	ITimer *pTimer = m_pUISystem->GetISystem()->GetITimer();
	const float fNow = pTimer ? pTimer->GetAsyncCurTime() : 0.0f;
	if (fNow < m_fVitaNextFrameTime)
		return true;	// not time for the next frame yet
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
	if (!Bink_GetNextFrame(m_VitaBink, yuv))
		return m_bLooping;

	VitaBinkYUVToRGBA(yuv, m_pSwapBuffer, m_nVitaWidth, m_nVitaHeight);
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
	m_szVideoFile = szResolved;

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

	m_nVitaWidth = (int)nWidth;
	m_nVitaHeight = (int)nHeight;
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
	if (!m_pSwapBuffer)
	{
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
		m_bPlaying = 0;
		return 0;
	}
	memset(m_pSwapBuffer, 0, sizeof(int) * m_nVitaWidth * m_nVitaHeight);

	m_iTextureID = m_pUISystem->GetIRenderer()->DownLoadToVideoMemory(
		(unsigned char *)m_pSwapBuffer, m_nVitaWidth, m_nVitaHeight,
		eTF_0888, eTF_0888, 0, 0, FILTER_LINEAR, 0, "$VideoPanel", FT_DYNAMIC);

	if (m_iTextureID == 0 || m_iTextureID == -1)
	{
		Bink_Close(m_VitaBink);
		m_VitaBink.isValid = false;
		m_VitaBink.instanceIndex = -1;
		delete [] m_pSwapBuffer;
		m_pSwapBuffer = 0;
		m_iTextureID = -1;
		m_bPlaying = 0;
		return 0;
	}

	m_bPaused = 0;
	m_bPlaying = 0;	// Play() starts it, matching the desktop contract
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
		if (m_bPlaying && !m_bPaused && m_VitaBink.isValid)
		{
			if (!VitaAdvanceFrame())
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
	if (!m_VitaBink.isValid)
	{
		if (m_szVideoFile.empty() || !LoadVideo(m_szVideoFile, 1))
			return 0;
	}
	m_bPlaying = 1;
	m_bPaused = 0;
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
	return 1;
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
	m_bPlaying = 0;
	m_bPaused = 0;
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
