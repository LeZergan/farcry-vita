/* Vita: real (synchronous) IStreamEngine implementation.
   CRefStreamEngine (CrySystem/RefStreamEngine.cpp) -- the real engine's
   real streaming implementation -- hangs in its own constructor on Vita3K
   (see CSystem::InitStreamEngine's comment in CrySystem/SystemInit.cpp);
   a prior session ruled out several causes and left m_pStreamEngine
   permanently NULL as a documented, deliberate scope cut, with every
   m_pStreamEngine call site meant to be null-checked.

   That held until real level loading started calling
   CStatObj::StreamCCGF (Cry3DEngine/StatObjStream.cpp), which calls
   m_pSys->GetStreamEngine()->StartRead(...) unconditionally -- there is no
   alternate non-streaming load path in this codebase, so a null check
   there isn't an option; CStatObj genuinely needs a real, working
   IStreamEngine to load any static object at all.

   This class is that real implementation, just synchronous instead of
   threaded: StartRead reads the whole (requested) file immediately via
   ICryPak and calls the completion callback before returning, exactly the
   usage the interface's own docs anticipate ("the error/success/progress
   callbacks can also be called from INSIDE this function" --
   CryCommon/IStreamEngine.h). Real file I/O through the real CryPak/pak
   system, not fabricated data -- just no background thread, no priority
   scheduling, no partial/progressive reads. See CrySystem/StreamEngine.h
   for how this replaces CRefStreamEngine under LINUX. */
#ifndef ENGINE_PORT_STREAM_ENGINE_VITA_H
#define ENGINE_PORT_STREAM_ENGINE_VITA_H

#include <IStreamEngine.h>

/* Vita: CrySystem/System.h needs CCryPak's full definition (it upcasts
   `CCryPak* m_pIPak` to `ICryPak*` inline in GetIPak()) -- previously this
   header's slot in the include chain (CrySystem/RefStreamEngine.h) pulled
   this in transitively via CryPak.h. */
#include "CryPak.h"

class CVitaReadStream : public IReadStream
{
public:
	CVitaReadStream(DWORD_PTR dwUserData);
	~CVitaReadStream();

	virtual bool IsError();
	virtual bool IsFinished();
	virtual unsigned int GetBytesRead(bool bWait = false);
	virtual const void *GetBuffer();
	virtual DWORD_PTR GetUserData();
	virtual void Wait();

	void SetResult(void *pBuffer, unsigned int nBytesRead, bool bError);

private:
	void *m_pBuffer;
	unsigned int m_nBytesRead;
	bool m_bError;
	DWORD_PTR m_dwUserData;
};

class CVitaStreamEngine : public IStreamEngine
{
public:
	CVitaStreamEngine(class ICryPak *pPak);
	virtual ~CVitaStreamEngine();

	virtual IReadStreamPtr StartRead(const char *szSource, const char *szFile, IStreamCallback *pCallback = NULL, StreamReadParams *pParams = NULL);
	virtual unsigned GetFileSize(const char *szFile, unsigned nCryPakFlags = 0);
	virtual void Update(unsigned nFlags = 0);
	virtual unsigned Wait(unsigned nMilliseconds, unsigned nFlags = 0);
	virtual void GetMemoryStatistics(ICrySizer *pSizer);
	virtual DWORD GetStreamCompressionMask() const;

	// Vita: real no-ops -- CSystem::Update (CrySystem/System.cpp) calls
	// these directly on the concrete CStreamEngine type (they aren't part
	// of IStreamEngine), tuning parameters for the real threaded engine
	// this class doesn't have.
	void SetCallbackTimeQuota(unsigned nMillis) {}
	void SetStreamCompressionMask(DWORD nMask) {}

private:
	class ICryPak *m_pPak;
};

#endif
