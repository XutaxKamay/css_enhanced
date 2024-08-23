#ifndef C_TRIGGERS_PUSH_H
#define C_TRIGGERS_PUSH_H

#include "c_triggers.h"
#include "predictable_entity.h"

class C_TriggerPush : public C_BaseTrigger
{
public:
	DECLARE_CLASS( C_TriggerPush, C_BaseTrigger );
    DECLARE_NETWORKCLASS();
    DECLARE_PREDICTABLE();

	virtual void Touch( CBaseEntity *pOther );

	Vector m_vecPushDir;
	
	float m_flAlternateTicksFix; // Scale factor to apply to the push speed when running with alternate ticks
	float m_flPushSpeed;
};

#endif // C_TRIGGERS_PUSH_H
