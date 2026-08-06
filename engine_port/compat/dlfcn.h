/* Compat shim: satisfies the LINUX branch's dlopen/dlsym/dlclose declarations
   so cross-DLL memory-pool resolution code compiles. Vita has no dynamic
   loading -- everything link statically into one binary, so these must never
   actually be called at runtime. Real fix: bypass this code path entirely
   under a VITA build define instead of relying on these stubs. */
#pragma once

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 0x100

#ifdef __cplusplus
extern "C" {
#endif

static inline void *dlopen(const char *filename, int flag) { (void)filename; (void)flag; return 0; }
static inline void *dlsym(void *handle, const char *symbol) { (void)handle; (void)symbol; return 0; }
static inline int   dlclose(void *handle) { (void)handle; return 0; }
static inline char *dlerror(void) { return (char*)"dlfcn stub: dynamic loading unavailable on this platform"; }

#ifdef __cplusplus
}
#endif
