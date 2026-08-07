//////////////////////////////////////////////////////////////////////
//
//  CryFont Source Code
//
//  File: ICryFont.cpp
//  Description: Create the font interface.
//
//  History:
//  - August 17, 2001: Created by Alberto Demichelis
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#ifndef _XBOX
_ACCESS_POOL;
#endif //_XBOX

#include "CryFont.h"

ISystem *gISystem = 0;
//! Get the system interface 
/* Vita: this module-local GetISystem() collided with CrySystem/System.cpp's
   real one under -Wl,--allow-multiple-definition -- see Cry3DEngine.cpp for
   the full explanation. */

///////////////////////////////////////////////
extern "C" ICryFont* CreateCryFontInterface(ISystem *pSystem)
{
	gISystem = pSystem;
	return new CCryFont(pSystem);
}

///////////////////////////////////////////////
#ifndef _XBOX
#ifndef PS2
#ifndef LINUX /* no DLL-entry-point concept on a statically-linked Vita build */
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}
#endif //LINUX
#endif
#endif