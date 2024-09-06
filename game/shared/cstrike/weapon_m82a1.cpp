//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "weapon_csbasegun.h"


#if defined( CLIENT_DLL )

	#define CWeaponM82A1 C_WeaponM82A1
	#include "c_cs_player.h"

#else

	#include "cs_player.h"
	#include "KeyValues.h"

#endif

#define SNIPER_ZOOM_CONTEXT		"SniperRifleThink"

enum FOVContext_t
{
	FOV_SCOPE_1,
	FOV_SCOPE_2,
	FOV_SCOPE_3,
	FOV_MAX
};

static constexpr int FOVValues[FOV_MAX] = { 40, 10, 3 };

#ifdef M82A1_UNZOOM
	ConVar sv_m82a1unzoomdelay( 
			"sv_m82a1unzoomdelay",
			"1.0",
			0,
			"how many seconds to zoom the zoom up after firing",
			true, 0,	// min value
			false, 0	// max value
			);
#endif


class CWeaponM82A1 : public CWeaponCSBaseGun
{
public:
	DECLARE_CLASS( CWeaponM82A1, CWeaponCSBaseGun );
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	
	CWeaponM82A1();

	virtual void Spawn();

	virtual void PrimaryAttack();
	virtual void SecondaryAttack();

 	virtual float GetInaccuracy() const;
	virtual float GetMaxSpeed() const;
	virtual bool IsM82a1() const;
	virtual bool Reload();
	virtual bool Deploy();

	virtual CSWeaponID GetWeaponID( void ) const		{ return WEAPON_M82A1; }

private:
  	int m_iLastZoom;

#ifdef M82A1_UNZOOM
	void				UnzoomThink( void );
#endif

	CWeaponM82A1( const CWeaponM82A1 & );
};

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponM82A1, DT_WeaponM82A1 )

BEGIN_NETWORK_TABLE( CWeaponM82A1, DT_WeaponM82A1 )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeaponM82A1 )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_m82a1, CWeaponM82A1 );
PRECACHE_WEAPON_REGISTER( weapon_m82a1 );

#ifndef CLIENT_DLL

	BEGIN_DATADESC( CWeaponM82A1 )
#ifdef M82A1_UNZOOM
		DEFINE_THINKFUNC( UnzoomThink ),
#endif
	END_DATADESC()

#endif

CWeaponM82A1::CWeaponM82A1()
{
}

void CWeaponM82A1::Spawn()
{
	Precache();

	BaseClass::Spawn();
}


void CWeaponM82A1::SecondaryAttack()
{
	const float kZoomTime = 0.15f;

	CCSPlayer *pPlayer = GetPlayerOwner();

	if ( pPlayer == NULL )
	{
		Assert( pPlayer != NULL );
		return;
	}

	if ( pPlayer->GetFOV() == pPlayer->GetDefaultFOV() )
	{
			pPlayer->SetFOV( pPlayer, FOVValues[FOV_SCOPE_1], kZoomTime );
			m_weaponMode = Secondary_Mode;
			m_fAccuracyPenalty += GetCSWpnData().m_fInaccuracyAltSwitch;
	}
	else if ( pPlayer->GetFOV() == FOVValues[FOV_SCOPE_1] )
	{
			pPlayer->SetFOV( pPlayer, FOVValues[FOV_SCOPE_2], kZoomTime );
			m_weaponMode = Secondary_Mode;
	}
	else if ( pPlayer->GetFOV() == FOVValues[FOV_SCOPE_2] )
	{
			pPlayer->SetFOV( pPlayer, FOVValues[FOV_SCOPE_3], kZoomTime );
			m_weaponMode = Secondary_Mode;
	}
	else
	{
		pPlayer->SetFOV( pPlayer, pPlayer->GetDefaultFOV(), kZoomTime );
		m_weaponMode = Primary_Mode;
	}

	m_iLastZoom = pPlayer->GetFOV();

#ifndef CLIENT_DLL
	// If this isn't guarded, the sound will be emitted twice, once by the server and once by the client.
	// Let the server play it since if only the client plays it, it's liable to get played twice cause of
	// a prediction error. joy.	
	
	//=============================================================================
	// HPE_BEGIN:
	// [tj] Playing this from the player so that we don't try to play the sound outside the level.
	//=============================================================================
	if ( GetPlayerOwner() )
	{
		GetPlayerOwner()->EmitSound( "Default.Zoom" );
	}
	//=============================================================================
	// HPE_END
	//=============================================================================
	// let the bots hear the rifle zoom
	IGameEvent * event = gameeventmanager->CreateEvent( "weapon_zoom" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
#endif

	m_flNextSecondaryAttack = gpGlobals->curtime + 0.4f;
	m_zoomFullyActiveTime = gpGlobals->curtime + 0.2f;

}

float CWeaponM82A1::GetInaccuracy() const
{
	if ( weapon_accuracy_model.GetInt() == 1 )
	{
		CCSPlayer *pPlayer = GetPlayerOwner();
		if ( !pPlayer )
			return 0.0f;
	
		float fSpread = 0.0f;
	
		if ( !FBitSet( pPlayer->GetFlags(), FL_ONGROUND ) )
			fSpread = 0.85f;
	
		else if ( pPlayer->GetAbsVelocity().Length2D() > 140 )
			fSpread = 0.25f;
	
		else if ( pPlayer->GetAbsVelocity().Length2D() > 10 )
			fSpread = 0.10f;
	
		else if ( FBitSet( pPlayer->GetFlags(), FL_DUCKING ) )
			fSpread = 0.0f;
	
		else
			fSpread = 0.0f;
	
		return fSpread;
	}
	else
	{
		return BaseClass::GetInaccuracy();
	}
}

void CWeaponM82A1::PrimaryAttack()
{
	CCSPlayer *pPlayer = GetPlayerOwner();
	if ( !pPlayer )
		return;

	if ( !CSBaseGunFire( GetCSWpnData().m_flCycleTime, m_weaponMode ) )
		return;

	if ( m_weaponMode == Secondary_Mode )
	{
		pPlayer->m_iLastZoom = m_iLastZoom;
		
		#ifdef M82A1_UNZOOM
			SetContextThink( &CWeaponM82A1::UnzoomThink, gpGlobals->curtime + sv_m82a1unzoomdelay.GetFloat(), SNIPER_ZOOM_CONTEXT );
		#else
			pPlayer->m_bResumeZoom = true;
			pPlayer->SetFOV( pPlayer, pPlayer->GetDefaultFOV(), 0.1f );
			m_weaponMode = Primary_Mode;
		#endif
	}

	QAngle angle = pPlayer->GetPunchAngle();
	angle.x -= 5;
	pPlayer->SetPunchAngle( angle );
}

#ifdef M82A1_UNZOOM
void CWeaponM82A1::UnzoomThink( void )
{
	CCSPlayer *pPlayer = GetPlayerOwner();

	if (pPlayer == NULL)
	{
		Assert(pPlayer != NULL);
		return;
	}

	pPlayer->SetFOV( pPlayer, pPlayer->GetDefaultFOV(), 0.1f );
}
#endif


float CWeaponM82A1::GetMaxSpeed() const
{
	CCSPlayer *pPlayer = GetPlayerOwner();

	if (pPlayer == NULL)
	{
		Assert(pPlayer != NULL);
		return BaseClass::GetMaxSpeed();
	}

	if ( pPlayer->GetFOV() == pPlayer->GetDefaultFOV() )
	{
		return BaseClass::GetMaxSpeed();
	}
	else
	{
		// Slower speed when zoomed in.
		return 100;
	}
}


bool CWeaponM82A1::IsM82a1() const
{
	return true;
}


bool CWeaponM82A1::Reload()
{
	m_weaponMode = Primary_Mode;
	return BaseClass::Reload();
}

bool CWeaponM82A1::Deploy()
{
	// don't allow weapon switching to shortcut cycle time (quickswitch exploit)
	float fOldNextPrimaryAttack	= m_flNextPrimaryAttack;
	float fOldNextSecondaryAttack = m_flNextSecondaryAttack;

	if ( !BaseClass::Deploy() )
		return false;

	m_weaponMode = Primary_Mode;
	m_flNextPrimaryAttack	= MAX( m_flNextPrimaryAttack, fOldNextPrimaryAttack );
	m_flNextSecondaryAttack	= MAX( m_flNextSecondaryAttack, fOldNextSecondaryAttack );
	return true;
}
