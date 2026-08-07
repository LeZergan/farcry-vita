# engine_port/compat

Shim headers added ahead of CryCommon/CrySystem on the include path so real
FarCry source compiles under arm-vita-eabi-g++. None of these edit the
original Crytek source; the engine tree itself is only touched for genuine
bugs (see below).

Compile with: `-I engine_port/compat -I CryCommon -I CrySystem -I CrySystem/zlib -DLINUX -DLINUX32 -std=gnu++11 -fpermissive`

## Shims (status)

- `new.h` - permanent. Old MSVC placement-new header; redirects to standard `<new>`.
- `dlfcn.h` - **temporary, functionally wrong.** Stubs dlopen/dlsym/dlclose as
  no-ops so CryMemoryManager.h's cross-DLL pool-resolution code compiles.
  Real fix: bypass that code path under a VITA build define instead -- Vita
  builds link everything into one static binary, so dynamic symbol
  resolution across "DLLs" should never happen at all.
- `WinBase.h` - stub, empty. Included unconditionally by platform.h under
  `LINUX` for a resource-compiler macro (`RC_EXECUTABLE`) that isn't
  exercised by the engine/game build. If a real symbol from here ever turns
  out to be needed, it means this LINUX branch never actually built
  upstream and needs a proper fix at the include site.
- `sys/io.h` - stub, empty. x86 raw I/O port access; meaningless on ARM.
- `asm/msr.h` - stub, empty. x86 Model-Specific-Register access, pulled in
  unconditionally by RenderDll/RenderPCH.h under `LINUX`; nothing in the
  renderer actually calls an MSR intrinsic from it.

## Real source fixes applied directly (not shims)

- `CryCommon/LinuxSpecific.h`: `#include </usr/include/ctype.h>` (hardcoded
  absolute path, a genuine bug) -> `#include <ctype.h>`.
- `CryCommon/Cry_Vector3.h`: four `Vec3_tpl<f32>::Vec3_tpl(...)` explicit
  specializations were missing the required `template<>` prefix -- valid
  under old permissive MSVC, rejected by modern GCC. Added `template<>`.

## Build defines required

- `-DLINUX -DLINUX32`: routes CryCommon through its existing (partial)
  Linux-server portability branch (`Linux32Specific.h` etc.) instead of the
  Win32 one. Crytek shipped this for their old dedicated-server Linux build.

## Known finding: the LINUX CryPak path was never finished upstream

`CryPak.cpp`/`CryPak.h` call several helper functions --
`adaptFilenameToLinux`, `comparePathNames`, `getFilenameNoCase`,
`fopen_nocase`, `replaceDoublePathFilename` -- that are **declared but never
defined anywhere in this source tree**. Combined with `CritSection.h` using
raw `CRITICAL_SECTION`/`InitializeCriticalSection` with no LINUX branch at
all, and the file-search code using MSVC's `_findfirst64`/`__finddata64_t`
with no POSIX (`opendir`/`readdir`) equivalent, this confirms Crytek's Linux
server port of CryPak was abandoned mid-way. This isn't a "missing header"
problem like the others above -- it needs real implementations, not shims:

- A pthread-mutex-backed `CCritSection` for the Vita build (vitasdk ships
  pthread-embedded already).
- A `Vec3`/`AABB`-style 64-bit file time replacement for `FILETIME`.
- A POSIX `opendir`/`readdir`-based implementation of the five missing
  filename helper functions, replacing the `_findfirst64` family.

This is real, scoped follow-up work -- tracked as task #3 in the session's
task list, not resolved by a shim.
