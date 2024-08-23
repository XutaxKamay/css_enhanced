#include "cbase.h"
#include "datamap.h"
#include "dt_recv.h"
#include "util_shared.h"
#include "c_trigger_push.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( trigger_push, C_TriggerPush );

// Since this is called only during creation
// we allow a small margin for prediction errors here
BEGIN_PREDICTION_DATA(C_TriggerPush)
//	DEFINE_PRED_FIELD(m_vecPushDir, FIELD_VECTOR, FTYPEDESC_INSENDTABLE),
//  DEFINE_PRED_FIELD(m_flAlternateTicksFix, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
//	DEFINE_PRED_FIELD(m_flPushSpeed, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA();

IMPLEMENT_CLIENTCLASS_DT(C_TriggerPush, DT_TriggerPush, CTriggerPush)
	RecvPropVector(RECVINFO(m_vecPushDir)),
	RecvPropFloat(RECVINFO(m_flAlternateTicksFix)),
	RecvPropFloat(RECVINFO(m_flPushSpeed)),
END_RECV_TABLE();

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOther - 
//-----------------------------------------------------------------------------
void C_TriggerPush::Touch( CBaseEntity *pOther )
{
	if ( !pOther->IsSolid() || (pOther->GetMoveType() == MOVETYPE_PUSH || pOther->GetMoveType() == MOVETYPE_NONE ) )
		return;

	if (!PassesTriggerFilters(pOther))
		return;

	// FIXME: If something is hierarchically attached, should we try to push the parent?
	if (pOther->GetMoveParent())
		return;

	// Transform the push dir into global space
	Vector vecAbsDir;
	VectorRotate( m_vecPushDir, EntityToWorldTransform(), vecAbsDir );

	// Instant trigger, just transfer velocity and remove
	if (HasSpawnFlags(SF_TRIG_PUSH_ONCE))
	{
		pOther->ApplyAbsVelocityImpulse( m_flPushSpeed * vecAbsDir );

		if ( vecAbsDir.z > 0 )
		{
			pOther->SetGroundEntity( NULL );
		}
		// UTIL_Remove( this );
		return;
	}

	switch( pOther->GetMoveType() )
	{
	case MOVETYPE_NONE:
	case MOVETYPE_PUSH:
	case MOVETYPE_NOCLIP:
		break;

	case MOVETYPE_VPHYSICS:
		{
			IPhysicsObject *pPhys = pOther->VPhysicsGetObject();
			if ( pPhys )
			{
				// UNDONE: Assume the velocity is for a 100kg object, scale with mass
				pPhys->ApplyForceCenter( m_flPushSpeed * vecAbsDir * 100.0f * gpGlobals->frametime );
				return;
			}
		}
		break;

	default:
		{
#if defined( HL2_DLL )
			// HACK HACK  HL2 players on ladders will only be disengaged if the sf is set, otherwise no push occurs.
			if ( pOther->IsPlayer() && 
				 pOther->GetMoveType() == MOVETYPE_LADDER )
			{
				if ( !HasSpawnFlags(SF_TRIG_PUSH_AFFECT_PLAYER_ON_LADDER) )
				{
					// Ignore the push
					return;
				}
			}
#endif

			Vector vecPush = (m_flPushSpeed * vecAbsDir);
			if ( pOther->GetFlags() & FL_BASEVELOCITY )
			{
				vecPush = vecPush + pOther->GetBaseVelocity();
			}
			if ( vecPush.z > 0 && (pOther->GetFlags() & FL_ONGROUND) )
			{
				pOther->SetGroundEntity( NULL );
				Vector origin = pOther->GetAbsOrigin();
				origin.z += 1.0f;
				pOther->SetAbsOrigin( origin );
			}

#ifdef HL1_DLL
			// Apply the z velocity as a force so it counteracts gravity properly
			Vector vecImpulse( 0, 0, vecPush.z * 0.025 );//magic hack number

			pOther->ApplyAbsVelocityImpulse( vecImpulse );

			// apply x, y as a base velocity so we travel at constant speed on conveyors
			vecPush.z = 0;
#endif			

			pOther->SetBaseVelocity( vecPush );
			pOther->AddFlag( FL_BASEVELOCITY );
		}
		break;
	}
}