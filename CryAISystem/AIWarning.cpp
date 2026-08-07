/* Declared in StdAfx.h, called from CTriangulator.cpp, never defined
   anywhere in this source tree (confirmed -- not a LINUX-only gap,
   genuinely missing from this SDK release entirely). Real implementation
   matching the ISystem.h CryLogAlways pattern, using ILog::eWarning
   instead of eAlways. See engine_port/compat/README.md. */
#include "StdAfx.h"
#include <ISystem.h>
#include <ILog.h>

void AIWarning( const char *format,... )
{
	if (GetISystem() && GetISystem()->GetILog())
	{
		va_list args;
		va_start(args,format);
		GetISystem()->GetILog()->LogV( ILog::eWarning,format,args );
		va_end(args);
	}
}
