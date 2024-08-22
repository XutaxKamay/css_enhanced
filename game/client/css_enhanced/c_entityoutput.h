#ifndef C_ENTITYOUTPUT_H
#define C_ENTITYOUTPUT_H

#ifdef WIN32
#pragma once
#endif

#include "cbase.h"
#include "css_enhanced/variant_t.h"
#include "isaverestore.h"
#include "gamestringpool.h"

#include "c_eventqueue.h"
#include "c_entitylistpool.h"

class variant_t;

#define EVENT_FIRE_ALWAYS	-1


//-----------------------------------------------------------------------------
// Purpose: A C_OutputEvent consists of an array of these C_EventActions.
//			Each C_EventAction holds the information to fire a single input in
//			a target entity, after a specific delay.
//-----------------------------------------------------------------------------
class C_EventAction
{
public:
	C_EventAction( const char *ActionData = NULL );

	string_t m_iTarget; // name of the entity(s) to cause the action in
	string_t m_iTargetInput; // the name of the action to fire
	string_t m_iParameter; // parameter to send, 0 if none
	float m_flDelay; // the number of seconds to wait before firing the action
	int m_nTimesToFire; // The number of times to fire this event, or EVENT_FIRE_ALWAYS.

	int m_iIDStamp;	// unique identifier stamp

	static int s_iNextIDStamp;

	C_EventAction *m_pNext;

	// allocates memory from engine.MPool/g_EntityListPool
	static void *operator new( size_t stAllocateBlock );
	static void *operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	static void operator delete( void *pMem );
	static void operator delete( void *pMem , int nBlockUse, const char *pFileName, int nLine ) { operator delete(pMem); }

	DECLARE_SIMPLE_DATADESC();
};

EXTERN_RECV_TABLE( DT_EventAction );

//-----------------------------------------------------------------------------
// Purpose: Stores a list of connections to other entities, for data/commands to be
//			communicated along.
//-----------------------------------------------------------------------------
class C_BaseEntityOutput
{
public:
	~C_BaseEntityOutput();

	void ParseEventAction( const char *EventData );
	void AddEventAction( C_EventAction *pEventAction );

	int Save( ISave &save );
	int Restore( IRestore &restore, int elementCount );

	int NumberOfElements( void );

	float GetMaxDelay( void );

	fieldtype_t ValueFieldType() { return m_Value.FieldType(); }

	void FireOutput( variant_t Value, CBaseEntity *pActivator, CBaseEntity *pCaller, float fDelay = 0 );

	/// Delete every single action in the action list.
	void DeleteAllElements( void ) ;

protected:
	variant_t m_Value;
	C_EventAction *m_ActionList;

	DECLARE_SIMPLE_DATADESC();

	C_BaseEntityOutput() {} // this class cannot be created, only it's children

private:
	C_BaseEntityOutput( C_BaseEntityOutput& ); // protect from accidental copying
};


//-----------------------------------------------------------------------------
// Purpose: wraps variant_t data handling in convenient, compiler type-checked template
//-----------------------------------------------------------------------------
template< class Type, fieldtype_t fieldType >
class C_EntityOutputTemplate : public C_BaseEntityOutput
{
public:
	//
	// Sets an initial value without firing the output.
	//
	void Init( Type value )
	{
		m_Value.Set( fieldType, &value );
	}

	//
	// Sets a value and fires the output.
	//
	void Set( Type value, CBaseEntity *pActivator, CBaseEntity *pCaller )
	{
		m_Value.Set( fieldType, &value );
		FireOutput( m_Value, pActivator, pCaller );
	}

	//
	// Returns the current value.
	//
	Type Get( void )
	{
		return *((Type*)&m_Value);
	}
};


//
// Template specializations for type Vector, so we can implement Get, Set, and Init differently.
//
template<>
class C_EntityOutputTemplate<class Vector, FIELD_VECTOR> : public C_BaseEntityOutput
{
public:
	void Init( const Vector &value )
	{
		m_Value.SetVector3D( value );
	}

	void Set( const Vector &value, CBaseEntity *pActivator, CBaseEntity *pCaller )
	{
		m_Value.SetVector3D( value );
		FireOutput( m_Value, pActivator, pCaller );
	}

	void Get( Vector &vec )
	{
		m_Value.Vector3D(vec);
	}
};


template<>
class C_EntityOutputTemplate<class Vector, FIELD_POSITION_VECTOR> : public C_BaseEntityOutput
{
public:
	void Init( const Vector &value )
	{
		m_Value.SetPositionVector3D( value );
	}

	void Set( const Vector &value, CBaseEntity *pActivator, CBaseEntity *pCaller )
	{
		m_Value.SetPositionVector3D( value );
		FireOutput( m_Value, pActivator, pCaller );
	}

	void Get( Vector &vec )
	{
		m_Value.Vector3D(vec);
	}
};


//-----------------------------------------------------------------------------
// Purpose: parameterless entity event
//-----------------------------------------------------------------------------
class C_OutputEvent : public C_BaseEntityOutput
{
public:
	// void Firing, no parameter
	void FireOutput( CBaseEntity *pActivator, CBaseEntity *pCaller, float fDelay = 0 );
};

// useful typedefs for allowed output data types
typedef C_EntityOutputTemplate<variant_t,FIELD_INPUT>		COutputVariant;
typedef C_EntityOutputTemplate<int,FIELD_INTEGER>			COutputInt;
typedef C_EntityOutputTemplate<float,FIELD_FLOAT>			COutputFloat;
typedef C_EntityOutputTemplate<string_t,FIELD_STRING>		COutputString;
typedef C_EntityOutputTemplate<EHANDLE,FIELD_EHANDLE>		COutputEHANDLE;
typedef C_EntityOutputTemplate<Vector,FIELD_VECTOR>			COutputVector;
typedef C_EntityOutputTemplate<Vector,FIELD_POSITION_VECTOR>	COutputPositionVector;
typedef C_EntityOutputTemplate<color32,FIELD_COLOR32>		COutputColor32;

#endif
