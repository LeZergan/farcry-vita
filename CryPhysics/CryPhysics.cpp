// CryPhysics.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"
//#include <float.h>

#ifndef GAMECUBE
#ifndef _XBOX
//#if !defined(LINUX)
_ACCESS_POOL;
//#endif//LINUX
#endif //_XBOX
#include <CrtDebugStats.h>
#endif

#include "IPhysics.h"
#include "geoman.h"
#include "bvtree.h"
#include "geometry.h"
#include "rigidbody.h"
#include "physicalplaceholder.h"
#include "physicalentity.h"
#include "physicalworld.h"

float g_costab[SINCOSTABSZ],g_sintab[SINCOSTABSZ];

//////////////////////////////////////////////////////////////////////////
// Pointer to Global ISystem.
static ISystem* gISystem = 0;
/* Vita: this module-local GetISystem() collided with CrySystem/System.cpp's
   real one under -Wl,--allow-multiple-definition -- see Cry3DEngine.cpp for
   the full explanation. */
//////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}
#endif

#ifndef _XBOX
CRYPHYSICS_API IPhysicalWorld *CreatePhysicalWorld(ISystem *pSystem)
#else
IPhysicalWorld *CreatePhysicalWorld(ISystem *pSystem)
#endif
{
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld entered, pSystem=%p\n", (void*)pSystem);
	gISystem = pSystem;
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld: before GetCPUFlags\n");
	g_bHasSSE = (pSystem->GetCPUFlags() & CPUF_SSE)!=0;
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld: before sincos loop\n");
	for(int i=0; i<SINCOSTABSZ; i++) {
		g_costab[i] = cosf(i*(pi*0.5f/SINCOSTABSZ));
		g_sintab[i] = sinf(i*(pi*0.5f/SINCOSTABSZ));
	}
	//_controlfp(_EM_ZERODIVIDE,_MCW_EM);
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld: before GetILog\n");
	ILog *pLog = pSystem->GetILog();
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld: before new CPhysicalWorld, pLog=%p\n", (void*)pLog);
	IPhysicalWorld *pWorld = new CPhysicalWorld(pLog);
	sceClibPrintf("[BOOTTRACE] CreatePhysicalWorld: after new CPhysicalWorld\n");
	return pWorld;
}

