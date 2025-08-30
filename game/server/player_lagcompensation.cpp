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

	inline void Push( const CIVLatchType& LatchType )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->LatchType() == LatchType )
			{
				variable->Push();
			}
		}
	}

	inline void SaveLastKnownValue( const CIVLatchType& LatchType )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->LatchType() == LatchType )
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

	inline void ClearHistory( const CIVLatchType& LatchType )
	{
		for ( auto&& variable : m_InterpolatedVarList.variables )
		{
			if ( variable->LatchType() == LatchType )
			{
				variable->ClearHistory();
			}
		}
	}

	CInterpolatedVarList m_InterpolatedVarList;
};

#define INTERPOLATED_VARIABLE_LIST_BASE                                                                                \
	INTERPOLATED_VAR( uint64, nSimulatedTickCount, CIVLatchType::SIMULATION )                                          \
	INTERPOLATED_VAR( uint64, nAnimatedTickCount, CIVLatchType::ANIMATION )                                            \
	INTERPOLATED_VAR( Vector, vecLocalOrigin, CIVLatchType::SIMULATION )                                               \
	INTERPOLATED_VAR( QAngle, angLocalAngles, CIVLatchType::SIMULATION )                                               \
	INTERPOLATED_VAR( Vector, vecMinsPreScaled, CIVLatchType::SIMULATION )                                             \
	INTERPOLATED_VAR( Vector, vecMaxsPreScaled, CIVLatchType::SIMULATION )                                             \
	INTERPOLATED_VAR( int, nSequence, CIVLatchType::ANIMATION )                                                        \
	INTERPOLATED_VAR( float, flCycle, CIVLatchType::ANIMATION )                                                        \
	INTERPOLATED_VAR_ARRAY( float, flPoseParameter, MAXSTUDIOPOSEPARAM, CIVLatchType::ANIMATION )                      \
	INTERPOLATED_VAR_ARRAY( float, flEncodedController, MAXSTUDIOBONECTRLS, CIVLatchType::ANIMATION )                  \
	INTERPOLATED_VAR_ARRAY( LayerRecord, LayerRecords, MAX_LAYER_RECORDS, CIVLatchType::ANIMATION )

#if defined( CSTRIKE_DLL )
#define INTERPOLATED_VARIABLE_LIST                                                                                     \
	INTERPOLATED_VARIABLE_LIST_BASE                                                                                    \
	INTERPOLATED_VAR( QAngle, angRenderAngles, CIVLatchType::SIMULATION )
#else
#endif

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
	CInterpolatedVar< type, MAX_UNLAG_TICKS > m_iv_##name { "LagTrack::m_iv_" #name,                                   \
															&m_RecordReferenced.m_##name,                              \
															ltype };
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype )                                                              \
	CInterpolatedVarArray< type, size, MAX_UNLAG_TICKS > m_iv_##name { "LagTrack::m_iv_" #name,                        \
																	   m_RecordReferenced.m_##name,                    \
																	   ltype };

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

	void SetInterpolationType( const CInterpolationType& InterpolationType )
	{
#define INTERPOLATED_VAR( type, name, ltype ) m_iv_##name.SetInterpolationType( InterpolationType );
#define INTERPOLATED_VAR_ARRAY( type, name, size, ltype )                                                              \
	for ( size_t i = 0; i < size; i++ )                                                                                \
	{                                                                                                                  \
		m_iv_##name[i].SetInterpolationType( InterpolationType );                                                      \
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
	void SpewBacktrackData( const std::string& pszContext,
							CBaseEntity* pEntity,
							size_t nAmountOfTicks,
							uint64 nClientStartTickCount,
							uint64 nClientEndTickCount,
							uint64 nServerStartTickCount,
							uint64 nServerEndTickCount );

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

	VPROF_BUDGET( "TrackEntity", "CLagCompensationManager" );

	auto index = pEntity->entindex();

	auto pLagTrack = &m_EntityTrack[index];
	auto pRecord   = &pLagTrack->m_RecordReferenced;

	pRecord->m_nSimulatedTickCount = pEntity->m_nSimulatedTickCount;
	pRecord->m_angLocalAngles	   = pEntity->GetLocalAngles();
	pRecord->m_vecLocalOrigin	   = pEntity->GetLocalOrigin();
	pRecord->m_vecMinsPreScaled	   = pEntity->CollisionProp()->OBBMinsPreScaled();
	pRecord->m_vecMaxsPreScaled	   = pEntity->CollisionProp()->OBBMaxsPreScaled();

#ifdef CSTRIKE_DLL
	auto csPlayer = dynamic_cast< CCSPlayer* >( pEntity );

	if ( csPlayer )
	{
		pRecord->m_angRenderAngles = csPlayer->GetRenderAngles();
	}
#endif

	if constexpr ( bPush )
	{
		auto pnSimulatedTickCount = pLagTrack->m_iv_nSimulatedTickCount.Get();

		// Has the entity just been created ?
		if ( !pEntity->m_bHasJustBeenCreatedThisFrame )
		{
			if ( pnSimulatedTickCount && *pnSimulatedTickCount > pRecord->m_nSimulatedTickCount )
			{
				// TODO_ENHANCED: this is just being paranoid right now.
				Error( "Abnormal simulation time that went backwards on entity %i %lld %lld\n",
					   index,
					   *pnSimulatedTickCount,
					   pRecord->m_nSimulatedTickCount );
			}
		}
		else
		{
			pLagTrack->ClearHistory( CIVLatchType::SIMULATION );
		}

		pnSimulatedTickCount = pLagTrack->m_iv_nSimulatedTickCount.Get();

		if ( !pnSimulatedTickCount || ( *pnSimulatedTickCount != pRecord->m_nSimulatedTickCount ) )
		{
			pLagTrack->Push( CIVLatchType::SIMULATION );

			if ( sv_unlag_debug.GetInt() >= 2 )
			{
				ConMsg( "%f: Pushing entity %i for lag compensation with simulation time %lld\n",
						gpGlobals->curtime,
						index,
						*pnSimulatedTickCount );
			}
		}
	}

	pLagTrack->SaveLastKnownValue( CIVLatchType::SIMULATION );

	auto pAnim = dynamic_cast< CBaseAnimating* >( pEntity );

	if ( pAnim )
	{
		// Save animation vars now
		pRecord->m_nAnimatedTickCount = pAnim->m_nAnimatedTickCount;
		pRecord->m_nSequence		  = pAnim->GetSequence();
		pRecord->m_flCycle			  = pAnim->GetCycle();

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

	if constexpr ( bPush )
	{
		auto pnAnimatedTickCount = pLagTrack->m_iv_nAnimatedTickCount.Get();

		// Has the entity just been created ?
		if ( !pEntity->m_bHasJustBeenCreatedThisFrame )
		{
			if ( pnAnimatedTickCount && *pnAnimatedTickCount > pRecord->m_nAnimatedTickCount )
			{
				// TODO_ENHANCED: this is just being paranoid right now.
				Error( "Abnormal animation time that went backwards on entity %i %lld %lld\n",
					   index,
					   *pnAnimatedTickCount,
					   pRecord->m_nAnimatedTickCount );
			}
		}
		else
		{
			pLagTrack->ClearHistory( CIVLatchType::ANIMATION );
		}

		pnAnimatedTickCount = pLagTrack->m_iv_nAnimatedTickCount.Get();

		if ( !pnAnimatedTickCount || ( *pnAnimatedTickCount != pRecord->m_nAnimatedTickCount ) )
		{
			pLagTrack->Push( CIVLatchType::ANIMATION );

			if ( sv_unlag_debug.GetInt() >= 3 )
			{
				ConMsg( "%f: Pushing entity %i for lag compensation with animation time %lld\n",
						gpGlobals->curtime,
						index,
						*pnAnimatedTickCount );
			}
		}
	}

	pLagTrack->SaveLastKnownValue( CIVLatchType::ANIMATION );
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

	bool bNeedsRestore			   = false;
	auto nTargetSimulatedTickCount = cmd->simulationdata[loopIndex].sim_tick_count;
	auto nTargetAnimatedTickCount  = cmd->simulationdata[loopIndex].anim_tick_count;
	auto pLagTrack				   = &m_EntityTrack[loopIndex];

	pLagTrack->SetInterpolationType( m_LagPlayer->m_iCurrentInterpolationType );

	// Somehow the client didn't care, might due to prediction or some other things like client created entities.
	if ( nTargetSimulatedTickCount == 0 )
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
	if ( nTargetAnimatedTickCount == 0 )
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
	auto nTargetSimulatedTickCount = cmd->simulationdata[loopIndex].sim_tick_count;
	auto flInterpolationAmountFrac = cmd->interpolated_amount_frac;
	auto pLagTrack				   = &m_EntityTrack[loopIndex];
	auto pInterpolatedRecord	   = &pLagTrack->m_RecordReferenced;

	// Find the corresponding simulation time, with the corresponding amount of ticks to interpolate with.
	// We need to count it this way because we have no idea how much time passed by due to server fps loss.
	size_t nAmountOfTicks = 0;

	for ( size_t i = 0; i < MAX_UNLAG_TICKS; i++ )
	{
		auto pnSimulatedTickCount = pLagTrack->m_iv_nSimulatedTickCount.Get( i );

		if ( !pnSimulatedTickCount )
		{
			break;
		}

		if ( *pnSimulatedTickCount == nTargetSimulatedTickCount )
		{
			nAmountOfTicks = i;
			break;
		}
	}

	size_t nMinimumAmountOfTicks = 0;

	switch ( m_LagPlayer->m_iCurrentInterpolationType )
	{
		case CInterpolationType::LINEAR:
		{
			nMinimumAmountOfTicks = 1;
			break;
		}
		case CInterpolationType::HERMITE:
		{
			nMinimumAmountOfTicks = 2;
			break;
		}
		default:
		{
			Error( "CLagCompensationManager::BacktrackSimulationData: Unsupported interpolation %i\n",
				   m_LagPlayer->m_iCurrentInterpolationType );
			break;
		}
	}

	if ( nAmountOfTicks < nMinimumAmountOfTicks )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Couldn't find (enough) simulated time for entity %i: target = %f | last = %f\n",
					pEntity->entindex(),
					nTargetSimulatedTickCount,
					pLagTrack->m_iv_nSimulatedTickCount.GetLastKnownValue() );
		}

		return;
	}

	auto nRealAmountOfTicks = pLagTrack->m_iv_nSimulatedTickCount.Interpolate( nAmountOfTicks,
																			   flInterpolationAmountFrac );

#ifdef USERCMD_DEBUG_SIMULATION_DATA
	SpewBacktrackData( "simulation",
					   pEntity,
					   nRealAmountOfTicks,
					   cmd->simulationdata[loopIndex].sim_tick_count,
					   cmd->simulationdata[loopIndex].end_sim_tick_count,
					   *pLagTrack->m_iv_nSimulatedTickCount.Get( nRealAmountOfTicks ),
					   *pLagTrack->m_iv_nSimulatedTickCount.Get( nRealAmountOfTicks - 1 ) );
#endif

	pLagTrack->m_iv_vecLocalOrigin.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
	pLagTrack->m_iv_angLocalAngles.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
	pLagTrack->m_iv_vecMinsPreScaled.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
	pLagTrack->m_iv_vecMaxsPreScaled.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );

#ifdef CSTRIKE_DLL
	pLagTrack->m_iv_angRenderAngles.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
#endif

	pEntity->m_nSimulatedTickCount = pInterpolatedRecord->m_nSimulatedTickCount;
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
	auto nTargetAnimatedTickCount  = cmd->simulationdata[loopIndex].anim_tick_count;
	auto flInterpolationAmountFrac = cmd->interpolated_amount_frac;
	auto pLagTrack				   = &m_EntityTrack[loopIndex];
	auto pInterpolatedRecord	   = &pLagTrack->m_RecordReferenced;

	// Find the corresponding simulation time, with the corresponding amount of ticks to interpolate with.
	// We need to count it this way because we have no idea how much time passed by due to server fps loss.
	size_t nAmountOfTicks = 0;

	for ( size_t i = 0; i < MAX_UNLAG_TICKS; i++ )
	{
		auto pnAnimatedTickCount = pLagTrack->m_iv_nAnimatedTickCount.Get( i );

		if ( !pnAnimatedTickCount )
		{
			break;
		}

		if ( *pnAnimatedTickCount == nTargetAnimatedTickCount )
		{
			nAmountOfTicks = i;
			break;
		}
	}

	size_t nMinimumAmountOfTicks = 0;

	switch ( m_LagPlayer->m_iCurrentInterpolationType )
	{
		case CInterpolationType::LINEAR:
		{
			nMinimumAmountOfTicks = 1;
			break;
		}
		case CInterpolationType::HERMITE:
		{
			nMinimumAmountOfTicks = 2;
			break;
		}
		default:
		{
			Error( "CLagCompensationManager::BacktrackAnimationData: Unsupported interpolation %i\n",
				   m_LagPlayer->m_iCurrentInterpolationType );
			break;
		}
	}

	if ( nAmountOfTicks < nMinimumAmountOfTicks )
	{
		if ( sv_unlag_debug.GetBool() )
		{
			ConMsg( "Couldn't find enough animated time for entity %i (%f)\n",
					pEntity->entindex(),
					nTargetAnimatedTickCount );
		}

		return;
	}

	auto pAnim		  = dynamic_cast< CBaseAnimating* >( pEntity );
	auto pAnimOverlay = dynamic_cast< CBaseAnimatingOverlay* >( pEntity );

	// Sadly due to looping sequences we need to interpolate variables seperately.
	auto nRealAmountOfTicks = pLagTrack->m_iv_nAnimatedTickCount.Interpolate( nAmountOfTicks,
																			  flInterpolationAmountFrac );

#ifdef USERCMD_DEBUG_SIMULATION_DATA
	SpewBacktrackData( "animation",
					   pEntity,
					   nRealAmountOfTicks,
					   cmd->simulationdata[loopIndex].anim_tick_count,
					   cmd->simulationdata[loopIndex].end_anim_tick_count,
					   *pLagTrack->m_iv_nAnimatedTickCount.Get( nAmountOfTicks ),
					   *pLagTrack->m_iv_nAnimatedTickCount.Get( nAmountOfTicks - 1 ) );

#endif

	if ( pAnim )
	{
		pAnim->m_nAnimatedTickCount = pInterpolatedRecord->m_nAnimatedTickCount;

		pLagTrack->m_iv_nSequence.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
		pAnim->SetSequence( pInterpolatedRecord->m_nSequence );

		// Check if looping
		pLagTrack->m_iv_flCycle.SetLooping( pAnim->IsSequenceLooping( pInterpolatedRecord->m_nSequence ) );
		pLagTrack->m_iv_flCycle.Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
		pAnim->SetCycle( pInterpolatedRecord->m_flCycle );

		auto pStudioHdr = pAnim->GetModelPtr();

		if ( pStudioHdr )
		{
			for ( int paramIndex = 0; paramIndex < pStudioHdr->GetNumPoseParameters(); paramIndex++ )
			{
				const mstudioposeparamdesc_t& Pose = pStudioHdr->pPoseParameter( paramIndex );
				pLagTrack->m_iv_flPoseParameter[paramIndex].SetLooping( Pose.loop != 0.0f );
				pLagTrack->m_iv_flPoseParameter[paramIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
				pAnim->SetPoseParameterRaw( paramIndex, pInterpolatedRecord->m_flPoseParameter[paramIndex] );
			}

			for ( int boneIndex = 0; boneIndex < pStudioHdr->GetNumBoneControllers(); boneIndex++ )
			{
				bool loop = ( pStudioHdr->pBonecontroller( boneIndex )->type & ( STUDIO_XR | STUDIO_YR | STUDIO_ZR ) )
							!= 0;
				pLagTrack->m_iv_flEncodedController[boneIndex].SetLooping( loop );
				pLagTrack->m_iv_flEncodedController[boneIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
				pAnim->SetPoseParameterRaw( boneIndex, pInterpolatedRecord->m_flEncodedController[boneIndex] );
			}
		}
	}

	if ( pAnimOverlay )
	{
		int layerCount = pAnimOverlay->GetNumAnimOverlays();

		for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
		{
			pLagTrack->m_iv_LayerRecords[layerIndex].Interpolate( nAmountOfTicks, flInterpolationAmountFrac );
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

		auto pLagTrack = &m_EntityTrack[i];
		pLagTrack->RestoreToLastKnownValue();

		auto restoreRecord = &pLagTrack->m_RecordReferenced;

		pEntity->m_nSimulatedTickCount = restoreRecord->m_nSimulatedTickCount;
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
			pAnim->m_nAnimatedTickCount = restoreRecord->m_nAnimatedTickCount;

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

void CLagCompensationManager::SpewBacktrackData( const std::string& pszContext,
												 CBaseEntity* pEntity,
												 size_t nAmountOfTicks,
												 uint64 nClientStartTickCount,
												 uint64 nClientEndTickCount,
												 uint64 nServerStartTickCount,
												 uint64 nServerEndTickCount )
{
	std::string result;

	bool bIsAnError = false;

	auto StartResult = [&]()
	{
		result += "[ " + std::to_string( m_LagPlayer->GetTimeBase() ) + " ]: player "
				  + std::string( m_LagPlayer->GetPlayerName() );
		result += " unlagged " + pszContext + " data on entity " + std::to_string( pEntity->entindex() ) + " with "
				  + std::to_string( nAmountOfTicks ) + " ticks ";
	};

	auto EndResult = [&]()
	{
		result += " server interval = [ " + std::to_string( nServerStartTickCount ) + ", "
				  + std::to_string( nServerEndTickCount ) + " ] |";
		result += " client interval = [ " + std::to_string( nClientStartTickCount ) + ", "
				  + std::to_string( nClientEndTickCount ) + " ]";
		std::string strInterpType;

		switch ( m_LagPlayer->m_iCurrentInterpolationType )
		{
			case CInterpolationType::LINEAR:
			{
				strInterpType = "linear";
				break;
			}
			case CInterpolationType::HERMITE:
			{
				strInterpType = "hermite";
				break;
			}
			default:
			{
				break;
			}
		}

		result += " interpolation type: " + strInterpType;
	};

	// TODO_ENHANCED: fix the case when a player spawn, interpolation is disabled. maybe add to simulation data
	// is_interpolated or set frac to 0 ?
	if ( nClientStartTickCount == nClientEndTickCount )
	{
		StartResult();
		result += "where client disabled interpolation or doesn't have enough history (could be a full entity "
				  "update, spawn, etc ...) -> ";
		EndResult();
	}
	// Should never happen anymore, it means TrackEntity doesn't track at the right time the client data or client
	// data isn't sent or received correctly somehow.
	else if ( nClientStartTickCount != nServerStartTickCount || nClientEndTickCount != nServerEndTickCount )
	{
		StartResult();
		result	   += "where client has a problematic server timeline -> ";
		bIsAnError	= true;
		EndResult();
	}

	if ( !result.empty() )
	{
		if ( bIsAnError )
		{
			AssertFatalMsg( false, "%s\n", result.c_str() );
		}
		else
		{
			ConMsg( "%s\n", result.c_str() );
		}
	}
}
