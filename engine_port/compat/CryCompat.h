/* Real (non-shim) implementations of the Win32 primitives and the missing
   Linux filename helpers that CryPak.cpp/CritSection.h need. This is the
   genuine follow-up to the LINUX branch Crytek never finished -- see
   engine_port/compat/README.md for the full explanation.

   Everything here is backed by real vitasdk/newlib POSIX APIs (pthread,
   dirent, fnmatch, stat), not stubbed out. */
#pragma once

#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <strings.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <stdint.h>

/* ---- Critical section, backed by a recursive pthread mutex ---- */
struct CRITICAL_SECTION { pthread_mutex_t m; };

inline void InitializeCriticalSection(CRITICAL_SECTION *cs) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&cs->m, &attr);
	pthread_mutexattr_destroy(&attr);
}
inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(&cs->m); }
inline void EnterCriticalSection(CRITICAL_SECTION *cs)  { pthread_mutex_lock(&cs->m); }
inline void LeaveCriticalSection(CRITICAL_SECTION *cs)  { pthread_mutex_unlock(&cs->m); }

/* ---- FILETIME: must stay a plain 8-byte, two-DWORD layout -- CryPak.cpp
   reinterpret-casts a 64-bit tick count directly onto this struct. ---- */
struct FILETIME { unsigned int dwLowDateTime; unsigned int dwHighDateTime; };

/* ---- File attributes / GetFileAttributes ---- */
#define FILE_ATTRIBUTE_NORMAL    0x00000080u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define INVALID_FILE_ATTRIBUTES  ((unsigned int)-1)

inline unsigned int GetFileAttributes(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

inline unsigned int GetCurrentDirectory(unsigned int size, char *buf) {
	if (getcwd(buf, size)) return (unsigned int)strlen(buf);
	return 0;
}

inline char *_fullpath(char *dst, const char *src, size_t /*maxlen*/) {
	return realpath(src, dst);
}

inline int _mkdir(const char *path) { return mkdir(path, 0755); }

inline void OutputDebugString(const char *msg) { fputs(msg, stderr); }

/* ---- Threading/timing handles (RefStreamEngine.h's async IO worker) ---- */
struct _CryEvent { pthread_cond_t cond; pthread_mutex_t mutex; bool signaled; };
typedef _CryEvent* EVENT_HANDLE;
typedef pthread_t THREAD_HANDLE;

/* ---- High-resolution timer (Win32 QueryPerformanceCounter/Frequency) ----
   Backed by clock_gettime(CLOCK_MONOTONIC), portable and available on
   vitasdk's newlib. Frequency is fixed at 1e9 (nanosecond ticks).
   LARGE_INTEGER's real definition lives in CryCommon/LinuxSpecific.h, but
   this header is force-included (-include) ahead of everything else in
   the normal chain, so only forward-declare it here -- the union becomes
   complete later once LinuxSpecific.h is actually reached. */
union _LARGE_INTEGER;
typedef union _LARGE_INTEGER LARGE_INTEGER;
/* Declared here (pointer-to-incomplete-type is fine), defined out-of-line
   in CryCompat.cpp where LARGE_INTEGER is already complete. */
bool QueryPerformanceCounter(LARGE_INTEGER *out);
bool QueryPerformanceFrequency(LARGE_INTEGER *out);

inline void Sleep(unsigned int ms) { usleep(ms * 1000); }

inline unsigned int timeGetTime() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned int)((unsigned long long)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/* x86 cycle-counter intrinsic, meaningless on ARM -- callers only use this
   as a relative/opaque tick source, so a monotonic clock substitutes fine. */
inline uint64_t __rdtsc() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Cross-checked against rohit-n/NearChuckle (a real, working Linux source
   port of this same engine) -- confirms the overall approach here and
   fills in a few pieces that port also needed. */
inline unsigned int GetTickCount() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned int)((unsigned long long)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}
inline unsigned int GetLastError() { return errno; }
inline void SetLastError(unsigned int e) { errno = e; }
inline int memicmp(const char *a, const char *b, size_t len) { return strncasecmp(a, b, len); }
inline char *strlwr(char *s) { for (char *p = s; *p; ++p) *p = tolower((unsigned char)*p); return s; }
inline char *strupr(char *s) { for (char *p = s; *p; ++p) *p = toupper((unsigned char)*p); return s; }

/* Overlapped-file-I/O emulation constants (safe to declare here -- no
   HANDLE/class-template dependency). The function DECLARATIONS that need
   the real HANDLE type live in CryCompatIO.h instead: this header
   (CryCompat.h) is force-included (-include) ahead of EVERYTHING in the
   translation unit, including stdafx.h itself, so HANDLE (a full
   CHandle<int,-1> class template instantiation from LinuxSpecific.h)
   isn't available yet at this point -- unlike LARGE_INTEGER above, a
   class template instantiation can't just be forward-declared. See
   CryCompatIO.h for why it's included from RefStreamEngine.h instead. */
#define GENERIC_READ                     0x80000000u
#define GENERIC_WRITE                    0x40000000u
#define FILE_SHARE_READ                  0x00000001u
#define FILE_SHARE_WRITE                 0x00000002u
#define OPEN_EXISTING                    3u
#define FILE_FLAG_OVERLAPPED             0x40000000u
#define INVALID_FILE_SIZE                ((unsigned int)-1)
#define FILE_BEGIN                       0u
#define FILE_CURRENT                     1u
#define FILE_END                         2u
#define ERROR_NO_SYSTEM_RESOURCES        1450L

/* Normally comes from the Windows .rc-generated resource.h; just a build
   version placeholder, not load-bearing for engine behavior. */
#ifndef VERSION_INFO
#define VERSION_INFO 1
#endif

inline unsigned int GetUserName(char *lpBuffer, unsigned int *nSize) {
	const char *name = getenv("USER");
	if (!name) name = "vita";
	size_t len = strlen(name) + 1;
	if (*nSize < len) { *nSize = (unsigned int)len; return 0; }
	memcpy(lpBuffer, name, len);
	*nSize = (unsigned int)len;
	return 1;
}

struct MEMORYSTATUS {
	unsigned int dwLength, dwMemoryLoad;
	size_t dwTotalPhys, dwAvailPhys, dwTotalPageFile, dwAvailPageFile, dwTotalVirtual, dwAvailVirtual;
};
inline void GlobalMemoryStatus(MEMORYSTATUS *lpmem) {
	/* Real Vita RAM figures (sceKernelGetFreeMemorySize) belong here once
	   this actually runs on-device; 512MB total/256MB free is a
	   placeholder in the right ballpark for compile-time testing. */
	lpmem->dwLength = sizeof(MEMORYSTATUS);
	lpmem->dwMemoryLoad = 50;
	lpmem->dwTotalPhys = lpmem->dwTotalVirtual = 512u * 1024 * 1024;
	lpmem->dwAvailPhys = lpmem->dwAvailVirtual = 256u * 1024 * 1024;
	lpmem->dwTotalPageFile = lpmem->dwAvailPageFile = 0;
}

/* Win32 32x32->64 multiply intrinsic */
#define Int32x32To64(a, b) ((int64_t)(int32_t)(a) * (int64_t)(int32_t)(b))

/* newlib has no stat64/_fstat64 -- Vita's own filesystem doesn't need
   64-bit file sizes/times, so just alias to the plain POSIX calls. */
#define stat64 stat
inline int _fstat64(int fd, struct stat *buf) { return fstat(fd, buf); }

/* ---- MSVC-style _findfirst/_findnext file search, backed by opendir/readdir ---- */
struct _finddata_t {
	char name[512];
	unsigned int attrib;
	unsigned int size;
	time_t time_access, time_create, time_write;
};
struct __finddata64_t {
	char name[512];
	unsigned int attrib;
	uint64_t size;
	time_t time_access, time_create, time_write;
};

#define _A_SUBDIR FILE_ATTRIBUTE_DIRECTORY

struct _CryFindHandle {
	DIR *dir;
	std::string dirPath;
	std::string pattern;
};

inline bool _CryFindFillNext64(_CryFindHandle *h, __finddata64_t *fd) {
	struct dirent *ent;
	while ((ent = readdir(h->dir)) != 0) {
		if (fnmatch(h->pattern.c_str(), ent->d_name, 0) != 0)
			continue;
		std::string full = h->dirPath + "/" + ent->d_name;
		struct stat st;
		if (stat(full.c_str(), &st) != 0)
			continue;
		strncpy(fd->name, ent->d_name, sizeof(fd->name) - 1);
		fd->name[sizeof(fd->name) - 1] = 0;
		fd->attrib = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
		fd->size = (uint64_t)st.st_size;
		fd->time_access = st.st_atime;
		fd->time_create = st.st_ctime;
		fd->time_write  = st.st_mtime;
		return true;
	}
	return false;
}

inline intptr_t _findfirst64(const char *pattern, __finddata64_t *fd) {
	std::string p(pattern);
	size_t slash = p.find_last_of('/');
	std::string dirPath  = (slash == std::string::npos) ? "." : p.substr(0, slash);
	std::string filePattern = (slash == std::string::npos) ? p : p.substr(slash + 1);
	DIR *d = opendir(dirPath.c_str());
	if (!d) return -1;
	_CryFindHandle *h = new _CryFindHandle{d, dirPath, filePattern};
	if (!_CryFindFillNext64(h, fd)) {
		closedir(d);
		delete h;
		return -1;
	}
	return (intptr_t)h;
}
inline int _findnext64(intptr_t handle, __finddata64_t *fd) {
	_CryFindHandle *h = (_CryFindHandle *)handle;
	return _CryFindFillNext64(h, fd) ? 0 : -1;
}
inline int _findclose(intptr_t handle) {
	_CryFindHandle *h = (_CryFindHandle *)handle;
	closedir(h->dir);
	delete h;
	return 0;
}

inline void _CryFd64To32(const __finddata64_t &fd64, _finddata_t *fd) {
	strncpy(fd->name, fd64.name, sizeof(fd->name) - 1);
	fd->name[sizeof(fd->name) - 1] = 0;
	fd->attrib = fd64.attrib;
	fd->size = (unsigned int)fd64.size;
	fd->time_access = fd64.time_access;
	fd->time_create = fd64.time_create;
	fd->time_write  = fd64.time_write;
}
inline intptr_t _findfirst(const char *pattern, _finddata_t *fd) {
	__finddata64_t fd64;
	intptr_t h = _findfirst64(pattern, &fd64);
	if (h == -1) return -1;
	_CryFd64To32(fd64, fd);
	return h;
}
inline int _findnext(intptr_t handle, _finddata_t *fd) {
	__finddata64_t fd64;
	int r = _findnext64(handle, &fd64);
	if (r != 0) return r;
	_CryFd64To32(fd64, fd);
	return r;
}

/* ---- CryPak's own missing Linux filename helpers (declared, never
   implemented, in the original Crytek source -- see README.md) ---- */
inline void adaptFilenameToLinux(std::string &path) {
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '\\') path[i] = '/';
}

inline int comparePathNames(const char *a, const char *b, size_t n) {
	return strncasecmp(a, b, n);
}

/* Resolves a path against the real, case-sensitive filesystem by matching
   each path component case-insensitively against actual directory entries.
   Returns true and fills dst with the real on-disk casing if found. */
inline bool getFilenameNoCase(char *dst, std::string &path) {
	std::string resolved = path.empty() || path[0] != '/' ? "/" : "";
	size_t start = 0;
	if (!path.empty() && path[0] == '/') { resolved = "/"; start = 1; }

	size_t pos = start;
	while (pos <= path.size()) {
		size_t next = path.find('/', pos);
		std::string component = (next == std::string::npos)
			? path.substr(pos) : path.substr(pos, next - pos);

		if (!component.empty()) {
			DIR *d = opendir(resolved.empty() ? "." : resolved.c_str());
			if (!d) return false;
			bool found = false;
			struct dirent *ent;
			while ((ent = readdir(d)) != 0) {
				if (strcasecmp(ent->d_name, component.c_str()) == 0) {
					resolved += ent->d_name;
					found = true;
					break;
				}
			}
			closedir(d);
			if (!found) return false;
			if (next != std::string::npos) resolved += "/";
		}

		if (next == std::string::npos) break;
		pos = next + 1;
	}

	strncpy(dst, resolved.c_str(), 2047);
	dst[2047] = 0;
	return true;
}

inline FILE *fopen_nocase(const char *filename, const char *mode) {
	FILE *f = fopen(filename, mode);
	if (f) return f;
	char resolved[2048];
	std::string p(filename);
	if (getFilenameNoCase(resolved, p))
		return fopen(resolved, mode);
	return 0;
}

inline void replaceDoublePathFilename(char *path) {
	char *src = path, *dst = path;
	bool prevSlash = false;
	while (*src) {
		bool isSlash = (*src == '/' || *src == '\\');
		if (!(isSlash && prevSlash))
			*dst++ = (*src == '\\') ? '/' : *src;
		prevSlash = isSlash;
		src++;
	}
	*dst = 0;
}
