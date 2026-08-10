
#ifndef LUACRYPAK_H
#define LUACRYPAK_H

//////////////////////////////////////////////////////////////////////////
// really annoying since the LUA library can't link any C++ code...

//#define USE_CRYPAK [marco] do not enable this


#ifdef __cplusplus
extern "C" {
#endif

FILE	*LuaCryPakOpen(const char *szFile,const char *szMode);
int	LuaCryPakClose(FILE	*fp);
int 	LuaCryPakFFlush(FILE	*fp);
int   LuaCryPakFSeek(FILE *handle, long seek, int mode);
int   LuaCryPakUngetc(int c, FILE *handle);
int   LuaCryPakGetc(FILE *handle);
size_t LuaCryPakFRead(void *data, size_t length, size_t elems, FILE *handle);
size_t LuaCryPakFWrite(void *data, size_t length, size_t elems, FILE *handle);
int   LuaCryPakFEof(FILE *handle);
int   LuaCryPakFScanf(FILE *handle, const char *format, ...);
int   LuaCryPakFPrintf(FILE *handle, const char *format, ...);
char *LuaCryPakFGets(char *str, int n, FILE *handle);

#ifdef __cplusplus
}
#endif

#endif
