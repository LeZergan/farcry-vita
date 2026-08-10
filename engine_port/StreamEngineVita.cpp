#include "RenderPCH.h"
#include "StreamEngineVita.h"
#include <ICryPak.h>

CVitaReadStream::CVitaReadStream(DWORD_PTR dwUserData)
	: m_pBuffer(NULL), m_nBytesRead(0), m_bError(false), m_dwUserData(dwUserData)
{
}

CVitaReadStream::~CVitaReadStream()
{
}

bool CVitaReadStream::IsError() { return m_bError; }
bool CVitaReadStream::IsFinished() { return true; } // Vita: synchronous -- always finished by the time this object exists.
unsigned int CVitaReadStream::GetBytesRead(bool bWait) { return m_nBytesRead; }
const void *CVitaReadStream::GetBuffer() { return m_pBuffer; }
DWORD_PTR CVitaReadStream::GetUserData() { return m_dwUserData; }
void CVitaReadStream::Wait() {} // Vita: already done -- see IsFinished.

void CVitaReadStream::SetResult(void *pBuffer, unsigned int nBytesRead, bool bError)
{
	m_pBuffer = pBuffer;
	m_nBytesRead = nBytesRead;
	m_bError = bError;
}

CVitaStreamEngine::CVitaStreamEngine(ICryPak *pPak)
	: m_pPak(pPak)
{
}

CVitaStreamEngine::~CVitaStreamEngine()
{
}

IReadStreamPtr CVitaStreamEngine::StartRead(const char *szSource, const char *szFile, IStreamCallback *pCallback, StreamReadParams *pParams)
{
	CVitaReadStream *pStream = new CVitaReadStream(pParams ? pParams->dwUserData : 0);

	if (!m_pPak || !szFile)
	{
		pStream->SetResult(NULL, 0, true);
		if (pCallback)
			pCallback->StreamOnComplete(pStream, ERROR_CANT_OPEN_FILE);
		return IReadStreamPtr(pStream);
	}

	FILE *fp = m_pPak->FOpen(szFile, "rb");
	if (!fp)
	{
		pStream->SetResult(NULL, 0, true);
		if (pCallback)
			pCallback->StreamOnComplete(pStream, ERROR_CANT_OPEN_FILE);
		return IReadStreamPtr(pStream);
	}

	unsigned nOffset = pParams ? pParams->nOffset : 0;
	unsigned nWantSize = pParams ? pParams->nSize : 0;

	m_pPak->FSeek(fp, 0, SEEK_END);
	long nFileSize = m_pPak->FTell(fp);
	m_pPak->FSeek(fp, (long)nOffset, SEEK_SET);

	unsigned nReadSize = nWantSize;
	if (nReadSize == 0 && nFileSize > (long)nOffset)
		nReadSize = (unsigned)(nFileSize - nOffset);

	void *pBuffer = (pParams && pParams->pBuffer) ? pParams->pBuffer : (nReadSize > 0 ? new byte[nReadSize] : NULL);
	unsigned nActuallyRead = 0;
	if (pBuffer && nReadSize > 0)
		nActuallyRead = (unsigned)m_pPak->FRead(pBuffer, 1, nReadSize, fp);

	m_pPak->FClose(fp);

	bool bError = (nActuallyRead != nReadSize);
	pStream->SetResult(pBuffer, nActuallyRead, bError);

	if (pCallback)
		pCallback->StreamOnComplete(pStream, bError ? ERROR_UNKNOWN_ERROR : 0);

	return IReadStreamPtr(pStream);
}

unsigned CVitaStreamEngine::GetFileSize(const char *szFile, unsigned nCryPakFlags)
{
	if (!m_pPak || !szFile)
		return 0;
	FILE *fp = m_pPak->FOpen(szFile, "rb");
	if (!fp)
		return 0;
	m_pPak->FSeek(fp, 0, SEEK_END);
	long nSize = m_pPak->FTell(fp);
	m_pPak->FClose(fp);
	return nSize > 0 ? (unsigned)nSize : 0;
}

void CVitaStreamEngine::Update(unsigned nFlags) {} // Vita: nothing pending -- see StartRead.
unsigned CVitaStreamEngine::Wait(unsigned nMilliseconds, unsigned nFlags) { return 0; }
void CVitaStreamEngine::GetMemoryStatistics(ICrySizer *pSizer) {}
DWORD CVitaStreamEngine::GetStreamCompressionMask() const { return 0; }
