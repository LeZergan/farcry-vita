////////////////////////////////////////////////////////////////////////////
//
//  Crytek Engine Source File.
//  Copyright (C), Crytek Studios, 2001.
// -------------------------------------------------------------------------
//  File name:   movie.cpp
//  Version:     v1.00
//  Created:     23/4/2002 by Timur.
//  Compilers:   Visual C++ 7.0
//  Description: 
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#if defined(__vita__)
#include <malloc.h>
#include <vitaGL.h>
#endif
#include "Movie.h"
#include "AnimSplineTrack.h"
#include "AnimSequence.h"
#include "SequenceIt.h"
#include "EntityNode.h"
#include "CVarNode.h"
#include "ScriptVarNode.h"
#include "AnimCameraNode.h"
#include "SceneNode.h"
#include "MaterialNode.h"

#include <ISystem.h>
#if defined(LINUX)
#include <sys/io.h>
#else
#include <io.h>
#endif
#include <ILog.h>
#include <IConsole.h>
#include <ITimer.h>

int CMovieSystem::m_mov_NoCutscenes = 0;

//////////////////////////////////////////////////////////////////////////
CMovieSystem::CMovieSystem( ISystem *system )
{
	m_system = system;
	m_bRecording = false;
	m_pCallback=NULL;
	m_pUser=NULL;
	m_bPaused = false;
	m_bLastFrameAnimateOnStop = true;
	m_lastGenId = 1;
	m_nOpenCutScenes = 0;
	m_fOpenCutSceneTime = 0.0f;
	m_fLastAsyncUpdateTime = 0.0f;
	m_sequenceStopBehavior = ONSTOP_GOTO_END_TIME;

	system->GetIConsole()->Register( "mov_NoCutscenes",&m_mov_NoCutscenes,0,0,"Disable playing of Cut-Scenes" );
}

//////////////////////////////////////////////////////////////////////////
CMovieSystem::~CMovieSystem()
{
}

//////////////////////////////////////////////////////////////////////////
bool CMovieSystem::Load(const char *pszFile, const char *pszMission)
{
	XmlNodeRef rootNode = m_system->LoadXmlFile(pszFile);
	if (!rootNode)
		return false;
	XmlNodeRef Node=NULL;
	for (int i=0;i<rootNode->getChildCount();i++)
	{
		XmlNodeRef missionNode=rootNode->getChild(i);
		XmlString sName;
		if (!(sName = missionNode->getAttr("Name")))
			continue;
		if (stricmp(sName.c_str(), pszMission))
			continue;
		Node=missionNode;
		break;
	}
	if (!Node)
		return false;
	Serialize(Node, true, true, false);
	return true;
}

//////////////////////////////////////////////////////////////////////////
IAnimNode* CMovieSystem::CreateNode( int nodeType,int nodeId )
{
	CAnimNode *node = NULL;
	if (!nodeId)
	{
		// Make uniq id.
		do {
			nodeId = m_lastGenId++;
		} while (GetNode(nodeId) != 0);
	}
	switch (nodeType)
	{
	case ANODE_ENTITY:
		node = new CAnimEntityNode(this);
		break;
	case ANODE_CAMERA:
		node = new CAnimCameraNode(this);
		break;
	case ANODE_CVAR:
		node = new CAnimCVarNode(this);
		break;
	case ANODE_SCRIPTVAR:
		node = new CAnimScriptVarNode(this);
		break;
	case ANODE_SCENE:
		node = new CAnimSceneNode(this);
		nodeId = 0;
		return node;
		break;
	case ANODE_MATERIAL:
		node = new CAnimMaterialNode(this);
		break;
	}
	if (node)
	{
		node->SetId(nodeId);
		m_nodes[nodeId] = node;
	}
	return node;
}

//////////////////////////////////////////////////////////////////////////
IAnimTrack* CMovieSystem::CreateTrack( EAnimTrackType type )
{
	switch (type)
	{
	case ATRACK_TCB_FLOAT:
		return new CTcbFloatTrack;
	case ATRACK_TCB_VECTOR:
		return new CTcbVectorTrack;
	case ATRACK_TCB_QUAT:
		return new CTcbQuatTrack;
	};
	//ATRACK_TCB_FLOAT,
	//ATRACK_TCB_VECTOR,
	//ATRACK_TCB_QUAT,
	//ATRACK_BOOL,
	// Unknown type of track.
//	CLogFile::WriteLine( "Error: Requesting unknown type of animation track!" );
	assert(0);
	return 0;
}

void CMovieSystem::ChangeAnimNodeId( int nodeId,int newNodeId )
{
	if (nodeId == newNodeId)
		return;
	Nodes::iterator it = m_nodes.find(nodeId);
	if (it != m_nodes.end())
	{
		IAnimNode *node = GetNode( nodeId );
		((CAnimNode*)node)->SetId( newNodeId );
		m_nodes[newNodeId] = node;
		m_nodes.erase(it);
	}

}

//////////////////////////////////////////////////////////////////////////
IAnimSequence* CMovieSystem::CreateSequence( const char *sequenceName )
{
	IAnimSequence *seq = new CAnimSequence( this );
	seq->SetName( sequenceName );
	m_sequences.push_back( seq );
	return seq;
}

//////////////////////////////////////////////////////////////////////////
IAnimSequence* CMovieSystem::LoadSequence( const char *pszFilePath )
{
	XmlNodeRef sequenceNode = m_system->LoadXmlFile( pszFilePath );
	if (sequenceNode)
	{
		return LoadSequence( sequenceNode );
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
IAnimSequence* CMovieSystem::LoadSequence( XmlNodeRef &xmlNode, bool bLoadEmpty )
{
	IAnimSequence *seq = new CAnimSequence( this );
	seq->Serialize( xmlNode,true,bLoadEmpty );
	// Delete previous sequence with the same name.
	IAnimSequence *pPrevSeq = FindSequence( seq->GetName() );
	if (pPrevSeq)
		RemoveSequence( pPrevSeq );
	m_sequences.push_back( seq );
	return seq;
}

//////////////////////////////////////////////////////////////////////////
IAnimSequence* CMovieSystem::FindSequence( const char *sequence )
{
	for (Sequences::iterator it = m_sequences.begin(); it != m_sequences.end(); ++it)
	{
		IAnimSequence *seq = *it;
		if (stricmp(seq->GetName(),sequence) == 0)
		{
			return seq;
		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
ISequenceIt* CMovieSystem::GetSequences()
{
	CSequenceIt *It=new CSequenceIt();
	for (Sequences::iterator it = m_sequences.begin(); it != m_sequences.end(); ++it)
	{
		It->add( *it );
	}
	return It;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::RemoveSequence( IAnimSequence *seq )
{
	assert( seq != 0 );
	if (seq)
	{
		IMovieCallback *pCallback=GetCallback();
		SetCallback(NULL);
		StopSequence(seq);

		for (Sequences::iterator it = m_sequences.begin(); it != m_sequences.end(); ++it)
		{
			if (seq == *it)
			{
				m_sequences.erase(it);
				break;
			}
		}
		SetCallback(pCallback);
	}
}

//////////////////////////////////////////////////////////////////////////
IAnimNode* CMovieSystem::GetNode( int nodeId ) const
{
	Nodes::const_iterator it = m_nodes.find(nodeId);
	if (it != m_nodes.end())
		return it->second;
	return 0;
}

//////////////////////////////////////////////////////////////////////////
IAnimNode* CMovieSystem::FindNode( const char *nodeName ) const
{
	for (Nodes::const_iterator it = m_nodes.begin(); it != m_nodes.end(); ++it)
	{
		IAnimNode *node = it->second;
		// Case insesentivy name comparasion.
		if (stricmp(node->GetName(),nodeName) == 0)
		{
			return node;
		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::RemoveNode( IAnimNode* node )
{
	assert( node != 0 );

	{
		// Remove this node from all sequences that reference this node.
		for (Sequences::iterator sit = m_sequences.begin(); sit != m_sequences.end(); ++sit)
		{
			(*sit)->RemoveNode( node );
		}
	}

	Nodes::iterator it = m_nodes.find(node->GetId());
	if (it != m_nodes.end())
		m_nodes.erase( it );
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::RemoveAllSequences()
{
	m_bLastFrameAnimateOnStop = false;
	IMovieCallback *pCallback=GetCallback();
	SetCallback(NULL);
	StopAllSequences();
	m_sequences.clear();
	SetCallback(pCallback);
	m_bLastFrameAnimateOnStop = true;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::RemoveAllNodes()
{
	m_bLastFrameAnimateOnStop = false;
	IMovieCallback *pCallback=GetCallback();
	SetCallback(NULL);
	StopAllSequences();
	m_nodes.clear();
	SetCallback(pCallback);
	m_bLastFrameAnimateOnStop = true;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::SaveNodes(XmlNodeRef nodesNode)
{
	for (Nodes::iterator It=m_nodes.begin();It!=m_nodes.end();++It)
	{
		XmlNodeRef nodeNode=nodesNode->newChild("Node");
		IAnimNode *pNode=It->second;
		nodeNode->setAttr("Id", pNode->GetId());
		nodeNode->setAttr("Type", pNode->GetType());
		nodeNode->setAttr("Name", pNode->GetName());
		switch (pNode->GetType())
		{
			case ANODE_CAMERA:	// FALL THROUGH
			case ANODE_ENTITY:
				IAnimNode *pTgt=pNode->GetTarget();
				if (pTgt)
					nodeNode->setAttr("TargetId", pTgt->GetId());
				break;
		}
		pNode->Serialize( nodeNode,false );
	}
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::PlaySequence( const char *sequenceName,bool bResetFx )
{
	IAnimSequence *seq = FindSequence(sequenceName);
	if (seq)
	{ 
		PlaySequence(seq,bResetFx);
	}
	else
		GetSystem ()->GetILog()->Log ("CMovieSystem::PlaySequence: Error: Sequence \"%s\" not found", sequenceName);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::PlaySequence( IAnimSequence *seq,bool bResetFx )
{
	assert( seq != 0 );
	if (!seq || IsPlaying(seq))
		return;

	if ((seq->GetFlags() & IAnimSequence::CUT_SCENE) || (seq->GetFlags() & IAnimSequence::NO_HUD))
	{
		// Dont play cut-scene if this console variable set.
		if (m_mov_NoCutscenes != 0)
			return;
	}

	//GetSystem ()->GetILog()->Log ("TEST: Playing Sequence (%s)", seq->GetName());

	// If this sequence is cut scene disable player.
	if (seq->GetFlags() & IAnimSequence::CUT_SCENE)
	{
		if (m_pUser)
		{
			m_pUser->BeginCutScene(seq->GetFlags(),bResetFx);
			++m_nOpenCutScenes;
		}
	}

	seq->Activate();
	PlayingSequence ps;
	ps.sequence = seq;
	ps.time = seq->GetTimeRange().start;
	m_playingSequences.push_back(ps);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::StopSequence( const char *sequenceName )
{
	IAnimSequence *seq = FindSequence(sequenceName);
	if (seq)
		StopSequence(seq);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::StopSequence( IAnimSequence *seq )
{
	assert( seq != 0 );
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); ++it)
	{
		if (it->sequence == seq)
		{
#if defined(__vita__)
			/* Where a cut scene ends is the whole question: reaching the end of
			   its own range is the sequence finishing normally, while stopping
			   far short of it means something else ended it (a script, a level
			   reset, or the player). */
			if (m_system && m_system->GetILog())
			{
				Range r = seq->GetTimeRange();
				m_system->GetILog()->LogToFile("\001[VITA][MOVIE] stopping '%s' at t=%.2f of [%.2f..%.2f] -- %s",
					seq->GetName(), it->time, r.start, r.end,
					(it->time >= r.end - 0.05f) ? "ran to its end" : "ended early by something else");
			}
#endif
			m_playingSequences.erase( it );

			if (m_bLastFrameAnimateOnStop)
			{
				if (m_sequenceStopBehavior == ONSTOP_GOTO_END_TIME)
				{
					SAnimContext ac;
					ac.bSingleFrame = true;
					ac.time = seq->GetTimeRange().end;
					seq->Animate(ac);
				}
				else if (m_sequenceStopBehavior == ONSTOP_GOTO_START_TIME)
				{
					SAnimContext ac;
					ac.bSingleFrame = true;
					ac.time = seq->GetTimeRange().start;
					seq->Animate(ac);
				}
				seq->Deactivate();
			}
			
			// If this sequence is cut scene end it.
			if (seq->GetFlags() & IAnimSequence::CUT_SCENE)
			{
				if (m_pUser)
				{
					m_pUser->EndCutScene();
					if (m_nOpenCutScenes > 0)
						--m_nOpenCutScenes;
				}
			}
			// Give the view back if that was the last sequence running.
			ReleaseCameraIfIdle();
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::ReleaseCameraIfIdle()
{
	/* Hand the view back to the player once nothing is playing.

	   A sequence's camera track assigns an active camera by entity id, and
	   CXClient::Update prefers that entity's camera over everything else: if
	   m_CameraParams->nCameraId is set it builds the view from that entity and
	   the player's own camera is never consulted.  Nothing cleared it when a
	   sequence stopped -- the only two places that reset it are Reset() and
	   PlayOnLoadSequences(), both of which run at level load.  So from the end
	   of a level's first cut scene onwards the view stayed welded to the cut
	   scene's camera for the rest of the session.

	   That is the whole of "first person doesn't work": CPlayer::Update runs,
	   m_bFirstPerson is set, UpdateFirstPersonView is reached and computes the
	   right camera every frame -- the device log confirms all four -- and then
	   the client throws it away in favour of a camera the cut scene left behind.
	   It is also why the camera sits still in mid air, and why the player's own
	   body is in shot: an external camera sees him. */
	if (!m_playingSequences.empty())
		return;
	SCameraParams CamParams = GetCameraParams();
	if (!CamParams.nCameraId && !CamParams.cameraNode)
		return;
	CamParams.cameraNode = NULL;
	CamParams.nCameraId = 0;
	SetCameraParams(CamParams);
	if (m_system && m_system->GetILog())
		m_system->GetILog()->LogToFile(
			"\001[VITA][MOVIE] no sequence playing -- camera released back to the player");
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::StopAllSequences()
{
	while (!m_playingSequences.empty())
	{
		StopSequence( m_playingSequences.begin()->sequence );
	}
	m_playingSequences.clear();
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::StopAllCutScenes()
{
	PlayingSequences::iterator next;
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); it = next)
	{
		next = it; ++next;
		IAnimSequence *seq = it->sequence;
		if (seq->GetFlags() & IAnimSequence::CUT_SCENE)
			StopSequence( seq );
	}
}

//////////////////////////////////////////////////////////////////////////
bool CMovieSystem::IsPlaying( IAnimSequence *seq ) const
{
	for (PlayingSequences::const_iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); ++it)
	{
		if (it->sequence == seq)
			return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Reset( bool bPlayOnReset )
{
	m_bLastFrameAnimateOnStop = false;
	StopAllSequences();
	m_bLastFrameAnimateOnStop = true;

	// Reset all sequences.
	for (Sequences::iterator sit = m_sequences.begin(); sit != m_sequences.end(); ++sit)
	{
		IAnimSequence *seq = *sit;
		seq->Reset();
	}

	// Reset all nodes.
	for (Nodes::const_iterator it = m_nodes.begin(); it != m_nodes.end(); ++it)
	{
		IAnimNode *node = it->second;
		node->Reset();
	}

	// Force end Cut-Scene on the reset.
/*	if (m_pUser)	// lennert why is this here ??? if there was a cutscene playing it will be stopped above...
	{
		m_pUser->EndCutScene();
	}*/

	if (bPlayOnReset)
	{
		for (Sequences::iterator sit = m_sequences.begin(); sit != m_sequences.end(); ++sit)
		{
			IAnimSequence *seq = *sit;
			if (seq->GetFlags() & IAnimSequence::PLAY_ONRESET)
				PlaySequence(seq);
		}
	}

	// Reset camera.
	SCameraParams CamParams=GetCameraParams();
	CamParams.cameraNode=NULL;
	CamParams.nCameraId=0;
	SetCameraParams(CamParams);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::PlayOnLoadSequences()
{
	for (Sequences::iterator sit = m_sequences.begin(); sit != m_sequences.end(); ++sit)
	{
		IAnimSequence *seq = *sit;
		if (seq->GetFlags() & IAnimSequence::PLAY_ONRESET)
			PlaySequence(seq);
	}

	// Reset camera.
	SCameraParams CamParams=GetCameraParams();
	CamParams.cameraNode=NULL;
	CamParams.nCameraId=0;
	SetCameraParams(CamParams);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Update( float dt )
{
	if (m_bPaused)
	{
#if defined(__vita__)
		/* A paused movie system with sequences still in m_playingSequences is
		   the signature of a cut scene that can never end: its clock is frozen,
		   so it never reaches the end of its range and never releases the camera
		   or the controls.  Say so rather than leaving it to be inferred. */
		if (!m_playingSequences.empty())
		{
			static unsigned s_nPausedReport = 0;
			if ((s_nPausedReport++ % 120) == 0 && m_system && m_system->GetILog())
				m_system->GetILog()->LogToFile(
					"\001[VITA][MOVIE] paused with %u sequence(s) still playing -- their time is frozen",
					(unsigned)m_playingSequences.size());
		}
#endif
		return;
	}

	SAnimContext ac;
	float fps = 60;
	Range timeRange;

	std::vector<IAnimSequence*> stopSequences;

	// cap delta time.
	dt = max( 0,min(0.5f,dt) );

#if defined(__vita__)
	/* A playing sequence with a frozen clock is always wrong, whatever the
	   caller's delta says.  The device log is unambiguous: repeated reports of
	   "playing 'first_cutscene' t=0.00 ... dt=0.0000" -- the sequence is
	   updated over and over and its time never moves, so it never reaches the
	   end of its range, never stops on its own, and holds the camera and the
	   player's controls until something else ends it.
	   Advance by real elapsed time instead.  A nominal 1/60 per call, which is
	   what this used to substitute, is only correct when the game is running at
	   60 fps: at the 20-odd fps a cut scene with characters on screen actually
	   costs, the sequence advances a third of a second for every second that
	   passes.  The dialogue and music are played by the sound system in real
	   time and do not slow down with it, so the scene drifts further behind its
	   own audio the longer it runs, and a 23-second sequence needs over a minute
	   of wall clock to reach the end of its range -- which is most of what "the
	   cut scene never ends" is.  Only applied while something is playing, so an
	   idle movie system still costs nothing, and a real delta from the caller is
	   always preferred when there is one. */
	if (m_system && m_system->GetITimer())
	{
		const float fNow = m_system->GetITimer()->GetAsyncCurTime();
		if (dt <= 0.0f && !m_playingSequences.empty())
		{
			dt = (m_fLastAsyncUpdateTime > 0.0f) ? (fNow - m_fLastAsyncUpdateTime) : (1.0f / 60.0f);
			dt = max(0.0f, min(0.5f, dt));
		}
		m_fLastAsyncUpdateTime = fNow;
	}
	else if (dt <= 0.0f && !m_playingSequences.empty())
		dt = 1.0f / 60.0f;
#endif

#if defined(__vita__)
	/* A cut scene that "focuses the camera but never stops" is either a sequence
	   whose time is not advancing or one whose end is never reached.  Those are
	   very different bugs and look the same on screen, so report the sequence's
	   own clock against its range once a second while anything is playing. */
	{
		/* Count updates rather than subtracting dt.  The previous version could
		   only report again after a second of dt had accumulated, so on the one
		   case worth seeing -- a sequence whose clock is frozen -- it printed
		   exactly once and then went quiet, which reads identically to the
		   sequence having ended.  Reporting dt itself is the point here. */
		static unsigned s_nMovieReportCounter = 0;
		if ((s_nMovieReportCounter++ % 120) == 0 && !m_playingSequences.empty() &&
			m_system && m_system->GetILog())
		{
			for (PlayingSequences::iterator rit = m_playingSequences.begin(); rit != m_playingSequences.end(); ++rit)
			{
				Range r = rit->sequence->GetTimeRange();
				m_system->GetILog()->LogToFile("\001[VITA][MOVIE] playing '%s' t=%.2f range=[%.2f..%.2f] flags=0x%x dt=%.4f openCutScenes=%d",
					rit->sequence->GetName(), rit->time, r.start, r.end,
					(unsigned)rit->sequence->GetFlags(), dt, m_nOpenCutScenes);
			}
		}
	}
#endif

	PlayingSequences::iterator next;
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); it = next)
	{
		next = it; ++next;

		PlayingSequence &ps = *it;

		ac.time = ps.time;
		ac.sequence = ps.sequence;
		ac.dt = dt;
		ac.fps = fps;

		// Increase play time.
		ps.time += dt;

		// Check time out of range.
		timeRange = ps.sequence->GetTimeRange();
		if (ps.time > timeRange.end)
		{
			int seqFlags = ps.sequence->GetFlags();
			if (seqFlags & IAnimSequence::ORT_LOOP)
			{
				// Time wrap's back to the start of the time range.
				ps.time = timeRange.start;
			}
			else if (seqFlags & IAnimSequence::ORT_CONSTANT)
			{
				// Time just continues normally past the end of time range.
			}
			else
			{
				// If no out-of-range type specified sequence stopped when time reaches end of range.
				// Que sequence for stopping.
				stopSequences.push_back(ps.sequence);
				continue;
			}
		}

		// Animate sequence. (Can invalidate iterator)
		ps.sequence->Animate( ac );
	}

	// Stop quied sequencs.
	for (int i = 0; i < (int)stopSequences.size(); i++)
	{
		StopSequence( stopSequences[i] );
	}

	/* Safety net.  BeginCutScene takes the player's controls away -- action map
	   "player_dead", physics and AI off -- and only EndCutScene gives them
	   back.  Any path that drops a sequence out of m_playingSequences without
	   going through InternalStopSequence (a level reset or a script stopping
	   the movie system mid-scene) therefore strands the player with no way to
	   move for the rest of the session.  Nothing playing means no cut scene can
	   still be in progress, so close any that are still open. */
	if (m_pUser && m_nOpenCutScenes > 0 && m_playingSequences.empty())
	{
		while (m_nOpenCutScenes > 0)
		{
			m_pUser->EndCutScene();
			--m_nOpenCutScenes;
		}
	}

	/* Same safety net for the camera.  A sequence dropped out of
	   m_playingSequences by any route other than StopSequence would otherwise
	   leave the view pinned to its camera permanently. */
	ReleaseCameraIfIdle();

	/* Second safety valve, for the case the one above cannot catch: a sequence
	   that is still "playing" but is never going to finish, or one whose camera
	   does not animate.  There is no skip binding on this platform, so without
	   a ceiling the player simply loses control of the game.  No cut scene in
	   the campaign runs anywhere near this long, so hitting it always means
	   something is wrong -- end the scene and give the controls back. */
	if (m_pUser && m_nOpenCutScenes > 0)
	{
		/* Count updates, not seconds.  This watchdog accumulated dt, which makes
		   it useless against the exact failure it exists to catch: a cut scene
		   whose clock is not advancing.  If dt is zero the sequence never
		   reaches the end of its range AND the watchdog never trips, so the
		   player keeps the "player_dead" action map indefinitely -- which is
		   what a cut scene that "never ends" actually is.  A count of update
		   calls advances regardless of what dt says. */
		const float kMaxCutSceneSeconds = 90.0f;
		m_fOpenCutSceneTime += (dt > 0.0f) ? dt : (1.0f / 60.0f);
		if (m_fOpenCutSceneTime > kMaxCutSceneSeconds)
		{
			if (m_system && m_system->GetILog())
				m_system->GetILog()->LogToFile(
					"\001[VITA][MOVIE] cut scene ran past %.0fs -- forcing it to end and restoring control",
					kMaxCutSceneSeconds);
			StopAllSequences();
			while (m_nOpenCutScenes > 0)
			{
				m_pUser->EndCutScene();
				--m_nOpenCutScenes;
			}
			m_fOpenCutSceneTime = 0.0f;
		}
	}
	else
		m_fOpenCutSceneTime = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Callback(ECallbackReason Reason)
{
	if (!m_pCallback)
		return;
	switch (Reason)
	{
		case CBR_ADDNODE:
			m_pCallback->OnAddNode();
			break;
		case CBR_REMOVENODE:
			m_pCallback->OnRemoveNode();
			break;
		case CBR_CHANGENODE:
			m_pCallback->OnChangeNode();
			break;
		case CBR_REGISTERNODECB:
			m_pCallback->OnRegisterNodeCallback();
			break;
		case CBR_UNREGISTERNODECB:
			m_pCallback->OnUnregisterNodeCallback();
			break;
	}
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Serialize( XmlNodeRef &xmlNode,bool bLoading,bool bRemoveOldNodes,bool bLoadEmpty )
{
	if (bLoading)
	{
		RemoveAllSequences();
		if (bRemoveOldNodes)
		{
			RemoveAllNodes();
		}
		//////////////////////////////////////////////////////////////////////////
		// Load animation nodes from XML.
		//////////////////////////////////////////////////////////////////////////
		XmlNodeRef nodeNode=xmlNode->findChild("NodeData");
		if (nodeNode)
		{
			std::map<int,int> mapNodeTarget;
			for (int i=0;i<nodeNode->getChildCount();i++)
			{
				XmlNodeRef node=nodeNode->getChild(i);
				IAnimNode *pAnimNode = NULL;
				string sTarget;

				int nodeId = atoi(node->getAttr("Id"));
				// If node with such ID already exists. skip it.
				if (!GetNode(nodeId))
				{
					int nodeType = atoi(node->getAttr("Type"));
					pAnimNode = CreateNode( nodeType,nodeId );
					if (pAnimNode)
					{
						pAnimNode->SetName(node->getAttr("Name"));
						int entityId = -1;
						if (node->getAttr("EntityId",entityId))
						{
							pAnimNode->SetEntity(entityId);
						}
						pAnimNode->Serialize( node,true );

						int targetId = -1;
						if (node->getAttr("TargetId",targetId))
						{
							if (!sTarget.empty())
								mapNodeTarget.insert(std::map<int,int>::value_type( pAnimNode->GetId(), targetId ));
						}
					}
				}
			}
			// After all nodes loaded,Bind targets.
			for (std::map<int,int>::iterator It=mapNodeTarget.begin();It!=mapNodeTarget.end();++It)
			{
				IAnimNode *pAnimNode=GetNode(It->first);
				assert(pAnimNode);
				pAnimNode->SetTarget(GetNode(It->second));
			}
		}
		//////////////////////////////////////////////////////////////////////////
		// Load sequences from XML.
		//////////////////////////////////////////////////////////////////////////
		XmlNodeRef seqNode=xmlNode->findChild("SequenceData");
		if (seqNode)
		{
			for (int i=0;i<seqNode->getChildCount();i++)
			{
				XmlNodeRef childNode = seqNode->getChild(i);
				if (!LoadSequence(childNode, bLoadEmpty))
					return;
			}
		}
		//Reset();
	}else
	{
		// Save animation nodes to xml.
		XmlNodeRef nodesNode = xmlNode->newChild("NodeData");
		for (Nodes::iterator nodeIt = m_nodes.begin(); nodeIt != m_nodes.end(); ++nodeIt)
		{
			XmlNodeRef nodeNode = nodesNode->newChild("Node");
			IAnimNode *pNode = nodeIt->second;
			pNode->Serialize( nodeNode,false );
		}

		XmlNodeRef sequencesNode=xmlNode->newChild("SequenceData");
		ISequenceIt *It=GetSequences();
		IAnimSequence *seq=It->first();;
		while (seq)
		{
			XmlNodeRef sequenceNode=sequencesNode->newChild("Sequence");
			seq->Serialize(sequenceNode, false);
			seq=It->next();
		}
		It->Release();
	}
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::SetCameraParams( const SCameraParams &Params )
{
	m_ActiveCameraParams = Params;
	if (m_pUser)
		m_pUser->SetActiveCamera(m_ActiveCameraParams);
	if (m_pCallback)
		m_pCallback->OnSetCamera( m_ActiveCameraParams );
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::SendGlobalEvent( const char *pszEvent )
{
	if (m_pUser)
		m_pUser->SendGlobalEvent(pszEvent);
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Pause()
{
	if (m_bPaused)
		return;
	m_bPaused = true;

	/*
	PlayingSequences::iterator next;
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); it = next)
	{
		next = it; ++next;
		PlayingSequence &ps = *it;
		ps.sequence->Pause();
	}
	*/
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::Resume()
{
	if (!m_bPaused)
		return;

	m_bPaused = false;

	/*
	PlayingSequences::iterator next;
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != m_playingSequences.end(); it = next)
	{
		next = it; ++next;
		PlayingSequence &ps = *it;
		ps.sequence->Resume();
	}
	*/
}

//////////////////////////////////////////////////////////////////////////
void CMovieSystem::OnPlaySound( ISound *pSound )
{
	if (m_pUser)
		m_pUser->PlaySubtitles( pSound );
}

float CMovieSystem::GetPlayingTime(IAnimSequence * pSeq)
{
	if (!pSeq)
		return -1;

	if (!IsPlaying(pSeq))
		return -1;

	PlayingSequences::const_iterator itend = m_playingSequences.end();
	for (PlayingSequences::const_iterator it = m_playingSequences.begin(); it != itend; ++it)
	{
		if (it->sequence == pSeq)
			return it->time;
	}

	return -1;
}

bool CMovieSystem::SetPlayingTime(IAnimSequence * pSeq, float fTime)
{
	if (!pSeq)
		return false;

	if (!IsPlaying(pSeq))
		return false;

	PlayingSequences::iterator itend = m_playingSequences.end();
	for (PlayingSequences::iterator it = m_playingSequences.begin(); it != itend; ++it)
	{
		if (it->sequence == pSeq)
			it->time = fTime;
	}


	return false;
}

void CMovieSystem::SetSequenceStopBehavior( ESequenceStopBehavior behavior )
{
	m_sequenceStopBehavior = behavior;
}
