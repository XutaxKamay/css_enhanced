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
	RecvPropInt(RECVINFO(m_hszLandmark))
END_RECV_TABLE();

void C_TriggerTeleport::Touch( CBaseEntity *pOther )
{
	CBaseEntity	*pentTarget = NULL;

	if (!PassesTriggerFilters(pOther))
	{
		return;
	}

	// The activator and caller are the same
	pentTarget = UTIL_FindEntityByNameCRC( pentTarget, m_hszTarget, NULL, pOther, pOther );
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
    pentLandmark = UTIL_FindEntityByNameCRC(pentLandmark, m_hszLandmark, NULL, pOther, pOther );
	
    if (pentLandmark)
    {
		ConColorMsg(Color(0, 255, 0, 255), "C_TriggerTeleport::Touch - pentLandmark: %p\n", pentTarget);

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

    if (pAngles)
    {
        if (!pOther->IsPlayer())
        {
            pOther->SetLocalAngles( *pAngles );
        }
        else
        {
            if ((C_BasePlayer*)pOther == C_BasePlayer::GetLocalPlayer())
            {
                auto angles = *pAngles;
                prediction->SetViewOrigin( tmp );

                // This needs to be set only once!
                if (prediction->IsFirstTimePredicted())
                {
                    engine->SetViewAngles( angles );
                }
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