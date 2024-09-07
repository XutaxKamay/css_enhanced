//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "const.h"
#include "debugoverlay_shared.h"
#include "shareddefs.h"
#include "strtools.h"
#ifndef CLIENT_DLL
#include "player.h"
#else
#include "cdll_client_int.h"
#endif
#include "weapon_csbase.h"
#include "decals.h"
#include "cs_gamerules.h"
#include "weapon_c4.h"
#include "in_buttons.h"
#include "datacache/imdlcache.h"

#ifdef CLIENT_DLL
	#include "c_cs_player.h"
#else
	#include "cs_player.h"
	#include "soundent.h"
	#include "bot/cs_bot.h"
	#include "KeyValues.h"
	#include "triggers.h"
	#include "cs_gamestats.h"
#endif

#include "cs_playeranimstate.h"
#include "basecombatweapon_shared.h"
#include "util_shared.h"
#include "takedamageinfo.h"
#include "effect_dispatch_data.h"
#include "engine/ivdebugoverlay.h"
#include "obstacle_pushaway.h"
#include "props_shared.h"

ConVar weapon_accuracy_nospread( "weapon_accuracy_nospread", "0", FCVAR_REPLICATED );
#define	CS_MASK_SHOOT (MASK_SOLID|CONTENTS_DEBRIS)

void DispatchEffect( const char *pName, const CEffectData &data );

#ifdef _DEBUG

	// This is some extra code to collect weapon accuracy stats:

	struct bulletdata_s
	{
		float	timedelta;	// time delta since first shot of this round
		float	derivation;	// derivation for first shoot view angle
		int		count;
	};

	#define STATS_MAX_BULLETS	50

	static bulletdata_s s_bullet_stats[STATS_MAX_BULLETS];

	Vector	s_firstImpact = Vector(0,0,0);
	float	s_firstTime = 0;
	float	s_LastTime = 0;
	int		s_bulletCount = 0;

	void ResetBulletStats()
	{
		s_firstTime = 0;
		s_LastTime = 0;
		s_bulletCount = 0;
		s_firstImpact = Vector(0,0,0);
		Q_memset( s_bullet_stats, 0, sizeof(s_bullet_stats) );
	}

	void PrintBulletStats()
	{
		for (int i=0; i<STATS_MAX_BULLETS; i++ )
		{
			if (s_bullet_stats[i].count == 0)
				break;

			Msg("%3i;%3i;%.4f;%.4f\n", i, s_bullet_stats[i].count,
				s_bullet_stats[i].timedelta, s_bullet_stats[i].derivation );
		}
	}

	void AddBulletStat( float time, float dist, Vector &impact )
	{
		if ( time > s_LastTime + 2.0f )
		{
			// time delta since last shoot is bigger than 2 seconds, start new row
			s_LastTime = s_firstTime = time;
			s_bulletCount = 0;
			s_firstImpact = impact;

		}
		else
		{
			s_LastTime = time;
			s_bulletCount++;
		}

		if ( s_bulletCount >= STATS_MAX_BULLETS )
			s_bulletCount = STATS_MAX_BULLETS -1;

		if ( dist < 1 )
			dist = 1;

		int i = s_bulletCount;

		float offset = VectorLength( s_firstImpact - impact );

		float timedelta = time - s_firstTime;
		float derivation = offset / dist;

		float weight = (float)s_bullet_stats[i].count/(float)(s_bullet_stats[i].count+1);

		s_bullet_stats[i].timedelta *= weight;
		s_bullet_stats[i].timedelta += (1.0f-weight) * timedelta;

		s_bullet_stats[i].derivation *= weight;
		s_bullet_stats[i].derivation += (1.0f-weight) * derivation;

		s_bullet_stats[i].count++;
	}

	CON_COMMAND( stats_bullets_reset, "Reset bullet stats")
	{
		ResetBulletStats();
	}

	CON_COMMAND( stats_bullets_print, "Print bullet stats")
	{
		PrintBulletStats();
	}

#endif

float CCSPlayer::GetPlayerMaxSpeed()
{
	if ( GetMoveType() == MOVETYPE_NONE )
	{
		return CS_PLAYER_SPEED_STOPPED;
	}

	if ( IsObserver() )
	{
		// Player gets speed bonus in observer mode
		return CS_PLAYER_SPEED_OBSERVER;
	}

	bool bValidMoveState = ( State_Get() == STATE_ACTIVE || State_Get() == STATE_OBSERVER_MODE );
	if ( !bValidMoveState || m_bIsDefusing || CSGameRules()->IsFreezePeriod() )
	{
		// Player should not move during the freeze period
		return CS_PLAYER_SPEED_STOPPED;
	}

	float speed = BaseClass::GetPlayerMaxSpeed();

	if ( IsVIP() == true )  // VIP is slow due to the armour he's wearing
	{
		speed = MIN(speed, CS_PLAYER_SPEED_VIP);
	}
	else
	{

		CWeaponCSBase *pWeapon = dynamic_cast<CWeaponCSBase*>( GetActiveWeapon() );

		if ( pWeapon )
		{
			if ( HasShield() && IsShieldDrawn() )
			{
				speed = MIN(speed, CS_PLAYER_SPEED_SHIELD);
			}
			else
			{
				speed = MIN(speed, pWeapon->GetMaxSpeed());
			}
		}
	}

	return speed;
}

void UTIL_ClipTraceToPlayersHull(const Vector& vecAbsStart, const Vector& vecAbsEnd, const Vector& mins, const Vector& maxs, unsigned int mask, ITraceFilter *filter, trace_t *tr )
{
	trace_t playerTrace;
	Ray_t ray;
	float smallestFraction = tr->fraction;
	const float maxRange = 60.0f;

	ray.Init( vecAbsStart, vecAbsEnd, mins, maxs );

	for ( int k = 1; k <= gpGlobals->maxClients; ++k )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( k );

		if ( !player || !player->IsAlive() )
			continue;

#ifdef CLIENT_DLL
		if ( player->IsDormant() )
			continue;
#endif // CLIENT_DLL

		if ( filter && filter->ShouldHitEntity( player, mask ) == false )
			continue;

		float range = DistanceToRay( player->WorldSpaceCenter(), vecAbsStart, vecAbsEnd );
		if ( range < 0.0f || range > maxRange )
			continue;

		enginetrace->ClipRayToEntity( ray, mask|CONTENTS_HITBOX, player, &playerTrace );
		if ( playerTrace.fraction < smallestFraction )
		{
			// we shortened the ray - save off the trace
			*tr = playerTrace;
			smallestFraction = playerTrace.fraction;
		}
	}
}

float CCSPlayer::GetBulletDiameter(int iBulletType)
{
    auto MMToUnits = [] (float&& mm)
    {
        return (mm / 10.f) / 1.905f;
    };
    
    if (IsAmmoType(iBulletType, BULLET_PLAYER_50AE))
    {
        return MMToUnits(13.8f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_50BMG))
    {
        return MMToUnits(20.f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_762MM))
    {
        return MMToUnits(7.62f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_556MM)
             || IsAmmoType(iBulletType, BULLET_PLAYER_556MM_BOX))
    {
        return MMToUnits(5.56f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_338MAG))
    {
        return MMToUnits(8.6f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_9MM))
    {
        return MMToUnits(9.f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_BUCKSHOT))
    {
        return MMToUnits(9.9f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_45ACP))
    {
        return MMToUnits(11.43f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_357SIG))
    {
        return MMToUnits(9.f);
    }
    else if (IsAmmoType(iBulletType, BULLET_PLAYER_57MM))
    {
        return MMToUnits(5.7f);
    }
    else
    {
        Assert(false);
        return 0.0f;
    }
}

void CCSPlayer::GetBulletTypeParameters(
	int iBulletType,
	float &fPenetrationPower,
	float &flPenetrationDistance,
	float &flBulletDiameter )
{
	//MIKETODO: make ammo types come from a script file.
	if ( IsAmmoType( iBulletType, BULLET_PLAYER_50AE ) )
	{
		fPenetrationPower = 30;
        flPenetrationDistance = 1000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_762MM ) )
	{
		fPenetrationPower = 39;
        flPenetrationDistance = 5000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_50BMG ) )
	{
		fPenetrationPower = 266.6f;
        flPenetrationDistance = 150000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_556MM ) ||
			  IsAmmoType( iBulletType, BULLET_PLAYER_556MM_BOX ) )
	{
		fPenetrationPower = 35;
        flPenetrationDistance = 4000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_338MAG ) )
	{
		fPenetrationPower = 45;
        flPenetrationDistance = 8000.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_9MM ) )
	{
		fPenetrationPower = 21;
        flPenetrationDistance = 800.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_BUCKSHOT ) )
	{
		fPenetrationPower = 0;
        flPenetrationDistance = 0.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_45ACP ) )
	{
		fPenetrationPower = 15;
        flPenetrationDistance = 500.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_357SIG ) )
	{
		fPenetrationPower = 25;
        flPenetrationDistance = 800.0;
	}
	else if ( IsAmmoType( iBulletType, BULLET_PLAYER_57MM ) )
	{
		fPenetrationPower = 30;
        flPenetrationDistance = 2000.0;
	}
	else
	{
		// What kind of ammo is this?
		Assert( false );
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
    }

    flBulletDiameter = GetBulletDiameter(iBulletType);
}

static void GetMaterialParameters( int iMaterial, float &flPenetrationModifier, float &flDamageModifier )
{
	switch ( iMaterial )
	{
		case CHAR_TEX_METAL :
			flPenetrationModifier = 0.5;  // If we hit metal, reduce the thickness of the brush we can't penetrate
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_DIRT :
			flPenetrationModifier = 0.5;
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_CONCRETE :
			flPenetrationModifier = 0.4;
			flDamageModifier = 0.25;
			break;
		case CHAR_TEX_GRATE	:
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.99;
			break;
		case CHAR_TEX_VENT :
			flPenetrationModifier = 0.5;
			flDamageModifier = 0.45;
			break;
		case CHAR_TEX_TILE :
			flPenetrationModifier = 0.65;
			flDamageModifier = 0.3;
			break;
		case CHAR_TEX_COMPUTER :
			flPenetrationModifier = 0.4;
			flDamageModifier = 0.45;
			break;
		case CHAR_TEX_WOOD :
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.6;
			break;
		default :
			flPenetrationModifier = 1.0;
			flDamageModifier = 0.5;
			break;
	}

	Assert( flPenetrationModifier > 0 );
	Assert( flDamageModifier < 1.0f ); // Less than 1.0f for avoiding infinite loops
}


static bool TraceToExit(Vector &start, Vector &dir, Vector &end, float flStepSize, float flMaxDistance )
{
	float flDistance = 0;
	Vector last = start;

	while ( flDistance <= flMaxDistance )
	{
		flDistance += flStepSize;

		end = start + flDistance *dir;

		if ( (UTIL_PointContents ( end ) & MASK_SOLID) == 0 )
		{
			// found first free point
			return true;
		}
	}

	return false;
}

inline void UTIL_TraceLineIgnoreTwoEntities(const Vector& vecAbsStart, const Vector& vecAbsEnd, const Vector& mins, const Vector& maxs, unsigned int mask,
					 const IHandleEntity *ignore, const IHandleEntity *ignore2, int collisionGroup, trace_t *ptr )
{
	Ray_t ray;
	ray.Init( vecAbsStart, vecAbsEnd, mins, maxs );
	CTraceFilterSkipTwoEntities traceFilter( ignore, ignore2, collisionGroup );
	enginetrace->TraceRay( ray, mask, &traceFilter, ptr );
	if( r_visualizetraces.GetBool() )
	{
		NDebugOverlay::SweptBox( ptr->startpos, ptr->endpos, mins, maxs, QAngle(), 255, 0, 0, true, 100.0f );
	}
}

#ifdef CLIENT_DLL
extern ConVar cl_debug_duration;
void CCSPlayer::DrawBullet(const Vector& src,
                    const Vector& endpos,
                    const Vector& mins,
                    const Vector& maxs,
                    int r,
                    int g,
                    int b,
                    int a,
                    float duration)
{
    NDebugOverlay::SweptBox(src, endpos, mins, maxs, QAngle(0, 0, 0), r, g, b, a, duration);
    NDebugOverlay::Box(endpos, mins, maxs, r, g, b, a, duration);
}
#endif

void CCSPlayer::FireBullet(
	int iBullet, // bullet number
	Vector vecSrc,	// shooting postion
	const QAngle &shootAngles,  //shooting angle
	float flDistance, // max distance
	int iPenetration, // how many obstacles can be penetrated
	int iBulletType, // ammo type
	int iDamage, // base damage
	float flRangeModifier, // damage range modifier
	CBaseEntity *pevAttacker, // shooter
	bool bDoEffects,
	float xSpread, float ySpread
	)
{
	VPROF( "CCSPlayer::FireBullet" );

	float fCurrentDamage = iDamage;   // damage of the bullet at it's current trajectory
	float flCurrentDistance = 0.0;  //distance that the bullet has traveled so far
	float flLastCurrentDistance = 0.0; // Sometimes some maps are badly made and we need to check the distance we traveled.

	Vector vecDirShooting, vecRight, vecUp;
	AngleVectors( shootAngles, &vecDirShooting, &vecRight, &vecUp );

	// MIKETODO: put all the ammo parameters into a script file and allow for CS-specific params.
	float flPenetrationPower = 0;		// thickness of a wall that this bullet can penetrate
	float flPenetrationDistance = 0;	// distance at which the bullet is capable of penetrating a wall
	float flDamageModifier = 0.5;		// default modification of bullets power after they go through a wall.
    float flPenetrationModifier = 1.f;
    float flBulletDiameter = 0.0f;

	GetBulletTypeParameters( iBulletType, flPenetrationPower, flPenetrationDistance, flBulletDiameter );

	bool is50bmg = false;

	if (IsAmmoType( iBulletType, BULLET_PLAYER_50BMG ))
	{
		is50bmg = true;
	}

    float flBulletRadius = flBulletDiameter / 2.0f;
    
    Vector vecBulletRadiusMaxs(flBulletRadius);
	Vector vecBulletRadiusMins(-flBulletRadius);

	if ( !pevAttacker )
		pevAttacker = this;  // the default attacker is ourselves

	if ( weapon_accuracy_nospread.GetBool() )
	{
		xSpread = 0.0f;
		ySpread = 0.0f;
	}

	// add the spray
	Vector vecDir = vecDirShooting + xSpread * vecRight + ySpread * vecUp;

	VectorNormalize( vecDir );


//=============================================================================
// HPE_BEGIN:
//=============================================================================

#ifndef CLIENT_DLL
	// [pfreese] Track number player entities killed with this bullet
	int iPenetrationKills = 0;

	// [menglish] Increment the shots fired for this player
	CCS_GameStats.Event_ShotFired( this, GetActiveWeapon() );
#endif

//=============================================================================
// HPE_END
//=============================================================================

	bool bFirstHit = true;

	CBasePlayer *lastPlayerHit = NULL;
    MDLCACHE_CRITICAL_SECTION();

	// Only show for one bullet.
    bool shouldDebugHitboxesOnFire = m_pCurrentCommand->debug_hitboxes & CUserCmd::DEBUG_HITBOXES_ON_FIRE && iBullet == 0;
    bool shouldDebugHitboxesOnHit = m_pCurrentCommand->debug_hitboxes & CUserCmd::DEBUG_HITBOXES_ON_HIT;

    // TODO_ENHANCED:
    // Send only to only one client,
    // others clients don't need the information on how other players lag compensated players.
#ifdef CLIENT_DLL
    static ConVarRef cl_showfirebullethitboxes("cl_showfirebullethitboxes");
    static ConVarRef cl_showimpacts("cl_showimpacts");
    shouldDebugHitboxesOnHit = shouldDebugHitboxesOnHit && (cl_showimpacts.GetInt() == 1 || cl_showimpacts.GetInt() == 2);
    shouldDebugHitboxesOnFire = shouldDebugHitboxesOnFire && (cl_showfirebullethitboxes.GetInt() == 1 || cl_showfirebullethitboxes.GetInt() == 2);
#endif

#ifndef CLIENT_DLL
	auto WritePlayerHitboxEvent = [&]( CBasePlayer* lagPlayer )
	{
		IGameEvent* event = gameeventmanager->CreateEvent( "bullet_player_hitboxes" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "player_index", lagPlayer->entindex() );
			event->SetInt( "tickbase", TIME_TO_TICKS( GetTimeBase() ) );
			event->SetInt( "bullet", iBullet );

			event->SetFloat( "simtime", lagPlayer->GetSimulationTime() );
			event->SetFloat( "animtime", lagPlayer->GetAnimTime() );

			Vector positions[MAXSTUDIOBONES];
			QAngle angles[MAXSTUDIOBONES];
			int indexes[MAXSTUDIOBONES];

			auto angle	  = lagPlayer->GetLocalAngles();
			auto position = lagPlayer->GetLocalOrigin();

			event->SetFloat( "position_x", position.x );
			event->SetFloat( "position_y", position.y );
			event->SetFloat( "position_z", position.z );

			event->SetFloat( "angle_x", angle.x );
			event->SetFloat( "angle_y", angle.y );
			event->SetFloat( "angle_z", angle.z );

			event->SetFloat( "cycle", lagPlayer->GetCycle() );
			event->SetInt( "sequence", lagPlayer->GetSequence() );

			int numhitboxes = lagPlayer->GetServerHitboxes( positions, angles, indexes );
			event->SetInt( "num_hitboxes", numhitboxes );

			for ( int i = 0; i < numhitboxes; i++ )
			{
				char buffer[256];
				V_sprintf_safe( buffer, "hitbox_index_%i", i );
				event->SetInt( buffer, indexes[i] );
				V_sprintf_safe( buffer, "hitbox_position_x_%i", i );
				event->SetFloat( buffer, positions[indexes[i]].x );
				V_sprintf_safe( buffer, "hitbox_position_y_%i", i );
				event->SetFloat( buffer, positions[indexes[i]].y );
				V_sprintf_safe( buffer, "hitbox_position_z_%i", i );
				event->SetFloat( buffer, positions[indexes[i]].z );
				V_sprintf_safe( buffer, "hitbox_angle_x_%i", i );
				event->SetFloat( buffer, angles[indexes[i]].x );
				V_sprintf_safe( buffer, "hitbox_angle_y_%i", i );
				event->SetFloat( buffer, angles[indexes[i]].y );
				V_sprintf_safe( buffer, "hitbox_angle_z_%i", i );
				event->SetFloat( buffer, angles[indexes[i]].z );
			}

			auto model = lagPlayer->GetModelPtr();

			auto numposeparams = model->GetNumPoseParameters();
			event->SetInt( "num_poseparams", numposeparams );

			for ( int i = 0; i < numposeparams; i++ )
			{
				char buffer[256];
				V_sprintf_safe( buffer, "pose_param_%i", i );
				event->SetFloat( buffer, lagPlayer->GetPoseParameterArray()[i] );
			}

			auto numbonecontrollers = model->GetNumBoneControllers();
			event->SetInt( "num_bonecontrollers", numbonecontrollers );

			for ( int i = 0; i < numbonecontrollers; i++ )
			{
				char buffer[256];
				V_sprintf_safe( buffer, "bone_controller_%i", i );
				event->SetFloat( buffer, lagPlayer->GetBoneControllerArray()[i] );
			}

			auto numanimoverlays = lagPlayer->GetNumAnimOverlays();
			event->SetInt( "num_anim_overlays", numanimoverlays );

			for ( int i = 0; i < numanimoverlays; i++ )
			{
				auto animOverlay = lagPlayer->GetAnimOverlay( i );

				char buffer[256];
				V_sprintf_safe( buffer, "anim_overlay_cycle_%i", i );
				event->SetFloat( buffer, animOverlay->m_flCycle.Get() );

				V_sprintf_safe( buffer, "anim_overlay_sequence_%i", i );
				event->SetInt( buffer, animOverlay->m_nSequence.Get() );

				V_sprintf_safe( buffer, "anim_overlay_weight_%i", i );
				event->SetFloat( buffer, animOverlay->m_flWeight.Get() );

				V_sprintf_safe( buffer, "anim_overlay_order_%i", i );
				event->SetInt( buffer, animOverlay->m_nOrder.Get() );

				V_sprintf_safe( buffer, "anim_overlay_flags_%i", i );
				event->SetInt( buffer, animOverlay->m_fFlags.Get() );
			}

			gameeventmanager->FireEvent( event );
		}
	};
#endif

	if ( shouldDebugHitboxesOnFire )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer* lagPlayer = UTIL_PlayerByIndex( i );
			if ( lagPlayer && lagPlayer != this )
			{
#ifdef CLIENT_DLL
				if ( !m_pCurrentCommand->hasbeenpredicted )
				{
					lagPlayer->DrawClientHitboxes( cl_debug_duration.GetFloat(), true );
				}
#else
				WritePlayerHitboxEvent( lagPlayer );
#endif
			}
		}
	}

	while ( fCurrentDamage > 0 )
	{
		Vector vecEnd = vecSrc + vecDir * flDistance;

		trace_t tr; // main enter bullet trace

		UTIL_TraceLineIgnoreTwoEntities(vecSrc, vecEnd, vecBulletRadiusMins, vecBulletRadiusMaxs, CS_MASK_SHOOT|CONTENTS_HITBOX, this, lastPlayerHit, COLLISION_GROUP_NONE, &tr );
		{
			CTraceFilterSkipTwoEntities filter( this, lastPlayerHit, COLLISION_GROUP_NONE );

			// Check for player hitboxes extending outside their collision bounds
			const float rayExtension = 40.0f;
			UTIL_ClipTraceToPlayersHull(vecSrc, vecEnd + vecDir * rayExtension, vecBulletRadiusMins, vecBulletRadiusMaxs, CS_MASK_SHOOT|CONTENTS_HITBOX, &filter, &tr );
		}

		lastPlayerHit = ToBasePlayer(tr.m_pEnt);

// #ifdef GAME_DLL
// 		DevMsg("1: %i %f %f %f\n", iPenetration, flPenetrationDistance, flCurrentDistance, flPenetrationPower);
// #endif
		if ( tr.fraction == 1.0f )
			break; // we didn't hit anything, stop tracing shoot

#ifdef _DEBUG
		if ( bFirstHit )
			AddBulletStat( gpGlobals->realtime, VectorLength( vecSrc-tr.endpos), tr.endpos );
#endif

		bFirstHit = false;

		/************* MATERIAL DETECTION ***********/
		surfacedata_t *pSurfaceData = physprops->GetSurfaceData( tr.surface.surfaceProps );
		int iEnterMaterial = pSurfaceData->game.material;

		GetMaterialParameters( iEnterMaterial, flPenetrationModifier, flDamageModifier );

		bool hitGrate = tr.contents & CONTENTS_GRATE;

		// since some railings in de_inferno are CONTENTS_GRATE but CHAR_TEX_CONCRETE, we'll trust the
		// CONTENTS_GRATE and use a high damage modifier.
		if ( hitGrate )
		{
			// If we're a concrete grate (TOOLS/TOOLSINVISIBLE texture) allow more penetrating power.
			flPenetrationModifier = 1.0f;
			flDamageModifier = 0.99f;
        }

		if (is50bmg && iEnterMaterial >= 'A')
		{
			flPenetrationModifier = 1.0f;
		}

		if ( shouldDebugHitboxesOnHit )
		{
#ifdef CLIENT_DLL
			if ( !m_pCurrentCommand->hasbeenpredicted )
			{
				DrawBullet( vecSrc,
							tr.endpos,
							vecBulletRadiusMins,
							vecBulletRadiusMaxs,
							0,
							255,
							0,
							127,
							cl_debug_duration.GetFloat() );
			}
#else
			IGameEvent* event = gameeventmanager->CreateEvent( "bullet_impact" );
			if ( event )
			{
				event->SetInt( "userid", GetUserID() );
				event->SetFloat( "src_x", vecSrc.x );
				event->SetFloat( "src_y", vecSrc.y );
				event->SetFloat( "src_z", vecSrc.z );
				event->SetFloat( "dst_x", tr.endpos.x );
				event->SetFloat( "dst_y", tr.endpos.y );
				event->SetFloat( "dst_z", tr.endpos.z );
				event->SetFloat( "radius", flBulletRadius );
				event->SetInt( "tickbase", TIME_TO_TICKS( GetTimeBase() ) );
				event->SetInt( "bullet", iBullet );
				gameeventmanager->FireEvent( event );
			}
#endif
			if ( tr.m_pEnt && tr.m_pEnt->IsPlayer() && !shouldDebugHitboxesOnFire )
			{
				const auto lagPlayer = UTIL_PlayerByIndex( tr.m_pEnt->entindex() );
#ifdef CLIENT_DLL
				if ( !m_pCurrentCommand->hasbeenpredicted )
				{
					lagPlayer->DrawClientHitboxes( cl_debug_duration.GetFloat(), true );
				}
#else
				WritePlayerHitboxEvent( lagPlayer );
#endif
			}
		}

		//calculate the damage based on the distance the bullet travelled.
		flCurrentDistance += tr.fraction * flDistance;
		fCurrentDamage *= pow (flRangeModifier, (flCurrentDistance / 500));

		// check if we reach penetration distance, no more penetrations after that
		if (flCurrentDistance > flPenetrationDistance && iPenetration > 0)
			iPenetration = 0;

#ifndef CLIENT_DLL
		// This just keeps track of sounds for AIs (it doesn't play anything).
		CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, tr.endpos, 400, 0.2f, this );
#endif

		int iDamageType = DMG_BULLET | DMG_NEVERGIB;

		if( bDoEffects )
		{
			// See if the bullet ended up underwater + started out of the water
			if ( enginetrace->GetPointContents( tr.endpos ) & (CONTENTS_WATER|CONTENTS_SLIME) )
			{
				trace_t waterTrace;
				UTIL_TraceHull( vecSrc, tr.endpos, vecBulletRadiusMins, vecBulletRadiusMaxs, (MASK_SHOT|CONTENTS_WATER|CONTENTS_SLIME), this, COLLISION_GROUP_NONE, &waterTrace );

				if( waterTrace.allsolid != 1 )
				{
					CEffectData	data;
 					data.m_vOrigin = waterTrace.endpos;
					data.m_vNormal = waterTrace.plane.normal;
					data.m_flScale = random->RandomFloat( 8, 12 );

					if ( waterTrace.contents & CONTENTS_SLIME )
					{
						data.m_fFlags |= FX_WATER_IN_SLIME;
					}

					DispatchEffect( "gunshotsplash", data );
				}
			}
			else
			{
				//Do Regular hit effects

				// Don't decal nodraw surfaces
				if ( !( tr.surface.flags & (SURF_SKY|SURF_NODRAW|SURF_HINT|SURF_SKIP) ) )
				{
					CBaseEntity *pEntity = tr.m_pEnt;
					if ( !( !friendlyfire.GetBool() && pEntity && pEntity->GetTeamNumber() == GetTeamNumber() ) )
					{
						UTIL_ImpactTrace( &tr, iDamageType );
					}
				}
			}
		} // bDoEffects

		// add damage to entity that we hit

#ifndef CLIENT_DLL
		ClearMultiDamage();

		//=============================================================================
		// HPE_BEGIN:
		// [pfreese] Check if enemy players were killed by this bullet, and if so,
		// add them to the iPenetrationKills count
		//=============================================================================
		
		CBaseEntity *pEntity = tr.m_pEnt;

		CTakeDamageInfo info( pevAttacker, pevAttacker, fCurrentDamage, iDamageType );
		CalculateBulletDamageForce( &info, iBulletType, vecDir, tr.endpos );
		pEntity->DispatchTraceAttack( info, vecDir, &tr );

		bool bWasAlive = pEntity->IsAlive();

		TraceAttackToTriggers( info, tr.startpos, tr.endpos, vecDir );

		ApplyMultiDamage();

		if (bWasAlive && !pEntity->IsAlive() && pEntity->IsPlayer() && pEntity->GetTeamNumber() != GetTeamNumber())
		{
			++iPenetrationKills;
		}
		
		//=============================================================================
		// HPE_END
		//=============================================================================

#endif

// #ifdef GAME_DLL
// 		DevMsg("2: %i %f %f %f\n", iPenetration, flPenetrationDistance, flCurrentDistance, flPenetrationPower);
// #endif
		// check if bullet can penetrate another entity
		if ( iPenetration == 0 && !hitGrate )
			break; // no, stop

		// If we hit a grate with iPenetration == 0, stop on the next thing we hit
		if ( iPenetration < 0 )
			break;

// #ifdef GAME_DLL
// 		DevMsg("3: %i %f %f %f\n", iPenetration, flPenetrationDistance, flCurrentDistance, flPenetrationPower);
// #endif
		Vector penetrationEnd;

		float flMaxPenetrationDistance = is50bmg ? 1024.0f : 128.0f;

		// try to penetrate object, maximum penetration is 128 inch
		if ( !TraceToExit( tr.endpos, vecDir, penetrationEnd, 24, flMaxPenetrationDistance ) )
			break;

// #ifdef GAME_DLL
// 		DevMsg("4: %i %f %f %f\n", iPenetration, flPenetrationDistance, flCurrentDistance, flPenetrationPower);
// #endif
		// find exact penetration exit
		trace_t exitTr;
		UTIL_TraceHull( penetrationEnd, tr.endpos, vecBulletRadiusMins, vecBulletRadiusMaxs, CS_MASK_SHOOT|CONTENTS_HITBOX, NULL, &exitTr );

		if( exitTr.m_pEnt != tr.m_pEnt && exitTr.m_pEnt != NULL )
		{
			// something was blocking, trace again
			UTIL_TraceHull( penetrationEnd, tr.endpos, vecBulletRadiusMins, vecBulletRadiusMaxs, CS_MASK_SHOOT|CONTENTS_HITBOX, exitTr.m_pEnt, COLLISION_GROUP_NONE, &exitTr );
		}

		// get material at exit point
		pSurfaceData = physprops->GetSurfaceData( exitTr.surface.surfaceProps );
		int iExitMaterial = pSurfaceData->game.material;

		hitGrate = hitGrate && ( exitTr.contents & CONTENTS_GRATE );

		// if enter & exit point is wood or metal we assume this is
		// a hollow crate or barrel and give a penetration bonus
		if ( iEnterMaterial == iExitMaterial )
		{
			if( iExitMaterial == CHAR_TEX_WOOD ||
				iExitMaterial == CHAR_TEX_METAL )
			{
				flPenetrationModifier *= 2;
			}
		}

		float flTraceDistance = VectorLength( exitTr.endpos - tr.endpos );

		// check if bullet has enough power to penetrate this distance for this material
		if ( flTraceDistance > ( flPenetrationPower * flPenetrationModifier ) )
			break; // bullet hasn't enough power to penetrate this distance

// #ifdef GAME_DLL
// 		DevMsg("5: %i %f %f %f\n", iPenetration, flPenetrationDistance, flCurrentDistance, flPenetrationPower);
// #endif
		// penetration was successful

		// bullet did penetrate object, exit Decal
		if ( bDoEffects )
		{
			UTIL_ImpactTrace( &exitTr, iDamageType );
		}

		//setup new start end parameters for successive trace

		flPenetrationPower -= flTraceDistance / flPenetrationModifier;
		flCurrentDistance += flTraceDistance;

		// NDebugOverlay::Box( exitTr.endpos, Vector(-2,-2,-2), Vector(2,2,2), 0,255,0,127, 8 );

		vecSrc = exitTr.endpos;
		flDistance = (flDistance - flCurrentDistance) * 0.5;

		// reduce damage power each time we hit something other than a grate
		fCurrentDamage *= flDamageModifier;

		// reduce penetration counter
		iPenetration--;
	}

#ifndef CLIENT_DLL
	//=============================================================================
	// HPE_BEGIN:
	// [pfreese] If we killed at least two enemies with a single bullet, award the
	// TWO_WITH_ONE_SHOT achievement
	//=============================================================================
	
	if (iPenetrationKills >= 2)
	{
		AwardAchievement(CSKillTwoWithOneShot);
	}
	
	//=============================================================================
	// HPE_END
	//=============================================================================
#endif
}


void CCSPlayer::UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity  )
{
	float speedSqr = vecVelocity.AsVector2D().LengthSqr();

	// the fastest walk is 135 ( scout ), see CCSGameMovement::CheckParameters()
	if ( speedSqr < 150.0 * 150.0 ) 
		return; // player is not running, no footsteps

	BaseClass::UpdateStepSound( psurface, vecOrigin, vecVelocity  );
}


// GOOSEMAN : Kick the view..
void CCSPlayer::KickBack( float up_base, float lateral_base, float up_modifier, float lateral_modifier, float up_max, float lateral_max, int direction_change )
{
	float flKickUp;
	float flKickLateral;

	if (m_iShotsFired == 1) // This is the first round fired
	{
		flKickUp = up_base;
		flKickLateral = lateral_base;
	}
	else
	{
		flKickUp = up_base + m_iShotsFired*up_modifier;
		flKickLateral = lateral_base + m_iShotsFired*lateral_modifier;
	}


	QAngle angle = GetPunchAngle();

	angle.x -= flKickUp;
	if ( angle.x < -1 * up_max )
		angle.x = -1 * up_max;

	if ( m_iDirection == 1 )
	{
		angle.y += flKickLateral;
		if (angle.y > lateral_max)
			angle.y = lateral_max;
	}
	else
	{
		angle.y -= flKickLateral;
		if ( angle.y < -1 * lateral_max )
			angle.y = -1 * lateral_max;
	}

	if ( !SharedRandomInt( "KickBack", 0, direction_change ) )
		m_iDirection = 1 - m_iDirection;

	SetPunchAngle( angle );
}


bool CCSPlayer::CanMove() const
{
	// When we're in intro camera mode, it's important to return false here
	// so our physics object doesn't fall out of the world.
	if ( GetMoveType() == MOVETYPE_NONE )
		return false;

	if ( IsObserver() )
		return true; // observers can move all the time

	bool bValidMoveState = (State_Get() == STATE_ACTIVE || State_Get() == STATE_OBSERVER_MODE);

	if ( m_bIsDefusing || !bValidMoveState || CSGameRules()->IsFreezePeriod() )
	{
		return false;
	}
	else
	{
		// Can't move while planting C4.
		CC4 *pC4 = dynamic_cast< CC4* >( GetActiveWeapon() );
		if ( pC4 && pC4->m_bStartedArming )
			return false;

		return true;
	}
}


void CCSPlayer::OnJump( float fImpulse )
{
	CWeaponCSBase* pActiveWeapon = GetActiveCSWeapon();
	if ( pActiveWeapon != NULL )
		pActiveWeapon->OnJump(fImpulse);
}


void CCSPlayer::OnLand( float fVelocity )
{
	CWeaponCSBase* pActiveWeapon = GetActiveCSWeapon();
	if ( pActiveWeapon != NULL )
		pActiveWeapon->OnLand(fVelocity);
}


//-------------------------------------------------------------------------------------------------------------------------------
/**
* Track the last time we were on a ladder, along with the ladder's normal and where we
* were grabbing it, so we don't reach behind us and grab it again as we are trying to
* dismount.
*/
void CCSPlayer::SurpressLadderChecks( const Vector& pos, const Vector& normal )
{
	m_ladderSurpressionTimer.Start( 1.0f );
	m_lastLadderPos = pos;
	m_lastLadderNormal = normal;
}


//-------------------------------------------------------------------------------------------------------------------------------
/**
* Prevent us from re-grabbing the same ladder we were just on:
*  - if the timer is elapsed, let us grab again
*  - if the normal is different, let us grab
*  - if the 2D pos is very different, let us grab, since it's probably a different ladder
*/
bool CCSPlayer::CanGrabLadder( const Vector& pos, const Vector& normal )
{
	if ( m_ladderSurpressionTimer.GetRemainingTime() <= 0.0f )
	{
		return true;
	}

	const float MaxDist = 64.0f;
	if ( pos.AsVector2D().DistToSqr( m_lastLadderPos.AsVector2D() ) < MaxDist * MaxDist )
	{
		return false;
	}

	if ( normal != m_lastLadderNormal )
	{
		return true;
	}

	return false;
}


void CCSPlayer::SetAnimation( PLAYER_ANIM playerAnim )
{
	// In CS, its CPlayerAnimState object manages ALL the animation state.
	return;
}


CWeaponCSBase* CCSPlayer::CSAnim_GetActiveWeapon()
{
	return GetActiveCSWeapon();
}


bool CCSPlayer::CSAnim_CanMove()
{
	return CanMove();
}

//--------------------------------------------------------------------------------------------------------------

#define MATERIAL_NAME_LENGTH 16

#ifdef GAME_DLL

class CFootstepControl : public CBaseTrigger
{
public:
	DECLARE_CLASS( CFootstepControl, CBaseTrigger );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	virtual int UpdateTransmitState( void );
	virtual void Spawn( void );

	CNetworkVar( string_t, m_source );
	CNetworkVar( string_t, m_destination );
};

LINK_ENTITY_TO_CLASS( func_footstep_control, CFootstepControl );


BEGIN_DATADESC( CFootstepControl )
	DEFINE_KEYFIELD( m_source, FIELD_STRING, "Source" ),
	DEFINE_KEYFIELD( m_destination, FIELD_STRING, "Destination" ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CFootstepControl, DT_FootstepControl )
	SendPropStringT( SENDINFO(m_source) ),
	SendPropStringT( SENDINFO(m_destination) ),
END_SEND_TABLE()

int CFootstepControl::UpdateTransmitState( void )
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CFootstepControl::Spawn( void )
{
	InitTrigger();
}

#else

//--------------------------------------------------------------------------------------------------------------

class C_FootstepControl : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_FootstepControl, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_FootstepControl( void );
	~C_FootstepControl();

	char m_source[MATERIAL_NAME_LENGTH];
	char m_destination[MATERIAL_NAME_LENGTH];
};

IMPLEMENT_CLIENTCLASS_DT(C_FootstepControl, DT_FootstepControl, CFootstepControl)
	RecvPropString( RECVINFO(m_source) ),
	RecvPropString( RECVINFO(m_destination) ),
END_RECV_TABLE()

CUtlVector< C_FootstepControl * > s_footstepControllers;

C_FootstepControl::C_FootstepControl( void )
{
	s_footstepControllers.AddToTail( this );
}

C_FootstepControl::~C_FootstepControl()
{
	s_footstepControllers.FindAndRemove( this );
}

surfacedata_t * CCSPlayer::GetFootstepSurface( const Vector &origin, const char *surfaceName )
{
	for ( int i=0; i<s_footstepControllers.Count(); ++i )
	{
		C_FootstepControl *control = s_footstepControllers[i];

		if ( FStrEq( control->m_source, surfaceName ) )
		{
			if ( control->CollisionProp()->IsPointInBounds( origin ) )
			{
				return physprops->GetSurfaceData( physprops->GetSurfaceIndex( control->m_destination ) );
			}
		}
	}

	return physprops->GetSurfaceData( physprops->GetSurfaceIndex( surfaceName ) );
}

#endif


