#include "cbase.h"
#include "c_baseplayer.h"
#include "datamap.h"
#include "dt_recv.h"
#include "entitylist_base.h"
#include "predictable_entity.h"
#include "util_shared.h"
#include "c_trigger_teleport.h"
#include "prediction.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS(trigger_teleport, C_TriggerTeleport);

// TODO_ENHANCED: what to do if m_iLandmark changes?
BEGIN_PREDICTION_DATA(C_TriggerTeleport)
//	DEFINE_PRED_FIELD(m_iLandmark, FIELD_STRING, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA();

IMPLEMENT_CLIENTCLASS_DT(C_TriggerTeleport, DT_TriggerTeleport, CTriggerTeleport)
	RecvPropString( RECVINFO( m_iLandmark ), 0, RecvProxy_StringToStringT )
END_RECV_TABLE();

BEGIN_DATADESC( C_TriggerTeleport )

	DEFINE_KEYFIELD( m_iLandmark, FIELD_STRING, "landmark" ),

END_DATADESC()

void C_TriggerTeleport::Touch( CBaseEntity *pOther )
{
	CBaseEntity	*pentTarget = NULL;

	if (!PassesTriggerFilters(pOther))
	{
		return;
	}

	// The activator and caller are the same
	pentTarget = UTIL_FindEntityByName( pentTarget, m_target, NULL, pOther, pOther );
	if (!pentTarget)
	{
	   return;
	}

	//
	// If a landmark was specified, offset the player relative to the landmark.
	//
	CBaseEntity	*pentLandmark = NULL;
	Vector vecLandmarkOffset(0, 0, 0);

    // The activator and caller are the same
    pentLandmark = UTIL_FindEntityByName(pentLandmark, m_iLandmark, NULL, pOther, pOther );
    if (pentLandmark)
    {
        vecLandmarkOffset = pOther->GetAbsOrigin() - pentLandmark->GetAbsOrigin();
    }

	pOther->SetGroundEntity( NULL );
	
	Vector tmp = pentTarget->GetAbsOrigin();

	if (!pentLandmark && pOther->IsPlayer())
	{
		// make origin adjustments in case the teleportee is a player. (origin in center, not at feet)
		tmp.z -= pOther->WorldAlignMins().z;
	}

	//
	// Only modify the toucher's angles and zero their velocity if no landmark was specified.
	//
	const QAngle *pAngles = NULL;
	Vector *pVelocity = NULL;

#ifdef HL1_DLL
	Vector vecZero(0,0,0);		
#endif

	if (!pentLandmark && !HasSpawnFlags(SF_TELEPORT_PRESERVE_ANGLES) )
	{
		pAngles = &pentTarget->GetAbsAngles();

#ifdef HL1_DLL
		pVelocity = &vecZero;
#else
		pVelocity = NULL;	//BUGBUG - This does not set the player's velocity to zero!!!
#endif
	}

    tmp += vecLandmarkOffset;

    UTIL_SetOrigin( pOther, tmp );
    pOther->SetNetworkOrigin( tmp );

	auto pLocalPlayer = C_BasePlayer::GetLocalPlayer();

	if ( ( C_BasePlayer* )pOther == pLocalPlayer )
	{
		// TODO_ENHANCED: do we really want this?
		prediction->SetViewOrigin( tmp );
		pLocalPlayer->m_bTeleportedThisTick = true;
	}

	if (pAngles)
    {
        // if (!pOther->IsPlayer())
        {
            auto angles = *pAngles;
            pOther->SetLocalAngles( angles );
            pOther->SetNetworkAngles( angles );
        }
        // else
        {
            if ( (C_BasePlayer*)pOther == pLocalPlayer )
            {
                auto angles = *pAngles;

                // This needs to be set only once!
                if (prediction->IsFirstTimePredicted())
                {
                    engine->SetViewAngles( angles );
                }

				pLocalPlayer->m_angTeleportAngle = angles;
				NormalizeAngles( pLocalPlayer->m_angTeleportAngle );
            }
        }
    }
}

// TODO_ENHANCED: point entity can change ? If yes should be predictable... ?
class C_PointEntity : public CBaseEntity
{
public:
	DECLARE_CLASS( C_PointEntity, CBaseEntity );
    DECLARE_NETWORKCLASS();

	virtual void Spawn( void );
	virtual int	ObjectCaps( void ) { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
private:
};

IMPLEMENT_CLIENTCLASS_DT(C_PointEntity, DT_PointEntity, CPointEntity)
END_RECV_TABLE();

void C_PointEntity::Spawn( void )
{
	SetSolid( SOLID_NONE );
//	UTIL_SetSize(this, vec3_origin, vec3_origin);
}

bool C_PointEntity::KeyValue( const char *szKeyName, const char *szValue ) 
{
	if ( FStrEq( szKeyName, "mins" ) || FStrEq( szKeyName, "maxs" ) )
	{
		Warning("Warning! Can't specify mins/maxs for point entities! (%s)\n", GetClassname() );
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}

LINK_ENTITY_TO_CLASS( info_teleport_destination, C_PointEntity );