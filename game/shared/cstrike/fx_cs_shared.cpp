//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "fx_cs_shared.h"
#include "convar.h"
#include "mathlib/vector.h"
#include "usercmd.h"
#include "util_shared.h"
#include "weapon_csbase.h"

#ifndef CLIENT_DLL
    #include "ilagcompensationmanager.h"
#endif

ConVar weapon_accuracy_logging( "weapon_accuracy_logging", "0", FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY | FCVAR_ARCHIVE );

#ifdef CLIENT_DLL
extern ConVar cl_bullet_debugger_shoot_position_rendering_trace;
extern ConVar cl_bullet_debugger_enable;
#include "fx_impact.h"

	// this is a cheap ripoff from CBaseCombatWeapon::WeaponSound():
	void FX_WeaponSound(
		int iPlayerIndex,
		WeaponSound_t sound_type,
		const Vector &vOrigin,
		CCSWeaponInfo *pWeaponInfo, float flSoundTime )
	{

		// If we have some sounds from the weapon classname.txt file, play a random one of them
		const char *shootsound = pWeaponInfo->aShootSounds[ sound_type ];
		if ( !shootsound || !shootsound[0] )
			return;

		CBroadcastRecipientFilter filter; // this is client side only
		if ( !te->CanPredict() )
			return;

		CBaseEntity::EmitSound( filter, iPlayerIndex, shootsound, &vOrigin, flSoundTime );
	}

	class CGroupedSound
	{
	public:
		string_t m_SoundName;
		Vector m_vPos;
	};

	CUtlVector<CGroupedSound> g_GroupedSounds;


	// Called by the ImpactSound function.
	void ShotgunImpactSoundGroup( const char *pSoundName, const Vector &vEndPos )
	{
		int i;
		// Don't play the sound if it's too close to another impact sound.
		for ( i=0; i < g_GroupedSounds.Count(); i++ )
		{
			CGroupedSound *pSound = &g_GroupedSounds[i];

			if ( vEndPos.DistToSqr( pSound->m_vPos ) < 300*300 )
			{
				if ( Q_stricmp( pSound->m_SoundName, pSoundName ) == 0 )
					return;
			}
		}

		// Ok, play the sound and add it to the list.
		CLocalPlayerFilter filter;
		C_BaseEntity::EmitSound( filter, NULL, pSoundName, &vEndPos );

		i = g_GroupedSounds.AddToTail();
		g_GroupedSounds[i].m_SoundName = pSoundName;
		g_GroupedSounds[i].m_vPos = vEndPos;
	}


	void StartGroupingSounds()
	{
		Assert( g_GroupedSounds.Count() == 0 );
		SetImpactSoundRoute( ShotgunImpactSoundGroup );
	}


	void EndGroupingSounds()
	{
		g_GroupedSounds.Purge();
		SetImpactSoundRoute( NULL );
	}

#else

	#include "te_shotgun_shot.h"

	// Server doesn't play sounds anyway.
	void StartGroupingSounds() {}
	void EndGroupingSounds() {}
	void FX_WeaponSound ( int iPlayerIndex,
		WeaponSound_t sound_type,
		const Vector &vOrigin,
		CCSWeaponInfo *pWeaponInfo, float flSoundTime ) {};

#endif


// This runs on both the client and the server.
// On the server, it only does the damage calculations.
// On the client, it does all the effects.
void FX_FireBullets(
	int	iPlayerIndex,
	const Vector &vOrigin,
	const QAngle &vAngles,
	int	iWeaponID,
	int	iMode,
	int iSeed,
	float fInaccuracy,
	float fSpread,
	float flSoundTime
	)
{
	bool bDoEffects = true;

#ifdef CLIENT_DLL
	C_CSPlayer *pPlayer = ToCSPlayer( ClientEntityList().GetBaseEntity( iPlayerIndex ) );
#else
	CCSPlayer *pPlayer = ToCSPlayer( UTIL_PlayerByIndex( iPlayerIndex) );
#endif

// #ifndef CLIENT_DLL
// 	DevMsg("server original shoot pos: %f %f %f\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z );
// #else
// 	DevMsg("client original shoot pos: %f %f %f\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z );
// #endif
	CUserCmd* playerCmd = NULL;

	if ( pPlayer )
	{
#ifdef CLIENT_DLL
		playerCmd = pPlayer->m_pCurrentCommand;
#else
		playerCmd = pPlayer->GetCurrentCommand();
#endif
	}

// 	if ( playerCmd )
// 	{
// 		vHookedOrigin = VectorLerp( pPlayer->m_vecPreviousEyePosition, vOrigin, playerCmd->interpolated_amount_frac );
// 	}

// #ifndef CLIENT_DLL
// 	DevMsg("server new shoot pos: %f %f %f - %f, has command: %s\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z, playerCmd->interpolated_amount_frac, playerCmd ? "true" : "false" );
// #else
// 	DevMsg("client new shoot pos: %f %f %f - %f, has command: %s\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z, playerCmd->interpolated_amount_frac, playerCmd ? "true" : "false" );
// #endif
	const char * weaponAlias =	WeaponIDToAlias( iWeaponID );

	if ( !weaponAlias )
	{
		DevMsg("FX_FireBullets: weapon alias for ID %i not found\n", iWeaponID );
		return;
	}

#if !defined(CLIENT_DLL)
	if ( weapon_accuracy_logging.GetBool() )
	{
		char szFlags[256];

		V_strcpy(szFlags, " ");

// #if defined(CLIENT_DLL)
// 		V_strcat(szFlags, "CLIENT ", sizeof(szFlags));
// #else
// 		V_strcat(szFlags, "SERVER ", sizeof(szFlags));
// #endif
//
		if ( pPlayer->GetMoveType() == MOVETYPE_LADDER )
			V_strcat(szFlags, "LADDER ", sizeof(szFlags));

		if ( FBitSet( pPlayer->GetFlags(), FL_ONGROUND ) )
			V_strcat(szFlags, "GROUND ", sizeof(szFlags));

		if ( FBitSet( pPlayer->GetFlags(), FL_DUCKING) )
			V_strcat(szFlags, "DUCKING ", sizeof(szFlags));

		float fVelocity = pPlayer->GetAbsVelocity().Length2D();

		Msg("FireBullets @ %10f [ %s ]: inaccuracy=%f  spread=%f  max dispersion=%f  mode=%2i  vel=%10f  seed=%3i  %s\n",
			gpGlobals->curtime, weaponAlias, fInaccuracy, fSpread, fInaccuracy + fSpread, iMode, fVelocity, iSeed, szFlags);
	}
#endif

	char wpnName[128];
	Q_snprintf( wpnName, sizeof( wpnName ), "weapon_%s", weaponAlias );
	WEAPON_FILE_INFO_HANDLE	hWpnInfo = LookupWeaponInfoSlot( wpnName );

	if ( hWpnInfo == GetInvalidWeaponInfoHandle() )
	{
		DevMsg("FX_FireBullets: LookupWeaponInfoSlot failed for weapon %s\n", wpnName );
		return;
	}

	CCSWeaponInfo *pWeaponInfo = static_cast< CCSWeaponInfo* >( GetFileWeaponInfoFromHandle( hWpnInfo ) );

#ifndef CLIENT_DLL
	// Do the firing animation event.
	if ( pPlayer && !pPlayer->IsDormant() )
	{
		if ( iMode == Primary_Mode )
			pPlayer->GetPlayerAnimState()->DoAnimationEvent( PLAYERANIMEVENT_FIRE_GUN_PRIMARY );
		else
			pPlayer->GetPlayerAnimState()->DoAnimationEvent( PLAYERANIMEVENT_FIRE_GUN_SECONDARY );
	}

	// if this is server code, send the effect over to client as temp entity
	// Dispatch one message for all the bullet impacts and sounds.
	TE_FireBullets(
		iPlayerIndex,
		vOrigin,
		vAngles,
		iWeaponID,
		iMode,
		iSeed,
		fInaccuracy,
		fSpread
		);


	// Let the player remember the usercmd he fired a weapon on. Assists in making decisions about lag compensation.
	pPlayer->NoteWeaponFired();

	bDoEffects = false; // no effects on server
#endif

	iSeed++;

	int		iDamage = pWeaponInfo->m_iDamage;
	float	flRange = pWeaponInfo->m_flRange;
	int		iPenetration = pWeaponInfo->m_iPenetration;
	float	flRangeModifier = pWeaponInfo->m_flRangeModifier;
	int		iAmmoType = pWeaponInfo->iAmmoType;

	WeaponSound_t sound_type = SINGLE;

	// CS HACK, tweak some weapon values based on primary/secondary mode

	if ( iWeaponID == WEAPON_GLOCK )
	{
		if ( iMode == Secondary_Mode )
		{
			iDamage = 18;	// reduced power for burst shots
			flRangeModifier = 0.9f;
		}
	}
	else if ( iWeaponID == WEAPON_M4A1 )
	{
		if ( iMode == Secondary_Mode )
		{
			flRangeModifier = 0.95f; // slower bullets in silenced mode
			sound_type = SPECIAL1;
		}
	}
	else if ( iWeaponID == WEAPON_USP )
	{
		if ( iMode == Secondary_Mode )
		{
			iDamage = 30; // reduced damage in silenced mode
			sound_type = SPECIAL1;
		}
	}

	if ( bDoEffects)
	{
		FX_WeaponSound( iPlayerIndex, sound_type, vOrigin, pWeaponInfo, flSoundTime );
	}


	// Fire bullets, calculate impacts & effects

	if ( !pPlayer )
		return;

	StartGroupingSounds();

#ifdef GAME_DLL
	pPlayer->StartNewBulletGroup();
#endif

	RandomSeed( iSeed );	// init random system with this seed

	// Get accuracy displacement
	float fTheta0 = RandomFloat(0.0f, 2.0f * M_PI);
	float fRadius0 = RandomFloat(0.0f, fInaccuracy);
	float x0 = fRadius0 * cosf(fTheta0);
	float y0 = fRadius0 * sinf(fTheta0);

#ifdef CLIENT_DLL
	if ( playerCmd && !playerCmd->hasbeenpredicted && playerCmd->debug_flags != 0 )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			auto lagPlayer = ( C_CSPlayer* )UTIL_PlayerByIndex( i );

			if ( !lagPlayer )
			{
				continue;
			}

			C_CSPlayer::HitboxRecord record;

			lagPlayer->CaptureCurrentState( record );
			lagPlayer->CaptureBoneMatrix( lagPlayer->GetModelPtr(), record.m_bonePositions, record.m_boneAngles, record.m_nNumHitboxBones, record.m_hitboxBoneIndexes );
			pPlayer->m_HitboxTrack[lagPlayer->index]->Push( std::move( record ) );
		}
	}
#endif

	for ( int iBullet=0; iBullet < pWeaponInfo->m_iBullets; iBullet++ )
    {
#ifdef CLIENT_DLL
        if (playerCmd && !playerCmd->hasbeenpredicted && cl_bullet_debugger_enable.GetBool() && cl_bullet_debugger_shoot_position_rendering_trace.GetInt() > 0)
        {
            // Capture fire-time position for comparison with rendered position
            C_CSPlayer* pCSPlayer = ToCSPlayer( pPlayer );
            if ( pCSPlayer )
            {
                C_CSPlayer::ShootPositionRecord record;
                record.m_vecFirePosition = vOrigin;
                record.m_nTickBase = pCSPlayer->m_nTickBase;
                record.m_bDrawn = false;
                pCSPlayer->m_BulletShootPositionTrack.Push( std::move( record ) );
            }
        }
        if (playerCmd && !playerCmd->hasbeenpredicted && cl_bullet_debugger_enable.GetBool() && cl_bullet_debugger_shoot_position_rendering_trace.GetInt() > 1)
        {
            gpGlobals->client_taking_screenshot = true;
        }
#endif
		float flSpreadX;
		float flSpreadY;

		if ( pWeaponInfo->m_bIsFirstBulletStraight && iBullet == 0 && fInaccuracy == 0.0 )
		{
			flSpreadX = 0;
			flSpreadY = 0;
		}
		else
		{
			RandomSeed( iSeed + iBullet + 1 ); // init random system with this seed

			float fTheta1  = RandomFloat( 0.0f, 2.0f * M_PI );
			float fRadius1 = RandomFloat( 0.0f, fSpread );
			float x1	   = fRadius1 * cosf( fTheta1 );
			float y1	   = fRadius1 * sinf( fTheta1 );
			flSpreadX	   = x0 + x1;
			flSpreadY	   = y0 + y1;
		}

		pPlayer->FireBullet( iBullet,
							 vOrigin,
							 vAngles,
							 flRange,
							 iPenetration,
							 iAmmoType,
							 iDamage,
							 flRangeModifier,
							 pPlayer,
							 bDoEffects,
							 flSpreadX,
							 flSpreadY );
	}

	EndGroupingSounds();
}

// This runs on both the client and the server.
// On the server, it dispatches a TE_PlantBomb to visible clients.
// On the client, it plays the planting animation.
void FX_PlantBomb( int iPlayerIndex, const Vector &vOrigin, PlantBombOption_t option )
{
#ifndef CLIENT_DLL
	CCSPlayer *pPlayer = ToCSPlayer( UTIL_PlayerByIndex( iPlayerIndex) );

	// Do the firing animation event.
	if ( pPlayer && !pPlayer->IsDormant() )
	{
		switch ( option )
		{
		case PLANTBOMB_PLANT:
			{
				pPlayer->GetPlayerAnimState()->DoAnimationEvent( PLAYERANIMEVENT_FIRE_GUN_PRIMARY );
			}
			break;

		case PLANTBOMB_ABORT:
			{
				pPlayer->GetPlayerAnimState()->DoAnimationEvent( PLAYERANIMEVENT_CLEAR_FIRING );
			}
			break;
		}
    }

	// if this is server code, send the effect over to client as temp entity
	// Dispatch one message for all the bullet impacts and sounds.
	TE_PlantBomb( iPlayerIndex, vOrigin, option );
#endif
}

#ifndef CLIENT_DLL
void WritePlayerHitboxEvent( CBasePlayer* shooter,
							 CBasePlayer* lagPlayer,
							 const char* context,
							 int bullet,
							 float interpAmount )
{
	IGameEvent* event = gameeventmanager->CreateEvent( context );
	if ( event )
	{
		event->SetInt( "userid", shooter->GetUserID() );
		event->SetInt( "player_index", lagPlayer->entindex() );
		event->SetInt( "tickbase", TIME_TO_TICKS( shooter->GetTimeBase() ) );
		event->SetInt( "bullet", bullet );
		event->SetFloat( "interpamount", interpAmount );
		event->SetInt( "simtickc", lagPlayer->m_nSimulatedTickCount );
		event->SetInt( "animtickc", lagPlayer->m_nAnimatedTickCount );

		Vector positions[MAXSTUDIOBONES];
		QAngle angles[MAXSTUDIOBONES];
		int indexes[MAXSTUDIOBONES];

		auto angle	  = lagPlayer->GetRenderAngles();
		auto position = lagPlayer->GetAbsOrigin();

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
}
#endif

