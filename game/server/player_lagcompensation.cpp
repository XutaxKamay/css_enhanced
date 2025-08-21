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
constexpr auto MAX_UNLAG_TICKS = 128; // 1 second almost

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

struct BaseLagTrack
{
	inline void AddVar( IInterpolatedVar* var )
	{
		m_InterpolatedVarList.AddVar( var );
	}

	inline void RemoveVar( IInterpolatedVar* var )
	{
		m_InterpolatedVarList.RemoveVar( var );
	}

	inline void Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac, InterpolatedVarType type )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->Type() == type )
			{
				variable->Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
			}
		}
	}

	inline void Push( InterpolatedVarType type )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->Type() == type )
			{
				variable->Push();
			}
		}
	}

	inline void SetHermite( bool bHermite )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			variable->SetHermite( bHermite );
		}
	}

	inline void SaveLastKnownValue( InterpolatedVarType type )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->Type() == type )
			{
				variable->SaveLastKnownValue();
			}
		}
	}

	inline void RestoreToLastKnownValue()
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			variable->RestoreToLastKnownValue();
		}
	}

	inline void ClearHistory()
	{
		m_InterpolatedVarList.ClearHistory();
	}

	CInterpolatedVarList m_InterpolatedVarList;
};

#define INTERPOLATED_VARIABLE_LIST                                                                                     \
	INTERPOLATED_VAR( Vector, vecLocalOrigin, LATCH_SIMULATION_VAR )                                                   \
	INTERPOLATED_VAR( QAngle, angLocalAngles, LATCH_SIMULATION_VAR )                                                   \
	INTERPOLATED_VAR( Vector, vecMinsPreScaled, LATCH_SIMULATION_VAR )                                                 \
	INTERPOLATED_VAR( Vector, vecMaxsPreScaled, LATCH_SIMULATION_VAR )                                                 \
	INTERPOLATED_VAR( float, flSimulationTime, LATCH_SIMULATION_VAR )                                                  \
	INTERPOLATED_VAR( QAngle, angRenderAngles, LATCH_SIMULATION_VAR )                                                  \
	INTERPOLATED_VAR( int, nSequence, LATCH_ANIMATION_VAR )                                                            \
	INTERPOLATED_VAR( float, flCycle, LATCH_ANIMATION_VAR )                                                            \
	INTERPOLATED_VAR_ARRAY( float, flPoseParameter, MAXSTUDIOPOSEPARAM, LATCH_ANIMATION_VAR )                          \
	INTERPOLATED_VAR_ARRAY( float, flEncodedController, MAXSTUDIOBONECTRLS, LATCH_ANIMATION_VAR )                      \
	INTERPOLATED_VAR_ARRAY( LayerRecord, LayerRecords, MAX_LAYER_RECORDS, LATCH_ANIMATION_VAR )                        \
	INTERPOLATED_VAR( float, flAnimTime, LATCH_ANIMATION_VAR )

struct LagRecord
{
#define INTERPOLATED_VAR( type, name, ltype )			  type m_##name;
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype ) type m_##name[size];

	INTERPOLATED_VARIABLE_LIST

#undef INTERPOLATED_VAR
#undef INTERPOLATED_VAR_ARRAY
};

struct LagTrack : public BaseLagTrack
{
	LagRecord m_RecordReferenced;

#define INTERPOLATED_VAR( type, name, ltype )                                                                          \
	CInterpolatedVar< type, MAX_UNLAG_TICKS > m_iv_##name { "LagTrack::m_iv_" #name, &m_RecordReferenced.m_##name, ltype };
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype )                                                              \
	CInterpolatedVarArray< type, size, MAX_UNLAG_TICKS > m_iv_##name { "LagTrack::m_iv_" #name, m_RecordReferenced.m_##name, ltype };

	INTERPOLATED_VARIABLE_LIST

#undef INTERPOLATED_VAR
#undef INTERPOLATED_VAR_ARRAY

	LagTrack()
	{
#define INTERPOLATED_VAR( type, name, ltype ) AddVar( &m_iv_##name );
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype )                                                              \
	for ( size_t i = 0; i < size; i++ )                                                                                \
	{                                                                                                                  \
		AddVar( &m_iv_##name[i] );                                                                                     \
	}

		INTERPOLATED_VARIABLE_LIST

#undef INTERPOLATED_VAR
#undef INTERPOLATED_VAR_ARRAY
	}

	void ResetLastKnownValue()
	{
#define INTERPOLATED_VAR( type, name, ltype ) m_iv_##name.GetLastKnownValue() = {};
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype )                                                              \
	for ( size_t i = 0; i < size; i++ )                                                                                \
	{                                                                                                                  \
		m_iv_##name[i].GetLastKnownValue() = {};                                                                       \
	}

		INTERPOLATED_VARIABLE_LIST

#undef INTERPOLATED_VAR
#undef INTERPOLATED_VAR_ARRAY
	}
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

inline LayerRecord LoopingLerp_Hermite( float flPercent,
										const LayerRecord& prev,
										const LayerRecord& from,
										const LayerRecord& to )
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

inline LayerRecord Lerp_Hermite( float flPercent,
								 const LayerRecord& prev,
								 const LayerRecord& from,
								 const LayerRecord& to )
{
	return LoopingLerp_Hermite( flPercent, prev, from, to );
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

std::string std::to_string( LayerRecord& obj )
{
	return "cycle: " + std::to_string( obj.m_flCycle ) + ", sequence: " + std::to_string( obj.m_nSequence )
		   + ", weight: " + std::to_string( obj.m_flWeight ) + ", flags: " + std::to_string( obj.m_fFlags )
		   + ", order: " + std::to_string( obj.m_nOrder );
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
	template < bool bPush >
	void TrackEntity( CBaseEntity* pEntity );
	void BacktrackEntity( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd );
	void BacktrackSimulationData( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd, bool& bNeedsRestore );
	void BacktrackAnimationData( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd, bool& bNeedsRestore );

	void ClearHistory()
	{
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			m_EntityTrack[i].ClearHistory();
			m_EntityTrack[i].ResetLastKnownValue();
		}
	}

	void FrameUpdatePostEntityThinkOnFinalTick() override
	{
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			auto pEntity = g_pFastEntityLookUp->entities[i];

			if ( !pEntity )
			{
				continue;
			}

			// // Players will be tracked during after PhysicsSimulate
			// if ( pEntity->IsPlayer() )
			// {
			// 	continue;
			// }

			TrackEntity< true >( pEntity );
		}
	}

	// keep a list of lag records for each entities
	LagTrack m_EntityTrack[MAX_EDICTS];

	// Scratchpad for determining what needs to be restored
	CBitVec< MAX_EDICTS > m_RestoreEntity;
	bool m_bNeedToRestore;
	CBasePlayer* m_LagPlayer;
};

static CLagCompensationManager g_LagCompensationManager( "CLagCompensationManager" );
ILagCompensationManager* lagcompensation = &g_LagCompensationManager;

//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all entities have had a chance to think
//-----------------------------------------------------------------------------
template < bool bPush >
void CLagCompensationManager::TrackEntity( CBaseEntity* pEntity )
{
	if ( !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}

	VPROF_BUDGET( "TrackEntities", "CLagCompensationManager" );

	auto index = pEntity->entindex();

	auto pTrack	 = &m_EntityTrack[index];
	auto pRecord = &pTrack->m_RecordReferenced;

	pRecord->m_flSimulationTime = pEntity->GetSimulationTime();
	pRecord->m_angLocalAngles	= pEntity->GetLocalAngles();
	pRecord->m_vecLocalOrigin	= pEntity->GetLocalOrigin();
	pRecord->m_vecMinsPreScaled = pEntity->CollisionProp()->OBBMinsPreScaled();
	pRecord->m_vecMaxsPreScaled = pEntity->CollisionProp()->OBBMaxsPreScaled();

#ifdef CSTRIKE_DLL
	auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

	if ( csPlayer )
	{
		pRecord->m_angRenderAngles = csPlayer->GetRenderAngles();
	}
#endif

	auto pflSimulationTime = pTrack->m_iv_flSimulationTime.Get();

	if constexpr ( bPush )
	{
		if ( !pflSimulationTime || ( *pflSimulationTime != pRecord->m_flSimulationTime ) )
		{
			pTrack->Push( LATCH_SIMULATION_VAR );

			if ( sv_unlag_debug.GetInt() >= 2 )
			{
				ConMsg( "%f: Pushing entity %i for lag compensation with simulation time %f\n",
						gpGlobals->curtime,
						index,
						*pflSimulationTime );
			}
		}
	}

	pTrack->SaveLastKnownValue( LATCH_SIMULATION_VAR );

	// Save animation vars now
	pRecord->m_flAnimTime = pEntity->GetAnimTime();

	auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

	if ( pAnim )
	{
		pRecord->m_nSequence = pAnim->GetSequence();
		pRecord->m_flCycle	 = pAnim->GetCycle();

		auto pStudioHdr = pAnim->GetModelPtr();

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				pRecord->m_flPoseParameter[paramIndex] = pAnim->GetPoseParameterArray()[paramIndex];
			}

			for ( int boneIndex = 0; boneIndex < pStudioHdr->GetNumBoneControllers(); boneIndex++ )
			{
				pRecord->m_flEncodedController[boneIndex] = pAnim->GetBoneControllerArray()[boneIndex];
			}
		}
	}

	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	if ( pAnimOverlay )
	{
		int layerCount = pAnimOverlay->GetNumAnimOverlays();

		for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			auto layerRecord			   = &pRecord->m_LayerRecords[layerIndex];
			CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

			if ( pCurrentLayer )
			{
				layerRecord->m_flCycle			  = pCurrentLayer->m_flCycle;
				layerRecord->m_nOrder			  = pCurrentLayer->m_nOrder;
				layerRecord->m_nSequence		  = pCurrentLayer->m_nSequence;
				layerRecord->m_flWeight			  = pCurrentLayer->m_flWeight;
				layerRecord->m_fFlags			  = pCurrentLayer->m_fFlags;
				layerRecord->m_flPrevCycle		  = pCurrentLayer->m_flPrevCycle;
				layerRecord->m_flLayerAnimtime	  = pCurrentLayer->m_flLayerAnimtime;
				layerRecord->m_flLayerFadeOuttime = pCurrentLayer->m_flLayerFadeOuttime;
			}
		}
	}

	auto pflAnimTime = pTrack->m_iv_flAnimTime.Get();

	if constexpr ( bPush )
	{
		if ( !pflAnimTime || ( *pflAnimTime != pRecord->m_flAnimTime ) )
		{
			pTrack->Push( LATCH_ANIMATION_VAR );

			if ( sv_unlag_debug.GetInt() >= 3 )
			{
				ConMsg( "%f: Pushing entity %i for lag compensation with animation time %f\n",
						gpGlobals->curtime,
						index,
						*pflAnimTime );
			}
		}
	}

	pTrack->SaveLastKnownValue( LATCH_ANIMATION_VAR );
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
			// Save entity values
			TrackEntity< false >( pEntity );
			// Move other entity back in time
			BacktrackEntity( pEntity, i, cmd );
		}

		if ( sv_unlag_debug_entity.GetInt() == pEntity->entindex() && player->entindex() == 1 )
		{
			auto pInterpolatedRecord = &m_EntityTrack[i].m_RecordReferenced;

			auto origin = pInterpolatedRecord->m_vecLocalOrigin;
			auto angle	= pInterpolatedRecord->m_angLocalAngles;
			auto mins	= pInterpolatedRecord->m_vecMinsPreScaled;
			auto maxs	= pInterpolatedRecord->m_vecMaxsPreScaled;

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

void CLagCompensationManager::BacktrackEntity( CBaseEntity* pEntity, int loopIndex, CUserCmd* cmd )
{
	VPROF_BUDGET( "BacktrackEntity", "CLagCompensationManager" );

	// TODO_ENHANCED: to limit cheaters backtracking, we could their measure latency and approximatively check if it's
	// reasonable enough.

	bool bNeedsRestore	   = false;
	float flTargetSimTime  = cmd->simulationdata[loopIndex].sim_time;
	float flTargetAnimTime = cmd->simulationdata[loopIndex].anim_time;
	auto pTrack			   = &m_EntityTrack[loopIndex];

	pTrack->SetHermite( !m_LagPlayer->m_bUseLinearInterpolationOnly );

	// Somehow the client didn't care, might due to prediction or some other things like client created entities.
	if ( flTargetSimTime == 0 )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Client has refused to lag compensate simulation data this entity, probably already predicted ( %i "
					")\n",
					pEntity->entindex() );
		}
	}
	else
	{
		BacktrackSimulationData( pEntity, loopIndex, cmd, bNeedsRestore );
	}

	// This entity doesn't have animation or not cared by the player somehow.
	if ( flTargetAnimTime == 0 )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Client has refused to lag compensate animation data this entity, probably already predicted ( %i "
					")\n",
					pEntity->entindex() );
		}
	}
	else
	{
		BacktrackAnimationData( pEntity, loopIndex, cmd, bNeedsRestore );
	}

	if ( bNeedsRestore )
	{
		m_RestoreEntity.Set( loopIndex ); // remember that we changed this entity
		m_bNeedToRestore = true;		  // we changed at least one entity
	}
}

void CLagCompensationManager::BacktrackSimulationData( CBaseEntity* pEntity,
													   int loopIndex,
													   CUserCmd* cmd,
													   bool& bNeedsRestore )
{
	auto flTargetSimTime		   = cmd->simulationdata[loopIndex].sim_time;
	auto flInterpolationAmountFrac = cmd->interpolated_amount_frac;
	auto pTrack					   = &m_EntityTrack[loopIndex];
	auto pInterpolatedRecord	   = &pTrack->m_RecordReferenced;
	auto bLinearOnly			   = m_LagPlayer->m_bUseLinearInterpolationOnly;

	// Find the corresponding simulation time, with the corresponding amount of ticks to interpolate with.
	// We need to count it this way because we have no idea how much time passed by due to server fps loss.
	size_t nAmountOfTicks = 0;

	for ( size_t i = 0; i < MAX_UNLAG_TICKS; i++ )
	{
		auto pflSimulationTime = pTrack->m_iv_flSimulationTime.Get( i );

		if ( !pflSimulationTime )
		{
			break;
		}

		if ( *pflSimulationTime == flTargetSimTime )
		{
			nAmountOfTicks = i;
			break;
		}
	}

	if ( nAmountOfTicks < ( bLinearOnly ? 1 : 2 ) )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Couldn't find enough simulated time for entity %i (%f)\n", pEntity->entindex(), flTargetSimTime );
		}

		return;
	}

	// if ( sv_unlag_debug.GetBool() )
	// {
	// 	ConMsg( "Unlagging simulated time for entity %i for %i ticks (%f)\n",
	// 			nAmountOfTicks,
	// 			pEntity->entindex(),
	// 			flTargetSimTime );
	// }

	// Interpolate now.
	pTrack->Interpolate( nAmountOfTicks, flInterpolationAmountFrac, LATCH_SIMULATION_VAR );

#ifdef USERCMD_DEBUG_SIMULATION_DATA
	if ( pInterpolatedRecord->m_flSimulationTime != cmd->simulationdata[loopIndex].interpolated_sim_time )
	{
		auto pStartTime = pTrack->m_iv_flSimulationTime.Get( nAmountOfTicks );
		auto pEndTime	= pTrack->m_iv_flSimulationTime.Get( nAmountOfTicks - 1 );

		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "The player %s (tickbase: %i) has unlagged entity %i with %i ticks for simulation but probably has "
					"floating point issues interpolated = %f | target = %f, start = %f | end = %f, not perfectly lag "
					"compensated. (linear only: %s)\n",
					m_LagPlayer->GetPlayerName(),
					TIME_TO_TICKS( m_LagPlayer->GetTimeBase() ),
					pEntity->entindex(),
					nAmountOfTicks,
					pInterpolatedRecord->m_flSimulationTime,
					cmd->simulationdata[loopIndex].interpolated_sim_time,
					pStartTime ? *pStartTime : -1.0f,
					pEndTime ? *pEndTime : -1.0f,
					bLinearOnly ? "true" : "false" );
		}
	}
#endif

	pEntity->m_flInterpolatedSimulationTime = pInterpolatedRecord->m_flSimulationTime;

	pEntity->SetLocalOrigin( pInterpolatedRecord->m_vecLocalOrigin );
	pEntity->SetLocalAngles( pInterpolatedRecord->m_angLocalAngles );
	pEntity->SetSize( pInterpolatedRecord->m_vecMinsPreScaled, pInterpolatedRecord->m_vecMaxsPreScaled );

	// Game specific lag compensation
#ifdef CSTRIKE_DLL
	auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

	if ( csPlayer )
	{
		csPlayer->m_angRenderAngles = pInterpolatedRecord->m_angRenderAngles;
	}
#endif

	bNeedsRestore = true;
}

void CLagCompensationManager::BacktrackAnimationData( CBaseEntity* pEntity,
													  int loopIndex,
													  CUserCmd* cmd,
													  bool& bNeedsRestore )
{
	auto flTargetAnimTime		   = cmd->simulationdata[loopIndex].anim_time;
	auto flInterpolationAmountFrac = cmd->interpolated_amount_frac;
	auto pTrack					   = &m_EntityTrack[loopIndex];
	auto pInterpolatedRecord	   = &pTrack->m_RecordReferenced;
	auto bLinearOnly			   = m_LagPlayer->m_bUseLinearInterpolationOnly;

	// Find the corresponding simulation time, with the corresponding amount of ticks to interpolate with.
	// We need to count it this way because we have no idea how much time passed by due to server fps loss.
	size_t nAmountOfTicks = 0;

	for ( size_t i = 0; i < MAX_UNLAG_TICKS; i++ )
	{
		auto pflAnimTime = pTrack->m_iv_flAnimTime.Get( i );

		if ( !pflAnimTime )
		{
			break;
		}

		if ( *pflAnimTime == flTargetAnimTime )
		{
			nAmountOfTicks = i;
			break;
		}
	}

	if ( nAmountOfTicks < ( bLinearOnly ? 1 : 2 ) )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Couldn't find enough animated time for entity %i (%f)\n", pEntity->entindex(), flTargetAnimTime );
		}

		return;
	}

	// if ( sv_unlag_debug.GetBool() )
	// {
	// 	ConMsg( "Unlagging animated time for entity %i for %i ticks (%f)\n",
	// 			nAmountOfTicks,
	// 			pEntity->entindex(),
	// 			flTargetAnimTime );
	// }

	auto pAnim		  = dynamic_cast< CBaseAnimating* >( pEntity );
	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	// Sadly due to looping sequences we need to interpolate variables seperately.
	pTrack->m_iv_flAnimTime.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );

#ifdef USERCMD_DEBUG_SIMULATION_DATA
	if ( pInterpolatedRecord->m_flAnimTime != cmd->simulationdata[loopIndex].interpolated_anim_time )
	{
		auto pStartTime = pTrack->m_iv_flAnimTime.Get( nAmountOfTicks );
		auto pEndTime	= pTrack->m_iv_flAnimTime.Get( nAmountOfTicks - 1 );

		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "The player %s (tickbase: %i) has unlagged entity %i with %i ticks for animation but probably has "
					"floating point issues interpolated = %f | target = %f, start = %f | end = %f, not perfectly lag "
					"compensated. (linear only: %s)\n",
					m_LagPlayer->GetPlayerName(),
					TIME_TO_TICKS( m_LagPlayer->GetTimeBase() ),
					pEntity->entindex(),
					nAmountOfTicks,
					pInterpolatedRecord->m_flAnimTime,
					cmd->simulationdata[loopIndex].interpolated_anim_time,
					pStartTime ? *pStartTime : -1.0f,
					pEndTime ? *pEndTime : -1.0f,
					bLinearOnly ? "true" : "false" );
		}
	}
#endif

	pEntity->m_flInterpolatedAnimTime = pInterpolatedRecord->m_flAnimTime;

	if ( pAnim )
	{
		pTrack->m_iv_nSequence.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
		pAnim->SetSequence( pInterpolatedRecord->m_nSequence );

		// Check if looping
		pTrack->m_iv_flCycle.SetLooping( pAnim->IsSequenceLooping( pInterpolatedRecord->m_nSequence ) );
		pTrack->m_iv_flCycle.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
		pAnim->SetCycle( pInterpolatedRecord->m_flCycle );

		auto pStudioHdr = pAnim->GetModelPtr();

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				const mstudioposeparamdesc_t& Pose = pStudioHdr->pPoseParameter( paramIndex );
				pTrack->m_iv_flPoseParameter[paramIndex].SetLooping( Pose.loop != 0.0f );
				pTrack->m_iv_flPoseParameter[paramIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
				pAnim->SetPoseParameterRaw( paramIndex, pInterpolatedRecord->m_flPoseParameter[paramIndex] );
			}

			for ( int boneIndex = 0; boneIndex < pStudioHdr->GetNumBoneControllers(); boneIndex++ )
			{
				bool loop = ( pStudioHdr->pBonecontroller( boneIndex )->type & ( STUDIO_XR | STUDIO_YR | STUDIO_ZR ) )
							!= 0;
				pTrack->m_iv_flEncodedController[boneIndex].SetLooping( loop );
				pTrack->m_iv_flEncodedController[boneIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
				pAnim->SetPoseParameterRaw( boneIndex, pInterpolatedRecord->m_flEncodedController[boneIndex] );
			}
		}
	}

	if ( pAnimOverlay )
	{
		int layerCount = pAnimOverlay->GetNumAnimOverlays();

		for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			pTrack->m_iv_LayerRecords[layerIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
			auto layerRecord			   = &pInterpolatedRecord->m_LayerRecords[layerIndex];
			CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

			if ( pCurrentLayer )
			{
				pCurrentLayer->m_flCycle			= layerRecord->m_flCycle;
				pCurrentLayer->m_nOrder				= layerRecord->m_nOrder;
				pCurrentLayer->m_nSequence			= layerRecord->m_nSequence;
				pCurrentLayer->m_flWeight			= layerRecord->m_flWeight;
				pCurrentLayer->m_fFlags				= layerRecord->m_fFlags;
				pCurrentLayer->m_flPrevCycle		= layerRecord->m_flPrevCycle;
				pCurrentLayer->m_flLayerAnimtime	= layerRecord->m_flLayerAnimtime;
				pCurrentLayer->m_flLayerFadeOuttime = layerRecord->m_flLayerFadeOuttime;
			}
		}
	}

	if ( sv_lagflushbonecache.GetBool() )
	{
		if ( pAnim )
		{
			pAnim->InvalidateBoneCache();
		}
	}

	bNeedsRestore = true;
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

		auto pTrack = &m_EntityTrack[i];
		pTrack->RestoreToLastKnownValue();

		auto restoreRecord = &pTrack->m_RecordReferenced;

		pEntity->SetSize( restoreRecord->m_vecMinsPreScaled, restoreRecord->m_vecMaxsPreScaled );
		pEntity->SetLocalAngles( restoreRecord->m_angLocalAngles );
		pEntity->SetLocalOrigin( restoreRecord->m_vecLocalOrigin );

#ifdef CSTRIKE_DLL
		auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

		if ( csPlayer )
		{
			csPlayer->m_angRenderAngles = restoreRecord->m_angRenderAngles;
		}
#endif

		auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

		if ( pAnim )
		{
			pAnim->SetSequence( restoreRecord->m_nSequence );
			pAnim->SetCycle( restoreRecord->m_flCycle );

			CStudioHdr* pStudioHdr = pAnim->GetModelPtr();

			if ( pStudioHdr )
			{
				for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
				{
					pAnim->SetPoseParameterRaw( paramIndex, restoreRecord->m_flPoseParameter[paramIndex] );
				}

				for ( int encIndex = 0; encIndex < pStudioHdr->GetNumBoneControllers(); encIndex++ )
				{
					pAnim->SetBoneControllerRaw( encIndex, restoreRecord->m_flEncodedController[encIndex] );
				}
			}
		}

		auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

		if ( pAnimOverlay )
		{
			int layerCount = pAnimOverlay->GetNumAnimOverlays();

			for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
			{
				auto layerRecord			   = &restoreRecord->m_LayerRecords[layerIndex];
				CAnimationLayer* pCurrentLayer = pAnimOverlay->GetAnimOverlay( layerIndex );

				if ( pCurrentLayer )
				{
					pCurrentLayer->m_flCycle			= layerRecord->m_flCycle;
					pCurrentLayer->m_nOrder				= layerRecord->m_nOrder;
					pCurrentLayer->m_nSequence			= layerRecord->m_nSequence;
					pCurrentLayer->m_flWeight			= layerRecord->m_flWeight;
					pCurrentLayer->m_fFlags				= layerRecord->m_fFlags;
					pCurrentLayer->m_flPrevCycle		= layerRecord->m_flPrevCycle;
					pCurrentLayer->m_flLayerAnimtime	= layerRecord->m_flLayerAnimtime;
					pCurrentLayer->m_flLayerFadeOuttime = layerRecord->m_flLayerFadeOuttime;
				}
			}
		}
	}
}
