/* See ScriptStubs.h. */
#include "StdAfx.h"
#include "ScriptStubs.h"

IFunctionHandler * CStubScriptSystem::GetFunctionHandler() { return 0; }
HSCRIPT CStubScriptSystem::GetScriptHandle() { return 0; }
bool CStubScriptSystem::ExecuteFile(const char * sFileName, bool bRaiseError, bool bForceReload) { return false; }
bool CStubScriptSystem::ExecuteBuffer(const char * sBuffer, size_t nSize) { return false; }
void CStubScriptSystem::UnloadScript(const char * sFileName) { }
void CStubScriptSystem::UnloadScripts() { }
bool CStubScriptSystem::ReloadScript(const char * sFileName, bool bRaiseError) { return false; }
bool CStubScriptSystem::ReloadScripts() { return false; }
void CStubScriptSystem::DumpLoadedScripts() { }
IScriptObject * CStubScriptSystem::GetGlobalObject() { return &m_sharedObject; }
IScriptObject* CStubScriptSystem::CreateEmptyObject() { return &m_sharedObject; }
IScriptObject* CStubScriptSystem::CreateObject() { return &m_sharedObject; }
IScriptObject* CStubScriptSystem::CreateGlobalObject(const char * sName) { return &m_sharedObject; }
int CStubScriptSystem::BeginCall(HSCRIPTFUNCTION hFunc) { return 0; }
int CStubScriptSystem::BeginCall(const char * sFuncName) { return 0; }
int CStubScriptSystem::BeginCall(const char * sTableName, const char * sFuncName) { return 0; }
void CStubScriptSystem::EndCall() { }
void CStubScriptSystem::EndCall(int & nRet) { }
void CStubScriptSystem::EndCall(float & fRet) { }
void CStubScriptSystem::EndCall(const char *& sRet) { }
void CStubScriptSystem::EndCall(bool & bRet) { }
void CStubScriptSystem::EndCall(IScriptObject * pScriptObject) { }
HSCRIPTFUNCTION CStubScriptSystem::GetFunctionPtr(const char * sFuncName) { return 0; }
HSCRIPTFUNCTION CStubScriptSystem::GetFunctionPtr(const char * sTableName, const char * sFuncName) { return 0; }
void CStubScriptSystem::ReleaseFunc(HSCRIPTFUNCTION f) { }
void CStubScriptSystem::PushFuncParam(int nVal) { }
void CStubScriptSystem::PushFuncParam(float fVal) { }
void CStubScriptSystem::PushFuncParam(const char * sVal) { }
void CStubScriptSystem::PushFuncParam(bool bVal) { }
void CStubScriptSystem::PushFuncParam(IScriptObject * pVal) { }
void CStubScriptSystem::SetGlobalValue(const char * sKey, int nVal) { }
void CStubScriptSystem::SetGlobalValue(const char * sKey, float fVal) { }
void CStubScriptSystem::SetGlobalValue(const char * sKey, const char * sVal) { }
void CStubScriptSystem::SetGlobalValue(const char * sKey, IScriptObject * pObj) { }
bool CStubScriptSystem::GetGlobalValue(const char * sKey, int & nVal) { return false; }
bool CStubScriptSystem::GetGlobalValue(const char * sKey, float & fVal) { return false; }
bool CStubScriptSystem::GetGlobalValue(const char * sKey, const char * & sVal) { return false; }
bool CStubScriptSystem::GetGlobalValue(const char * sKey, IScriptObject * pObj) { return false; }
void CStubScriptSystem::SetGlobalToNull(const char * sKey) { }
HTAG CStubScriptSystem::CreateTaggedValue(const char * sKey, int * pVal) { return 0; }
HTAG CStubScriptSystem::CreateTaggedValue(const char * sKey, float * pVal) { return 0; }
HTAG CStubScriptSystem::CreateTaggedValue(const char * sKey, char * pVal) { return 0; }
USER_DATA CStubScriptSystem::CreateUserData(INT_PTR nVal, int nCookie) { return 0; }
void CStubScriptSystem::RemoveTaggedValue(HTAG tag) { }
void CStubScriptSystem::RaiseError(const char * sErr, ...) { }
void CStubScriptSystem::ForceGarbageCollection() { }
int CStubScriptSystem::GetCGCount() { return 0; }
void CStubScriptSystem::SetGCThreshhold(int nKb) { }
void CStubScriptSystem::UnbindUserdata() { }
void CStubScriptSystem::Release() { }
void CStubScriptSystem::EnableDebugger(IScriptDebugSink * pDebugSink) { }
IScriptObject * CStubScriptSystem::GetBreakPoints() { return 0; }
HBREAKPOINT CStubScriptSystem::AddBreakPoint(const char * sFile, int nLineNumber) { return 0; }
IScriptObject * CStubScriptSystem::GetLocalVariables(int nLevel) { return 0; }
IScriptObject * CStubScriptSystem::GetCallsStack() { return 0; }
void CStubScriptSystem::DebugContinue() { }
void CStubScriptSystem::DebugStepNext() { }
void CStubScriptSystem::DebugStepInto() { }
void CStubScriptSystem::DebugDisable() { }
BreakState CStubScriptSystem::GetBreakState() { return bsNoBreak; }
void CStubScriptSystem::GetMemoryStatistics(ICrySizer * pSizer) { }
void CStubScriptSystem::GetScriptHash(const char * sPath, const char * szKey, unsigned int & dwHash) { }
void CStubScriptSystem::PostInit() { }

int CStubScriptObject::GetRef() { return 0; }
void CStubScriptObject::Attach() { }
void CStubScriptObject::Attach(IScriptObject * so) { }
void CStubScriptObject::Delegate(IScriptObject * pObj) { }
void CStubScriptObject::PushBack(int nVal) { }
void CStubScriptObject::PushBack(float fVal) { }
void CStubScriptObject::PushBack(const char * sVal) { }
void CStubScriptObject::PushBack(bool bVal) { }
void CStubScriptObject::PushBack(IScriptObject * pObj) { }
void CStubScriptObject::SetValue(const char * sKey, int nVal) { }
void CStubScriptObject::SetValue(const char * sKey, float fVal) { }
void CStubScriptObject::SetValue(const char * sKey, const char * sVal) { }
void CStubScriptObject::SetValue(const char * sKey, bool bVal) { }
void CStubScriptObject::SetValue(const char * sKey, IScriptObject * pObj) { }
void CStubScriptObject::SetValue(const char * sKey, USER_DATA ud) { }
void CStubScriptObject::SetToNull(const char * sKey) { }
bool CStubScriptObject::GetValue(const char * sKey, int & nVal) { return false; }
bool CStubScriptObject::GetValue(const char * sKey, float & fVal) { return false; }
bool CStubScriptObject::GetValue(const char * sKey, bool & bVal) { return false; }
bool CStubScriptObject::GetValue(const char * sKey, const char* & sVal) { return false; }
bool CStubScriptObject::GetValue(const char * sKey, IScriptObject * pObj) { return false; }
bool CStubScriptObject::GetValue(const char * sKey, HSCRIPTFUNCTION & funcVal) { return false; }
bool CStubScriptObject::GetUDValue(const char * sKey, USER_DATA & nValue, int & nCookie) { return false; }
bool CStubScriptObject::GetFuncData(const char * sKey, unsigned int * & pCode, int & iSize) { return false; }
bool CStubScriptObject::BeginSetGetChain() { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, int & nVal) { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, float & fVal) { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, bool & bVal) { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, const char* & sVal) { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, IScriptObject * pObj) { return false; }
bool CStubScriptObject::GetValueChain(const char * sKey, HSCRIPTFUNCTION & funcVal) { return false; }
bool CStubScriptObject::GetUDValueChain(const char * sKey, USER_DATA & nValue, int & nCookie) { return false; }
void CStubScriptObject::SetValueChain(const char * sKey, int nVal) { }
void CStubScriptObject::SetValueChain(const char * sKey, float fVal) { }
void CStubScriptObject::SetValueChain(const char * sKey, const char * sVal) { }
void CStubScriptObject::SetValueChain(const char * sKey, bool bVal) { }
void CStubScriptObject::SetValueChain(const char * sKey, IScriptObject * pObj) { }
void CStubScriptObject::SetValueChain(const char * sKey, USER_DATA ud) { }
void CStubScriptObject::SetToNullChain(const char * sKey) { }
void CStubScriptObject::EndSetGetChain() { }
ScriptVarType CStubScriptObject::GetValueType(const char * sKey) { return svtNull; }
ScriptVarType CStubScriptObject::GetAtType(int nIdx) { return svtNull; }
void CStubScriptObject::SetAt(int nIdx, int nVal) { }
void CStubScriptObject::SetAt(int nIdx, float fVal) { }
void CStubScriptObject::SetAt(int nIdx, bool bVal) { }
void CStubScriptObject::SetAt(int nIdx, const char* sVal) { }
void CStubScriptObject::SetAt(int nIdx, IScriptObject * pObj) { }
void CStubScriptObject::SetAtUD(int nIdx, USER_DATA nValue) { }
void CStubScriptObject::SetNullAt(int nIdx) { }
bool CStubScriptObject::GetAt(int nIdx, int & nVal) { return false; }
bool CStubScriptObject::GetAt(int nIdx, float & fVal) { return false; }
bool CStubScriptObject::GetAt(int nIdx, bool & bVal) { return false; }
bool CStubScriptObject::GetAt(int nIdx, const char* & sVal) { return false; }
bool CStubScriptObject::GetAt(int nIdx, IScriptObject * pObj) { return false; }
bool CStubScriptObject::GetAtUD(int nIdx, USER_DATA & nValue, int & nCookie) { return false; }
bool CStubScriptObject::BeginIteration() { return false; }
bool CStubScriptObject::MoveNext() { return false; }
bool CStubScriptObject::GetCurrent(int & nVal) { return false; }
bool CStubScriptObject::GetCurrent(float & fVal) { return false; }
bool CStubScriptObject::GetCurrent(bool & bVal) { return false; }
bool CStubScriptObject::GetCurrent(const char* & sVal) { return false; }
bool CStubScriptObject::GetCurrent(IScriptObject * pObj) { return false; }
bool CStubScriptObject::GetCurrentPtr(const void * & pObj) { return false; }
bool CStubScriptObject::GetCurrentFuncData(unsigned int * & pCode, int & iSize) { return false; }
bool CStubScriptObject::GetCurrentKey(const char* & sVal) { return false; }
bool CStubScriptObject::GetCurrentKey(int & nKey) { return false; }
ScriptVarType CStubScriptObject::GetCurrentType() { return svtNull; }
void CStubScriptObject::EndIteration() { }
void CStubScriptObject::SetNativeData(void * a0) { }
void * CStubScriptObject::GetNativeData() { return 0; }
void CStubScriptObject::Clear() { }
int CStubScriptObject::Count() { return 0; }
bool CStubScriptObject::Clone(IScriptObject * pObj) { return false; }
void CStubScriptObject::Dump(IScriptObjectDumpSink * p) { }
bool CStubScriptObject::AddFunction(const char * sName, SCRIPT_FUNCTION pThunk, int nFuncID) { return false; }
bool CStubScriptObject::AddSetGetHandlers(SCRIPT_FUNCTION pSetThunk, SCRIPT_FUNCTION pGetThunk) { return false; }
void CStubScriptObject::RegisterParent(IScriptObjectSink * pSink) { }
void CStubScriptObject::Detach() { }
void CStubScriptObject::Release() { }
bool CStubScriptObject::GetValueRecursive(const char * szPath, IScriptObject * pObj) { return false; }

void CStubFunctionHandler::__Attach(HSCRIPT hScript) { }
THIS_PTR CStubFunctionHandler::GetThis() { return 0; }
int CStubFunctionHandler::GetFunctionID() { return 0; }
int CStubFunctionHandler::GetParamCount() { return 0; }
bool CStubFunctionHandler::GetParam(int nIdx, int & n) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, float & f) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, const char * & s) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, bool & b) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, IScriptObject * pObj) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, HSCRIPTFUNCTION & hFunc, int nReference) { return false; }
bool CStubFunctionHandler::GetParam(int nIdx, USER_DATA & ud) { return false; }
bool CStubFunctionHandler::GetParamUDVal(int nIdx, USER_DATA & val, int & cookie) { return false; }
ScriptVarType CStubFunctionHandler::GetParamType(int nIdx) { return svtNull; }
int CStubFunctionHandler::EndFunctionNull() { return 0; }
int CStubFunctionHandler::EndFunction(int nRetVal) { return 0; }
int CStubFunctionHandler::EndFunction(float fRetVal) { return 0; }
int CStubFunctionHandler::EndFunction(const char* fRetVal) { return 0; }
int CStubFunctionHandler::EndFunction(bool bRetVal) { return 0; }
int CStubFunctionHandler::EndFunction(IScriptObject * pObj) { return 0; }
int CStubFunctionHandler::EndFunction(HSCRIPTFUNCTION hFunc) { return 0; }
int CStubFunctionHandler::EndFunction(USER_DATA ud) { return 0; }
int CStubFunctionHandler::EndFunction() { return 0; }
int CStubFunctionHandler::EndFunction(int nRetVal1, int nRetVal2) { return 0; }
int CStubFunctionHandler::EndFunction(float fRetVal1, float fRetVal2) { return 0; }
void CStubFunctionHandler::Unref(HSCRIPTFUNCTION hFunc) { }
