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
ConVar weapon_accuracy_noinaccuracy( "weapon_accuracy_noinaccuracy", "0", FCVAR_REPLICATED | FCVAR_NOTIFY );

#ifdef CLIENT_DLL
ConVar debug_screenshot_bullet_position("debug_screenshot_bullet_position", "0");
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
    if (weapon_accuracy_noinaccuracy.GetBool())
    {
        fInaccuracy = 0.0f;
    }

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
// 		vHookedOrigin = VectorLerp( pPlayer->m_vecPreviousEyePosition, vOrigin, playerCmd->interpolated_amount );
// 	}

// #ifndef CLIENT_DLL
// 	DevMsg("server new shoot pos: %f %f %f - %f, has command: %s\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z, playerCmd->interpolated_amount, playerCmd ? "true" : "false" );
// #else
// 	DevMsg("client new shoot pos: %f %f %f - %f, has command: %s\n", vHookedOrigin.x, vHookedOrigin.y, vHookedOrigin.z, playerCmd->interpolated_amount, playerCmd ? "true" : "false" );
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
    static ConVarRef cl_showfirebullethitboxes("cl_showfirebullethitboxes");
	static ConVarRef cl_showimpacts( "cl_showimpacts" );

	if ( playerCmd && !playerCmd->hasbeenpredicted && ( cl_showfirebullethitboxes.GetBool() || cl_showimpacts.GetBool() ) )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			auto lagPlayer = ( C_CSPlayer* )UTIL_PlayerByIndex( i );

			if ( !lagPlayer )
			{
				continue;
			}

			C_CSPlayer::HitboxRecord record;

			record.m_vecRenderOrigin = lagPlayer->GetRenderOrigin();
			record.m_angRenderAngles = lagPlayer->GetRenderAngles();

			record.m_nAttackerTickBase = pPlayer->m_nTickBase;
			record.m_flSimulationTime  = lagPlayer->m_flInterpolatedSimulationTime;
			record.m_flAnimTime		   = lagPlayer->GetAnimTime();
			record.m_flCycle		   = lagPlayer->GetCycle();
			record.m_nSequence		   = lagPlayer->GetSequence();

			lagPlayer->GetPoseParameters( lagPlayer->GetModelPtr(), record.m_flPoseParameters );
			lagPlayer->GetBoneControllers( record.m_flEncodedControllers );

			for ( int i = 0; i < lagPlayer->GetNumAnimOverlays(); i++ )
			{
				CAnimationLayer* layer = lagPlayer->GetAnimOverlay( i );

				if ( layer )
				{
					record.m_AnimationLayer[i].m_flCycle   = layer->m_flCycle;
					record.m_AnimationLayer[i].m_nOrder	   = layer->m_nOrder;
					record.m_AnimationLayer[i].m_nSequence = layer->m_nSequence;
					record.m_AnimationLayer[i].m_flWeight  = layer->m_flWeight;
					record.m_AnimationLayer[i].m_fFlags	   = layer->m_fFlags;
				}
			}

			pPlayer->m_HitboxTrack[lagPlayer->index].Push( record );
		}
	}
#endif

	for ( int iBullet=0; iBullet < pWeaponInfo->m_iBullets; iBullet++ )
    {
#ifdef CLIENT_DLL
        if (pPlayer->IsLocalPlayer() && debug_screenshot_bullet_position.GetBool())
        {
            gpGlobals->client_taking_screenshot = true;
        }
#endif

		RandomSeed( iSeed + iBullet ); // init random system with this seed

		float fTheta1  = RandomFloat( 0.0f, 2.0f * M_PI );
		float fRadius1 = RandomFloat( 0.0f, fSpread );
		float x1	   = fRadius1 * cosf( fTheta1 );
		float y1	   = fRadius1 * sinf( fTheta1 );

		// Always straight for the first bullet;
		if ( weapon_accuracy_noinaccuracy.GetBool() && iBullet == 0 )
		{
			x0 = 0.0f;
			y0 = 0.0f;
			x1 = 0.0f;
			y1 = 0.0f;
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
							 x0 + x1,
							 y0 + y1 );
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

