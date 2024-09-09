#include "cbase.h"
#include "c_triggers.h"
#include "in_buttons.h"
#include "collisionutils.h"
#include "prediction.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// >> TriggerMultiple
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS(trigger_multiple, C_TriggerMultiple);

BEGIN_PREDICTION_DATA(C_TriggerMultiple)
END_PREDICTION_DATA();

IMPLEMENT_CLIENTCLASS_DT(C_TriggerMultiple, DT_TriggerMultiple, CTriggerMultiple)
END_RECV_TABLE();

BEGIN_DATADESC( C_TriggerMultiple )

	// Outputs
	DEFINE_OUTPUT(m_OnTrigger, "OnTrigger")

END_DATADESC()

// Global list of triggers that care about weapon fire
// Doesn't need saving, the triggers re-add themselves on restore.
CUtlVector< CHandle<C_TriggerMultiple> >	g_hWeaponFireTriggers;

//-----------------------------------------------------------------------------
// Purpose: Called when spawning, after keyvalues have been handled.
//-----------------------------------------------------------------------------
void C_TriggerMultiple::Spawn(void)
{
	BaseClass::Spawn();

	InitTrigger();

	if (m_flWait == 0)
	{
		m_flWait = 0.2;
	}

	SetTouch(&C_TriggerMultiple::MultiTouch);
}

//-----------------------------------------------------------------------------
// Purpose: Touch function. Activates the trigger.
// Input  : pOther - The thing that touched us.
//-----------------------------------------------------------------------------
void C_TriggerMultiple::MultiTouch(CBaseEntity *pOther)
{
	if (PassesTriggerFilters(pOther))
	{
		ActivateMultiTrigger(pOther);
	}
}

//-----------------------------------------------------------------------------
// Purpose: The wait time has passed, so set back up for another activation
//-----------------------------------------------------------------------------
void C_TriggerMultiple::MultiWaitOver(void)
{
	SetThink(NULL);
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : pActivator -
//-----------------------------------------------------------------------------
void C_TriggerMultiple::ActivateMultiTrigger(CBaseEntity *pActivator)
{
	if (GetNextThink() > gpGlobals->curtime)
		return;         // still waiting for reset time

	m_hActivator = pActivator;

	m_OnTrigger.FireOutput(m_hActivator, this);

	if (m_flWait > 0)
	{
		SetThink(&C_TriggerMultiple::MultiWaitOver);
		SetNextThink(gpGlobals->curtime + m_flWait);
	}
	else
	{
		// we can't just remove (self) here, because this is a touch function
		// called while C code is looping through area links...
		SetTouch(NULL);
		SetNextThink(gpGlobals->curtime + 0.1f);
		SetThink(&C_TriggerMultiple::SUB_Remove);
	}
}
