#include "cbase.h"
#include "c_basetoggle.h"
#include "gamestringpool.h"
#include "util_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

enum togglemovetypes_t
{
	MOVE_TOGGLE_NONE = 0,
	MOVE_TOGGLE_LINEAR = 1,
	MOVE_TOGGLE_ANGULAR = 2,
};

// datadesc & recvtable from momentum
extern void RecvProxy_EffectFlags( const CRecvProxyData *pData, void *pStruct, void *pOut );
extern void RecvProxy_MoveCollide(const CRecvProxyData *pData, void *pStruct, void *pOut);
extern void RecvProxy_MoveType(const CRecvProxyData *pData, void *pStruct, void *pOut);

BEGIN_PREDICTION_DATA_NO_BASE(C_BaseToggle)
	DEFINE_PRED_TYPEDESCRIPTION(m_Collision, CCollisionProperty),
	DEFINE_PRED_FIELD(m_vecNetworkOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_angNetworkAngles, FIELD_VECTOR, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_iszDamageFilterName, FIELD_STRING, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_iName, FIELD_STRING, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_spawnflags, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_nModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_CollisionGroup, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_fEffects, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_FIELD(m_iEFlags, FIELD_INTEGER),
END_PREDICTION_DATA();

IMPLEMENT_CLIENTCLASS_DT_NOBASE(C_BaseToggle, DT_BaseToggle, CBaseToggle)
	RecvPropDataTable(RECVINFO_DT(m_Collision), 0, &REFERENCE_RECV_TABLE(DT_CollisionProperty)),
	RecvPropVector(RECVINFO_NAME(m_vecNetworkOrigin, m_vecOrigin)),
	RecvPropQAngles(RECVINFO_NAME(m_angNetworkAngles, m_angRotation)),
	RecvPropString(RECVINFO(m_sMaster)),
	RecvPropString(RECVINFO(m_iName)),
	RecvPropString(RECVINFO(m_iszDamageFilterName)),
	RecvPropInt(RECVINFO(m_nModelIndex)),
	RecvPropInt(RECVINFO(m_CollisionGroup)),
	RecvPropInt(RECVINFO(m_spawnflags)),
	RecvPropInt(RECVINFO(m_fEffects), 0, RecvProxy_EffectFlags ),
	RecvPropInt( "movecollide", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveCollide ),
	RecvPropInt( "movetype", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveType ),
END_RECV_TABLE();


C_BaseToggle::C_BaseToggle()
{
#ifdef _DEBUG
	// necessary since in debug, we initialize vectors to NAN for debugging
	m_vecPosition1.Init();
	m_vecPosition2.Init();
	m_vecAngle1.Init();
	m_vecAngle2.Init();
	m_vecFinalDest.Init();
	m_vecFinalAngle.Init();
#endif
}

bool C_BaseToggle::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "lip"))
	{
		m_flLip = atof(szValue);
	}
	else if (FStrEq(szKeyName, "wait"))
	{
		m_flWait = atof(szValue);
	}
	else if (FStrEq(szKeyName, "master"))
	{
		m_sMaster = AllocPooledString(szValue);
	}
	else if (FStrEq(szKeyName, "distance"))
	{
		m_flMoveDistance = atof(szValue);
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Calculate m_vecVelocity and m_flNextThink to reach vecDest from
//			GetOrigin() traveling at flSpeed.
// Input  : Vector	vecDest - 
//			flSpeed - 
//-----------------------------------------------------------------------------
void C_BaseToggle::LinearMove( const Vector &vecDest, float flSpeed )
{
	//ASSERTSZ(flSpeed != 0, "LinearMove:  no speed is defined!");
	
	m_vecFinalDest = vecDest;

	m_movementType = MOVE_TOGGLE_LINEAR;
	// Already there?
	if (vecDest == GetLocalOrigin())
	{
		MoveDone();
		return;
	}
		
	// set destdelta to the vector needed to move
	Vector vecDestDelta = vecDest - GetLocalOrigin();
	
	// divide vector length by speed to get time to reach dest
	float flTravelTime = vecDestDelta.Length() / flSpeed;

	// set m_flNextThink to trigger a call to LinearMoveDone when dest is reached
	SetMoveDoneTime( flTravelTime );

	// scale the destdelta vector by the time spent traveling to get velocity
	SetLocalVelocity( vecDestDelta / flTravelTime );
}


void C_BaseToggle::MoveDone( void )
{
	switch ( m_movementType )
	{
	case MOVE_TOGGLE_LINEAR:
		LinearMoveDone();
		break;
	case MOVE_TOGGLE_ANGULAR:
		AngularMoveDone();
		break;
	}
	m_movementType = MOVE_TOGGLE_NONE;
	BaseClass::MoveDone();
}
//-----------------------------------------------------------------------------
// Purpose: After moving, set origin to exact final destination, call "move done" function.
//-----------------------------------------------------------------------------
void C_BaseToggle::LinearMoveDone( void )
{
	UTIL_SetOrigin( this, m_vecFinalDest);
	SetAbsVelocity( vec3_origin );
	SetMoveDoneTime( -1 );
}


// DVS TODO: obselete, remove?
bool C_BaseToggle::IsLockedByMaster( void )
{
	if (m_sMaster != NULL_STRING && !UTIL_IsMasterTriggered(m_sMaster, m_hActivator))
		return true;
	else
		return false;
}


//-----------------------------------------------------------------------------
// Purpose: Calculate m_vecVelocity and m_flNextThink to reach vecDest from
//			GetLocalOrigin() traveling at flSpeed. Just like LinearMove, but rotational.
// Input  : vecDestAngle - 
//			flSpeed - 
//-----------------------------------------------------------------------------
void C_BaseToggle::AngularMove( const QAngle &vecDestAngle, float flSpeed )
{
	//ASSERTSZ(flSpeed != 0, "AngularMove:  no speed is defined!");
	
	m_vecFinalAngle = vecDestAngle;

	m_movementType = MOVE_TOGGLE_ANGULAR;
	// Already there?
	if (vecDestAngle == GetLocalAngles())
	{
		MoveDone();
		return;
	}
	
	// set destdelta to the vector needed to move
	QAngle vecDestDelta = vecDestAngle - GetLocalAngles();
	
	// divide by speed to get time to reach dest
	float flTravelTime = vecDestDelta.Length() / flSpeed;

	const float MinTravelTime = 0.01f;
	if ( flTravelTime < MinTravelTime )
	{
		// If we only travel for a short time, we can fail WillSimulateGamePhysics()
		flTravelTime = MinTravelTime;
		flSpeed = vecDestDelta.Length() / flTravelTime;
	}

	// set m_flNextThink to trigger a call to AngularMoveDone when dest is reached
	SetMoveDoneTime( flTravelTime );

	// scale the destdelta vector by the time spent traveling to get velocity
	SetLocalAngularVelocity( vecDestDelta * (1.0 / flTravelTime) );
}


//-----------------------------------------------------------------------------
// Purpose: After rotating, set angle to exact final angle, call "move done" function.
//-----------------------------------------------------------------------------
void C_BaseToggle::AngularMoveDone( void )
{
	SetLocalAngles( m_vecFinalAngle );
	SetLocalAngularVelocity( vec3_angle );
	SetMoveDoneTime( -1 );
}


float C_BaseToggle::AxisValue( int flags, const QAngle &angles )
{
	if ( FBitSet(flags, SF_DOOR_ROTATE_ROLL) )
		return angles.z;
	if ( FBitSet(flags, SF_DOOR_ROTATE_PITCH) )
		return angles.x;

	return angles.y;
}


void C_BaseToggle::AxisDir( void )
{
	if ( m_spawnflags & SF_DOOR_ROTATE_ROLL )
		m_vecMoveAng = QAngle( 0, 0, 1 );	// angles are roll
	else if ( m_spawnflags & SF_DOOR_ROTATE_PITCH )
		m_vecMoveAng = QAngle( 1, 0, 0 );	// angles are pitch
	else
		m_vecMoveAng = QAngle( 0, 1, 0 );		// angles are yaw
}


float C_BaseToggle::AxisDelta( int flags, const QAngle &angle1, const QAngle &angle2 )
{
	// UNDONE: Use AngleDistance() here?
	if ( FBitSet (flags, SF_DOOR_ROTATE_ROLL) )
		return angle1.z - angle2.z;
	
	if ( FBitSet (flags, SF_DOOR_ROTATE_PITCH) )
		return angle1.x - angle2.x;

	return angle1.y - angle2.y;
}
