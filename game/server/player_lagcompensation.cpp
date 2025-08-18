//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "entitylist.h"
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
#ifdef CSTRIKE_DLL
#include "cs_player.h"
#endif

#include "lerp_functions.h"
#include "interpolatedvar.h"

#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sv_unlag( "sv_unlag", "1", 0, "Enables entity lag compensation" );
// Enable by default to avoid some bugs.
ConVar sv_lagflushbonecache( "sv_lagflushbonecache", "1", 0, "Flushes entity bone cache on lag compensation" );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

struct LayerRecord
{
	int m_nSequence;
	float m_flCycle;
	float m_flPrevCycle;
	float m_flWeight;
	int m_nOrder;
	int m_fFlags;
	float m_flLayerAnimtime;
	float m_flLayerFadeOuttime;
};

struct LagRecord
{
  public:
	// Player position, orientation and bbox
	Vector m_vecOrigin;
	QAngle m_vecAngles;
	Vector m_vecMinsPreScaled;
	Vector m_vecMaxsPreScaled;

	float m_flSimulationTime;
	float m_flAnimTime;

	// Player animation details, so we can get the legs in the right spot.
	LayerRecord m_LayerRecords[MAX_LAYER_RECORDS];
	int m_nSequence;
	float m_flCycle;
	float m_flPoseParameter[MAXSTUDIOPOSEPARAM];
	float m_flEncodedController[MAXSTUDIOBONECTRLS];
#ifdef CSTRIKE_DLL
	QAngle m_angRenderAngles;
#endif
};

//
// Try to take the entity from his current origin to vWantedPos.
// If it can't get there, leave the entity where he is.
//

ConVar sv_unlag_debug( "sv_unlag_debug", "0" );
ConVar sv_unlag_debug_entity( "sv_unlag_debug_entity", "-1" );

inline LayerRecord LoopingLerp( float flPercent, LayerRecord& from, LayerRecord& to )
{
	LayerRecord output;

	output.m_nSequence			= to.m_nSequence;
	output.m_nOrder				= to.m_nOrder;
	output.m_fFlags				= to.m_fFlags;
	output.m_flLayerAnimtime	= to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
	output.m_flCycle			= LoopingLerp( flPercent, from.m_flCycle, to.m_flCycle );

	// If sequences changes, we need to set them directly in order to avoid artifacts at the expense of not being
	// exactly all the time smooth, and it is okay.
	if ( from.m_nSequence == to.m_nSequence )
	{
		output.m_flWeight	 = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
		output.m_flPrevCycle = from.m_flPrevCycle;
	}
	else
	{
		output.m_flWeight	 = to.m_flWeight;
		output.m_flPrevCycle = to.m_flPrevCycle;
	}

	return output;
}

inline LayerRecord Lerp( float flPercent, LayerRecord& from, LayerRecord& to )
{
	return LoopingLerp( flPercent, from, to );
}

inline LayerRecord LoopingLerp_Hermite( float flPercent, LayerRecord& prev, LayerRecord& from, LayerRecord& to )
{
	LayerRecord output;

	output.m_nSequence			= to.m_nSequence;
	output.m_nOrder				= to.m_nOrder;
	output.m_fFlags				= to.m_fFlags;
	output.m_flLayerAnimtime	= to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
	output.m_flCycle			= LoopingLerp_Hermite( flPercent, prev.m_flCycle, from.m_flCycle, to.m_flCycle );

	// If sequences changes, we need to set them directly in order to avoid artifacts at the expense of not being
	// exactly all the time smooth, and it is okay.
	if ( from.m_nSequence == to.m_nSequence )
	{
		output.m_flWeight	 = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
		output.m_flPrevCycle = from.m_flPrevCycle;
	}
	else
	{
		output.m_flWeight	 = to.m_flWeight;
		output.m_flPrevCycle = to.m_flPrevCycle;
	}

	return output;
}

inline void Lerp_Clamp( LayerRecord& val )
{
	Lerp_Clamp( val.m_nSequence );
	Lerp_Clamp( val.m_flCycle );
	Lerp_Clamp( val.m_flPrevCycle );
	Lerp_Clamp( val.m_flWeight );
	Lerp_Clamp( val.m_nOrder );
	Lerp_Clamp( val.m_flLayerAnimtime );
	Lerp_Clamp( val.m_flLayerFadeOuttime );
}

inline bool operator==( LayerRecord& lr1, LayerRecord& lr2 )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CLagCompensationManager : public CAutoGameSystemPerFrame,
								public ILagCompensationManager
{
  public:
	CLagCompensationManager( const char* name )
	{
		ClearHistory();
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
	// Some entities gets simulated at different times. For example players can run multiples commands per second.
	virtual void TrackEntity( CBaseEntity* pEntity ) override;
	inline void BacktrackEntity( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd );

	void ClearHistory()
	{
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			m_EntityTrack[i].Clear();
			m_flOldSimulationTime[i] = 0;
		}
	}

	void FrameUpdatePostEntityThink() override
	{
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			auto pEntity = g_pFastEntityLookUp->entities[i];

			if ( !pEntity )
			{
				continue;
			}

			// Don't track if simulation time haven't changed.
			if ( pEntity->m_flSimulationTime == m_flOldSimulationTime[i] )
			{
				continue;
			}

			m_flOldSimulationTime[i] = pEntity->m_flSimulationTime;

			TrackEntity( pEntity );
		}
	}

	// keep a list of lag records for each entities
	CUtlCircularBuffer< LagRecord, MAX_INTERPOLATION_TICK_HISTORY > m_EntityTrack[MAX_EDICTS];

	// Scratchpad for determining what needs to be restored
	CBitVec< MAX_EDICTS > m_RestoreEntity;
	bool m_bNeedToRestore;

	LagRecord m_RestoreData[MAX_EDICTS]; // entities data before we moved him back
	float m_flOldSimulationTime[MAX_EDICTS];
	CBasePlayer* m_LagPlayer;
};

static CLagCompensationManager g_LagCompensationManager( "CLagCompensationManager" );
ILagCompensationManager* lagcompensation = &g_LagCompensationManager;

//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all entities have had a chance to think
//-----------------------------------------------------------------------------
void CLagCompensationManager::TrackEntity( CBaseEntity* pEntity )
{
	LagRecord record;

	if ( !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}

	VPROF_BUDGET( "TrackEntities", "CLagCompensationManager" );

	// remove all records before that time:
	auto track = &m_EntityTrack[pEntity->entindex()];

	// add new record to entity track
	record.m_flSimulationTime = pEntity->GetSimulationTime();
	record.m_flAnimTime		  = pEntity->GetAnimTime();
	record.m_vecAngles		  = pEntity->GetLocalAngles();
	record.m_vecOrigin		  = pEntity->GetLocalOrigin();
	record.m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
	record.m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();

#ifdef CSTRIKE_DLL
	auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

	if ( csPlayer )
	{
		record.m_angRenderAngles = csPlayer->GetRenderAngles();
	}
#endif

	auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

	if ( pAnim )
	{
		record.m_nSequence = pAnim->GetSequence();
		record.m_flCycle   = pAnim->GetCycle();

		CStudioHdr* pStudioHDr = pAnim->GetModelPtr();

		if ( pStudioHDr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHDr->GetNumPoseParameters(); paramIndex++ )
			{
				record.m_flPoseParameter[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
			}

			for ( int boneIndex = 0; boneIndex < pStudioHDr->GetNumBoneControllers(); boneIndex++ )
			{
				record.m_flEncodedController[boneIndex] = pAnim->GetBoneControllerArray()[boneIndex];
			}
		}
	}

	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	if ( pAnimOverlay )
	{
		int layerCount = pAnimOverlay->GetNumAnimOverlays();

		for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

			if ( pCurrentLayer )
			{
				record.m_LayerRecords[layerIndex].m_flCycle			   = pCurrentLayer->m_flCycle;
				record.m_LayerRecords[layerIndex].m_nOrder			   = pCurrentLayer->m_nOrder;
				record.m_LayerRecords[layerIndex].m_nSequence		   = pCurrentLayer->m_nSequence;
				record.m_LayerRecords[layerIndex].m_flWeight		   = pCurrentLayer->m_flWeight;
				record.m_LayerRecords[layerIndex].m_fFlags			   = pCurrentLayer->m_fFlags;
				record.m_LayerRecords[layerIndex].m_flPrevCycle		   = pCurrentLayer->m_flPrevCycle;
				record.m_LayerRecords[layerIndex].m_flLayerAnimtime	   = pCurrentLayer->m_flLayerAnimtime;
				record.m_LayerRecords[layerIndex].m_flLayerFadeOuttime = pCurrentLayer->m_flLayerFadeOuttime;
			}
		}
	}

	track->Push( record );
}

// Called during player movement to set up/restore after lag compensation
void CLagCompensationManager::StartLagCompensation( CBasePlayer* player, CUserCmd* cmd )
{
	m_LagPlayer = player;

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

	// Iterate all active entities
	const CBitVec< MAX_EDICTS >* pEntityTransmitBits = engine->GetEntityTransmitBitsForClient( player->entindex() - 1 );

	auto entities = g_pFastEntityLookUp->entities;

	// Iterate all active entities
	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		CBaseEntity* pEntity = entities[i];

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

		// TODO_ENHANCED:
		// Physics on entities that collides player like moving platforms, funcs and moving triggers needs to be redone.
		// Technically, Physics_RunThinkFunctions needs to be removed or drastically changed in order to ignore players
		// collisions and make player collisions only happen in player command functions. For now we ignore other
		// entities.
		// If you'd like to lag compensate entities, I'd let you give a look on g_pPushedEntities.
		if ( pEntity->IsPlayer() )
		{
			// Move other entity back in time
			BacktrackEntity( pEntity, i, cmd );
		}

		if ( sv_unlag_debug_entity.GetInt() == pEntity->entindex() && player->entindex() == 1 )
		{
			LagRecord* restore = &m_RestoreData[i];

			auto origin = restore->m_vecOrigin;
			auto angle	= restore->m_vecAngles;
			auto mins	= restore->m_vecMinsPreScaled;
			auto maxs	= restore->m_vecMaxsPreScaled;

			debugoverlay->AddBoxOverlay( origin, mins, maxs, angle, 0, 0, 255, 128, gpGlobals->interval_per_tick );

			debugoverlay->AddBoxOverlay( pEntity->GetLocalOrigin(),
										 pEntity->CollisionProp()->OBBMinsPreScaled(),
										 pEntity->CollisionProp()->OBBMaxsPreScaled(),
										 pEntity->GetLocalAngles(),
										 0,
										 255,
										 0,
										 128,
										 gpGlobals->interval_per_tick * 2.0f );
		}
	}
}

inline void CLagCompensationManager::BacktrackEntity( CBaseEntity* pEntity, int loopindex, CUserCmd* cmd )
{
	VPROF_BUDGET( "BacktrackEntity", "CLagCompensationManager" );

	// TODO_ENHANCED: to limit cheaters backtracking, we could their measure latency and approximatively check if it's
	// reasonable enough.

	LagRecord* nextRecordSim  = NULL;
	LagRecord* recordSim	  = NULL;
	LagRecord* nextRecordAnim = NULL;
	LagRecord* recordAnim	  = NULL;

	float flTargetSimTime			 = cmd->simulationdata[loopindex].sim_time;
	float flTargetAnimTime			 = cmd->simulationdata[loopindex].anim_time;
	auto bUseLinearInterpolationOnly = m_LagPlayer->m_bUseLinearInterpolationOnly;

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
	auto track	  = &m_EntityTrack[loopindex];
	bool foundSim = false;

	for ( int i = 0; i < MAX_INTERPOLATION_TICK_HISTORY; i++ )
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

		if ( flTargetSimTime > recordSim->m_flSimulationTime )
		{
			foundSim	  = true;
			nextRecordSim = track->Get( i - 1 );
			break;
		}
	}

	if ( !foundSim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "No valid simulation in history for BacktrackEntity client ( %i )\n", pEntity->entindex() );
		}

		return;
	}

	// First save up entity variables.
	LagRecord* restore = &m_RestoreData[loopindex];

	restore->m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
	restore->m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();
	restore->m_vecAngles		= pEntity->GetLocalAngles();
	restore->m_vecOrigin		= pEntity->GetLocalOrigin();

	auto pAnim = pEntity->GetBaseAnimating();

	if ( pAnim )
	{
		restore->m_nSequence = pAnim->GetSequence();
		restore->m_flCycle	 = pAnim->GetCycle();

		CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				restore->m_flPoseParameter[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				restore->m_flEncodedController[encIndex] = pAnim->GetBoneControllerArray()[encIndex];
			}
		}
	}

	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	if ( pAnimOverlay )
	{
		for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
		{
			CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

			if ( pCurrentLayer )
			{
				restore->m_LayerRecords[layerIndex].m_flCycle			 = pCurrentLayer->m_flCycle;
				restore->m_LayerRecords[layerIndex].m_nOrder			 = pCurrentLayer->m_nOrder;
				restore->m_LayerRecords[layerIndex].m_nSequence			 = pCurrentLayer->m_nSequence;
				restore->m_LayerRecords[layerIndex].m_flWeight			 = pCurrentLayer->m_flWeight;
				restore->m_LayerRecords[layerIndex].m_fFlags			 = pCurrentLayer->m_fFlags;
				restore->m_LayerRecords[layerIndex].m_flPrevCycle		 = pCurrentLayer->m_flPrevCycle;
				restore->m_LayerRecords[layerIndex].m_flLayerAnimtime	 = pCurrentLayer->m_flLayerAnimtime;
				restore->m_LayerRecords[layerIndex].m_flLayerFadeOuttime = pCurrentLayer->m_flLayerFadeOuttime;
			}
		}
	}

#ifdef CSTRIKE_DLL
	auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

	if ( csPlayer )
	{
		restore->m_angRenderAngles = csPlayer->GetRenderAngles();
	}
#endif

	// Always remember the pristine simulation time in case we need to restore it.
	restore->m_flSimulationTime = pEntity->GetSimulationTime();
	restore->m_flAnimTime		= pEntity->GetAnimTime();

	Vector org;
	Vector minsPreScaled;
	Vector maxsPreScaled;
	QAngle ang;
#ifdef CSTRIKE_DLL
	QAngle angRenderAng;
#endif
	// Now interpolate entity.
	if ( nextRecordSim )
	{
		auto frac = cmd->interpolated_amount_frac;

		ang			  = Interpolate_Linear( frac, recordSim->m_vecAngles, nextRecordSim->m_vecAngles );
		org			  = Interpolate_Linear( frac, recordSim->m_vecOrigin, nextRecordSim->m_vecOrigin );
		minsPreScaled = Interpolate_Linear( frac, recordSim->m_vecMinsPreScaled, nextRecordSim->m_vecMinsPreScaled );
		maxsPreScaled = Interpolate_Linear( frac, recordSim->m_vecMaxsPreScaled, nextRecordSim->m_vecMaxsPreScaled );

#ifdef CSTRIKE_DLL
		if ( csPlayer )
		{
			angRenderAng = Interpolate_Linear( frac, recordSim->m_angRenderAngles, nextRecordSim->m_angRenderAngles );
		}
#endif

		// printf( "%i target: %f frac: %f new: %f old: %f\n",
		// 		pEntity->entindex(),
		// 		flTargetSimTime,
		// 		fracSim,
		// 		recordSim->m_flSimulationTime,
		// 		prevRecordSim->m_flSimulationTime );
	}
	else
	{
		// we found the exact record or no other record to interpolate with
		// just copy these values since they are the best we have
		org			  = recordSim->m_vecOrigin;
		ang			  = recordSim->m_vecAngles;
		minsPreScaled = recordSim->m_vecMinsPreScaled;
		maxsPreScaled = recordSim->m_vecMaxsPreScaled;

#ifdef CSTRIKE_DLL
		if ( csPlayer )
		{
			angRenderAng = recordSim->m_angRenderAngles;
		}
#endif
	}

	pEntity->SetSize( minsPreScaled, maxsPreScaled );
	pEntity->SetLocalAngles( ang );
	pEntity->SetLocalOrigin( org );

#ifdef CSTRIKE_DLL
	if ( csPlayer )
	{
		csPlayer->m_angRenderAngles = angRenderAng;
	}
#endif

	auto Finish = [&]()
	{
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

		m_RestoreEntity.Set( loopindex ); // remember that we changed this entity
		m_bNeedToRestore = true;		  // we changed at least one entity
	};

	// Somehow the client didn't care or there's nothing to do
	if ( flTargetAnimTime == 0 || !pAnim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "Client has no anim time info ( %i )\n", pEntity->entindex() );
		}

		Finish();
		return;
	}

	bool foundAnim = false;

	for ( int i = 0; i < MAX_INTERPOLATION_TICK_HISTORY; i++ )
	{
		recordAnim = track->Get( i );

		if ( !recordAnim )
		{
			break;
		}

		if ( flTargetAnimTime == recordAnim->m_flAnimTime )
		{
			foundAnim = true;
			break;
		}

		if ( flTargetAnimTime > recordAnim->m_flAnimTime )
		{
			foundAnim	   = true;
			nextRecordAnim = track->Get( i - 1 );
			break;
		}
	}

	if ( !foundAnim )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			DevMsg( "Can't lag compensate, no history for animation for client entity ( %i )\n", pEntity->entindex() );
		}

		Finish();
		return;
	}

	CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

	// Now interpolate entity animation.
	if ( nextRecordAnim )
	{
		auto frac = cmd->interpolated_amount_frac;

		auto newSequence = Interpolate_Linear( frac, recordAnim->m_nSequence, nextRecordAnim->m_nSequence );

		auto newCycle = Interpolate_Linear( frac,
											recordAnim->m_flCycle,
											nextRecordAnim->m_flCycle,
											pAnim->IsSequenceLooping( pStudioHdr, recordAnim->m_nSequence ) );

		pAnim->SetSequence( newSequence );
		pAnim->SetCycle( newCycle );

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				auto&& Pose = pStudioHdr->pPoseParameter( paramIndex );

				auto poseParameter = Interpolate_Linear( frac,
														 recordAnim->m_flPoseParameter[paramIndex],
														 nextRecordAnim->m_flPoseParameter[paramIndex],
														 Pose.loop != 0.0f );

				pAnim->SetPoseParameterRaw( paramIndex, poseParameter );
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				auto loop = ( pStudioHdr->pBonecontroller( encIndex )->type & ( STUDIO_XR | STUDIO_YR | STUDIO_ZR ) )
							!= 0;

				auto encodedController = Interpolate_Linear( frac,
															 recordAnim->m_flEncodedController[encIndex],
															 nextRecordAnim->m_flEncodedController[encIndex],
															 loop );

				pAnim->SetBoneControllerRaw( encIndex, encodedController );
			}
		}

		if ( pAnimOverlay )
		{
			for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
			{
				CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( pCurrentLayer )
				{
					auto newAnimLayer = Interpolate_Linear( frac,
															recordAnim->m_LayerRecords[layerIndex],
															nextRecordAnim->m_LayerRecords[layerIndex] );

					pCurrentLayer->m_flCycle			= newAnimLayer.m_flCycle;
					pCurrentLayer->m_nOrder				= newAnimLayer.m_nOrder;
					pCurrentLayer->m_nSequence			= newAnimLayer.m_nSequence;
					pCurrentLayer->m_flWeight			= newAnimLayer.m_flWeight;
					pCurrentLayer->m_fFlags				= newAnimLayer.m_fFlags;
					pCurrentLayer->m_flPrevCycle		= newAnimLayer.m_flPrevCycle;
					pCurrentLayer->m_flLayerAnimtime	= newAnimLayer.m_flLayerAnimtime;
					pCurrentLayer->m_flLayerFadeOuttime = newAnimLayer.m_flLayerFadeOuttime;
				}
			}
		}
	}
	else
	{
		pAnim->SetSequence( recordAnim->m_nSequence );
		pAnim->SetCycle( recordAnim->m_flCycle );

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				float poseParameter = recordAnim->m_flPoseParameter[paramIndex];

				pAnim->SetPoseParameterRaw( paramIndex, poseParameter );
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				float encodedController = recordAnim->m_flEncodedController[encIndex];

				pAnim->SetBoneControllerRaw( encIndex, encodedController );
			}
		}

		if ( pAnimOverlay )
		{
			for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
			{
				CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( pCurrentLayer )
				{
					pCurrentLayer->m_flCycle			= recordAnim->m_LayerRecords[layerIndex].m_flCycle;
					pCurrentLayer->m_nOrder				= recordAnim->m_LayerRecords[layerIndex].m_nOrder;
					pCurrentLayer->m_nSequence			= recordAnim->m_LayerRecords[layerIndex].m_nSequence;
					pCurrentLayer->m_flWeight			= recordAnim->m_LayerRecords[layerIndex].m_flWeight;
					pCurrentLayer->m_fFlags				= recordAnim->m_LayerRecords[layerIndex].m_fFlags;
					pCurrentLayer->m_flPrevCycle		= recordAnim->m_LayerRecords[layerIndex].m_flPrevCycle;
					pCurrentLayer->m_flLayerAnimtime	= recordAnim->m_LayerRecords[layerIndex].m_flLayerAnimtime;
					pCurrentLayer->m_flLayerFadeOuttime = recordAnim->m_LayerRecords[layerIndex].m_flLayerFadeOuttime;
				}
			}
		}
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

	auto entities = g_pFastEntityLookUp->entities;

	// Iterate all active entities
	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		if ( !m_RestoreEntity.Get( i ) )
		{
			// entity wasn't changed by lag compensation
			continue;
		}

		CBaseEntity* pEntity = entities[i];

		if ( !pEntity )
		{
			continue;
		}

		LagRecord* restore = &m_RestoreData[i];

		pEntity->SetSize( restore->m_vecMinsPreScaled, restore->m_vecMaxsPreScaled );
		pEntity->SetLocalAngles( restore->m_vecAngles );
		pEntity->SetLocalOrigin( restore->m_vecOrigin );

#ifdef CSTRIKE_DLL
		auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

		if ( csPlayer )
		{
			csPlayer->m_angRenderAngles = restore->m_angRenderAngles;
		}
#endif

		auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

		if ( pAnim )
		{
			pAnim->SetSequence( restore->m_nSequence );
			pAnim->SetCycle( restore->m_flCycle );

			CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

			if ( pStudioHdr )
			{
				for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
				{
					pAnim->SetPoseParameterRaw( paramIndex, restore->m_flPoseParameter[paramIndex] );
				}

				for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
				{
					pAnim->SetBoneControllerRaw( encIndex, restore->m_flEncodedController[encIndex] );
				}
			}
		}

		auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

		if ( pAnimOverlay )
		{
			int layerCount = pAnimOverlay->GetNumAnimOverlays();

			for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
			{
				CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( pCurrentLayer )
				{
					pCurrentLayer->m_flCycle			= restore->m_LayerRecords[layerIndex].m_flCycle;
					pCurrentLayer->m_nOrder				= restore->m_LayerRecords[layerIndex].m_nOrder;
					pCurrentLayer->m_nSequence			= restore->m_LayerRecords[layerIndex].m_nSequence;
					pCurrentLayer->m_flWeight			= restore->m_LayerRecords[layerIndex].m_flWeight;
					pCurrentLayer->m_fFlags				= restore->m_LayerRecords[layerIndex].m_fFlags;
					pCurrentLayer->m_flPrevCycle		= restore->m_LayerRecords[layerIndex].m_flPrevCycle;
					pCurrentLayer->m_flLayerAnimtime	= restore->m_LayerRecords[layerIndex].m_flLayerAnimtime;
					pCurrentLayer->m_flLayerFadeOuttime = restore->m_LayerRecords[layerIndex].m_flLayerFadeOuttime;
				}
			}
		}

		pEntity->SetSimulationTime( restore->m_flSimulationTime );
		pEntity->SetAnimTime( restore->m_flAnimTime );
	}
}
