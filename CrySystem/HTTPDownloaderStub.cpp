/* HTTPDownloader.cpp itself is excluded from this build: its real
   implementation is entirely WinINet-based (InternetOpen/InternetOpenUrl/
   InternetReadFile), a Windows-only API with no portable equivalent
   wired up in this tree. DownloadManager.cpp (now compiled for real)
   still references these methods though. HTTP downloads aren't needed
   to boot the engine or render single-player assets on Vita, so this is
   an honest "downloads unsupported on this platform" implementation,
   not a fake success. See engine_port/compat/README.md. */
#include "StdAfx.h"
#include "HTTPDownloader.h"
#include "DownloadManager.h"

CHTTPDownloader::CHTTPDownloader()
: m_hThread(INVALID_HANDLE_VALUE),
	m_hINET(0),
	m_hUrl(0),
	m_iFileSize(0),
	m_pBuffer(0),
	m_iState(HTTP_STATE_NONE),
	m_pSystem(0),
	m_pParent(0)
{
}

CHTTPDownloader::~CHTTPDownloader()
{
}

int CHTTPDownloader::Create(ISystem *pISystem, CDownloadManager *pParent)
{
	m_pSystem = pISystem;
	m_pParent = pParent;
	m_iState = HTTP_STATE_ERROR;
	return 0;
}

void CHTTPDownloader::Release()
{
	if (m_pBuffer)
	{
		delete[] m_pBuffer;
		m_pBuffer = 0;
	}
	if (m_pParent)
		m_pParent->RemoveDownload(this);
	delete this;
}

void CHTTPDownloader::OnError()
{
}

void CHTTPDownloader::OnComplete()
{
}

void CHTTPDownloader::OnCancel()
{
}
