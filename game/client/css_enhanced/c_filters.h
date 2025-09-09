#ifndef C_FILTERS
#define C_FILTERS

#ifdef WIN32
#pragma once
#endif

#include "cbase.h"
#include "c_entityoutput.h"

#include "takedamageinfo.h"

#define MAX_FILTERS 5

#define SF_FILTER_ENEMY_NO_LOSE_AQUIRED	(1<<0)

enum filter_t
{
	FILTER_AND,
	FILTER_OR,
};

class C_BaseFilter : public C_BaseEntity
{
	DECLARE_CLASS(C_BaseFilter, C_BaseEntity);

public:
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

	C_BaseFilter();

	virtual bool ShouldPredict( void );
	bool PassesFilter( CBaseEntity *pCaller, CBaseEntity *pEntity );
	bool PassesDamageFilter( const CTakeDamageInfo &info );

	CNetworkVar(bool, m_bNegated);

	// Inputs
	void InputTestActivator( inputdata_t &inputdata );

	// Outputs
	C_OutputEvent	m_OnPass;		// Fired when filter is passed
	C_OutputEvent	m_OnFail;		// Fired when filter is failed

protected:

	virtual bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity );
	virtual bool PassesDamageFilterImpl(const CTakeDamageInfo &info);
};

#endif