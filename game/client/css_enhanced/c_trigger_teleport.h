#ifndef C_TRIGGERS_TELEPORT_H
#define C_TRIGGERS_TELEPORT_H

#include "c_triggers.h"
#include "predictable_entity.h"

constexpr int SF_TELEPORT_PRESERVE_ANGLES = 0x20;	// Preserve angles even when a local landmark is not specified

class C_TriggerTeleport : public C_BaseTrigger
{
public:
	DECLARE_CLASS( C_TriggerTeleport, C_BaseTrigger );
    DECLARE_CLIENTCLASS();
    DECLARE_PREDICTABLE();
	DECLARE_DATADESC();

	virtual void Touch( CBaseEntity *pOther );

	string_t m_iLandmark;
};

#endif // C_TRIGGERS_TELEPORT_H
