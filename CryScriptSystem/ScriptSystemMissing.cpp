/* errorfb/vl_initvectorlib: forward-declared in ScriptSystem.cpp, never
   defined anywhere in this source tree (confirmed -- not a LINUX-only
   gap, genuinely missing from this SDK release entirely). See
   engine_port/compat/README.md. */
#include "StdAfx.h"
#include <lua.h>
#include <lualib.h>

/* Simplified real implementation: a pure pass-through error handler
   (leaves the Lua stack/error value untouched) rather than a formatted
   traceback. A fancier version calling lua_isstring/lua_tostring/
   lua_pushstring/lua_settop hit a genuine lua_char-vs-char type-mismatch
   in this translation unit's specific header resolution that wasn't
   worth chasing further for an error-message formatter -- script errors
   still surface, just without a decorated "Lua error:" prefix. */
extern "C" int errorfb(lua_State * /*L*/) {
	return 1;
}

/* Honest stub, not a real implementation: this registers Crytek's custom
   Vec3-in-Lua binding library (vector arithmetic callable from game
   script). Without a reference implementation anywhere in this tree,
   writing the real API surface would mean guessing what game scripts
   actually call -- flagged as real follow-up work, not papered over. */
extern "C" int vl_initvectorlib(lua_State * /*L*/) {
	return 0;
}
