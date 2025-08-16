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

#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

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
	LayerRecord m_layerRecords[MAX_LAYER_RECORDS];
	int m_masterSequence;
	float m_masterCycle;
	float m_poseParameters[MAXSTUDIOPOSEPARAM];
	float m_encodedControllers[MAXSTUDIOBONECTRLS];
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

	// Sequence and order settings always come from target state
	output.m_nSequence	 = to.m_nSequence;
	output.m_nOrder		 = to.m_nOrder;
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight	 = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_fFlags		 = to.m_fFlags;
	output.m_flLayerAnimtime	= to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;

	// Unified cycle handling for both sequence transitions and same sequences
	float cycleDelta = to.m_flCycle - from.m_flCycle;
	if ( abs( cycleDelta ) > 0.5f )
	{
		// Compensate for animation loop wrapping
		float wrappedCycle = ( cycleDelta > 0 ) ? to.m_flCycle - 1.0f : to.m_flCycle + 1.0f;
		float blended	   = from.m_flCycle + flPercent * ( wrappedCycle - from.m_flCycle );
		output.m_flCycle   = fmodf( blended + 1.0f, 1.0f ); // Normalize [0,1)
	}
	else
	{
		// Normal interpolation
		output.m_flCycle = Lerp( flPercent, from.m_flCycle, to.m_flCycle );
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

	// Sequence and order settings
	output.m_nSequence	 = to.m_nSequence;
	output.m_nOrder		 = to.m_nOrder;
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight	 = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_fFlags		 = to.m_fFlags;
	output.m_flLayerAnimtime	= to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;

	// Sequence transition handling
	if ( from.m_nSequence != to.m_nSequence )
	{
		// Transition handling
		const float baseCycle = to.m_flPrevCycle;
		output.m_flCycle	  = baseCycle + flPercent * ( to.m_flCycle - baseCycle );
	}
	else
	{
		// Original Hermite implementation for same sequences
		output.m_flCycle = LoopingLerp_Hermite( flPercent, prev.m_flCycle, from.m_flCycle, to.m_flCycle );
	}

	return output;
}

inline LayerRecord Lerp_Hermite( float flPercent, LayerRecord& prev, LayerRecord& from, LayerRecord& to )
{
	return LoopingLerp_Hermite( flPercent, prev, from, to );
}

inline void Lerp_Clamp( LayerRecord &val )
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

inline static float LinearInterpOnlyFrac( float targettime, float newer_change_time, float older_change_time )
{
	float frac;

	float dt = newer_change_time - older_change_time;

	if ( dt > 0.0001f )
	{
		frac = ( targettime - older_change_time ) / ( newer_change_time - older_change_time );
		frac = MIN( frac, 1.0f );
		frac = MAX( frac, 0.0f );
	}
	else
	{
		frac = 0;
	}

	return frac;
}

// TODO_ENHANCED: Taken from interpolatedvar.h, we might need hermite interpolation one day ...
template < typename T >
inline static T Interpolate( float frac, T& start, T& end, bool bLooping = false )
{
	Assert( frac >= 0.0f && frac <= 1.0f );

	if ( start == end )
	{
		return start;
	}

	if ( frac == 0.0f )
	{
		return start;
	}

	if ( frac == 1.0f )
	{
		return end;
	}

	T out;

	if ( bLooping )
	{
		out = LoopingLerp( frac, start, end );
	}
	else
	{
		out = Lerp( frac, start, end );
	}

	Lerp_Clamp( out );

	return out;
}

// template < typename T >
// inline void TimeFixup_Hermite( T& fixup,
// 							   T& prev,
// 							   T& start,
// 							   T& end,
// 							   float flStartTime,
// 							   float flPrevTime,
// 							   bool bLooping = false )
// {
// 	float dt2 = flStartTime - flPrevTime;

// 	if ( fabs( dt1 - dt2 ) > 0.0001f && dt2 > 0.0001f )
// 	{
// 		float frac = dt1 / dt2;

// 		if ( bLooping )
// 		{
// 			fixup = LoopingLerp( 1 - frac, prev, start );
// 		}
// 		else
// 		{
// 			fixup = Lerp( 1 - frac, prev, start );
// 		}

// 		prev = fixup;
// 	}
// }

// template < typename T >
// inline static T Interpolate_Hermite( float flStartTime,
// 									 float flPrevTime,
// 									 float flEndTime,
// 									 T& prev,
// 									 T& start,
// 									 T& end,
// 									 bool bLooping = false )
// {
// 	T fixup;
// 	TimeFixup_Hermite( fixup, prev, start, end );

// 	Lerp_Clamp( out );

// 	return out;
// }

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
	CBasePlayer* m_LagPlayer;
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

	auto entities = g_pFastEntityLookUp->entities;

	// Iterate all active entities
	for ( int i = 0; i < MAX_EDICTS; i++ )
	{
		CBaseEntity* pEntity = entities[i];

		if ( !pEntity )
		{
			continue;
		}

		// remove all records before that time:
		auto track = &m_EntityTrack[i];

		// add new record to entity track
		record.m_flSimulationTime = pEntity->GetSimulationTime();
		record.m_flAnimTime		  = pEntity->GetAnimTime();
		record.m_vecAngles		  = pEntity->GetLocalAngles();
		record.m_vecOrigin		  = pEntity->GetLocalOrigin();
		record.m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
		record.m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();

		auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

		if ( pAnim )
		{
			record.m_masterSequence = pAnim->GetSequence();
			record.m_masterCycle	= pAnim->GetCycle();

			CStudioHdr* pStudioHDr = pAnim->GetModelPtr();

			if ( pStudioHDr )
			{
				for ( int paramIndex = 0; paramIndex < pStudioHDr->GetNumPoseParameters(); paramIndex++ )
				{
					record.m_poseParameters[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
				}

				for ( int boneIndex = 0; boneIndex < pStudioHDr->GetNumBoneControllers(); boneIndex++ )
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
					record.m_layerRecords[layerIndex].m_flCycle			   = currentLayer->m_flCycle;
					record.m_layerRecords[layerIndex].m_nOrder			   = currentLayer->m_nOrder;
					record.m_layerRecords[layerIndex].m_nSequence		   = currentLayer->m_nSequence;
					record.m_layerRecords[layerIndex].m_flWeight		   = currentLayer->m_flWeight;
					record.m_layerRecords[layerIndex].m_fFlags			   = currentLayer->m_fFlags;
					record.m_layerRecords[layerIndex].m_flPrevCycle		   = currentLayer->m_flPrevCycle;
					record.m_layerRecords[layerIndex].m_flLayerAnimtime	   = currentLayer->m_flLayerAnimtime;
					record.m_layerRecords[layerIndex].m_flLayerFadeOuttime = currentLayer->m_flLayerFadeOuttime;
				}
			}
		}

#ifdef CSTRIKE_DLL
		auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

		if ( csPlayer )
		{
			record.m_angRenderAngles = csPlayer->GetRenderAngles();
		}
#endif

		track->Push( record );
	}
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
			auto angles = restore->m_vecAngles;
			auto mins	= restore->m_vecMinsPreScaled;
			auto maxs	= restore->m_vecMaxsPreScaled;

			debugoverlay->AddBoxOverlay( origin, mins, maxs, angles, 0, 0, 255, 128, gpGlobals->interval_per_tick * 2.0f );

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

	LagRecord* nextRecordSim = NULL;
	LagRecord* recordSim = NULL;
	LagRecord* prevRecordSim = NULL;
	LagRecord* nextRecordAnim = NULL;
	LagRecord* recordAnim = NULL;
	LagRecord* prevRecordAnim = NULL;

	float flTargetSimTime  = cmd->simulationdata[loopindex].sim_time;
	float flTargetAnimTime = cmd->simulationdata[loopindex].anim_time;
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
			nextRecordSim = track->Get( i - 1 );
			prevRecordSim = track->Get( i + 1 );
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
		restore->m_masterSequence = pAnim->GetSequence();
		restore->m_masterCycle	  = pAnim->GetCycle();

		CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				restore->m_poseParameters[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				restore->m_encodedControllers[encIndex] = pAnim->GetBoneControllerArray()[encIndex];
			}
		}
	}

	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	if ( pAnimOverlay )
	{
		for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
		{
			CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

			if ( currentLayer )
			{
				restore->m_layerRecords[layerIndex].m_flCycle			 = currentLayer->m_flCycle;
				restore->m_layerRecords[layerIndex].m_nOrder			 = currentLayer->m_nOrder;
				restore->m_layerRecords[layerIndex].m_nSequence			 = currentLayer->m_nSequence;
				restore->m_layerRecords[layerIndex].m_flWeight			 = currentLayer->m_flWeight;
				restore->m_layerRecords[layerIndex].m_fFlags			 = currentLayer->m_fFlags;
				restore->m_layerRecords[layerIndex].m_flPrevCycle		 = currentLayer->m_flPrevCycle;
				restore->m_layerRecords[layerIndex].m_flLayerAnimtime	 = currentLayer->m_flLayerAnimtime;
				restore->m_layerRecords[layerIndex].m_flLayerFadeOuttime = currentLayer->m_flLayerFadeOuttime;
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
	if ( nextRecordSim && ( nextRecordSim->m_flSimulationTime > recordSim->m_flSimulationTime ) )
	{
		auto fracSim = LinearInterpOnlyFrac( flTargetSimTime,
											nextRecordSim->m_flSimulationTime,
											recordSim->m_flSimulationTime );

		ang			  = Interpolate( fracSim, recordSim->m_vecAngles, nextRecordSim->m_vecAngles );
		org			  = Interpolate( fracSim, recordSim->m_vecOrigin, nextRecordSim->m_vecOrigin );
		minsPreScaled = Interpolate( fracSim, recordSim->m_vecMinsPreScaled, nextRecordSim->m_vecMinsPreScaled );
		maxsPreScaled = Interpolate( fracSim, recordSim->m_vecMaxsPreScaled, nextRecordSim->m_vecMaxsPreScaled );

#ifdef CSTRIKE_DLL
		if ( csPlayer )
		{
			angRenderAng = Interpolate( fracSim, recordSim->m_angRenderAngles, nextRecordSim->m_angRenderAngles );
		}
#endif

		// printf( "%i target: %f frac: %f new: %f old: %f\n",
		// 		pEntity->entindex(),
		// 		flTargetSimTime,
		// 		fracSim,
		// 		recordSim->m_flSimulationTime,
		// 		nextRecordSim->m_flSimulationTime );
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

	for ( int i = 0; i < MAX_TICKS_SAVED; i++ )
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

		if ( recordAnim->m_flAnimTime < flTargetAnimTime )
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
	if ( nextRecordAnim && ( nextRecordAnim->m_flAnimTime > recordAnim->m_flAnimTime ) )
	{
		auto fracAnim = LinearInterpOnlyFrac( flTargetAnimTime, nextRecordAnim->m_flAnimTime, recordAnim->m_flAnimTime );

		auto newSequence = Interpolate( fracAnim, recordAnim->m_masterSequence, nextRecordAnim->m_masterSequence );
		auto newCycle	 = Interpolate( fracAnim,
										recordAnim->m_masterCycle,
										nextRecordAnim->m_masterCycle,
										pAnim->IsSequenceLooping( pStudioHdr, newSequence ) );

		pAnim->SetSequence( newSequence );
		pAnim->SetCycle( newCycle );

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				auto&& Pose = pStudioHdr->pPoseParameter( paramIndex );

				auto poseParameter = Interpolate( fracAnim,
												  recordAnim->m_poseParameters[paramIndex],
												  nextRecordAnim->m_poseParameters[paramIndex],
												  Pose.loop != 0.0f );

				pAnim->SetPoseParameterRaw( paramIndex, poseParameter );
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				auto loop = ( pStudioHdr->pBonecontroller( encIndex )->type & ( STUDIO_XR | STUDIO_YR | STUDIO_ZR ) )
							!= 0;

				auto encodedController = Interpolate( fracAnim,
													  recordAnim->m_encodedControllers[encIndex],
													  nextRecordAnim->m_encodedControllers[encIndex],
													  loop );

				pAnim->SetBoneControllerRaw( encIndex, encodedController );
			}
		}

		if ( pAnimOverlay )
		{
			for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
			{
				CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( currentLayer )
				{
					auto newAnimLayer = Interpolate(
					  fracAnim,
					  recordAnim->m_layerRecords[layerIndex],
					  nextRecordAnim->m_layerRecords[layerIndex],
					  pAnimOverlay->IsSequenceLooping( nextRecordAnim->m_layerRecords[layerIndex].m_nSequence ) );

					currentLayer->m_flCycle			   = newAnimLayer.m_flCycle;
					currentLayer->m_nOrder			   = newAnimLayer.m_nOrder;
					currentLayer->m_nSequence		   = newAnimLayer.m_nSequence;
					currentLayer->m_flWeight		   = newAnimLayer.m_flWeight;
					currentLayer->m_fFlags			   = newAnimLayer.m_fFlags;
					currentLayer->m_flPrevCycle		   = newAnimLayer.m_flPrevCycle;
					currentLayer->m_flLayerAnimtime	   = newAnimLayer.m_flLayerAnimtime;
					currentLayer->m_flLayerFadeOuttime = newAnimLayer.m_flLayerFadeOuttime;
				}
			}
		}
	}
	else
	{
		pAnim->SetSequence( recordAnim->m_masterSequence );
		pAnim->SetCycle( recordAnim->m_masterCycle );

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				float poseParameter = recordAnim->m_poseParameters[paramIndex];

				pAnim->SetPoseParameterRaw( paramIndex, poseParameter );
			}

			for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
			{
				float encodedController = recordAnim->m_encodedControllers[encIndex];

				pAnim->SetBoneControllerRaw( encIndex, encodedController );
			}
		}

		if ( pAnimOverlay )
		{
			for ( int layerIndex = 0; layerIndex < pAnimOverlay->GetNumAnimOverlays(); ++layerIndex )
			{
				CAnimationLayer* currentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( currentLayer )
				{
					currentLayer->m_flCycle			   = recordAnim->m_layerRecords[layerIndex].m_flCycle;
					currentLayer->m_nOrder			   = recordAnim->m_layerRecords[layerIndex].m_nOrder;
					currentLayer->m_nSequence		   = recordAnim->m_layerRecords[layerIndex].m_nSequence;
					currentLayer->m_flWeight		   = recordAnim->m_layerRecords[layerIndex].m_flWeight;
					currentLayer->m_fFlags			   = recordAnim->m_layerRecords[layerIndex].m_fFlags;
					currentLayer->m_flPrevCycle		   = recordAnim->m_layerRecords[layerIndex].m_flPrevCycle;
					currentLayer->m_flLayerAnimtime	   = recordAnim->m_layerRecords[layerIndex].m_flLayerAnimtime;
					currentLayer->m_flLayerFadeOuttime = recordAnim->m_layerRecords[layerIndex].m_flLayerFadeOuttime;
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

		auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

		if ( pAnim )
		{
			pAnim->SetSequence( restore->m_masterSequence );
			pAnim->SetCycle( restore->m_masterCycle );

			CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

			if ( pStudioHdr )
			{
				for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
				{
					pAnim->SetPoseParameterRaw( paramIndex, restore->m_poseParameters[paramIndex] );
				}

				for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
				{
					pAnim->SetBoneControllerRaw( encIndex, restore->m_encodedControllers[encIndex] );
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
					currentLayer->m_flCycle			   = restore->m_layerRecords[layerIndex].m_flCycle;
					currentLayer->m_nOrder			   = restore->m_layerRecords[layerIndex].m_nOrder;
					currentLayer->m_nSequence		   = restore->m_layerRecords[layerIndex].m_nSequence;
					currentLayer->m_flWeight		   = restore->m_layerRecords[layerIndex].m_flWeight;
					currentLayer->m_fFlags			   = restore->m_layerRecords[layerIndex].m_fFlags;
					currentLayer->m_flPrevCycle		   = restore->m_layerRecords[layerIndex].m_flPrevCycle;
					currentLayer->m_flLayerAnimtime	   = restore->m_layerRecords[layerIndex].m_flLayerAnimtime;
					currentLayer->m_flLayerFadeOuttime = restore->m_layerRecords[layerIndex].m_flLayerFadeOuttime;
				}
			}
		}

#ifdef CSTRIKE_DLL
		auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

		if ( csPlayer )
		{
			csPlayer->m_angRenderAngles = restore->m_angRenderAngles;
		}
#endif

		pEntity->SetSimulationTime( restore->m_flSimulationTime );
		pEntity->SetAnimTime( restore->m_flAnimTime );
	}
}
