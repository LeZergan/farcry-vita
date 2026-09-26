//-------------------------------------------------------------------------------------------------
// Author: M�rcio Martins
//
// Purpose:
//  - A Bink Video Control
//
// History:
//  - [8/8/2003] created the file
//
//-------------------------------------------------------------------------------------------------
#ifndef UIVIDEOPANEL_H
#define UIVIDEOPANEL_H 

#define UICLASSNAME_VIDEOPANEL			"UIVideoPanel"


#include "UIWidget.h"
#include "UISystem.h"

#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
#	include "../binksdk/bink.h"
#endif

#if defined(__vita__)
// Exported interface of the vendored LGPL Bink decoder, standing in for the
// licensed RAD SDK.  Not BinkDecoder.h directly -- see VitaBink.h.
#	include "../engine_port/VitaBink.h"
	struct CS_STREAM;
#endif


class CUISystem;


class CUIVideoPanel : public CUIWidget, public _ScriptableEx<CUIVideoPanel>
{
public:

	UI_WIDGET(CUIVideoPanel)

	CUIVideoPanel();
	~CUIVideoPanel();

	CUISystem* GetUISystem() { return m_pUISystem; }

	string GetClassName();

	LRESULT Update(unsigned int iMessage, WPARAM wParam, LPARAM lParam);	//AMD Port
	int Draw(int iPass);

	int InitBink();

	int LoadVideo(const string &szFileName, bool bSound);
//	int LoadVideo_DivX(const string &szFileName, bool bSound);

	int ReleaseVideo();
	int Play();
	int Stop();
	int Pause(bool bPause = 1);
	int IsPlaying();
	int IsPaused();

	int SetVolume(int iTrackID, float fVolume);
	int SetPan(int iTrackID, float fPan);

	int SetFrameRate(int iFrameRate);

	int EnableVideo(bool bEnable = 1);
	int EnableAudio(bool bEnable = 1);

	int OnError(const char *szError);
	int OnFinished();

	static void InitializeTemplate(IScriptSystem *pScriptSystem);

	//------------------------------------------------------------------------------------------------- 
	// Script Functions
	//------------------------------------------------------------------------------------------------- 
	int LoadVideo(IFunctionHandler *pH);
	int ReleaseVideo(IFunctionHandler *pH);

	int Play(IFunctionHandler *pH);
	int Stop(IFunctionHandler *pH);
	int Pause(IFunctionHandler *pH);
	
	int IsPlaying(IFunctionHandler *pH);
	int IsPaused(IFunctionHandler *pH);

	int SetVolume(IFunctionHandler *pH);
	int SetPan(IFunctionHandler *pH);

	int SetFrameRate(IFunctionHandler *pH);

	int EnableVideo(IFunctionHandler *pH);
	int EnableAudio(IFunctionHandler *pH);

	bool					m_DivX_Active;

	string				m_szVideoFile;
#if !defined(WIN64) && !defined(LINUX) && !defined(NOT_USE_BINK_SDK)
	HBINK					m_hBink;
#endif
#if defined(__vita__)
	BinkHandle		m_VitaBink;
	int					m_nVitaSourceWidth;
	int					m_nVitaSourceHeight;
	int						m_nVitaWidth;
	int						m_nVitaHeight;
	int						m_nVitaNumFrames;
	float					m_fVitaFrameRate;
	float					m_fVitaNextFrameTime;	//!< when the next frame is due, in seconds
	//! Set when a video fails to open: OnFinished is fired one frame later, in
	//! Update, where the LoadVideo -> OnError -> OnFinished -> LoadVideo
	//! recursion that overflows Lua's stack cannot form.
	bool					m_bVitaFinishPending;
	int					*m_pVitaDecodeBuffer;
	int					m_nVitaDecodeThread;
	volatile int			m_nVitaWorkerRun;
	volatile int			m_nVitaWorkerPlay;
	/* Decode several frames ahead.  A single ready flag made the decoder stop
	   after every frame until the render thread uploaded it, which also stopped
	   Bink PCM production and guaranteed audio starvation on a busy frame. */
	static const unsigned int kVitaVideoQueueFrames = 4;
	volatile unsigned int	m_nVitaVideoRead;
	volatile unsigned int	m_nVitaVideoWrite;
	volatile unsigned int	m_nVitaLastDecodeUs;
	volatile unsigned int	m_nVitaLastConvertUs;
	unsigned int			m_nVitaPresentedFrames;
	bool					m_bVitaPresentationStarted;
	volatile int			m_nVitaWorkerEOF;
	CS_STREAM				*m_pVitaAudioStream;
	int					m_nVitaAudioChannel;
	bool					m_bVitaAudioEnabled;
	unsigned int			m_nVitaAudioTrack;
	unsigned int			m_nVitaAudioChannels;
	unsigned int			m_nVitaAudioScratchBytes;
	unsigned char			*m_pVitaAudioScratch;
	unsigned char			*m_pVitaAudioRing;
	unsigned int			m_nVitaAudioRingBytes;
	unsigned int			m_nVitaAudioPrebufferBytes;
	volatile unsigned int	m_nVitaAudioRead;
	volatile unsigned int	m_nVitaAudioWrite;
	volatile unsigned int	m_nVitaAudioUnderruns;
	/* Vita-ready .mp4 sidecars use Sony's hardware video path.  The Bink
	   decoder remains as a compatibility fallback for installations which have
	   not run the data converter yet. */
	static const unsigned int kVitaAvFrameBuffers = 5;
	int					m_nVitaAvHandle;
	unsigned int			m_arrVitaAvTextureIds[kVitaAvFrameBuffers];
	void				*m_arrVitaAvGxmTextures[kVitaAvFrameBuffers];
	/* vitaGL's own backing store for each of those textures.  The frame loop
	   re-points the gxm texture at sceAvPlayer's buffers, so this is what has to
	   be put back before glDeleteTextures -- otherwise vitaGL frees memory that
	   belongs to the player and its allocator never recovers. */
	void				*m_arrVitaAvOwnedData[kVitaAvFrameBuffers];
	unsigned int			m_nVitaAvFrameIndex;
	unsigned int			m_nVitaAvIdleTicks;
	int					m_nVitaAvAudioThread;
	int					m_nVitaAvAudioPort;
	int					m_nVitaAvVolume;
	volatile int			m_nVitaAvAudioRun;
	bool				m_bVitaAvActive;
	bool				m_bVitaAvStarted;
	bool				m_bVitaAvFirstFrame;
	bool				m_bVitaAvSound;
	bool					VitaAdvanceFrame();		//!< decodes/uploads a frame, false at end of video
	static int				VitaDecodeThread(unsigned int nArgs, void *pArgs);
	static signed char		VitaAudioCallback(CS_STREAM *pStream, void *pBuffer, int nBytes, int nParam);
	void					VitaStopDecodeThread();
	/* Why a hardware-video open gave up.  Every failure here silently degrades
	   to the software Bink path at a much lower resolution, which looks like a
	   video bug rather than a rejected sidecar, so say which step refused. */
	void					VitaAvLog(const char *szFormat, ...);
	bool					VitaAvPlayerOpen(const char *szPath, bool bSound);
	bool					VitaAvPlayerAdvanceFrame();
	void					VitaAvPlayerClose();
	static int				VitaAvAudioThread(unsigned int nArgs, void *pArgs);
#endif
	bool					m_bPaused;
	bool					m_bPlaying;
	bool					m_bLooping;
	bool					m_bKeepAspect;
	int						m_iTextureID;
	UISkinTexture m_pOverlay;
	int						*m_pSwapBuffer;
};

#endif
