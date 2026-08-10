//////////////////////////////////////////////////////////////////////////
// Declaration of CStreamEngine: implementation of IStreamEngine interface
// 
#ifndef _CRY_SYSTEM_STREAM_ENGINE_HDR_
#define _CRY_SYSTEM_STREAM_ENGINE_HDR_

#if defined(LINUX)
// Vita: CRefStreamEngine's real constructor hangs on Vita3K -- see
// engine_port/StreamEngineVita.h for the full explanation and the real
// (synchronous) replacement used instead.
#include "../engine_port/StreamEngineVita.h"
typedef CVitaStreamEngine CStreamEngine;
#else
#include "RefStreamEngine.h"
// This is reference implementation
typedef CRefStreamEngine CStreamEngine;
#endif

#endif