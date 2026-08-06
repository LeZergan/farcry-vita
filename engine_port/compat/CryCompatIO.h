/* HANDLE-dependent overlapped-I/O declarations, split out of CryCompat.h
   because HANDLE (CHandle<int,-1>, from CryCommon/LinuxSpecific.h) isn't
   available yet inside that force-included header. This one is a normal
   #include, pulled in from RefStreamEngine.h at a point in the real
   include chain where LinuxSpecific.h has already been processed via
   stdafx.h -> platform.h -> Linux32Specific.h -> LinuxSpecific.h.

   Implementations are in CryCompat.cpp. See its header comment and
   engine_port/compat/README.md for why these are synchronous-under-the-
   hood rather than truly async (an intentional, standard simplification
   for this class of Vita/homebrew port -- CryPak's own synchronous path,
   already compiling clean, is what actually does most loading). */
#pragma once

HANDLE CreateFile(const char *lpFileName, unsigned int dwDesiredAccess, unsigned int dwShareMode,
                   void *lpSecurityAttributes, unsigned int dwCreationDisposition,
                   unsigned int dwFlagsAndAttributes, HANDLE hTemplateFile);
bool CloseHandle(HANDLE hObject);
bool CancelIo(HANDLE hFile);
unsigned int GetFileSize(HANDLE hFile, unsigned int *lpFileSizeHigh);
bool ReadFile(HANDLE hFile, void *lpBuffer, unsigned int nNumberOfBytesToRead,
              unsigned int *lpNumberOfBytesRead, void *lpOverlapped);
bool ReadFileEx(HANDLE hFile, void *lpBuffer, unsigned int nNumberOfBytesToRead,
                void *lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
unsigned int SetFilePointer(HANDLE hFile, long lDistanceToMove, long *lpDistanceToMoveHigh,
                             unsigned int dwMoveMethod);
int GetOverlappedResult(HANDLE hFile, void *lpOverlapped, unsigned int *lpNumberOfBytesTransferred, bool bWait);

/* ZipDirStructures.cpp: converts a DOS-encoded zip entry timestamp to a
   FILETIME via this Win32-style intermediate struct. */
struct SYSTEMTIME {
	unsigned short wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};
bool SystemTimeToFileTime(const SYSTEMTIME *st, FILETIME *ft);
