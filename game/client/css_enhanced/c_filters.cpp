#include "c_filters.h"

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

	DEFINE_FIELD( m_bInitialized, FIELD_BOOLEAN ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(C_BaseFilter, DT_BaseFilter, CBaseFilter)
	RecvPropBool(RECVINFO(m_bNegated)),
	RecvPropInt(RECVINFO(m_hszName)),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA_NO_BASE(C_BaseFilter)
	DEFINE_PRED_FIELD(m_hszName, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_bNegated, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA();

bool C_BaseFilter::PassesFilter( C_BaseEntity* pCaller, C_BaseEntity* pEntity )
{
	bool baseResult = PassesFilterImpl( pCaller, pEntity );
	return (m_bNegated) ? !baseResult : baseResult;
}

bool C_BaseFilter::PassesDamageFilter( const CTakeDamageInfo& info )
{
	bool baseResult = PassesDamageFilterImpl(info);
	return (m_bNegated) ? !baseResult : baseResult;
}

bool C_BaseFilter::PassesFilterImpl( C_BaseEntity* pCaller, C_BaseEntity* pEntity )
{
	return true;
}

bool C_BaseFilter::PassesDamageFilterImpl( const CTakeDamageInfo& info )
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

extern void MapEntityInit(C_BaseEntity* pEntity);

void C_BaseFilter::PostDataUpdate( DataUpdateType_t updateType )
{
	if (updateType == DATA_UPDATE_CREATED && !m_bInitialized)
	{
		MapEntityInit( this );

		m_bInitialized = true;
	}

	BaseClass::PostDataUpdate( updateType );
}



// ###################################################################
//	> FilterMultiple
//
//   Allows one to filter through mutiple filters
// ###################################################################
#define MAX_FILTERS 5
enum filter_t
{
	FILTER_AND,
	FILTER_OR,
};

class CFilterMultiple : public C_BaseFilter
{
public:
	DECLARE_CLASS( CFilterMultiple, C_BaseFilter );
	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

	filter_t	m_nFilterType;
	string_t	m_iFilterName[MAX_FILTERS];
	EHANDLE		m_hFilter[MAX_FILTERS];

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity );
	bool PassesDamageFilterImpl(const CTakeDamageInfo &info);
	void Spawn(void);

	CRC32_t m_hszFilterName[MAX_FILTERS];
};

LINK_ENTITY_TO_CLASS(filter_multi, CFilterMultiple);

BEGIN_DATADESC( CFilterMultiple )


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

// DataTable definition
IMPLEMENT_CLIENTCLASS_DT(CFilterMultiple, DT_FilterMultiple, CFilterMultiple)
    RecvPropArray3(RECVINFO_ARRAY(m_hszFilterName), RecvPropInt(RECVINFO(m_hszFilterName))),
END_RECV_TABLE()




//------------------------------------------------------------------------------
// Purpose : Called after all entities have been loaded
//------------------------------------------------------------------------------
void CFilterMultiple::Spawn( void )
{
	BaseClass::Spawn();
	
	// We may reject an entity specified in the array of names, but we want the array of valid filters to be contiguous!
	int nNextFilter = 0;

	// Get handles to my filter entities
	for ( int i = 0; i < MAX_FILTERS; i++ )
	{
		if ( m_iFilterName[i] != NULL_STRING )
		{
			CBaseEntity *pEntity = UTIL_FindEntityByNameCRC( NULL, m_hszFilterName[i] );
			C_BaseFilter *pFilter = dynamic_cast<C_BaseFilter *>(pEntity);
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

    ConMsg("CFilterMultiple::Activate - %i %i %i %i %i\n", m_hszFilterName[0], m_hszFilterName[1], m_hszFilterName[2], m_hszFilterName[3], m_hszFilterName[4]);
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if the entity passes our filter, false if not.
// Input  : pEntity - Entity to test.
//-----------------------------------------------------------------------------
bool CFilterMultiple::PassesFilterImpl( C_BaseEntity *pCaller, C_BaseEntity *pEntity )
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
bool CFilterMultiple::PassesDamageFilterImpl(const CTakeDamageInfo &info)
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
class CFilterName : public C_BaseFilter
{
	DECLARE_CLASS( CFilterName, C_BaseFilter );
  	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

public:
	CRC32_t m_hszFilterName;

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
		// special check for !player as GetEntityName for player won't return "!player" as a name

        static CRC32_t hsz_player;
        static bool bCalc = false;

        if (!bCalc)
        {
            hsz_player = UTIL_GetCheckSum("!player");

            ConMsg("CFilterName - %i\n", m_hszFilterName);

            bCalc = true;
        }

		if (hsz_player == m_hszFilterName) //(FStrEq(STRING(m_iFilterName), "!player"))
		{
			return pEntity->IsPlayer();
		}
		else
		{
            ConMsg("\tCFilterName Check - %i - %i\n", pEntity->m_hszName, m_hszFilterName);
            return pEntity->m_hszName == m_hszFilterName;
		}
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_name, CFilterName );

BEGIN_DATADESC( CFilterName )

	DEFINE_FIELD( m_hszFilterName,	FIELD_INTEGER ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(CFilterName, DT_FilterName, CFilterName)
    RecvPropInt(RECVINFO(m_hszFilterName)),
END_RECV_TABLE();


// ###################################################################
//	> FilterClass
// ###################################################################
class CFilterClass : public C_BaseFilter
{
	DECLARE_CLASS( CFilterClass, C_BaseFilter );
  	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

public:
	CRC32_t m_hszFilterClass;

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
        ConMsg("\tCFilterClass Check - %i - %i\n", pEntity->m_hszClassname, m_hszFilterClass);
		return pEntity->m_hszClassname == m_hszFilterClass;
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_class, CFilterClass );

BEGIN_DATADESC( CFilterClass )

	DEFINE_FIELD( m_hszFilterClass,	FIELD_INTEGER ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(CFilterClass, DT_FilterClass, CFilterClass)
    RecvPropInt(RECVINFO(m_hszFilterClass)),
END_RECV_TABLE();


// ###################################################################
//	> FilterTeam
// ###################################################################
class FilterTeam : public C_BaseFilter
{
	DECLARE_CLASS( FilterTeam, C_BaseFilter );
  	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

public:
	int		m_iFilterTeam;

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
	 	return ( pEntity->GetTeamNumber() == m_iFilterTeam );
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_team, FilterTeam );

BEGIN_DATADESC( FilterTeam )

	DEFINE_FIELD( m_iFilterTeam,	FIELD_INTEGER ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(FilterTeam, DT_FilterTeam, FilterTeam)
    RecvPropInt(RECVINFO(m_iFilterTeam)),
END_RECV_TABLE();


// ###################################################################
//	> FilterMassGreater
// ###################################################################
class CFilterMassGreater : public C_BaseFilter
{
	DECLARE_CLASS( CFilterMassGreater, C_BaseFilter );
  	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

public:
	float m_fFilterMass;

	bool PassesFilterImpl( CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
		if ( pEntity->VPhysicsGetObject() == NULL )
			return false;

		return ( pEntity->VPhysicsGetObject()->GetMass() > m_fFilterMass );
	}
};

LINK_ENTITY_TO_CLASS( filter_activator_mass_greater, CFilterMassGreater );

BEGIN_DATADESC( CFilterMassGreater )

DEFINE_FIELD( m_fFilterMass,	FIELD_FLOAT ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(CFilterMassGreater, DT_FilterMassGreater, CFilterMassGreater)
    RecvPropFloat(RECVINFO(m_fFilterMass)),
END_RECV_TABLE();


// ###################################################################
//	> FilterDamageType
// ###################################################################
class FilterDamageType : public C_BaseFilter
{
	DECLARE_CLASS( FilterDamageType, C_BaseFilter );
  	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

protected:

	bool PassesFilterImpl(CBaseEntity *pCaller, CBaseEntity *pEntity )
	{
	 	return true;
	}

	bool PassesDamageFilterImpl(const CTakeDamageInfo &info)
	{
	 	return info.GetDamageType() == m_iDamageType;
	}

	int m_iDamageType;
};

LINK_ENTITY_TO_CLASS( filter_damage_type, FilterDamageType );

BEGIN_DATADESC( FilterDamageType )

	DEFINE_FIELD( m_iDamageType,	FIELD_INTEGER ),

END_DATADESC()

IMPLEMENT_CLIENTCLASS_DT(FilterDamageType, DT_FilterDamageType, FilterDamageType)
    RecvPropInt(RECVINFO(m_iDamageType)),
END_RECV_TABLE();
