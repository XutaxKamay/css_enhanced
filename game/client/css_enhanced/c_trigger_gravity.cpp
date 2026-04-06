#include "cbase.h"

#include "c_trigger_gravity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( trigger_gravity, C_TriggerGravity );

BEGIN_DATADESC( C_TriggerGravity )

	// Function Pointers
	// DEFINE_FUNCTION(GravityTouch),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(C_TriggerGravity, DT_TriggerGravity, CTriggerGravity)
	RecvPropFloat( RECVINFO(m_flGravity) )
END_RECV_TABLE();

void C_TriggerGravity::Spawn( void )
{
	BaseClass::Spawn();
	InitTrigger();
	SetTouch( &C_TriggerGravity::GravityTouch );
}

void C_TriggerGravity::GravityTouch( CBaseEntity *pOther )
{
	// Only save on clients
	if ( !pOther->IsPlayer() )
		return;

	pOther->SetGravity( GetGravity() );
}