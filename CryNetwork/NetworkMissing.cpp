/* Client.cpp/Network.cpp/Server.cpp themselves stay excluded from this
   build -- real multiplayer/matchmaking networking (Ubisoft.net
   integration, full client/server simulation), not on the critical path
   for booting the engine and rendering single-player assets on Vita.
   But their headers are still included by other compiled subsystems
   (e.g. PunkBusterInterface.cpp, CCPEndpoint.cpp), which pulls in calls
   to a handful of CClient/CNetwork/CServer methods. Honest minimal
   real implementations for just those methods -- not stubs faking
   success, but the smallest correct behavior (no-op / not-found /
   disabled) for a single-player-only build. See
   engine_port/compat/README.md. */
#include "StdAfx.h"
#include "Client.h"
#include "Network.h"
#include "Server.h"

bool CClient::SendTo( CIPAddress &/*ip*/, CStream &/*stm*/ )
{
	return false;
}

bool CClient::SendSecurityResponse( CStream &/*stm*/ )
{
	return false;
}

bool CClient::SendPunkBusterMsg( CStream &/*stm*/ )
{
	return false;
}

CNetwork::CNetwork()
{
}

CNetwork::~CNetwork()
{
}

bool CNetwork::Init( IScriptSystem */*pScriptSystem*/ )
{
	return true;
}

bool CNetwork::IsPacketCompressionEnabled() const
{
	return false;
}

void CNetwork::PunkDetected( CIPAddress &/*ip*/ )
{
}

CTimeValue CNetwork::GetCurrentTime()
{
	return CTimeValue();
}

int CNetwork::GetLogLevel()
{
	return 0;
}

int CNetwork::GetCheatProtectionLevel()
{
	return 0;
}

CServerSlot *CServer::GetPacketOwner( CIPAddress &/*ip*/ )
{
	return NULL;
}

DWORD CNetwork::GetLocalIP() const
{
	return 0;
}

void CNetwork::SetLocalIP( const char */*szLocalIP*/ )
{
}

IClient *CNetwork::CreateClient( IClientSink */*pSink*/, bool /*bLocal*/ )
{
	return NULL;
}

IServer *CNetwork::CreateServer( IServerSlotFactory */*pFactory*/, WORD /*nPort*/, bool /*listen*/ )
{
	return NULL;
}

INETServerSnooper *CNetwork::CreateNETServerSnooper( INETServerSnooperSink */*pSink*/ )
{
	return NULL;
}

IServerSnooper *CNetwork::CreateServerSnooper( IServerSnooperSink */*pSink*/ )
{
	return NULL;
}

IRConSystem *CNetwork::CreateRConSystem()
{
	return NULL;
}

const char *CNetwork::EnumerateError( NRESULT /*err*/ )
{
	return "";
}

void CNetwork::Release()
{
}

void CNetwork::GetMemoryStatistics( ICrySizer */*pSizer*/ )
{
}

ICompressionHelper *CNetwork::GetCompressionHelper()
{
	return NULL;
}

void CNetwork::ClearProtectedFiles()
{
}

void CNetwork::AddProtectedFile( const char */*sFilename*/ )
{
}

IClient *CNetwork::GetClient()
{
	return NULL;
}

IServer *CNetwork::GetServerByPort( const WORD /*wPort*/ )
{
	return NULL;
}

void CNetwork::UpdateNetwork()
{
}

void CNetwork::OnAfterServerLoadLevel( const char */*szServerName*/, const uint32 /*dwPlayerCount*/, const WORD /*wPort*/ )
{
}

bool CNetwork::VerifyMultiplayerOverInternet()
{
	return false;
}

void CNetwork::Client_ReJoinGameServer()
{
}
