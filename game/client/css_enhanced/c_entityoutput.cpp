#include "cbase.h"
#include "c_entityoutput.h"
#include "predictable_entity.h"

BEGIN_SIMPLE_DATADESC( C_EventAction )
	DEFINE_FIELD( m_iTarget, FIELD_STRING ),
	DEFINE_FIELD( m_iTargetInput, FIELD_STRING ),
	DEFINE_FIELD( m_iParameter, FIELD_STRING ),
	DEFINE_FIELD( m_flDelay, FIELD_FLOAT ),
	DEFINE_FIELD( m_nTimesToFire, FIELD_INTEGER ),
	DEFINE_FIELD( m_iIDStamp, FIELD_INTEGER ),

	// This is dealt with by the Restore method
	// DEFINE_FIELD( m_pNext, C_EventAction ),
END_DATADESC();

// ID Stamp used to uniquely identify every output
int C_EventAction::s_iNextIDStamp = 0;

//-----------------------------------------------------------------------------
// Purpose: Creates an event action and assigns it an unique ID stamp.
// Input  : ActionData - the map file data block descibing the event action.
//-----------------------------------------------------------------------------
C_EventAction::C_EventAction( const char *ActionData )
{
	ConColorMsg(Color(0, 255, 0, 255), "C_EventAction::C_EventAction: %s\n", ActionData);

	m_pNext = NULL;
	m_iIDStamp = ++s_iNextIDStamp;

	m_flDelay = 0;
	m_iTarget = NULL_STRING;
	m_iParameter = NULL_STRING;
	m_iTargetInput = NULL_STRING;
	m_nTimesToFire = EVENT_FIRE_ALWAYS;

	if (ActionData == NULL)
		return;

	char szToken[256];

	//
	// Parse the target name.
	//
	const char *psz = nexttoken(szToken, ActionData, ',', sizeof(szToken));
	if (szToken[0] != '\0')
	{
		m_iTarget = AllocPooledString(szToken);
	}

	//
	// Parse the input name.
	//
	psz = nexttoken(szToken, psz, ',', sizeof(szToken));
	if (szToken[0] != '\0')
	{
		m_iTargetInput = AllocPooledString(szToken);
	}
	else
	{
		m_iTargetInput = AllocPooledString("Use");
	}

	//
	// Parse the parameter override.
	//
	psz = nexttoken(szToken, psz, ',', sizeof(szToken));
	if (szToken[0] != '\0')
	{
		m_iParameter = AllocPooledString(szToken);
	}

	//
	// Parse the delay.
	//
	psz = nexttoken(szToken, psz, ',', sizeof(szToken));
	if (szToken[0] != '\0')
	{
		m_flDelay = atof(szToken);
	}

	//
	// Parse the number of times to fire.
	//
	nexttoken(szToken, psz, ',', sizeof(szToken));
	if (szToken[0] != '\0')
	{
		m_nTimesToFire = atoi(szToken);
		if (m_nTimesToFire == 0)
		{
			m_nTimesToFire = EVENT_FIRE_ALWAYS;
		}
	}
}

#include "tier0/memdbgoff.h"

void *C_EventAction::operator new( size_t stAllocateBlock )
{
	return g_EntityListPool.Alloc( stAllocateBlock );
}

void *C_EventAction::operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine )
{
	return g_EntityListPool.Alloc( stAllocateBlock );
}

void C_EventAction::operator delete( void *pMem )
{
	g_EntityListPool.Free( pMem );
}

#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Returns the highest-valued delay in our list of event actions.
//-----------------------------------------------------------------------------
float C_BaseEntityOutput::GetMaxDelay(void)
{
	float flMaxDelay = 0;
	C_EventAction *ev = m_ActionList;

	while (ev != NULL)
	{
		if (ev->m_flDelay > flMaxDelay)
		{
			flMaxDelay = ev->m_flDelay;
		}
		ev = ev->m_pNext;
	}

	return(flMaxDelay);
}


//-----------------------------------------------------------------------------
// Purpose: Destructor.
//-----------------------------------------------------------------------------
C_BaseEntityOutput::~C_BaseEntityOutput()
{
	C_EventAction *ev = m_ActionList;
	while (ev != NULL)
	{
		C_EventAction *pNext = ev->m_pNext;
		delete ev;
		ev = pNext;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Fires the event, causing a sequence of action to occur in other ents.
// Input  : pActivator - Entity that initiated this sequence of actions.
//			pCaller - Entity that is actually causing the event.
//-----------------------------------------------------------------------------
void C_BaseEntityOutput::FireOutput(variant_t Value, CBaseEntity *pActivator, CBaseEntity *pCaller, float fDelay)
{
	//
	// Iterate through all eventactions and fire them off.
	//
	C_EventAction *ev = m_ActionList;
	C_EventAction *prev = NULL;

	while (ev != NULL)
	{
		if (ev->m_iParameter == NULL_STRING)
		{
			//
			// Post the event with the default parameter.
			//
			g_EventQueue.AddEvent( STRING(ev->m_iTarget), STRING(ev->m_iTargetInput), Value, ev->m_flDelay + fDelay, pActivator, pCaller, ev->m_iIDStamp );
		}
		else
		{
			//
			// Post the event with a parameter override.
			//
			variant_t ValueOverride;
			ValueOverride.SetString( ev->m_iParameter );
			g_EventQueue.AddEvent( STRING(ev->m_iTarget), STRING(ev->m_iTargetInput), ValueOverride, ev->m_flDelay, pActivator, pCaller, ev->m_iIDStamp );
		}

		if ( ev->m_flDelay )
		{
			char szBuffer[256];
			Q_snprintf( szBuffer,
				sizeof(szBuffer),
				"(%0.2f) output: (%s,%s) -> (%s,%s,%.1f)(%s)\n",
				gpGlobals->curtime,
				pCaller ? STRING(pCaller->m_iClassname) : "NULL",
				pCaller ? pCaller->GetDebugName() : "NULL",
				STRING(ev->m_iTarget),
				STRING(ev->m_iTargetInput),
				ev->m_flDelay,
				STRING(ev->m_iParameter) );

			DevMsg( 2, "%s", szBuffer );
#ifdef GAME_DLL
			ADD_DEBUG_HISTORY( HISTORY_ENTITY_IO, szBuffer );
#endif
		}
		else
		{
			char szBuffer[256];
			Q_snprintf( szBuffer,
				sizeof(szBuffer),
				"(%0.2f) output: (%s,%s) -> (%s,%s)(%s)\n",
				gpGlobals->curtime,
				pCaller ? STRING(pCaller->m_iClassname) : "NULL",
				pCaller ? pCaller->GetDebugName() : "NULL", STRING(ev->m_iTarget),
				STRING(ev->m_iTargetInput),
				STRING(ev->m_iParameter) );

			DevMsg( 2, "%s", szBuffer );
#ifdef GAME_DLL
			ADD_DEBUG_HISTORY( HISTORY_ENTITY_IO, szBuffer );
#endif
		}
#ifdef GAME_DLL
		if ( pCaller && pCaller->m_debugOverlays & OVERLAY_MESSAGE_BIT)
		{
			pCaller->DrawOutputOverlay(ev);
		}
#endif

		//
		// Remove the event action from the list if it was set to be fired a finite
		// number of times (and has been).
		//
		bool bRemove = false;
		if (ev->m_nTimesToFire != EVENT_FIRE_ALWAYS)
		{
			ev->m_nTimesToFire--;
			if (ev->m_nTimesToFire == 0)
			{
				char szBuffer[256];
				Q_snprintf( szBuffer,
				sizeof(szBuffer),
				"Removing from action list: (%s,%s) -> (%s,%s)\n",
				pCaller ? STRING(pCaller->m_iClassname) : "NULL",
				pCaller ? pCaller->GetDebugName() : "NULL",
				STRING(ev->m_iTarget), STRING(ev->m_iTargetInput) );
				DevMsg( 2, "%s", szBuffer );
#ifdef GAME_DLL
				ADD_DEBUG_HISTORY( HISTORY_ENTITY_IO, szBuffer );
#endif
				bRemove = true;
			}
		}

		if (!bRemove)
		{
			prev = ev;
			ev = ev->m_pNext;
		}
		else
		{
			if (prev != NULL)
			{
				prev->m_pNext = ev->m_pNext;
			}
			else
			{
				m_ActionList = ev->m_pNext;
			}

			C_EventAction *next = ev->m_pNext;
			delete ev;
			ev = next;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Parameterless firing of an event
// Input  : pActivator -
//			pCaller -
//-----------------------------------------------------------------------------
void C_OutputEvent::FireOutput(CBaseEntity *pActivator, CBaseEntity *pCaller, float fDelay)
{
	variant_t Val;
	Val.Set( FIELD_VOID, NULL );
	C_BaseEntityOutput::FireOutput(Val, pActivator, pCaller, fDelay);
}


void C_BaseEntityOutput::ParseEventAction( const char *EventData )
{
	AddEventAction( new C_EventAction( EventData ) );
}

void C_BaseEntityOutput::AddEventAction( C_EventAction *pEventAction )
{
	pEventAction->m_pNext = m_ActionList;
	m_ActionList = pEventAction;
}

// save data description for the event queue
BEGIN_SIMPLE_DATADESC( C_BaseEntityOutput )
	DEFINE_CUSTOM_FIELD( m_Value, variantFuncs ),

	// This is saved manually by C_BaseEntityOutput::Save
	// DEFINE_FIELD( m_ActionList, C_EventAction ),
END_DATADESC()

int C_BaseEntityOutput::Save( ISave &save )
{
	// save that value out to disk, so we know how many to restore
	if ( !save.WriteFields( "Value", this, NULL, m_DataMap.dataDesc, m_DataMap.dataNumFields ) )
		return 0;

	for ( C_EventAction *ev = m_ActionList; ev != NULL; ev = ev->m_pNext )
	{
		if ( !save.WriteFields( "EntityOutput", ev, NULL, ev->m_DataMap.dataDesc, ev->m_DataMap.dataNumFields ) )
			return 0;
	}

	return 1;
}

int C_BaseEntityOutput::Restore( IRestore &restore, int elementCount )
{
	// load the number of items saved
	if ( !restore.ReadFields( "Value", this, NULL, m_DataMap.dataDesc, m_DataMap.dataNumFields ) )
		return 0;

	m_ActionList = NULL;

	// read in all the fields
	C_EventAction *lastEv = NULL;
	for ( int i = 0; i < elementCount; i++ )
	{
		C_EventAction *ev = new C_EventAction(NULL);

		if ( !restore.ReadFields( "EntityOutput", ev, NULL, ev->m_DataMap.dataDesc, ev->m_DataMap.dataNumFields ) )
			return 0;

		// add it to the list in the same order it was saved in
		if ( lastEv )
		{
			lastEv->m_pNext = ev;
		}
		else
		{
			m_ActionList = ev;
		}
		ev->m_pNext = NULL;
		lastEv = ev;
	}

	return 1;
}

int C_BaseEntityOutput::NumberOfElements( void )
{
	int count = 0;
	for ( C_EventAction *ev = m_ActionList; ev != NULL; ev = ev->m_pNext )
	{
		count++;
	}
	return count;
}

/// Delete every single action in the action list.
void C_BaseEntityOutput::DeleteAllElements( void )
{
	// walk front to back, deleting as we go. We needn't fix up pointers because
	// EVERYTHING will die.

	C_EventAction *pNext = m_ActionList;
	// wipe out the head
	m_ActionList = NULL;
	while (pNext)
	{
		C_EventAction *strikeThis = pNext;
		pNext = pNext->m_pNext;
		delete strikeThis;
	}

}