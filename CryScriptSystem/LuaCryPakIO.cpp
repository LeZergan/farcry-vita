
//////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include <stdio.h>
#include "LuaCryPakIO.h"

// needed for crypak
#include <ISystem.h>
#include <ICryPak.h>

//////////////////////////////////////////////////////////////////////////
FILE	*LuaCryPakOpen(const char *szFile,const char *szMode)
{
#ifdef USE_CRYPAK
	ICryPak *pPak=GetISystem()->GetIPak();
	return pPak->FOpen(szFile,szMode);
#else
	return (fopen(szFile,szMode));
#endif
}

//////////////////////////////////////////////////////////////////////////
int LuaCryPakClose(FILE *fp)
{
#ifdef USE_CRYPAK
	ICryPak *pPak=GetISystem()->GetIPak();
	return pPak->FClose(fp);
#else
	return fclose(fp);
#endif
}

//////////////////////////////////////////////////////////////////////////
int LuaCryPakFFlush(FILE *fp)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FFlush(fp);
#else
  return fflush(fp);
#endif
}

//////////////////////////////////////////////////////////////////////////
char *LuaCryPakFGets(char *str, int n, FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FGets(str, n, handle);
#else
  return fgets(str, n, handle);
#endif
}

//////////////////////////////////////////////////////////////////////////
int   LuaCryPakUngetc(int c, FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->Ungetc(c, handle);
#else
	return(ungetc(c,handle));
#endif
}

//////////////////////////////////////////////////////////////////////////
int   LuaCryPakGetc(FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->Getc(handle);
#else
	return(getc(handle));
#endif
}

//////////////////////////////////////////////////////////////////////////
int LuaCryPakFScanf(FILE *handle, const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FScanf(handle, format, arglist);
#else
  return fscanf(handle, format, arglist);
#endif
}

//////////////////////////////////////////////////////////////////////////
int LuaCryPakFSeek(FILE *handle, long seek, int mode)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FSeek(handle, seek, mode);
#else
  return fseek(handle, seek, mode);
#endif
}

//////////////////////////////////////////////////////////////////////////
int LuaCryPakFPrintf(FILE *handle, const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FPrintf(handle, format, arglist);
#else
  return fprintf(handle, format, arglist);
#endif
}

//////////////////////////////////////////////////////////////////////////
size_t LuaCryPakFRead(void *data, size_t length, size_t elems, FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FRead(data, length, elems, handle);
#else
	return fread(data, length, elems, handle);
#endif
}

//////////////////////////////////////////////////////////////////////////
size_t LuaCryPakFWrite(void *data, size_t length, size_t elems, FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FWrite(data, length, elems, handle);
#else
  return fwrite(data, length, elems, handle);
#endif
}

//////////////////////////////////////////////////////////////////////////
int   LuaCryPakFEof(FILE *handle)
{
#ifdef USE_CRYPAK
  ICryPak *pPak=GetISystem()->GetIPak();
  return pPak->FEof(handle);
#else
	return (feof(handle));
#endif
}
