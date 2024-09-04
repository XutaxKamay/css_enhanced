//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "icvar.h"
#include "player.h"
#include "shareddefs.h"
#include "studio.h"
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

#define LC_NONE				   0
#define LC_ALIVE			   ( 1 << 0 )

#define LC_ORIGIN_CHANGED	   ( 1 << 8 )
#define LC_ANGLES_CHANGED	   ( 1 << 9 )
#define LC_SIZE_CHANGED		   ( 1 << 10 )
#define LC_ANIMATION_CHANGED   ( 1 << 11 )
#define LC_POSE_PARAMS_CHANGED ( 1 << 12 )
#define LC_ENCD_CONS_CHANGED   ( 1 << 13 )
#define LC_ANIM_OVERS_CHANGED  ( 1 << 14 )

// Default to 1 second max.
#define MAX_TICKS_SAVED		   1024

ConVar sv_unlag( "sv_unlag", "1", 0, "Enables entity lag compensation" );
// Enable by default to avoid some bugs.
ConVar sv_lagflushbonecache( "sv_lagflushbonecache", "1", 0, "Flushes entity bone cache on lag compensation" );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

struct LayerRecord
{
	int m_sequence;
	float m_cycle;
	float m_weight;
	int m_order;
	int m_flags;
};

struct LagRecord
{
  public:
	// Did entity die this frame
	int m_fFlags;

	// Player position, orientation and bbox
	Vector m_vecOrigin;
	QAngle m_vecAngles;
	Vector m_vecMinsPreScaled;
	Vector m_vecMaxsPreScaled;

	float m_flSimulationTime;
	float m_flAnimTime;

	// Player animation details, so we can get the legs in the right spot.
	LayerRecord m_layerRecords[MAX_LAYER_RECORDS];
	int m_masterSequence;
	float m_masterCycle;
	float m_poseParameters[MAXSTUDIOPOSEPARAM];
	float m_encodedControllers[MAXSTUDIOBONECTRLS];
};

//
// Try to take the entity from his current origin to vWantedPos.
// If it can't get there, leave the entity where he is.
//

ConVar sv_unlag_debug( "sv_unlag_debug", "0" );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CLagCompensationManager : public CAutoGameSystemPerFrame,
								public ILagCompensationManager
{
  public:
	CLagCompensationManager( const char* name )
	{
	}

	// IServerSystem stuff
	void Shutdown() override
	{
		ClearHistory();
	}

	void LevelShutdownPostEntity() override
	{
		ClearHistory();
	}

	// ILagCompensationManager stuff

	// Called during player movement to set up/restore after lag compensation
	void StartLagCompensation( CBasePlayer* player, CUserCmd* cmd ) override;
	void FinishLagCompensation( CBasePlayer* player ) override;
	void TrackEntities( void );
	inline void BacktrackEntity( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd );

	void ClearHistory()
	{
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			m_EntityTrack[i].Clear();
		}
	}

	void FrameUpdatePostEntityThink() override
	{
		TrackEntities();
	}

	// keep a list of lag records for each entities
	CUtlCircularBuffer< LagRecord, MAX_TICKS_SAVED > m_EntityTrack[MAX_EDICTS];

	// Scratchpad for determining what needs to be restored
	CBitVec< MAX_EDICTS > m_RestoreEntity;
	bool m_bNeedToRestore;

	LagRecord m_RestoreData[MAX_EDICTS]; // entities data before we moved him back
	LagRecord m_ChangeData[MAX_EDICTS];	 // entities data where we moved him back
};

static CLagCompensationManager g_LagCompensationManager( "CLagCompensationManager" );
ILagCompensationManager* lagcompensation = &g_LagCompensationManager;

//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all entities have had a chance to think
//-----------------------------------------------------------------------------
void CLagCompensationManager::TrackEntities()
{
	LagRecord record;

	if ( !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}

	VPROF_BUDGET( "TrackEntities", "CLagCompensationManager" );

	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		CBaseEntity* pEntity = UTIL_EntityByIndex( i );

		if ( !pEntity )
		{
			continue;
		}

		// remove all records before that time:
		auto track = &m_EntityTrack[i];

		// add new record to entity track

		record.m_fFlags			  = LC_NONE;
		record.m_flSimulationTime = pEntity->GetSimulationTime();
		record.m_flAnimTime		  = pEntity->GetAnimTime();
		record.m_vecAngles		  = pEntity->GetAbsAngles();
		record.m_vecOrigin		  = pEntity->GetAbsOrigin();
		record.m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
		record.m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();

		auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

		if ( pAnim )
		{
			record.m_masterSequence = pAnim->GetSequence();
			record.m_masterCycle	= pAnim->GetCycle();

			CStudioHdr* hdr = pAnim->GetModelPtr();

			if ( hdr )
			{
				for ( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
				{
					record.m_poseParameters[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
				}

				for ( int boneIndex = 0; boneIndex < hdr->GetNumBoneControllers(); boneIndex++ )
				{
					record.m_encodedControllers[boneIndex] = pAnim->GetBoneControllerArray()[boneIndex];
				}
			}
		}

		auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

		if ( pAnimOverlay )
		{
			int layerCount = pAnimOverlay->GetNumAnimOverlays();

			for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
			{
				CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );
				if ( currentLayer )
				{
					record.m_layerRecords[layerIndex].m_cycle	 = currentLayer->m_flCycle;
					record.m_layerRecords[layerIndex].m_order	 = currentLayer->m_nOrder;
					record.m_layerRecords[layerIndex].m_sequence = currentLayer->m_nSequence;
					record.m_layerRecords[layerIndex].m_weight	 = currentLayer->m_flWeight;
					record.m_layerRecords[layerIndex].m_flags	 = currentLayer->m_fFlags;
				}
			}
		}

		track->Push( record );
	}
}

// Called during player movement to set up/restore after lag compensation
void CLagCompensationManager::StartLagCompensation( CBasePlayer* player, CUserCmd* cmd )
{
	// Assume no entities need to be restored
	m_RestoreEntity.ClearAll();
	m_bNeedToRestore = false;

	if ( !player->m_bLagCompensation // Player not wanting lag compensation
		 || !sv_unlag.GetBool()		 // disabled by server admin
		 || player->IsBot()			 // not for bots
		 || player->IsObserver()	 // not for spectators
	)
	{
		return;
	}

	// NOTE: Put this here so that it won't show up in single player mode.
	VPROF_BUDGET( "StartLagCompensation", VPROF_BUDGETGROUP_OTHER_NETWORKING );
	Q_memset( m_RestoreData, 0, sizeof( m_RestoreData ) );
	Q_memset( m_ChangeData, 0, sizeof( m_ChangeData ) );

	// Iterate all active entities
	const CBitVec< MAX_EDICTS >* pEntityTransmitBits = engine->GetEntityTransmitBitsForClient( player->entindex() - 1 );

	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		CBaseEntity* pEntity = UTIL_EntityByIndex( i );

		if ( !pEntity )
		{
			continue;
		}

		// Don't lag compensate yourself you loser...
		if ( player->entindex() == pEntity->entindex() )
		{
			continue;
		}

		// Custom checks for if things should lag compensate (based on things like what team the entity is on).
		if ( !player->WantsLagCompensationOnEntity( pEntity, cmd, pEntityTransmitBits ) )
		{
			continue;
		}

		// Move other entity back in time
		BacktrackEntity( pEntity, i, cmd );
	}
}

inline void CLagCompensationManager::BacktrackEntity( CBaseEntity* pEntity, int loopindex, CUserCmd* cmd )
{
	VPROF_BUDGET( "BacktrackEntity", "CLagCompensationManager" );

	Vector org;
	Vector minsPreScaled;
	Vector maxsPreScaled;
	QAngle ang;

	LagRecord* prevRecordSim;
	LagRecord* recordSim;
	LagRecord* recordAnim;

	int pl_index = loopindex;

	float flTargetSimTime  = cmd->simulationdata[pl_index].sim_time;
	float flTargetAnimTime = cmd->simulationdata[pl_index].anim_time;

	// Somehow the client didn't care.
	if ( flTargetSimTime == 0 )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "Client has refused to lag compensate this entity, probably already predicted ( %i )\n",
					pEntity->entindex() );
		}

		return;
	}

	// get track history of this entity
	auto track	   = &m_EntityTrack[pl_index];
	bool foundSim  = false;
	bool foundAnim = false;

	for ( int i = 0; i < MAX_TICKS_SAVED; i++ )
	{
		recordSim = track->Get( i );

		if ( !recordSim )
		{
			break;
		}

		if ( flTargetSimTime == recordSim->m_flSimulationTime )
		{
			foundSim = true;
			break;
		}

		if ( recordSim->m_flSimulationTime < flTargetSimTime )
		{
			foundSim	  = true;
			prevRecordSim = track->Get( i - 1 );
			break;
		}
	}

	if ( !foundSim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "No valid simulation in history for BacktrackPlayer client ( %i )\n", pEntity->entindex() );
		}

		return;
	}

	float fracSim = 0.0f;
	if ( prevRecordSim && ( recordSim->m_flSimulationTime < flTargetSimTime )
		 && ( recordSim->m_flSimulationTime < prevRecordSim->m_flSimulationTime ) )
	{
		// we didn't find the exact time but have a valid previous record
		// so interpolate between these two records;

		Assert( prevRecordSim->m_flSimulationTime > recordSim->m_flSimulationTime );
		Assert( flTargetSimTime < prevRecordSim->m_flSimulationTime );

		// calc fraction between both records
		fracSim = float( ( double( flTargetSimTime ) - double( recordSim->m_flSimulationTime ) )
						 / ( double( prevRecordSim->m_flSimulationTime ) - double( recordSim->m_flSimulationTime ) ) );

		Assert( fracSim > 0 && fracSim < 1 ); // should never extrapolate

		ang			  = Lerp( fracSim, recordSim->m_vecAngles, prevRecordSim->m_vecAngles );
		org			  = Lerp( fracSim, recordSim->m_vecOrigin, prevRecordSim->m_vecOrigin );
		minsPreScaled = Lerp( fracSim, recordSim->m_vecMinsPreScaled, prevRecordSim->m_vecMinsPreScaled );
		maxsPreScaled = Lerp( fracSim, recordSim->m_vecMaxsPreScaled, prevRecordSim->m_vecMaxsPreScaled );
	}
	else
	{
		// we found the exact record or no other record to interpolate with
		// just copy these values since they are the best we have
		org			  = recordSim->m_vecOrigin;
		ang			  = recordSim->m_vecAngles;
		minsPreScaled = recordSim->m_vecMinsPreScaled;
		maxsPreScaled = recordSim->m_vecMaxsPreScaled;
	}

	// See if this represents a change for the entity
	int flags		   = 0;
	LagRecord* restore = &m_RestoreData[pl_index];
	LagRecord* change  = &m_ChangeData[pl_index];

	QAngle angdiff = pEntity->GetAbsAngles() - ang;
	Vector orgdiff = pEntity->GetAbsOrigin() - org;

	// Always remember the pristine simulation time in case we need to restore it.
	restore->m_flSimulationTime = pEntity->GetSimulationTime();
	restore->m_flAnimTime		= pEntity->GetAnimTime();

	if ( angdiff.LengthSqr() > 0.0f )
	{
		flags				 |= LC_ANGLES_CHANGED;
		restore->m_vecAngles  = pEntity->GetAbsAngles();
		pEntity->SetAbsAngles( ang );
		change->m_vecAngles = ang;
	}

	// Use absolute equality here
	if ( minsPreScaled != pEntity->CollisionProp()->OBBMinsPreScaled()
		 || maxsPreScaled != pEntity->CollisionProp()->OBBMaxsPreScaled() )
	{
		flags |= LC_SIZE_CHANGED;

		restore->m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
		restore->m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();

		pEntity->SetSize( minsPreScaled, maxsPreScaled );

		change->m_vecMinsPreScaled = minsPreScaled;
		change->m_vecMaxsPreScaled = maxsPreScaled;
	}

	// Note, do origin at end since it causes a relink into the k/d tree
	if ( orgdiff.LengthSqr() > 0.0f )
	{
		flags				 |= LC_ORIGIN_CHANGED;
		restore->m_vecOrigin  = pEntity->GetAbsOrigin();
		pEntity->SetAbsOrigin( org );
		change->m_vecOrigin = org;
	}

	auto pAnim = pEntity->GetBaseAnimating();

	auto Finish = [&]()
	{
		if ( !flags )
		{
			return; // we didn't change anything
		}

		// Set lag compensated entity's times
		pEntity->SetSimulationTime( flTargetSimTime );
		pEntity->SetAnimTime( flTargetAnimTime );

		if ( sv_lagflushbonecache.GetBool() )
		{
			if ( pAnim )
			{
				pAnim->InvalidateBoneCache();
			}
		}

		m_RestoreEntity.Set( pl_index ); // remember that we changed this entity
		m_bNeedToRestore  = true;		 // we changed at least one entity
		restore->m_fFlags = flags;		 // we need to restore these flags
		change->m_fFlags  = flags;		 // we have changed these flags
	};

	// Somehow the client didn't care.
	if ( flTargetAnimTime == 0 )
	{
		if ( sv_unlag_debug.GetBool() && !pAnim )
		{
			DevMsg( "Client has no anim time info ( %i )\n", pEntity->entindex() );
		}

		Finish();
		return;
	}

	if ( pAnim )
	{
		for ( int i = 0; i < MAX_TICKS_SAVED; i++ )
		{
			recordAnim = track->Get( i );

			if ( !recordAnim )
			{
				break;
			}

			if ( recordAnim->m_flAnimTime == flTargetAnimTime )
			{
				foundAnim = true;
				break;
			}
		}
	}

	if ( !foundAnim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "Can't lag compensate, no history for animation fpr client entity ( %i )\n", pEntity->entindex() );
		}

		Finish();
		return;
	}

	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	if ( pAnim && foundAnim )
	{
		// Sorry for the loss of the optimization for the case of people
		// standing still, but you breathe even on the server.
		// This is quicker than actually comparing all bazillion floats.
		flags					  |= LC_ANIMATION_CHANGED;
		restore->m_masterSequence  = pAnim->GetSequence();
		restore->m_masterCycle	   = pAnim->GetCycle();

		pAnim->SetSequence( recordAnim->m_masterSequence );
		pAnim->SetCycle( recordAnim->m_masterCycle );

		// Now do pose parameters
		CStudioHdr* hdr = pAnim->GetModelPtr();

		if ( hdr )
		{
			for ( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
			{
				restore->m_poseParameters[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
				float poseParameter					  = recordAnim->m_poseParameters[paramIndex];

				pAnim->SetPoseParameterRaw( paramIndex, poseParameter );
			}

			flags |= LC_POSE_PARAMS_CHANGED;

			for ( int encIndex = 0; encIndex < hdr->GetNumBoneControllers(); encIndex++ )
			{
				restore->m_encodedControllers[encIndex] = pAnim->GetBoneControllerArray()[encIndex];
				float encodedController					= recordAnim->m_encodedControllers[encIndex];

				pAnim->SetBoneControllerRaw( encIndex, encodedController );
			}

			flags |= LC_ENCD_CONS_CHANGED;
		}
	}

	if ( pAnimOverlay && foundAnim )
	{
		////////////////////////
		// Now do all the layers
		int layerCount = pAnimOverlay->GetNumAnimOverlays();

		for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );
			if ( currentLayer )
			{
				restore->m_layerRecords[layerIndex].m_cycle	   = currentLayer->m_flCycle;
				restore->m_layerRecords[layerIndex].m_order	   = currentLayer->m_nOrder;
				restore->m_layerRecords[layerIndex].m_sequence = currentLayer->m_nSequence;
				restore->m_layerRecords[layerIndex].m_weight   = currentLayer->m_flWeight;
				restore->m_layerRecords[layerIndex].m_flags	   = currentLayer->m_fFlags;

				currentLayer->m_flCycle	  = recordAnim->m_layerRecords[layerIndex].m_cycle;
				currentLayer->m_nOrder	  = recordAnim->m_layerRecords[layerIndex].m_order;
				currentLayer->m_nSequence = recordAnim->m_layerRecords[layerIndex].m_sequence;
				currentLayer->m_flWeight  = recordAnim->m_layerRecords[layerIndex].m_weight;
				currentLayer->m_fFlags	  = recordAnim->m_layerRecords[layerIndex].m_flags;
			}
		}

		flags |= LC_ANIM_OVERS_CHANGED;
	}

	Finish();
}

void CLagCompensationManager::FinishLagCompensation( CBasePlayer* player )
{
	VPROF_BUDGET_FLAGS( "FinishLagCompensation",
						VPROF_BUDGETGROUP_OTHER_NETWORKING,
						BUDGETFLAG_CLIENT | BUDGETFLAG_SERVER );

	if ( !m_bNeedToRestore )
	{
		return; // no entities was changed at all
	}

	// Iterate all active entities
	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		if ( !m_RestoreEntity.Get( i ) )
		{
			// entity wasn't changed by lag compensation
			continue;
		}

		CBaseEntity* pEntity = UTIL_EntityByIndex( i );
		if ( !pEntity )
		{
			continue;
		}

		LagRecord* restore = &m_RestoreData[i];
		LagRecord* change  = &m_ChangeData[i];

		if ( restore->m_fFlags & LC_SIZE_CHANGED )
		{
			pEntity->SetSize( restore->m_vecMinsPreScaled, restore->m_vecMaxsPreScaled );
		}

		if ( restore->m_fFlags & LC_ANGLES_CHANGED )
		{
			pEntity->SetAbsAngles( restore->m_vecAngles );
		}

		if ( restore->m_fFlags & LC_ORIGIN_CHANGED )
		{
			pEntity->SetAbsOrigin( restore->m_vecOrigin );
		}

		auto pAnim		  = dynamic_cast< CBaseAnimating* >( pEntity );
		auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

		if ( pAnim )
		{
			if ( restore->m_fFlags & LC_ANIMATION_CHANGED )
			{
				pAnim->SetSequence( restore->m_masterSequence );
				pAnim->SetCycle( restore->m_masterCycle );
			}

			CStudioHdr* hdr = pAnim->GetModelPtr();

			if ( hdr )
			{
				if ( restore->m_fFlags & LC_POSE_PARAMS_CHANGED )
				{
					for ( int paramIndex = 0; paramIndex < hdr->GetNumPoseParameters(); paramIndex++ )
					{
						pAnim->SetPoseParameterRaw( paramIndex, restore->m_poseParameters[paramIndex] );
					}
				}

				if ( restore->m_fFlags & LC_ENCD_CONS_CHANGED )
				{
					for ( int encIndex = 0; encIndex < hdr->GetNumBoneControllers(); encIndex++ )
					{
						pAnim->SetBoneControllerRaw( encIndex, restore->m_encodedControllers[encIndex] );
					}
				}
			}
		}

		if ( restore->m_fFlags & LC_ANIM_OVERS_CHANGED && pAnimOverlay )
		{
			int layerCount = pAnimOverlay->GetNumAnimOverlays();

			for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
			{
				CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );
				if ( currentLayer )
				{
					currentLayer->m_flCycle	  = restore->m_layerRecords[layerIndex].m_cycle;
					currentLayer->m_nOrder	  = restore->m_layerRecords[layerIndex].m_order;
					currentLayer->m_nSequence = restore->m_layerRecords[layerIndex].m_sequence;
					currentLayer->m_flWeight  = restore->m_layerRecords[layerIndex].m_weight;
					currentLayer->m_fFlags	  = restore->m_layerRecords[layerIndex].m_flags;
				}
			}
		}

		pEntity->SetSimulationTime( restore->m_flSimulationTime );
		pEntity->SetAnimTime( restore->m_flAnimTime );
	}
}
