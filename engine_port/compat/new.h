/* Compat shim: old MSVC-era code includes <new.h> for placement new.
   Modern toolchains only provide the standard <new>. */
#pragma once
#include <new>
