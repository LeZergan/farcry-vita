/* Honest, real (not fake-success) no-op stubs for IScriptSystem/
   IScriptObject/IFunctionHandler, used when the real Lua-backed
   CScriptSystem can't be constructed (CreateScriptSystem's call
   boundary hangs on Vita3K the same way QueryPerformanceFrequency's
   did -- see engine_port/compat/README.md). The rest of the engine
   assumes m_pScriptSystem/script-object results are always valid
   non-null pointers (there's no null-checking at most call sites),
   so this exists to satisfy that assumption safely rather than
   patch every individual call site across the codebase.

   Mechanically generated from CryCommon/IScriptSystem.h's interface
   declarations (matching every method's exact signature), then
   given real, sensible no-op bodies -- not a subset, not guesses. */
#ifndef _SCRIPT_STUBS_H_
#define _SCRIPT_STUBS_H_

#include <IScriptSystem.h>

class CStubScriptObject : public IScriptObject
{
public:
	virtual int GetRef();
	virtual void Attach();
	virtual void Attach(IScriptObject *so);
	virtual void Delegate(IScriptObject *pObj);
	virtual void PushBack(int nVal);
	virtual void PushBack(float fVal);
	virtual void PushBack(const char *sVal);
	virtual void PushBack(bool bVal);
	virtual void PushBack(IScriptObject *pObj);
	virtual void SetValue(const char *sKey, int nVal);
	virtual void SetValue(const char *sKey, float fVal);
	virtual void SetValue(const char *sKey, const char *sVal);
	virtual void SetValue(const char *sKey, bool bVal);
	virtual void SetValue(const char *sKey, IScriptObject *pObj);
	virtual void SetValue(const char *sKey, USER_DATA ud);
	virtual void SetToNull(const char *sKey);
	virtual bool GetValue(const char *sKey, int &nVal);
	virtual bool GetValue(const char *sKey, float &fVal);
	virtual bool GetValue(const char *sKey, bool &bVal);
	virtual bool GetValue(const char *sKey, const char* &sVal);
	virtual bool GetValue(const char *sKey, IScriptObject *pObj);
	virtual bool GetValue(const char *sKey, HSCRIPTFUNCTION &funcVal);
	virtual bool GetUDValue(const char *sKey, USER_DATA &nValue, int &nCookie);
	virtual bool GetFuncData(const char *sKey, unsigned int * &pCode, int &iSize);
	virtual bool BeginSetGetChain();
	virtual bool GetValueChain(const char *sKey, int &nVal);
	virtual bool GetValueChain(const char *sKey, float &fVal);
	virtual bool GetValueChain(const char *sKey, bool &bVal);
	virtual bool GetValueChain(const char *sKey, const char* &sVal);
	virtual bool GetValueChain(const char *sKey, IScriptObject *pObj);
	virtual bool GetValueChain(const char *sKey, HSCRIPTFUNCTION &funcVal);
	virtual bool GetUDValueChain(const char *sKey, USER_DATA &nValue, int &nCookie);
	virtual void SetValueChain(const char *sKey, int nVal);
	virtual void SetValueChain(const char *sKey, float fVal);
	virtual void SetValueChain(const char *sKey, const char *sVal);
	virtual void SetValueChain(const char *sKey, bool bVal);
	virtual void SetValueChain(const char *sKey, IScriptObject *pObj);
	virtual void SetValueChain(const char *sKey, USER_DATA ud);
	virtual void SetToNullChain(const char *sKey);
	virtual void EndSetGetChain();
	virtual ScriptVarType GetValueType(const char *sKey);
	virtual ScriptVarType GetAtType(int nIdx);
	virtual void SetAt(int nIdx,int nVal);
	virtual void SetAt(int nIdx,float fVal);
	virtual void SetAt(int nIdx,bool bVal);
	virtual void SetAt(int nIdx,const char* sVal);
	virtual void SetAt(int nIdx,IScriptObject *pObj);
	virtual void SetAtUD(int nIdx,USER_DATA nValue);
	virtual void SetNullAt(int nIdx);
	virtual bool GetAt(int nIdx,int &nVal);
	virtual bool GetAt(int nIdx,float &fVal);
	virtual bool GetAt(int nIdx,bool &bVal);
	virtual bool GetAt(int nIdx,const char* &sVal);
	virtual bool GetAt(int nIdx,IScriptObject *pObj);
	virtual bool GetAtUD(int nIdx, USER_DATA &nValue, int &nCookie);
	virtual bool BeginIteration();
	virtual bool MoveNext();
	virtual bool GetCurrent(int &nVal);
	virtual bool GetCurrent(float &fVal);
	virtual bool GetCurrent(bool &bVal);
	virtual bool GetCurrent(const char* &sVal);
	virtual bool GetCurrent(IScriptObject *pObj);
	virtual bool GetCurrentPtr(const void * &pObj);
	virtual bool GetCurrentFuncData(unsigned int * &pCode, int &iSize);
	virtual bool GetCurrentKey(const char* &sVal);
	virtual bool GetCurrentKey(int &nKey);
	virtual ScriptVarType GetCurrentType();
	virtual void EndIteration();
	virtual void SetNativeData(void *);
	virtual void *GetNativeData();
	virtual void Clear();
	virtual int Count();
	virtual bool Clone(IScriptObject *pObj);
	virtual void Dump(IScriptObjectDumpSink *p);
	virtual bool AddFunction(const char *sName, SCRIPT_FUNCTION pThunk, int nFuncID);
	virtual bool AddSetGetHandlers(SCRIPT_FUNCTION pSetThunk,SCRIPT_FUNCTION pGetThunk);
	virtual void RegisterParent(IScriptObjectSink *pSink);
	virtual void Detach();
	virtual void Release();
	virtual bool GetValueRecursive( const char *szPath, IScriptObject *pObj );
};

class CStubFunctionHandler : public IFunctionHandler
{
public:
	virtual void __Attach(HSCRIPT hScript);
	virtual THIS_PTR GetThis();
	virtual int GetFunctionID();
	virtual int GetParamCount();
	virtual bool GetParam(int nIdx, int &n);
	virtual bool GetParam(int nIdx, float &f);
	virtual bool GetParam(int nIdx, const char * &s);
	virtual bool GetParam(int nIdx,bool &b);
	virtual bool GetParam(int nIdx, IScriptObject *pObj);
	virtual bool GetParam(int nIdx, HSCRIPTFUNCTION &hFunc, int nReference = 0);
	virtual bool GetParam(int nIdx,USER_DATA &ud);
	virtual bool GetParamUDVal(int nIdx,USER_DATA &val,int &cookie);
	virtual ScriptVarType GetParamType(int nIdx);
	virtual int EndFunctionNull();
	virtual int EndFunction(int nRetVal);
	virtual int EndFunction(float fRetVal);
	virtual int EndFunction(const char* fRetVal);
	virtual int EndFunction(bool bRetVal);
	virtual int EndFunction(IScriptObject *pObj);
	virtual int EndFunction(HSCRIPTFUNCTION hFunc);
	virtual int EndFunction(USER_DATA ud);
	virtual int EndFunction();
	virtual int EndFunction(int nRetVal1,int nRetVal2);
	virtual int EndFunction(float fRetVal1,float fRetVal2);
	virtual void Unref(HSCRIPTFUNCTION hFunc);
};

class CStubScriptSystem : public IScriptSystem
{
public:
	virtual IFunctionHandler * GetFunctionHandler();
	virtual HSCRIPT GetScriptHandle();
	virtual bool ExecuteFile(const char *sFileName,bool bRaiseError = true, bool bForceReload=false);
	virtual bool ExecuteBuffer(const char *sBuffer, size_t nSize);
	virtual void UnloadScript(const char *sFileName);
	virtual void UnloadScripts();
	virtual bool ReloadScript(const char *sFileName,bool bRaiseError = true);
	virtual bool ReloadScripts();
	virtual void DumpLoadedScripts();
	virtual IScriptObject * GetGlobalObject();
	virtual IScriptObject* CreateEmptyObject();
	virtual IScriptObject* CreateObject();
	virtual IScriptObject* CreateGlobalObject(const char *sName);
	virtual int BeginCall(HSCRIPTFUNCTION hFunc);
	virtual int BeginCall(const char *sFuncName);
	virtual int BeginCall(const char *sTableName, const char *sFuncName);
	virtual void EndCall();
	virtual void EndCall(int &nRet);
	virtual void EndCall(float &fRet);
	virtual void EndCall(const char *&sRet);
	virtual void EndCall(bool &bRet);
	virtual void EndCall(IScriptObject *pScriptObject);
	virtual HSCRIPTFUNCTION GetFunctionPtr(const char *sFuncName);
	virtual HSCRIPTFUNCTION GetFunctionPtr(const char *sTableName, const char *sFuncName);
	virtual void ReleaseFunc(HSCRIPTFUNCTION f);
	virtual void PushFuncParam(int nVal);
	virtual void PushFuncParam(float fVal);
	virtual void PushFuncParam(const char *sVal);
	virtual void PushFuncParam(bool bVal);
	virtual void PushFuncParam(IScriptObject *pVal);
	virtual void SetGlobalValue(const char *sKey, int nVal);
	virtual void SetGlobalValue(const char *sKey, float fVal);
	virtual void SetGlobalValue(const char *sKey, const char *sVal);
	virtual void SetGlobalValue(const char *sKey, IScriptObject *pObj);
	virtual bool GetGlobalValue(const char *sKey, int &nVal);
	virtual bool GetGlobalValue(const char *sKey, float &fVal);
	virtual bool GetGlobalValue(const char *sKey, const char * &sVal);
	virtual bool GetGlobalValue(const char *sKey, IScriptObject *pObj);
	virtual void SetGlobalToNull(const char *sKey);
	virtual HTAG CreateTaggedValue(const char *sKey, int *pVal);
	virtual HTAG CreateTaggedValue(const char *sKey, float *pVal);
	virtual HTAG CreateTaggedValue(const char *sKey, char *pVal);
	virtual USER_DATA CreateUserData(INT_PTR nVal,int nCookie);
	virtual void RemoveTaggedValue(HTAG tag);
	virtual void RaiseError(const char *sErr,...);
	virtual void ForceGarbageCollection();
	virtual int GetCGCount();
	virtual void SetGCThreshhold(int nKb);
	virtual void UnbindUserdata();
	virtual void Release();
	virtual void EnableDebugger(IScriptDebugSink *pDebugSink);
	virtual IScriptObject * GetBreakPoints();
	virtual HBREAKPOINT AddBreakPoint(const char *sFile,int nLineNumber);
	virtual IScriptObject * GetLocalVariables(int nLevel = 0);
	virtual IScriptObject * GetCallsStack();
	virtual void DebugContinue();
	virtual void DebugStepNext();
	virtual void DebugStepInto();
	virtual void DebugDisable();
	virtual BreakState GetBreakState();
	virtual void GetMemoryStatistics(ICrySizer *pSizer);
	virtual void GetScriptHash(const char *sPath, const char *szKey, unsigned int &dwHash);
	virtual void PostInit();

private:
	CStubScriptObject m_sharedObject;
};

#endif //_SCRIPT_STUBS_H_
