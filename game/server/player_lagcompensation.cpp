//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "bone_setup.h"
#include "cbase.h"
#include "icvar.h"
#include "usercmd.h"
#include "igamesystem.h"
#include "ilagcompensationmanager.h"
#include "inetchannelinfo.h"
#include "util.h"
#include "utllinkedlist.h"
#include "BaseAnimatingOverlay.h"
#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define LC_NONE				0
#define LC_ALIVE			(1<<0)

#define LC_ORIGIN_CHANGED	(1<<8)
#define LC_ANGLES_CHANGED	(1<<9)
#define LC_SIZE_CHANGED		(1<<10)
#define LC_ANIMATION_CHANGED (1<<11)
#define LC_POSE_PARAMS_CHANGED (1<<12)
#define LC_ENCD_CONS_CHANGED (1<<13)

ConVar sv_unlag( "sv_unlag", "1", FCVAR_DEVELOPMENTONLY, "Enables player lag compensation" );
ConVar sv_maxunlag( "sv_maxunlag", "1.0", FCVAR_DEVELOPMENTONLY, "Maximum lag compensation in seconds", true, 0.0f, true, 1.0f );
ConVar sv_lagflushbonecache( "sv_lagflushbonecache", "0", FCVAR_DEVELOPMENTONLY, "Flushes entity bone cache on lag compensation" );
ConVar sv_unlag_fixstuck( "sv_unlag_fixstuck", "0", FCVAR_DEVELOPMENTONLY, "Disallow backtracking a player for lag compensation if it will cause them to become stuck" );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

struct LagRecord
{
public:
	LagRecord()
	{
		m_fFlags = 0;
		m_vecOrigin.Init();
		m_vecAngles.Init();
		m_vecMinsPreScaled.Init();
		m_vecMaxsPreScaled.Init();
		m_flSimulationTime = -1;
		m_flAnimTime = -1;
		m_masterSequence = 0;
		m_masterCycle = 0;

		for (int i = 0; i < MAX_LAYER_RECORDS; i++)
		{
			m_poseParameters[i] = -1;
		}

		for (int i = 0; i < MAX_ENCODED_CONTROLLERS; i++)
		{
			m_encodedControllers[i] = -1;
		}
	}

	LagRecord( const LagRecord& src )
	{
		m_fFlags = src.m_fFlags;
		m_vecOrigin = src.m_vecOrigin;
		m_vecAngles = src.m_vecAngles;
		m_vecMinsPreScaled = src.m_vecMinsPreScaled;
		m_vecMaxsPreScaled = src.m_vecMaxsPreScaled;
		m_flSimulationTime = src.m_flSimulationTime;
		m_flAnimTime = src.m_flAnimTime;
		for( int layerIndex = 0; layerIndex < MAX_LAYER_RECORDS; ++layerIndex )
		{
			m_layerRecords[layerIndex] = src.m_layerRecords[layerIndex];
		}
		m_masterSequence = src.m_masterSequence;
		m_masterCycle = src.m_masterCycle;

		for (int i = 0; i < MAX_LAYER_RECORDS; i++)
		{
			m_poseParameters[i] = src.m_poseParameters[i];
		}

		for (int i = 0; i < MAX_LAYER_RECORDS; i++)
		{
			m_encodedControllers[i] = src.m_encodedControllers[i];
		}
	}

	// Did player die this frame
	int						m_fFlags;

	// Player position, orientation and bbox
	Vector					m_vecOrigin;
	QAngle					m_vecAngles;
	Vector					m_vecMinsPreScaled;
	Vector					m_vecMaxsPreScaled;

	float					m_flSimulationTime;
	float					m_flAnimTime;	
	
	// Player animation details, so we can get the legs in the right spot.
	LayerRecord				m_layerRecords[MAX_LAYER_RECORDS];
	int						m_masterSequence;
	float					m_masterCycle;
	float					m_poseParameters[MAX_POSE_PARAMETERS];
	float					m_encodedControllers[MAX_ENCODED_CONTROLLERS];
};


//
// Try to take the player from his current origin to vWantedPos.
// If it can't get there, leave the player where he is.
// 

ConVar sv_unlag_debug( "sv_unlag_debug", "0", FCVAR_GAMEDLL | FCVAR_DEVELOPMENTONLY );

float g_flFractionScale = 0.95;
static void RestorePlayerTo( CBasePlayer *pPlayer, const Vector &vWantedPos )
{
	// Try to move to the wanted position from our current position.
	trace_t tr;
	VPROF_BUDGET( "RestorePlayerTo", "CLagCompensationManager" );
	UTIL_TraceEntity( pPlayer, vWantedPos, vWantedPos, MASK_PLAYERSOLID, pPlayer, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
	if ( tr.startsolid || tr.allsolid )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "RestorePlayerTo() could not restore player position for client \"%s\" ( %.1f %.1f %.1f )\n",
					pPlayer->GetPlayerName(), vWantedPos.x, vWantedPos.y, vWantedPos.z );
		}

		UTIL_TraceEntity( pPlayer, pPlayer->GetLocalOrigin(), vWantedPos, MASK_PLAYERSOLID, pPlayer, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
		if ( tr.startsolid || tr.allsolid )
		{
			// In this case, the guy got stuck back wherever we lag compensated him to. Nasty.

			if ( sv_unlag_debug.GetBool() )
				DevMsg( " restore failed entirely\n" );
		}
		else
		{
			// We can get to a valid place, but not all the way back to where we were.
			Vector vPos;
			VectorLerp( pPlayer->GetLocalOrigin(), vWantedPos, tr.fraction * g_flFractionScale, vPos );
			UTIL_SetOrigin( pPlayer, vPos, true );

			if ( sv_unlag_debug.GetBool() )
				DevMsg( " restore got most of the way\n" );
		}
	}
	else
	{
		// Cool, the player can go back to whence he came.
		UTIL_SetOrigin( pPlayer, tr.endpos, true );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CLagCompensationManager : public CAutoGameSystemPerFrame, public ILagCompensationManager
{
public:
	CLagCompensationManager( char const *name ) : CAutoGameSystemPerFrame( name )
	{
	}

	// IServerSystem stuff
	virtual void Shutdown()
	{
		ClearHistory();
	}

	virtual void LevelShutdownPostEntity()
	{
		ClearHistory();
	}

	// called after entities think
	virtual void FrameUpdatePostEntityThink();

	// ILagCompensationManager stuff

	// Called during player movement to set up/restore after lag compensation
	void			StartLagCompensation( CBasePlayer *player, CUserCmd *cmd );
	void			FinishLagCompensation( CBasePlayer *player );

private:
	virtual void			BacktrackPlayer( CBasePlayer *player, CUserCmd *cmd );

	void ClearHistory()
	{
		for ( int i=0; i<MAX_PLAYERS; i++ )
			m_PlayerTrack[i].Purge();
	}

	// keep a list of lag records for each player
	CUtlFixedLinkedList< LagRecord >	m_PlayerTrack[ MAX_PLAYERS ];

	// Scratchpad for determining what needs to be restored
	CBitVec<MAX_PLAYERS>	m_RestorePlayer;
	bool					m_bNeedToRestore;
	
	LagRecord				m_RestoreData[ MAX_PLAYERS ];	// player data before we moved him back
	LagRecord				m_ChangeData[ MAX_PLAYERS ];	// player data where we moved him back

	CBasePlayer				*m_pCurrentPlayer;	// The player we are doing lag compensation for
};

static CLagCompensationManager g_LagCompensationManager( "CLagCompensationManager" );
ILagCompensationManager *lagcompensation = &g_LagCompensationManager;


//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all entities have had a chance to think
//-----------------------------------------------------------------------------
void CLagCompensationManager::FrameUpdatePostEntityThink()
{
	if ( (gpGlobals->maxClients <= 1) || !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}

	VPROF_BUDGET( "FrameUpdatePostEntityThink", "CLagCompensationManager" );

	// remove all records before that time:
	int flDeadtime = gpGlobals->curtime - sv_maxunlag.GetFloat();

	// Iterate all active players
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		CUtlFixedLinkedList< LagRecord > *track = &m_PlayerTrack[i-1];

		if ( !pPlayer )
		{
			if ( track->Count() > 0 )
			{
				track->RemoveAll();
			}

			continue;
		}

		Assert( track->Count() < 1000 ); // insanity check

		// remove tail records that are too old
		intp tailIndex = track->Tail();
		while ( track->IsValidIndex( tailIndex ) )
		{
			LagRecord &tail = track->Element( tailIndex );

			// if tail is within limits, stop
			if ( tail.m_flSimulationTime >= flDeadtime )
				break;
			
			// remove tail, get new tail
			track->Remove( tailIndex );
			tailIndex = track->Tail();
		}

		// check if head has same simulation time
		if ( track->Count() > 0 )
		{
			LagRecord &head = track->Element( track->Head() );

			// check if player changed simulation time since last time updated
			if ( head.m_flSimulationTime >= pPlayer->GetSimulationTime() )
				continue; // don't add new entry for same or older time
		}

		// add new record to player track
		LagRecord &record = track->Element( track->AddToHead() );

		record.m_fFlags = 0;
		if ( pPlayer->IsAlive() )
		{
			record.m_fFlags |= LC_ALIVE;
		}

		record.m_flSimulationTime	= pPlayer->GetSimulationTime();
		record.m_flAnimTime			= pPlayer->GetAnimTime();
		record.m_vecAngles			= pPlayer->GetLocalAngles();
		record.m_vecOrigin			= pPlayer->GetLocalOrigin();
		record.m_vecMinsPreScaled	= pPlayer->CollisionProp()->OBBMinsPreScaled();
		record.m_vecMaxsPreScaled	= pPlayer->CollisionProp()->OBBMaxsPreScaled();

		int layerCount = pPlayer->GetNumAnimOverlays();
		for( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			CAnimationLayer *currentLayer = pPlayer->GetAnimOverlay(layerIndex);
			if( currentLayer )
			{
				record.m_layerRecords[layerIndex].m_cycle = currentLayer->m_flCycle;
				record.m_layerRecords[layerIndex].m_order = currentLayer->m_nOrder;
				record.m_layerRecords[layerIndex].m_sequence = currentLayer->m_nSequence;
				record.m_layerRecords[layerIndex].m_weight = currentLayer->m_flWeight;
			}
		}
		record.m_masterSequence = pPlayer->GetSequence();
		record.m_masterCycle = pPlayer->GetCycle();

		CStudioHdr *hdr = pPlayer->GetModelPtr();
		if( hdr )
		{
			for( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
			{
				record.m_poseParameters[paramIndex] = pPlayer->GetPoseParameterArray()[ paramIndex ];
			}
		}

		if( hdr )
		{
			for( int paramIndex = 0; paramIndex < hdr->GetNumBoneControllers(); paramIndex++ )
			{
				record.m_encodedControllers[paramIndex] = pPlayer->GetEncodedControllerArray()[ paramIndex ];
			}
		}
	}

	//Clear the current player.
	m_pCurrentPlayer = NULL;
}

// Called during player movement to set up/restore after lag compensation
void CLagCompensationManager::StartLagCompensation( CBasePlayer *player, CUserCmd *cmd )
{
	//DONT LAG COMP AGAIN THIS FRAME IF THERES ALREADY ONE IN PROGRESS
	//IF YOU'RE HITTING THIS THEN IT MEANS THERES A CODE BUG
	if ( m_pCurrentPlayer )
	{
		Assert( m_pCurrentPlayer == NULL );
		Warning( "Trying to start a new lag compensation session while one is already active!\n" );
		return;
	}

	// Assume no players need to be restored
	m_RestorePlayer.ClearAll();
	m_bNeedToRestore = false;

	m_pCurrentPlayer = player;
	
	if ( !player->m_bLagCompensation		// Player not wanting lag compensation
		 || (gpGlobals->maxClients <= 1)	// no lag compensation in single player
		 || !sv_unlag.GetBool()				// disabled by server admin
		 || player->IsBot() 				// not for bots
		 || player->IsObserver()			// not for spectators
		)
		return;

	// NOTE: Put this here so that it won't show up in single player mode.
	VPROF_BUDGET( "StartLagCompensation", VPROF_BUDGETGROUP_OTHER_NETWORKING );
	Q_memset( m_RestoreData, 0, sizeof( m_RestoreData ) );
	Q_memset( m_ChangeData, 0, sizeof( m_ChangeData ) );

	// Iterate all active players
	const CBitVec<MAX_EDICTS> *pEntityTransmitBits = engine->GetEntityTransmitBitsForClient( player->entindex() - 1 );
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		if ( !pPlayer )
		{
			continue;
		}

		// Don't lag compensate yourself you loser...
		if ( player == pPlayer )
		{
			continue;
		}

		// Custom checks for if things should lag compensate (based on things like what team the player is on).
		if ( !player->WantsLagCompensationOnEntity( pPlayer, cmd, pEntityTransmitBits ) )
			continue;

		// Move other player back in time
		BacktrackPlayer( pPlayer, cmd );
	}
}

void CLagCompensationManager::BacktrackPlayer( CBasePlayer *pPlayer, CUserCmd *cmd )
{
	Vector org;
	Vector minsPreScaled;
	Vector maxsPreScaled;
	QAngle ang;

	VPROF_BUDGET( "BacktrackPlayer", "CLagCompensationManager" );
	int pl_index = pPlayer->entindex() - 1;

	float flTargetSimulationTime = cmd->simulationdata[pl_index + 1].m_flSimulationTime;
    float flTargetSimulatedAnimationTime = cmd->simulationdata[pl_index + 1].m_flAnimTime;
    
	// get track history of this player
	CUtlFixedLinkedList< LagRecord > *trackSim = &m_PlayerTrack[ pl_index ];
	CUtlFixedLinkedList< LagRecord > *trackAnim = &m_PlayerTrack[ pl_index ];

	// check if we have at leat one entry
	if ( trackSim->Count() <= 0 || trackAnim->Count() <= 0 )
		return;

    intp currSim = trackSim->Head();

	LagRecord *prevRecordSim = NULL;
	LagRecord *recordSim = NULL;
	LagRecord *recordAnim = NULL;

    Vector prevOrg = pPlayer->GetLocalOrigin();
    bool foundAnimationData = false;
	
	// Walk context looking for any invalidating event
	while( trackSim->IsValidIndex(currSim) )
	{
		// remember last record
		prevRecordSim = recordSim;

		// get next record
        recordSim = &trackSim->Element(currSim);

        if (recordSim->m_flSimulationTime
            <= flTargetSimulatedAnimationTime && !foundAnimationData)
        {
            recordAnim = recordSim;
            foundAnimationData = true;
		}

		if ( !(recordSim->m_fFlags & LC_ALIVE) )
		{
			// player most be alive, lost track
			return;
		}

		// TODO_ENHANCED: do proper teleportation checks.

		// did we find a context smaller than target time ?
		if ( recordSim->m_flSimulationTime <= flTargetSimulationTime )
			break; // hurra, stop

		prevOrg = recordSim->m_vecOrigin;

		// go one step back
		currSim = trackSim->Next( currSim );
	}

	Assert( recordAnim );
	Assert( recordSim );

	if ( !recordSim || !recordAnim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "No valid positions in history for BacktrackPlayer client ( %s )\n", pPlayer->GetPlayerName() );
		}

		return; // that should never happen
	}

    float fracSim = 0.0f;
	if ( prevRecordSim && 
		 (recordSim->m_flSimulationTime < flTargetSimulationTime) &&
		 (recordSim->m_flSimulationTime < prevRecordSim->m_flSimulationTime) )
	{
		// we didn't find the exact time but have a valid previous record
		// so interpolate between these two records;

		Assert( prevRecordSim->m_flSimulationTime > recordSim->m_flSimulationTime );
		Assert( flTargetSimulationTime < prevRecordSim->m_flSimulationTime );

        // calc fraction between both records
		fracSim = float(( double(flTargetSimulationTime) - double(recordSim->m_flSimulationTime) ) / 
			( double(prevRecordSim->m_flSimulationTime) - double(recordSim->m_flSimulationTime) ));

		Assert( fracSim > 0 && fracSim < 1 ); // should never extrapolate

		ang				= Lerp( fracSim, recordSim->m_vecAngles, prevRecordSim->m_vecAngles );
		org				= Lerp( fracSim, recordSim->m_vecOrigin, prevRecordSim->m_vecOrigin );
		minsPreScaled	= Lerp( fracSim, recordSim->m_vecMinsPreScaled, prevRecordSim->m_vecMinsPreScaled );
		maxsPreScaled	= Lerp( fracSim, recordSim->m_vecMaxsPreScaled, prevRecordSim->m_vecMaxsPreScaled );
	}
	else
	{
		// we found the exact record or no other record to interpolate with
		// just copy these values since they are the best we have
		org				= recordSim->m_vecOrigin;
		ang				= recordSim->m_vecAngles;
		minsPreScaled	= recordSim->m_vecMinsPreScaled;
		maxsPreScaled	= recordSim->m_vecMaxsPreScaled;
	}

	// See if this is still a valid position for us to teleport to
	if ( sv_unlag_fixstuck.GetBool() )
	{
		// Try to move to the wanted position from our current position.
		trace_t tr;
		UTIL_TraceEntity( pPlayer, org, org, MASK_PLAYERSOLID, &tr );
		if ( tr.startsolid || tr.allsolid )
		{
			if ( sv_unlag_debug.GetBool() )
				DevMsg( "WARNING: BackupPlayer trying to back player into a bad position - client %s\n", pPlayer->GetPlayerName() );

			CBasePlayer *pHitPlayer = dynamic_cast<CBasePlayer *>( tr.m_pEnt );

			// don't lag compensate the current player
			if ( pHitPlayer && ( pHitPlayer != m_pCurrentPlayer ) )	
			{
				// If we haven't backtracked this player, do it now
				// this deliberately ignores WantsLagCompensationOnEntity.
				if ( !m_RestorePlayer.Get( pHitPlayer->entindex() - 1 ) )
				{
					// prevent recursion - save a copy of m_RestorePlayer,
					// pretend that this player is off-limits
					int pl_index = pPlayer->entindex() - 1;

					// Temp turn this flag on
					m_RestorePlayer.Set( pl_index );

					BacktrackPlayer( pHitPlayer, cmd );

					// Remove the temp flag
					m_RestorePlayer.Clear( pl_index );
				}				
			}

			// now trace us back as far as we can go
			UTIL_TraceEntity( pPlayer, pPlayer->GetLocalOrigin(), org, MASK_PLAYERSOLID, &tr );

			if ( tr.startsolid || tr.allsolid )
			{
				// Our starting position is bogus

				if ( sv_unlag_debug.GetBool() )
					DevMsg( "Backtrack failed completely, bad starting position\n" );
			}
			else
			{
				// We can get to a valid place, but not all the way to the target
				Vector vPos;
				VectorLerp( pPlayer->GetLocalOrigin(), org, tr.fraction * g_flFractionScale, vPos );
				
				// This is as close as we're going to get
				org = vPos;

				if ( sv_unlag_debug.GetBool() )
					DevMsg( "Backtrack got most of the way\n" );
			}
		}
	}
	
	// See if this represents a change for the player
	int flags = 0;
	LagRecord *restore = &m_RestoreData[ pl_index ];
	LagRecord *change  = &m_ChangeData[ pl_index ];

	QAngle angdiff = pPlayer->GetLocalAngles() - ang;
	Vector orgdiff = pPlayer->GetLocalOrigin() - org;

	// Always remember the pristine simulation time in case we need to restore it.
	restore->m_flSimulationTime = pPlayer->GetSimulationTime();
	restore->m_flAnimTime = pPlayer->GetAnimTime();

	if ( angdiff.LengthSqr() > 0.0f )
	{
		flags |= LC_ANGLES_CHANGED;
		restore->m_vecAngles = pPlayer->GetLocalAngles();
		pPlayer->SetLocalAngles( ang );
		change->m_vecAngles = ang;
	}

	// Use absolute equality here
	if ( minsPreScaled != pPlayer->CollisionProp()->OBBMinsPreScaled() || maxsPreScaled != pPlayer->CollisionProp()->OBBMaxsPreScaled() )
	{
		flags |= LC_SIZE_CHANGED;

		restore->m_vecMinsPreScaled = pPlayer->CollisionProp()->OBBMinsPreScaled();
		restore->m_vecMaxsPreScaled = pPlayer->CollisionProp()->OBBMaxsPreScaled();
		
		pPlayer->SetSize( minsPreScaled, maxsPreScaled );
		
		change->m_vecMinsPreScaled = minsPreScaled;
		change->m_vecMaxsPreScaled = maxsPreScaled;
	}

	// Note, do origin at end since it causes a relink into the k/d tree
	if ( orgdiff.LengthSqr() > 0.0f )
	{
		flags |= LC_ORIGIN_CHANGED;
		restore->m_vecOrigin = pPlayer->GetLocalOrigin();
		pPlayer->SetLocalOrigin( org );
		change->m_vecOrigin = org;
	}

	// Sorry for the loss of the optimization for the case of people
	// standing still, but you breathe even on the server.
	// This is quicker than actually comparing all bazillion floats.
	flags |= LC_ANIMATION_CHANGED;
	restore->m_masterSequence = pPlayer->GetSequence();
	restore->m_masterCycle = pPlayer->GetCycle();

	pPlayer->SetSequence(recordAnim->m_masterSequence);
	pPlayer->SetCycle(recordAnim->m_masterCycle);

	////////////////////////
	// Now do all the layers
	int layerCount = pPlayer->GetNumAnimOverlays();
	for( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
	{
		CAnimationLayer *currentLayer = pPlayer->GetAnimOverlay(layerIndex);
		if( currentLayer )
		{
			restore->m_layerRecords[layerIndex].m_cycle = currentLayer->m_flCycle;
			restore->m_layerRecords[layerIndex].m_order = currentLayer->m_nOrder;
			restore->m_layerRecords[layerIndex].m_sequence = currentLayer->m_nSequence;
            restore->m_layerRecords[layerIndex].m_weight = currentLayer
                                                             ->m_flWeight;

			currentLayer->m_flCycle = recordAnim->m_layerRecords[layerIndex].m_cycle;
			currentLayer->m_nOrder = recordAnim->m_layerRecords[layerIndex].m_order;
			currentLayer->m_nSequence = recordAnim->m_layerRecords[layerIndex].m_sequence;
            currentLayer->m_flWeight = recordAnim->m_layerRecords[layerIndex].m_weight;
        }
	}
	
	flags |= LC_POSE_PARAMS_CHANGED;

	// Now do pose parameters
	CStudioHdr *hdr = pPlayer->GetModelPtr();
	if( hdr )
	{
		for( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
		{
			restore->m_poseParameters[paramIndex] = pPlayer->GetPoseParameterArray()[paramIndex];
			float poseParameter = recordAnim->m_poseParameters[paramIndex];

			pPlayer->SetPoseParameterRaw(paramIndex, poseParameter);
		}
	}

	flags |= LC_ENCD_CONS_CHANGED;

	if( hdr )
	{
		for( int paramIndex = 0; paramIndex < hdr->GetNumBoneControllers(); paramIndex++ )
		{
			restore->m_encodedControllers[paramIndex] = pPlayer->GetEncodedControllerArray()[paramIndex];
            float encodedController = recordAnim->m_encodedControllers[paramIndex];

			pPlayer->SetBoneControllerRaw( paramIndex, encodedController );
		}
	}

	if ( !flags )
		return; // we didn't change anything

	// Set lag compensated player's times
	pPlayer->SetSimulationTime(flTargetSimulationTime);
	// pPlayer->SetAnimTime(animationData->m_flAnimTime);

	if ( sv_lagflushbonecache.GetBool() )
        pPlayer->InvalidateBoneCache();

	/*char text[256]; Q_snprintf( text, sizeof(text), "time %.2f", flTargetTime );
	pPlayer->DrawServerHitboxes( 10 );
	NDebugOverlay::Text( org, text, false, 10 );
	NDebugOverlay::EntityBounds( pPlayer, 255, 0, 0, 32, 10 ); */

	m_RestorePlayer.Set( pl_index ); //remember that we changed this player
	m_bNeedToRestore = true;  // we changed at least one player
	restore->m_fFlags = flags; // we need to restore these flags
	change->m_fFlags = flags; // we have changed these flags
}


void CLagCompensationManager::FinishLagCompensation( CBasePlayer *player )
{
	VPROF_BUDGET_FLAGS( "FinishLagCompensation", VPROF_BUDGETGROUP_OTHER_NETWORKING, BUDGETFLAG_CLIENT|BUDGETFLAG_SERVER );

	m_pCurrentPlayer = NULL;

	if ( !m_bNeedToRestore )
		return; // no player was changed at all

	// Iterate all active players
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		int pl_index = i - 1;
		
		if ( !m_RestorePlayer.Get( pl_index ) )
		{
			// player wasn't changed by lag compensation
			continue;
		}

		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( !pPlayer )
		{
			continue;
		}

		LagRecord *restore = &m_RestoreData[ pl_index ];
		LagRecord *change  = &m_ChangeData[ pl_index ];

		if ( restore->m_fFlags & LC_SIZE_CHANGED )
		{
			// see if simulation made any changes, if no, then do the restore, otherwise,
			//  leave new values in
			if ( pPlayer->CollisionProp()->OBBMinsPreScaled() == change->m_vecMinsPreScaled &&
				pPlayer->CollisionProp()->OBBMaxsPreScaled() == change->m_vecMaxsPreScaled )
			{
				// Restore it
				pPlayer->SetSize( restore->m_vecMinsPreScaled, restore->m_vecMaxsPreScaled );
			}
#ifdef STAGING_ONLY
			else
			{
				Warning( "Should we really not restore the size?\n" );
			}
#endif
		}

		if ( restore->m_fFlags & LC_ANGLES_CHANGED )
		{
			if ( pPlayer->GetLocalAngles() == change->m_vecAngles )
			{
				pPlayer->SetLocalAngles( restore->m_vecAngles );
			}
		}

		if ( restore->m_fFlags & LC_ORIGIN_CHANGED )
		{
			// Okay, let's see if we can do something reasonable with the change
			Vector delta = pPlayer->GetLocalOrigin() - change->m_vecOrigin;
			
			RestorePlayerTo( pPlayer, restore->m_vecOrigin + delta );
		}

		if( restore->m_fFlags & LC_ANIMATION_CHANGED )
		{
			pPlayer->SetSequence(restore->m_masterSequence);
			pPlayer->SetCycle(restore->m_masterCycle);

			int layerCount = pPlayer->GetNumAnimOverlays();
			for( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
			{
				CAnimationLayer *currentLayer = pPlayer->GetAnimOverlay(layerIndex);
				if( currentLayer )
				{
					currentLayer->m_flCycle = restore->m_layerRecords[layerIndex].m_cycle;
					currentLayer->m_nOrder = restore->m_layerRecords[layerIndex].m_order;
					currentLayer->m_nSequence = restore->m_layerRecords[layerIndex].m_sequence;
					currentLayer->m_flWeight = restore->m_layerRecords[layerIndex].m_weight;
				}
			}
		}

		if( restore->m_fFlags & LC_POSE_PARAMS_CHANGED )
		{
			CStudioHdr *hdr = pPlayer->GetModelPtr();
			if( hdr )
			{
				for( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
				{
					pPlayer->SetPoseParameterRaw( paramIndex, restore->m_poseParameters[paramIndex] );
				}
			}
		}

		if( restore->m_fFlags & LC_ENCD_CONS_CHANGED )
		{
			CStudioHdr *hdr = pPlayer->GetModelPtr();
			if( hdr )
			{
				for( int paramIndex = 0; paramIndex < hdr->GetNumBoneControllers(); paramIndex++ )
				{
					pPlayer->SetBoneControllerRaw( paramIndex, restore->m_encodedControllers[paramIndex] );
				}
			}
		}

		pPlayer->SetSimulationTime( restore->m_flSimulationTime );
        pPlayer->SetAnimTime(restore->m_flAnimTime);
	}
}


