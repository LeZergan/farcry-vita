#pragma once


#if !defined(LINUX)
	#include <wininet.h>
	#include <dbghelp.h>
#else
	/* Opaque handle type only, matching the existing pattern for other
	   Win32 handles on this platform (see LinuxSpecific.h's HANDLE).
	   Download()'s actual WinINet-based implementation only compiles
	   under WIN32 to begin with (see HTTPDownloader.cpp, excluded from
	   this build -- see engine_port/compat/README.md); this only needs
	   to exist so the class itself, and the InitializeTemplate/
	   ReleaseTemplate methods ScriptBinding.cpp's real script-registration
	   code calls, compile and link. */
	typedef void *HINTERNET;
#endif
#include <IScriptSystem.h>
#include <_ScriptableEx.h>


#define HTTP_BUFFER_SIZE		(16384)


enum
{
	HTTP_STATE_WORKING = 0,
	HTTP_STATE_COMPLETE,
	HTTP_STATE_CANCELED,
	HTTP_STATE_ERROR,
	HTTP_STATE_NONE,
};


class CDownloadManager;


class CHTTPDownloader:
	public _ScriptableEx<CHTTPDownloader>
{
public:
	CHTTPDownloader();
	virtual ~CHTTPDownloader();

	static
	void InitializeTemplate(IScriptSystem *pSS);

	int Create(ISystem *pISystem, CDownloadManager *pParent);
	int	Download(const char *szURL, const char *szDestination);
	void Cancel();
	int GetState() { return m_iState; };
	int	GetFileSize() const { return m_iFileSize; };
	const string& GetURL() const { return m_szURL; };
	const string& GetDstFileName() const { return m_szDstFile; };
	void	Release();

	int Download(IFunctionHandler *pH);
	int Cancel(IFunctionHandler *pH);
	int Release(IFunctionHandler *pH);

	int GetURL(IFunctionHandler *pH);
	int GetFileSize(IFunctionHandler *pH);
	int GetFileName(IFunctionHandler *pH);

	void OnError();
	void OnComplete();
	void OnCancel();

private:

	static
	DWORD DownloadProc(CHTTPDownloader *_this);
	void	CreateThread();
	DWORD DoDownload();
	void	PrepareBuffer();

	string						m_szURL;
	string						m_szDstFile;
	THREAD_HANDLE 		m_hThread;
	HINTERNET					m_hINET;
	HINTERNET					m_hUrl;

	unsigned char			*m_pBuffer;
	int								m_iFileSize;

	volatile int			m_iState;
	volatile bool			m_bContinue;

	ISystem						*m_pSystem;
	CDownloadManager	*m_pParent;
};

//#endif //LINUX