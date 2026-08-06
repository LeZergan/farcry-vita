/* Out-of-line definitions for CryCompat.h declarations that need a
   complete LARGE_INTEGER (only available once CryCommon/LinuxSpecific.h
   has actually been processed via the normal include chain -- CryCompat.h
   itself is force-included ahead of that, so can only declare these). */
#include "CryCompat.h"
#include <platform.h> /* pulls in LinuxSpecific.h -> the real LARGE_INTEGER/HANDLE/FILETIME definitions */
#include "CryCompatIO.h"
#include <time.h>

bool QueryPerformanceCounter(LARGE_INTEGER *out) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	out->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return true;
}

bool QueryPerformanceFrequency(LARGE_INTEGER *out) {
	out->QuadPart = 1000000000LL;
	return true;
}

#include <cstdio>
#include <cstring>

static FILE *HandleToFile(HANDLE h) {
	int v = (int)(HANDLE::HandleType)h;
	return v == -1 ? 0 : (FILE *)(intptr_t)v;
}
static HANDLE FileToHandle(FILE *f) {
	return f ? (HANDLE)(int)(intptr_t)f : (HANDLE)(int)-1l;
}

HANDLE CreateFile(const char *lpFileName, unsigned int dwDesiredAccess, unsigned int /*dwShareMode*/,
                   void * /*lpSecurityAttributes*/, unsigned int /*dwCreationDisposition*/,
                   unsigned int /*dwFlagsAndAttributes*/, HANDLE /*hTemplateFile*/) {
	const char *mode = (dwDesiredAccess & GENERIC_WRITE) ? "r+b" : "rb";
	FILE *f = fopen(lpFileName, mode);
	return FileToHandle(f);
}

bool CloseHandle(HANDLE hObject) {
	FILE *f = HandleToFile(hObject);
	if (!f) return false;
	fclose(f);
	return true;
}

bool CancelIo(HANDLE /*hFile*/) { return true; /* nothing truly in-flight to cancel */ }

unsigned int GetFileSize(HANDLE hFile, unsigned int *lpFileSizeHigh) {
	FILE *f = HandleToFile(hFile);
	if (lpFileSizeHigh) *lpFileSizeHigh = 0;
	if (!f) return INVALID_FILE_SIZE;
	long cur = ftell(f);
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, cur, SEEK_SET);
	return (unsigned int)sz;
}

bool ReadFile(HANDLE hFile, void *lpBuffer, unsigned int nNumberOfBytesToRead,
              unsigned int *lpNumberOfBytesRead, void *lpOverlapped) {
	FILE *f = HandleToFile(hFile);
	if (!f) return false;
	if (lpOverlapped)
		fseek(f, (long)((OVERLAPPED *)lpOverlapped)->Offset, SEEK_SET);
	size_t n = fread(lpBuffer, 1, nNumberOfBytesToRead, f);
	if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (unsigned int)n;
	return true;
}

bool ReadFileEx(HANDLE hFile, void *lpBuffer, unsigned int nNumberOfBytesToRead,
                void *lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	OVERLAPPED *ov = (OVERLAPPED *)lpOverlapped;
	unsigned int read = 0;
	bool ok = ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, &read, ov);
	ov->dwNumberOfBytesTransfered = read;
	if (lpCompletionRoutine)
		lpCompletionRoutine(ok ? 0 : 1, read, ov);
	return ok;
}

unsigned int SetFilePointer(HANDLE hFile, long lDistanceToMove, long * /*lpDistanceToMoveHigh*/,
                             unsigned int dwMoveMethod) {
	FILE *f = HandleToFile(hFile);
	if (!f) return INVALID_FILE_SIZE;
	int whence = (dwMoveMethod == FILE_BEGIN) ? SEEK_SET : (dwMoveMethod == FILE_CURRENT) ? SEEK_CUR : SEEK_END;
	fseek(f, lDistanceToMove, whence);
	return (unsigned int)ftell(f);
}

int GetOverlappedResult(HANDLE /*hFile*/, void *lpOverlapped, unsigned int *lpNumberOfBytesTransferred, bool /*bWait*/) {
	OVERLAPPED *ov = (OVERLAPPED *)lpOverlapped;
	if (lpNumberOfBytesTransferred) *lpNumberOfBytesTransferred = ov->dwNumberOfBytesTransfered;
	return 1; /* TRUE: the (synchronous, already-complete) read succeeded */
}

#include <pthread.h>

EVENT_HANDLE CreateEvent(void *, bool /*bManualReset*/, bool bInitialState, const char *) {
	_CryEvent *e = new _CryEvent();
	pthread_mutex_init(&e->mutex, 0);
	pthread_cond_init(&e->cond, 0);
	e->signaled = bInitialState;
	return e;
}
bool SetEvent(EVENT_HANDLE hEvent) {
	pthread_mutex_lock(&hEvent->mutex);
	hEvent->signaled = true;
	pthread_cond_broadcast(&hEvent->cond);
	pthread_mutex_unlock(&hEvent->mutex);
	return true;
}
bool ResetEvent(EVENT_HANDLE hEvent) {
	pthread_mutex_lock(&hEvent->mutex);
	hEvent->signaled = false;
	pthread_mutex_unlock(&hEvent->mutex);
	return true;
}
unsigned int WaitForSingleObject(EVENT_HANDLE hHandle, unsigned int dwMilliseconds) {
	pthread_mutex_lock(&hHandle->mutex);
	unsigned int result = WAIT_OBJECT_0;
	if (!hHandle->signaled) {
		if (dwMilliseconds == INFINITE) {
			while (!hHandle->signaled)
				pthread_cond_wait(&hHandle->cond, &hHandle->mutex);
		} else {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += dwMilliseconds / 1000;
			ts.tv_nsec += (dwMilliseconds % 1000) * 1000000L;
			if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
			while (!hHandle->signaled) {
				if (pthread_cond_timedwait(&hHandle->cond, &hHandle->mutex, &ts) != 0) {
					result = WAIT_TIMEOUT;
					break;
				}
			}
		}
	}
	/* auto-reset semantics, matching how RefStreamEngine actually uses these */
	if (result == WAIT_OBJECT_0) hHandle->signaled = false;
	pthread_mutex_unlock(&hHandle->mutex);
	return result;
}
unsigned int WaitForSingleObjectEx(EVENT_HANDLE hHandle, unsigned int dwMilliseconds, bool /*bAlertable*/) {
	return WaitForSingleObject(hHandle, dwMilliseconds);
}

struct _CryThreadStart { unsigned int (*fn)(void *); void *arg; };
static void *_CryThreadTrampoline(void *p) {
	_CryThreadStart *s = (_CryThreadStart *)p;
	unsigned int (*fn)(void *) = s->fn;
	void *arg = s->arg;
	delete s;
	fn(arg);
	return 0;
}
THREAD_HANDLE CreateThread(void *, size_t, unsigned int (*lpStartAddress)(void *),
                           void *lpParameter, unsigned int, unsigned int *lpThreadId) {
	pthread_t t;
	_CryThreadStart *s = new _CryThreadStart{lpStartAddress, lpParameter};
	pthread_create(&t, 0, _CryThreadTrampoline, s);
	if (lpThreadId) *lpThreadId = (unsigned int)t;
	return t;
}
unsigned int GetCurrentThreadId() { return (unsigned int)pthread_self(); }
unsigned int SleepEx(unsigned int dwMilliseconds, bool) { usleep(dwMilliseconds * 1000); return 0; }

bool SystemTimeToFileTime(const SYSTEMTIME *st, FILETIME *ft) {
	struct tm tmv;
	memset(&tmv, 0, sizeof(tmv));
	tmv.tm_year = st->wYear - 1900;
	tmv.tm_mon  = st->wMonth - 1;
	tmv.tm_mday = st->wDay;
	tmv.tm_hour = st->wHour;
	tmv.tm_min  = st->wMinute;
	tmv.tm_sec  = st->wSecond;
	time_t t = mktime(&tmv);
	long long ll = (long long)t * 10000000LL + 116444736000000000LL;
	ft->dwLowDateTime  = (unsigned int)(ll & 0xFFFFFFFFu);
	ft->dwHighDateTime = (unsigned int)(ll >> 32);
	return true;
}
