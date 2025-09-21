#ifndef C_TRIGGERS_MULTIPLE_H
#define C_TRIGGERS_MULTIPLE_H

#include "c_triggers.h"
#include "predictable_entity.h"

//-----------------------------------------------------------------------------
// Purpose: Variable sized repeatable trigger.  Must be targeted at one or more entities.
//			If "delay" is set, the trigger waits some time after activating before firing.
//			"wait" : Seconds between triggerings. (.2 default/minimum)
//-----------------------------------------------------------------------------
class C_TriggerMultiple : public C_BaseTrigger
{
public:
	DECLARE_CLASS(C_TriggerMultiple, C_BaseTrigger);
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_DATADESC();

	virtual void Spawn(void);

	void MultiTouch(C_BaseEntity *pOther);
	void MultiWaitOver(void);
	void ActivateMultiTrigger(C_BaseEntity *pActivator);

	// Outputs
	C_OutputEvent m_OnTrigger;
};

#endif