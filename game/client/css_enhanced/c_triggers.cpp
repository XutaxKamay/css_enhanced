#include "cbase.h"
#include "const.h"
#include "datamap.h"
#include "dt_utlvector_recv.h"
#include "in_buttons.h"
#include "collisionutils.h"
#include "platform.h"
#include "prediction.h"
#include "mapentities_shared.h"
#include "saverestore_utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "predictioncopy.h"

#include "c_triggers.h"

#include "tier0/memdbgon.h"

CUtlHash< C_BaseEntity* > g_TriggerEntities(
  MAX_EDICTS,
  0,
  0,
  []( C_BaseEntity* const& pEntA, C_BaseEntity* const& pEntB )
  {
	  return pEntA == pEntB;
  },
  []( C_BaseEntity* const& pEnt ) -> unsigned int
  {
	  if ( sizeof( &pEnt ) == 8 )
	  {
		  return Hash8( &pEnt );
	  }
	  else
	  {
		  return Hash4( &pEnt );
	  }
  } );

bool IsTriggerClass( CBaseEntity *pEntity );

CON_COMMAND(report_triggerinfo, "")
{
    for (int i = 0; i < cl_entitylist->GetHighestEntityIndex(); i++)
    {
        C_BaseEntity* pEntity = (C_BaseEntity*) cl_entitylist->GetClientEntity( i );

		if ( !pEntity )
		{
			continue;
		}

		if (!IsTriggerClass(pEntity))
		{
			continue;
		}

		C_BaseTrigger* pTrigger = (C_BaseTrigger*) ( pEntity );

	ConMsg(
		"------ Trigger Information ------\n"
		"  >> Trigger Index: %i\n"
		"  >> Name         : %s\n"
		"  >> Classname    : %s\n"
		"  >> Target Name  : %s\n"
		"  >> Filter Name  : %s\n"
		"  >> Origin       : %.2f, %.2f, %.2f\n"
		"---------------------------------\n\n",
		pTrigger->entindex(),
		pTrigger->GetEntityName(), pTrigger->GetClassname(), pTrigger->m_target, pTrigger->m_iFilterName,
		pTrigger->GetAbsOrigin().x, pTrigger->GetAbsOrigin().y, pTrigger->GetAbsOrigin().z);
    }
}

// Command to dynamically toggle trigger visibility
void Cmd_ShowtriggersToggle_f( const CCommand &args )
{
	for (int i = 0; i < cl_entitylist->GetHighestEntityIndex(); i++)
    {
        C_BaseEntity* pEntity = (C_BaseEntity*) cl_entitylist->GetClientEntity( i );

		if ( pEntity && IsTriggerClass(pEntity) )
		{
			if ( pEntity->IsEffectActive( EF_NODRAW ) )
			{
				pEntity->RemoveEffects( EF_NODRAW );
			}
			else
			{
				pEntity->AddEffects( EF_NODRAW );
			}
		}
    }
}

static ConCommand showtriggers_toggle( "cl_showtriggers_toggle", Cmd_ShowtriggersToggle_f, "Toggle show triggers" );

// Global Savedata for base trigger
BEGIN_DATADESC( C_BaseTrigger )

	// Keyfields
	DEFINE_KEYFIELD( m_iFilterName,	FIELD_STRING,	"filtername" ),
	DEFINE_FIELD( m_hFilter,	FIELD_EHANDLE ),
	DEFINE_KEYFIELD( m_bDisabled,		FIELD_BOOLEAN,	"StartDisabled" ),
	DEFINE_UTLVECTOR( m_hTouchingEntities, FIELD_EHANDLE ),

	DEFINE_PRED_FIELD(m_iName, FIELD_STRING, FTYPEDESC_INSENDTABLE),

	// Inputs	
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Toggle", InputToggle ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TouchTest", InputTouchTest ),

	DEFINE_INPUTFUNC( FIELD_VOID, "StartTouch", InputStartTouch ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndTouch", InputEndTouch ),

	// Outputs
	DEFINE_OUTPUT( m_OnStartTouch, "OnStartTouch"),
	DEFINE_OUTPUT( m_OnStartTouchAll, "OnStartTouchAll"),
	DEFINE_OUTPUT( m_OnEndTouch, "OnEndTouch"),
	DEFINE_OUTPUT( m_OnEndTouchAll, "OnEndTouchAll"),
	DEFINE_OUTPUT( m_OnTouching, "OnTouching" ),
	DEFINE_OUTPUT( m_OnNotTouching, "OnNotTouching" ),

END_DATADESC()

// TODO_ENHANCED: this should be predicted, but since we don't have yet proper spawn, m_target would be an empty string all the time.
BEGIN_PREDICTION_DATA(C_BaseTrigger)
	// DEFINE_PRED_FIELD(m_bDisabled, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE),
	// DEFINE_PRED_FIELD(m_target, FIELD_STRING, FTYPEDESC_INSENDTABLE),
	// DEFINE_PRED_FIELD(m_iFilterName, FIELD_STRING, FTYPEDESC_INSENDTABLE),
	// DEFINE_PRED_ARRAY(m_hPredictedTouchingEntities, FIELD_EHANDLE, MAX_EDICTS, FTYPEDESC_PRIVATE),
	// DEFINE_PRED_FIELD(m_iCountPredictedTouchingEntities, FIELD_INTEGER, FTYPEDESC_PRIVATE)
END_PREDICTION_DATA();

// Incase server decides to change m_bDisabled
void RecvProxy_Disabled(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
	C_BaseTrigger *entity = (C_BaseTrigger *) pStruct;

	entity->m_bDisabled = pData->m_Value.m_Int;
	entity->m_bDisabled == 1 ? entity->Disable() : entity->Enable();
}

#define RECVINFO_OUTPUT(outputName) #outputName ".m_ActionList", offsetof(currentRecvDTClass, outputName)

IMPLEMENT_CLIENTCLASS_DT(C_BaseTrigger, DT_BaseTrigger, CBaseTrigger)
	RecvPropInt(RECVINFO(m_bDisabled), NULL, RecvProxy_Disabled),
	RecvPropString(RECVINFO(m_target), 0, RecvProxy_StringToStringT),
	RecvPropString(RECVINFO(m_iFilterName), NULL, RecvProxy_StringToStringT),
END_RECV_TABLE();

LINK_ENTITY_TO_CLASS( trigger, C_BaseTrigger );


C_BaseTrigger::C_BaseTrigger()
{
    SetPredictionEligible( true );
	AddEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
	g_TriggerEntities.Insert( this );
	// m_iCountPredictedTouchingEntities = 0;
	// Q_memset(m_hPredictedTouchingEntities, 0, sizeof(m_hPredictedTouchingEntities));
}

C_BaseTrigger::~C_BaseTrigger()
{
	auto handle = g_TriggerEntities.Find( this );

	if ( handle != g_TriggerEntities.InvalidHandle() )
	{
		g_TriggerEntities.Remove( handle );
	}
}

void C_BaseTrigger::Spawn()
{
	SetSolid(SOLID_BSP);
	AddSolidFlags(FSOLID_TRIGGER);
	SetMoveType(MOVETYPE_NONE);

	// why doens't NotifyShouldTransmit call this
	UpdatePartitionListEntry();
}

void C_BaseTrigger::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate( updateType );
}

bool C_BaseTrigger::ShouldPredict( void )
{
	// TODO_ENHANCED: this isn't a predictable, but local player needs it to predict correctly.
	return false;
}

void C_BaseTrigger::UpdatePartitionListEntry(void)
{
	::partition->RemoveAndInsert(
		PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS,  // remove
		PARTITION_CLIENT_TRIGGER_ENTITIES,  // add
		CollisionProp()->GetPartitionHandle() );
}

void C_BaseTrigger::UpdateFilter(void)
{
	// We do this since, since we dont know what order the entities are sent in, so a trigger might be sent
	// before the client know about the filter entity
	if ( !m_iFilterName || m_iFilterName[0] == 0 )
	{
		m_hFilter = NULL;
	}
	else
	{
		m_hFilter = static_cast< C_BaseFilter* >( UTIL_FindEntityByName( m_iFilterName ) );
	}
}

//------------------------------------------------------------------------------
// Purpose: Input handler to turn on this trigger.
//------------------------------------------------------------------------------
void C_BaseTrigger::InputEnable( inputdata_t &inputdata )
{
	Enable();
}


//------------------------------------------------------------------------------
// Purpose: Input handler to turn off this trigger.
//------------------------------------------------------------------------------
void C_BaseTrigger::InputDisable( inputdata_t &inputdata )
{
	Disable();
}

void C_BaseTrigger::InputTouchTest( inputdata_t &inputdata )
{
	TouchTest();
}


//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------
void C_BaseTrigger::UpdateOnRemove( void )
{
	if ( VPhysicsGetObject())
	{
		VPhysicsGetObject()->RemoveTrigger();
	}

	BaseClass::UpdateOnRemove();
}

//------------------------------------------------------------------------------
// Purpose: Turns on this trigger.
//------------------------------------------------------------------------------
void C_BaseTrigger::Enable( void )
{
	m_bDisabled = false;

	if ( VPhysicsGetObject())
	{
		VPhysicsGetObject()->EnableCollisions( true );
	}

	if (!IsSolidFlagSet( FSOLID_TRIGGER ))
	{
		AddSolidFlags( FSOLID_TRIGGER );
		PhysicsTouchTriggers();
	}
}


//------------------------------------------------------------------------------
// Purpose: Turns off this trigger.
//------------------------------------------------------------------------------
void C_BaseTrigger::Disable( void )
{
	m_bDisabled = true;

	if ( VPhysicsGetObject())
	{
		VPhysicsGetObject()->EnableCollisions( false );
	}

	if (IsSolidFlagSet(FSOLID_TRIGGER))
	{
		RemoveSolidFlags( FSOLID_TRIGGER );
		PhysicsTouchTriggers();
	}
}
//------------------------------------------------------------------------------
// Purpose: Tests to see if anything is touching this trigger.
//------------------------------------------------------------------------------
void C_BaseTrigger::TouchTest( void )
{
	// If the trigger is disabled don't test to see if anything is touching it.
	if ( !m_bDisabled )
	{
		if ( m_hTouchingEntities.Count() !=0 )
		{
			m_OnTouching.FireOutput( this, this );
		}
		else
		{
			m_OnNotTouching.FireOutput( this, this );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Return true if the specified point is within this zone
//-----------------------------------------------------------------------------
bool C_BaseTrigger::PointIsWithin( const Vector &vecPoint )
{
	Ray_t ray;
	trace_t tr;
	ICollideable *pCollide = CollisionProp();
	ray.Init( vecPoint, vecPoint );
	enginetrace->ClipRayToCollideable( ray, MASK_ALL, pCollide, &tr );
	return ( tr.startsolid );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void C_BaseTrigger::InitTrigger( )
{
	SetSolid(SOLID_VPHYSICS);
	AddSolidFlags( FSOLID_NOT_SOLID );

	if (m_bDisabled)
	{
		RemoveSolidFlags( FSOLID_TRIGGER );
	}
	else
	{
		AddSolidFlags( FSOLID_TRIGGER );
	}

	SetMoveType( MOVETYPE_NONE );

	m_hTouchingEntities.Purge();

	if ( HasSpawnFlags( SF_TRIG_TOUCH_DEBRIS ) )
	{
		CollisionProp()->AddSolidFlags( FSOLID_TRIGGER_TOUCH_DEBRIS );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if this entity passes the filter criteria, false if not.
// Input  : pOther - The entity to be filtered.
//-----------------------------------------------------------------------------
bool C_BaseTrigger::PassesTriggerFilters(CBaseEntity *pOther)
{
	if (m_bDisabled)
	{
		return false;
	}

	// First test spawn flag filters
	if (HasSpawnFlags(SF_TRIGGER_ALLOW_ALL) ||
		(HasSpawnFlags(SF_TRIGGER_ALLOW_CLIENTS) && (pOther->GetFlags() & FL_CLIENT)) ||
		(HasSpawnFlags(SF_TRIGGER_ALLOW_NPCS) && (pOther->GetFlags() & FL_NPC)) ||
		(HasSpawnFlags(SF_TRIGGER_ALLOW_PUSHABLES) && FClassnameIs(pOther, "func_pushable")) ||
		(HasSpawnFlags(SF_TRIGGER_ALLOW_PHYSICS) && pOther->GetMoveType() == MOVETYPE_VPHYSICS))
#if defined( HL2_EPISODIC ) || defined( TF_DLL )		
		||
		(	HasSpawnFlags(SF_TRIG_TOUCH_DEBRIS) && 
			(pOther->GetCollisionGroup() == COLLISION_GROUP_DEBRIS ||
			pOther->GetCollisionGroup() == COLLISION_GROUP_DEBRIS_TRIGGER || 
			pOther->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS)
		)
#endif
	{
		if (pOther->GetFlags() & FL_NPC)
		{
			C_AI_BaseNPC *pNPC = pOther->MyNPCPointer();

			if ( HasSpawnFlags( SF_TRIGGER_ONLY_PLAYER_ALLY_NPCS ) )
			{
				if ( !pNPC /*|| !pNPC->IsPlayerAlly()*/ )
				{
					return false;
				}
			}

			if ( HasSpawnFlags( SF_TRIGGER_ONLY_NPCS_IN_VEHICLES ) )
			{
				if ( !pNPC /*|| !pNPC->IsInAVehicle()*/ )
					return false;
			}
		}

		bool bOtherIsPlayer = pOther->IsPlayer();

		if (bOtherIsPlayer)
		{
			CBasePlayer *pPlayer = (CBasePlayer*)pOther;
			if (!pPlayer->IsAlive())
				return false;

			if (HasSpawnFlags(SF_TRIGGER_ONLY_CLIENTS_IN_VEHICLES))
			{
				if (!pPlayer->IsInAVehicle())
					return false;

				// Make sure we're also not exiting the vehicle at the moment
				IClientVehicle *pVehicle = pPlayer->GetVehicle();

				if (pVehicle == NULL)
					return false;

				//TODO_ENHANCED: Check if exit anim is on
				//if (pVehicle->IsPassengerExiting())
					return false;
			}

			if (HasSpawnFlags(SF_TRIGGER_ONLY_CLIENTS_OUT_OF_VEHICLES))
			{
				if (pPlayer->IsInAVehicle())
					return false;
			}

			if (HasSpawnFlags(SF_TRIGGER_DISALLOW_BOTS))
			{
				return false;
			}
		}

		UpdateFilter();

		C_BaseFilter *pFilter = m_hFilter.Get();
		return (!pFilter) ? true : pFilter->PassesFilter( this, pOther );
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Called to simulate what happens when an entity touches the trigger.
// Input  : pOther - The entity that is touching us.
//-----------------------------------------------------------------------------
void C_BaseTrigger::InputStartTouch( inputdata_t &inputdata )
{
	//Pretend we just touched the trigger.
	StartTouch( inputdata.pCaller );
}
//-----------------------------------------------------------------------------
// Purpose: Called to simulate what happens when an entity leaves the trigger.
// Input  : pOther - The entity that is touching us.
//-----------------------------------------------------------------------------
void C_BaseTrigger::InputEndTouch( inputdata_t &inputdata )
{
	//And... pretend we left the trigger.
	EndTouch( inputdata.pCaller );
}

//-----------------------------------------------------------------------------
// Purpose: Called when an entity starts touching us.
// Input  : pOther - The entity that is touching us.
//-----------------------------------------------------------------------------
void C_BaseTrigger::StartTouch(CBaseEntity *pOther)
{
	// DevMsg("(%s:%s) Start touching (%s:%s)\n", m_iName, m_iClassname, pOther->m_iName, pOther->m_iClassname);

	if (PassesTriggerFilters(pOther))
	{
		// DevMsg("(%s:%s) Start touching (passed filters) (%s:%s)\n", m_iName, m_iClassname, pOther->m_iName, pOther->m_iClassname);

		EHANDLE hOther;
		hOther = pOther;

		bool bAdded = false;
		if ( m_hTouchingEntities.Find( hOther ) == m_hTouchingEntities.InvalidIndex() )
		{
			m_hTouchingEntities.AddToTail( hOther );
			bAdded = true;
		}

		m_OnStartTouch.FireOutput(pOther, this);

		if ( bAdded && ( m_hTouchingEntities.Count() == 1 ) )
        {
			// First entity to touch us that passes our filters
			m_OnStartTouchAll.FireOutput( pOther, this );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Called when an entity stops touching us.
// Input  : pOther - The entity that was touching us.
//-----------------------------------------------------------------------------
void C_BaseTrigger::EndTouch(CBaseEntity *pOther)
{
	// DevMsg("(%s:%s) End touching (%s:%s)\n", m_iName, m_iClassname, pOther->m_iName, pOther->m_iClassname);

	if ( IsTouching( pOther ) )
    {
		// DevMsg("(%s:%s) End touching (passed is touching) (%s:%s)\n", m_iName, m_iClassname, pOther->m_iName, pOther->m_iClassname);

		EHANDLE hOther;
		hOther = pOther;
		m_hTouchingEntities.FindAndRemove( hOther );

		//FIXME: Without this, triggers fire their EndTouch outputs when they are disabled!
		//if ( !m_bDisabled )
		//{
			m_OnEndTouch.FireOutput(pOther, this);
		//}

		// If there are no more entities touching this trigger, fire the lost all touches
		// Loop through the touching entities backwards. Clean out old ones, and look for existing
		bool bFoundOtherTouchee = false;
		int iSize = m_hTouchingEntities.Count();
		for ( int i = iSize-1; i >= 0; i-- )
		{
			EHANDLE hOther;
			hOther = m_hTouchingEntities[i];

			if ( !hOther )
			{
				m_hTouchingEntities.Remove( i );
			}
			else if ( hOther->IsPlayer() && !hOther->IsAlive() )
			{
				m_hTouchingEntities.Remove( i );
			}
			else
			{
				bFoundOtherTouchee = true;
			}
		}

		//FIXME: Without this, triggers fire their EndTouch outputs when they are disabled!
		// Didn't find one?
		if ( !bFoundOtherTouchee /*&& !m_bDisabled*/ )
		{
			m_OnEndTouchAll.FireOutput(pOther, this);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Return true if the specified entity is touching us
//-----------------------------------------------------------------------------
bool C_BaseTrigger::IsTouching( CBaseEntity *pOther )
{
	EHANDLE hOther;
	hOther = pOther;
	return ( m_hTouchingEntities.Find( hOther ) != m_hTouchingEntities.InvalidIndex() );
}

//-----------------------------------------------------------------------------
// Purpose: Return a pointer to the first entity of the specified type being touched by this trigger
//-----------------------------------------------------------------------------
C_BaseEntity *C_BaseTrigger::GetTouchedEntityOfType( const char *sClassName )
{
	int iCount = m_hTouchingEntities.Count();
	for ( int i = 0; i < iCount; i++ )
	{
		C_BaseEntity *pEntity = m_hTouchingEntities[i];
		if ( FClassnameIs( pEntity, sClassName ) )
			return pEntity;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Toggles this trigger between enabled and disabled.
//-----------------------------------------------------------------------------
void C_BaseTrigger::InputToggle( inputdata_t &inputdata )
{
	if (IsSolidFlagSet( FSOLID_TRIGGER ))
	{
		RemoveSolidFlags(FSOLID_TRIGGER);
	}
	else
	{
		AddSolidFlags(FSOLID_TRIGGER);
	}

	PhysicsTouchTriggers();
}

bool IsTriggerClass( CBaseEntity *pEntity )
{
	if ( pEntity->IsTrigger() )
		return true;

	//if ( NULL != dynamic_cast<C_TriggerVPhysicsMotion *>(pEntity) )
	//	return true;

	//if ( NULL != dynamic_cast<C_TriggerVolume *>(pEntity) )
	//	return true;

	return false;
}
