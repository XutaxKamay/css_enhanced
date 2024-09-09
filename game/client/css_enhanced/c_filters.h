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

class C_BaseFilter : public C_BaseEntity
{
public:
	DECLARE_CLASS(C_BaseFilter, C_BaseEntity);
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	virtual void PostDataUpdate( DataUpdateType_t updateType );

	bool PassesFilter( C_BaseEntity *pCaller, C_BaseEntity *pEntity );
	bool PassesDamageFilter( const CTakeDamageInfo &info );

	// Inputs
	void InputTestActivator( inputdata_t &inputdata );

	// Outputs
	C_OutputEvent	m_OnPass;		// Fired when filter is passed
	C_OutputEvent	m_OnFail;		// Fired when filter is failed

	// Vars
	CNetworkVar(bool, m_bNegated);

	bool m_bInitialized;

protected:
	virtual bool PassesFilterImpl( C_BaseEntity *pCaller, C_BaseEntity *pEntity );
	virtual bool PassesDamageFilterImpl(const CTakeDamageInfo &info);

	DECLARE_DATADESC();
};

#endif