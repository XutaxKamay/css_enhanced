#ifndef C_TRIGGERS_GRAVITY_H
#define C_TRIGGERS_GRAVITY_H

#include "c_triggers.h"

class C_TriggerGravity : public C_BaseTrigger
{
public:
	DECLARE_CLASS( C_TriggerGravity, C_BaseTrigger );
	DECLARE_DATADESC();
    DECLARE_CLIENTCLASS();

	void Spawn( void );
	void GravityTouch( CBaseEntity *pOther );
};

#endif