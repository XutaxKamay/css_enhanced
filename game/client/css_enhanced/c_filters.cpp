#include "cbase.h"
#include "c_filters.h"
#include "c_ai_basenpc.h"

#include "tier0/memdbgon.h"

// ###################################################################
//	> BaseFilter
// ###################################################################
LINK_ENTITY_TO_CLASS(filter_base, C_BaseFilter);

BEGIN_DATADESC( C_BaseFilter )

	DEFINE_KEYFIELD(m_bNegated, FIELD_BOOLEAN, "Negated"),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_INPUT, "TestActivator", InputTestActivator ),

	// Outputs
	DEFINE_OUTPUT( m_OnPass, "OnPass"),
	DEFINE_OUTPUT( m_OnFail, "OnFail"),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT( C_BaseFilter, DT_BaseFilter, CBaseFilter )
	RecvPropBool(RECVINFO(m_bNegated))
END_RECV_TABLE()

C_BaseFilter::C_BaseFilter()
{
}

//-----------------------------------------------------------------------------

bool C_BaseFilter::PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
{
	return true;
}


bool C_BaseFilter::PassesFilter( CBaseEntity *pCaller, CBaseEntity *pEntity )
{
	bool baseResult = PassesFilterImpl( pCaller, pEntity );
	return (m_bNegated) ? !baseResult : baseResult;
}


bool C_BaseFilter::PassesDamageFilter(const CTakeDamageInfo &info)
{
	bool baseResult = PassesDamageFilterImpl(info);
	return (m_bNegated) ? !baseResult : baseResult;
}


bool C_BaseFilter::PassesDamageFilterImpl( const CTakeDamageInfo &info )
{
	return PassesFilterImpl( NULL, info.GetAttacker() );
}

//-----------------------------------------------------------------------------
// Purpose: Input handler for testing the activator. If the activator passes the
//			filter test, the OnPass output is fired. If not, the OnFail output is fired.
//-----------------------------------------------------------------------------
void C_BaseFilter::InputTestActivator( inputdata_t &inputdata )
{
	if ( PassesFilter( inputdata.pCaller, inputdata.pActivator ) )
	{
		m_OnPass.FireOutput( inputdata.pActivator, this );
	}
	else
	{
		m_OnFail.FireOutput( inputdata.pActivator, this );
	}
}


// ###################################################################
//	> FilterMultiple
//
//   Allows one to filter through mutiple filters
// ###################################################################

class C_FilterMultiple : public C_BaseFilter
{
	DECLARE_CLASS( C_FilterMultiple, C_BaseFilter );
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

	CNetworkVar(filter_t,	m_nFilterType);
	char	    m_iFilterName[MAX_FILTERS][MAX_PATH];
	EHANDLE		m_hFilter[MAX_FILTERS];

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity );
	bool PassesDamageFilterImpl(const CTakeDamageInfo &info);
	void Activate(void);
};

LINK_ENTITY_TO_CLASS(filter_multi, C_FilterMultiple);

BEGIN_DATADESC( C_FilterMultiple )


	// Keys
	DEFINE_KEYFIELD(m_nFilterType, FIELD_INTEGER, "FilterType"),

	// Silence, Classcheck!
//	DEFINE_ARRAY( m_iFilterName, FIELD_STRING, MAX_FILTERS ),

	DEFINE_KEYFIELD(m_iFilterName[0], FIELD_STRING, "Filter01"),
	DEFINE_KEYFIELD(m_iFilterName[1], FIELD_STRING, "Filter02"),
	DEFINE_KEYFIELD(m_iFilterName[2], FIELD_STRING, "Filter03"),
	DEFINE_KEYFIELD(m_iFilterName[3], FIELD_STRING, "Filter04"),
	DEFINE_KEYFIELD(m_iFilterName[4], FIELD_STRING, "Filter05"),
	DEFINE_ARRAY( m_hFilter, FIELD_EHANDLE, MAX_FILTERS ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT( C_FilterMultiple, DT_FilterMultiple, CFilterMultiple )
	RecvPropArray( RecvPropString( RECVINFO( m_iFilterName[0]) ), m_iFilterName ),
	RecvPropInt( RECVINFO(m_nFilterType) )
END_RECV_TABLE()

//------------------------------------------------------------------------------
// Purpose : Called after all entities have been loaded
//------------------------------------------------------------------------------
void C_FilterMultiple::Activate( void )
{
	BaseClass::Activate();
	
	// We may reject an entity specified in the array of names, but we want the array of valid filters to be contiguous!
	int nNextFilter = 0;

	// Get handles to my filter entities
	for ( int i = 0; i < MAX_FILTERS; i++ )
	{
		CBaseEntity *pEntity = UTIL_FindEntityByName( NULL, m_iFilterName[i] );
		C_BaseFilter *pFilter = (C_BaseFilter *)(pEntity);
		if ( pFilter == NULL )
		{
			Warning("filter_multi: Tried to add entity (%s) which is not a filter entity!\n", STRING( m_iFilterName[i] ) );
			continue;
		}

		// Take this entity and increment out array pointer
		m_hFilter[nNextFilter] = pFilter;
		nNextFilter++;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if the entity passes our filter, false if not.
// Input  : pEntity - Entity to test.
//-----------------------------------------------------------------------------
bool C_FilterMultiple::PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
{
	// Test against each filter
	if (m_nFilterType == FILTER_AND)
	{
		for (int i=0;i<MAX_FILTERS;i++)
		{
			if (m_hFilter[i] != NULL)
			{
				C_BaseFilter* pFilter = (C_BaseFilter *)(m_hFilter[i].Get());
				if (!pFilter->PassesFilter( pCaller, pEntity ) )
				{
					return false;
				}
			}
		}
		return true;
	}
	else  // m_nFilterType == FILTER_OR
	{
		for (int i=0;i<MAX_FILTERS;i++)
		{
			if (m_hFilter[i] != NULL)
			{
				C_BaseFilter* pFilter = (C_BaseFilter *)(m_hFilter[i].Get());
				if (pFilter->PassesFilter( pCaller, pEntity ) )
				{
					return true;
				}
			}
		}
		return false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if the entity passes our filter, false if not.
// Input  : pEntity - Entity to test.
//-----------------------------------------------------------------------------
bool C_FilterMultiple::PassesDamageFilterImpl(const CTakeDamageInfo &info)
{
	// Test against each filter
	if (m_nFilterType == FILTER_AND)
	{
		for (int i=0;i<MAX_FILTERS;i++)
		{
			if (m_hFilter[i] != NULL)
			{
				C_BaseFilter* pFilter = (C_BaseFilter *)(m_hFilter[i].Get());
				if (!pFilter->PassesDamageFilter(info))
				{
					return false;
				}
			}
		}
		return true;
	}
	else  // m_nFilterType == FILTER_OR
	{
		for (int i=0;i<MAX_FILTERS;i++)
		{
			if (m_hFilter[i] != NULL)
			{
				C_BaseFilter* pFilter = (C_BaseFilter *)(m_hFilter[i].Get());
				if (pFilter->PassesDamageFilter(info))
				{
					return true;
				}
			}
		}
		return false;
	}
}


// ###################################################################
//	> FilterName
// ###################################################################
class C_FilterName : public C_BaseFilter
{
	DECLARE_CLASS( C_FilterName, C_BaseFilter );
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

public:
	char m_iFilterName[MAX_PATH];

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
		// special check for !player as GetEntityName for player won't return "!player" as a name
		if (FStrEq(STRING(m_iFilterName), "!player"))
		{
			return pEntity->IsPlayer();
		}
		else
		{
			return pEntity->NameMatches( STRING(m_iFilterName) );
		}
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_name, C_FilterName );

BEGIN_DATADESC( C_FilterName )

	// Keyfields
	DEFINE_KEYFIELD( m_iFilterName,	FIELD_STRING,	"filtername" ),

END_DATADESC()


IMPLEMENT_CLIENTCLASS_DT( C_FilterName, DT_FilterName, CFilterName )
	RecvPropString( RECVINFO(m_iFilterName) )
END_RECV_TABLE()

// ###################################################################
//	> FilterClass
// ###################################################################
class C_FilterClass : public C_BaseFilter
{
	DECLARE_CLASS( C_FilterClass, C_BaseFilter );
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

public:
	char m_iFilterClass[MAX_PATH];

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
		return pEntity->ClassMatches( STRING(m_iFilterClass) );
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_class, C_FilterClass );

BEGIN_DATADESC( C_FilterClass )

	// Keyfields
	DEFINE_KEYFIELD( m_iFilterClass,	FIELD_STRING,	"filterclass" ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT( C_FilterClass, DT_FilterClass, CFilterClass )
	RecvPropString( RECVINFO(m_iFilterClass) )
END_RECV_TABLE()

// ###################################################################
//	> FilterTeam
// ###################################################################
class C_FilterTeam : public C_BaseFilter
{
	DECLARE_CLASS( C_FilterTeam, C_BaseFilter );
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

public:
	CNetworkVar(int, m_iFilterTeam);

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
	 	return ( pEntity->GetTeamNumber() == m_iFilterTeam );
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_team, C_FilterTeam );

BEGIN_DATADESC( C_FilterTeam )

	// Keyfields
	DEFINE_KEYFIELD( m_iFilterTeam,	FIELD_INTEGER,	"filterteam" ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT( C_FilterTeam, DT_FIlterTeam, CFilterTeam )
	RecvPropInt(RECVINFO(m_iFilterTeam))
END_RECV_TABLE()

// ###################################################################
//	> FilterMassGreater
// ###################################################################
class C_FilterMassGreater : public C_BaseFilter
{
	DECLARE_CLASS( C_FilterMassGreater, C_BaseFilter );
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

public:
	CNetworkVar(float, m_fFilterMass);

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
		if ( pEntity->VPhysicsGetObject() == NULL )
			return false;

		return ( pEntity->VPhysicsGetObject()->GetMass() > m_fFilterMass );
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_mass_greater, C_FilterMassGreater );

BEGIN_DATADESC( C_FilterMassGreater )

// Keyfields
DEFINE_KEYFIELD( m_fFilterMass,	FIELD_FLOAT,	"filtermass" ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT( C_FilterMassGreater, DT_FilterMassGreater, CFilterMassGreater )
	RecvPropFloat(RECVINFO(m_fFilterMass))
END_RECV_TABLE()