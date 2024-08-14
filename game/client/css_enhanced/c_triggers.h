#ifndef C_TRIGGERS_H
#define C_TRIGGERS_H
#ifdef _WIN32
#pragma once
#endif

#include "c_basetoggle.h"
#include "c_entityoutput.h"
#include "c_filters.h"

//
// Spawnflags
//

enum
{
	SF_TRIGGER_ALLOW_CLIENTS				= 0x01,		// Players can fire this trigger
	SF_TRIGGER_ALLOW_NPCS					= 0x02,		// NPCS can fire this trigger
	SF_TRIGGER_ALLOW_PUSHABLES				= 0x04,		// Pushables can fire this trigger
	SF_TRIGGER_ALLOW_PHYSICS				= 0x08,		// Physics objects can fire this trigger
	SF_TRIGGER_ONLY_PLAYER_ALLY_NPCS		= 0x10,		// *if* NPCs can fire this trigger, this flag means only player allies do so
	SF_TRIGGER_ONLY_CLIENTS_IN_VEHICLES		= 0x20,		// *if* Players can fire this trigger, this flag means only players inside vehicles can 
	SF_TRIGGER_ALLOW_ALL					= 0x40,		// Everything can fire this trigger EXCEPT DEBRIS!
	SF_TRIGGER_ONLY_CLIENTS_OUT_OF_VEHICLES	= 0x200,	// *if* Players can fire this trigger, this flag means only players outside vehicles can 
	SF_TRIG_PUSH_ONCE						= 0x80,		// trigger_push removes itself after firing once
	SF_TRIG_PUSH_AFFECT_PLAYER_ON_LADDER	= 0x100,	// if pushed object is player on a ladder, then this disengages them from the ladder (HL2only)
	SF_TRIG_TOUCH_DEBRIS 					= 0x400,	// Will touch physics debris objects
	SF_TRIGGER_ONLY_NPCS_IN_VEHICLES		= 0X800,	// *if* NPCs can fire this trigger, only NPCs in vehicles do so (respects player ally flag too)
	SF_TRIGGER_DISALLOW_BOTS                = 0x1000,   // Bots are not allowed to fire this trigger
};

// DVS TODO: get rid of CBaseToggle
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_BaseTrigger : public C_BaseToggle
{
	DECLARE_CLASS(C_BaseTrigger, C_BaseToggle);
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
public:
	C_BaseTrigger();
	
	void InitTrigger( void );

	void Enable( void );
	void Disable( void );
	void Spawn( void );
	void UpdateOnRemove( void );
	void UpdatePartitionListEntry();
	void TouchTest(  void );
	
	void PostDataUpdate( DataUpdateType_t updateType );

	// Input handlers
	virtual void InputEnable( inputdata_t &inputdata );
	virtual void InputDisable( inputdata_t &inputdata );
	virtual void InputToggle( inputdata_t &inputdata );
	virtual void InputTouchTest ( inputdata_t &inputdata );

	virtual void InputStartTouch( inputdata_t &inputdata );
	virtual void InputEndTouch( inputdata_t &inputdata );

	virtual bool UsesFilter( void ){ return ( m_hFilter.Get() != NULL ); }
	virtual bool PassesTriggerFilters(CBaseEntity *pOther);
	virtual void StartTouch(CBaseEntity *pOther);
	virtual void EndTouch(CBaseEntity *pOther);
	bool IsTouching( CBaseEntity *pOther );

	CBaseEntity *GetTouchedEntityOfType( const char *sClassName );

	// by default, triggers don't deal with TraceAttack
	void TraceAttack(CBaseEntity *pAttacker, float flDamage, const Vector &vecDir, trace_t *ptr, int bitsDamageType) {}

	bool PointIsWithin( const Vector &vecPoint );

	// from momentum 
	void UpdateFilter(void);

	bool		m_bDisabled;
	char m_iFilterName[MAX_PATH];
	CHandle<class C_BaseFilter>	m_hFilter;

	// unlike m_iName this should be constant
	char m_target[MAX_PATH];

protected:
	// Network the outputs
	// so that if a stripper config
	// wants to change them 
	// it gets sent to the client too
	//

	// Outputs
	C_OutputEvent m_OnStartTouch;
	C_OutputEvent m_OnStartTouchAll;
	C_OutputEvent m_OnEndTouch;
	C_OutputEvent m_OnEndTouchAll;
	C_OutputEvent m_OnTouching;
	C_OutputEvent m_OnNotTouching;

	// Entities currently being touched by this trigger
	CUtlVector< EHANDLE >	m_hTouchingEntities;

	DECLARE_DATADESC();
};

//-----------------------------------------------------------------------------
// Purpose: Variable sized repeatable trigger.  Must be targeted at one or more entities.
//			If "delay" is set, the trigger waits some time after activating before firing.
//			"wait" : Seconds between triggerings. (.2 default/minimum)
//-----------------------------------------------------------------------------
class C_TriggerMultiple : public C_BaseTrigger
{
public:
	DECLARE_CLASS(C_TriggerMultiple, C_BaseTrigger);
	DECLARE_NETWORKCLASS();
	
	DECLARE_PREDICTABLE();

	virtual void Spawn(void);

	void MultiTouch(C_BaseEntity *pOther);
	void MultiWaitOver(void);
	void ActivateMultiTrigger(C_BaseEntity *pActivator);

	// Outputs
	C_OutputEvent m_OnTrigger;
};

#endif // TRIGGERS_H
