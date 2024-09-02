//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "c_baseanimatingoverlay.h"
#include "bone_setup.h"
#include "interpolatedvar.h"
#include "studio.h"
#include "tier0/vprof.h"
#include "engine/ivdebugoverlay.h"
#include "datacache/imdlcache.h"
#include "eventlist.h"

#include "dt_utlvector_recv.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar r_sequence_debug;

C_BaseAnimatingOverlay::C_BaseAnimatingOverlay()
{
	// FIXME: where does this initialization go now?
	//for ( int i=0; i < MAX_OVERLAYS; i++ )
	//{
	//	memset( &m_Layer[i], 0, sizeof(m_Layer[0]) );
	//	m_Layer[i].m_nOrder = MAX_OVERLAYS;
	//}

	// FIXME: where does this initialization go now?
	// AddVar( m_Layer, &m_iv_AnimOverlay, LATCH_ANIMATION_VAR );
}

#undef CBaseAnimatingOverlay



BEGIN_RECV_TABLE_NOBASE(CAnimationLayer, DT_Animationlayer)
	RecvPropInt(	RECVINFO_NAME(m_nSequence, m_nSequence)),
	RecvPropFloat(	RECVINFO_NAME(m_flCycle, m_flCycle)),
	RecvPropFloat(	RECVINFO_NAME(m_flPrevCycle, m_flPrevCycle)),
	RecvPropFloat(	RECVINFO_NAME(m_flWeight, m_flWeight)),
	RecvPropInt(	RECVINFO_NAME(m_nOrder, m_nOrder)),
	RecvPropInt(	RECVINFO_NAME(m_fFlags, m_fFlags)),
END_RECV_TABLE()

const char *s_m_iv_AnimOverlayNames[C_BaseAnimatingOverlay::MAX_OVERLAYS] =
{
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay00",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay01",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay02",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay03",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay04",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay05",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay06",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay07",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay08",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay09",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay10",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay11",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay12",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay13",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay14"
};

void ResizeAnimationLayerCallback( void *pStruct, int offsetToUtlVector, int len )
{
	C_BaseAnimatingOverlay *pEnt = (C_BaseAnimatingOverlay*)pStruct;
	CUtlVector < C_AnimationLayer > *pVec = &pEnt->m_AnimOverlay;
	CUtlVector< CInterpolatedVar< C_AnimationLayer > > *pVecIV = &pEnt->m_iv_AnimOverlay;
	
	Assert( (char*)pVec - (char*)pEnt == offsetToUtlVector );
	Assert( pVec->Count() == pVecIV->Count() );
	Assert( pVec->Count() <= C_BaseAnimatingOverlay::MAX_OVERLAYS );
	
	int diff = len - pVec->Count();

	

	if ( diff == 0 )
		return;

	// remove all entries
	for ( int i=0; i < pVec->Count(); i++ )
	{
		// pEnt->RemoveVar( &pVec->Element( i ) );
	}

	// adjust vector sizes
	if ( diff > 0 )
	{
		pVec->AddMultipleToTail( diff );
		pVecIV->AddMultipleToTail( diff );
	}
	else
	{
		pVec->RemoveMultiple( len, -diff );
		pVecIV->RemoveMultiple( len, -diff );
	}

	// Rebind all the variables in the ent's list.
	for ( int i=0; i < len; i++ )
	{
		IInterpolatedVar *pWatcher = &pVecIV->Element( i );
		pWatcher->SetDebugName( s_m_iv_AnimOverlayNames[i] );
		// pEnt->AddVar( &pVec->Element( i ), pWatcher, LATCH_ANIMATION_VAR );
	}
	// FIXME: need to set historical values of nOrder in pVecIV to MAX_OVERLAY
	
}

BEGIN_RECV_TABLE_NOBASE( C_BaseAnimatingOverlay, DT_OverlayVars )
	 RecvPropUtlVector( 
		RECVINFO_UTLVECTOR_SIZEFN( m_AnimOverlay, ResizeAnimationLayerCallback ), 
		C_BaseAnimatingOverlay::MAX_OVERLAYS,
		RecvPropDataTable(NULL, 0, 0, &REFERENCE_RECV_TABLE( DT_Animationlayer ) ) )
END_RECV_TABLE()


IMPLEMENT_CLIENTCLASS_DT( C_BaseAnimatingOverlay, DT_BaseAnimatingOverlay, CBaseAnimatingOverlay )
	RecvPropDataTable( "overlay_vars", 0, 0, &REFERENCE_RECV_TABLE( DT_OverlayVars ) )
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_BaseAnimatingOverlay )

/*
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flWeight, FIELD_FLOAT),
*/

END_PREDICTION_DATA()

C_AnimationLayer* C_BaseAnimatingOverlay::GetAnimOverlay( int i )
{
	Assert( i >= 0 && i < MAX_OVERLAYS );
	return &m_AnimOverlay[i];
}


void C_BaseAnimatingOverlay::SetNumAnimOverlays( int num )
{
	if ( m_AnimOverlay.Count() < num )
	{
		m_AnimOverlay.AddMultipleToTail( num - m_AnimOverlay.Count() );
	}
	else if ( m_AnimOverlay.Count() > num )
	{
		m_AnimOverlay.RemoveMultiple( num, m_AnimOverlay.Count() - num );
	}
}


int C_BaseAnimatingOverlay::GetNumAnimOverlays() const
{
	return m_AnimOverlay.Count();
}


void C_BaseAnimatingOverlay::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	BaseClass::GetRenderBounds( theMins, theMaxs );

	if ( !IsRagdoll() )
	{
		CStudioHdr *pStudioHdr = GetModelPtr();
		if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
			return;

		int nSequences = pStudioHdr->GetNumSeq();

		int i;
		for (i = 0; i < m_AnimOverlay.Count(); i++)
		{
			if (m_AnimOverlay[i].m_flWeight > 0.0)
			{
				if ( m_AnimOverlay[i].m_nSequence >= nSequences )
				{
					continue;
				}

				mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( m_AnimOverlay[i].m_nSequence );
				VectorMin( seqdesc.bbmin, theMins, theMins );
				VectorMax( seqdesc.bbmax, theMaxs, theMaxs );
			}
		}
	}
}

void C_BaseAnimatingOverlay::AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime )
{
    BaseClass::AccumulateLayers(boneSetup, pos, q, currentTime);

	// sort the layers
	int layer[MAX_OVERLAYS] = {};
	int i;
	for (i = 0; i < m_AnimOverlay.Count(); i++)
    {
		layer[i] = MAX_OVERLAYS;
	}
	for (i = 0; i < m_AnimOverlay.Count(); i++)
	{
		CAnimationLayer &pLayer = m_AnimOverlay[i];
		if( (pLayer.m_flWeight > 0) && pLayer.IsActive() && pLayer.m_nOrder >= 0 && pLayer.m_nOrder < m_AnimOverlay.Count())
		{
			layer[pLayer.m_nOrder] = i;
		}
    }
	for (i = 0; i < m_AnimOverlay.Count(); i++)
	{
		if (layer[i] >= 0 && layer[i] < m_AnimOverlay.Count())
		{
			CAnimationLayer &pLayer = m_AnimOverlay[layer[i]];
			// UNDONE: Is it correct to use overlay weight for IK too?
			boneSetup.AccumulatePose( pos, q, pLayer.m_nSequence, pLayer.m_flCycle, pLayer.m_flWeight, currentTime, m_pIk );
		}
	}
}

void C_BaseAnimatingOverlay::DoAnimationEvents( CStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
		return;

	MDLCACHE_CRITICAL_SECTION();

	int nSequences = pStudioHdr->GetNumSeq();

	BaseClass::DoAnimationEvents( pStudioHdr );

	bool watch = false; // Q_strstr( hdr->name, "rifle" ) ? true : false;

	int j;
	for (j = 0; j < m_AnimOverlay.Count(); j++)
	{
		if ( m_AnimOverlay[j].m_nSequence >= nSequences )
		{
			continue;
		}

		mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( m_AnimOverlay[j].m_nSequence );
		if ( seqdesc.numevents == 0 )
			continue;

		// stalled?
		if (m_AnimOverlay[j].m_flCycle == m_flOverlayPrevEventCycle[j])
			continue;

		bool bLoopingSequence = IsSequenceLooping( m_AnimOverlay[j].m_nSequence );

		bool bLooped = false;

		//in client code, m_flOverlayPrevEventCycle is set to -1 when we first start an overlay, looping or not
		if ( bLoopingSequence &&
			m_flOverlayPrevEventCycle[j] > 0.0f &&
			m_AnimOverlay[j].m_flCycle <= m_flOverlayPrevEventCycle[j] )
		{
			if (m_flOverlayPrevEventCycle[j] - m_AnimOverlay[j].m_flCycle > 0.5)
			{
				bLooped = true;
			}
			else
			{
				// things have backed up, which is bad since it'll probably result in a hitch in the animation playback
				// but, don't play events again for the same time slice
				return;
			}
		}

		mstudioevent_t *pevent = seqdesc.pEvent( 0 );

		// This makes sure events that occur at the end of a sequence occur are
		// sent before events that occur at the beginning of a sequence.
		if (bLooped)
		{
			for (int i = 0; i < (int)seqdesc.numevents; i++)
			{
				// ignore all non-client-side events
				if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
				{
					if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
						 continue;
				}
				else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
					continue;
			
				if ( pevent[i].cycle <= m_flOverlayPrevEventCycle[j] )
					continue;
				
				if ( watch )
				{
					Msg( "%i FE %i Looped cycle %f, prev %f ev %f (time %.3f)\n",
						gpGlobals->tickcount,
						pevent[i].event,
						pevent[i].cycle,
						m_flOverlayPrevEventCycle[j],
						(float)m_AnimOverlay[j].m_flCycle,
						gpGlobals->curtime );
				}
					
					
				FireEvent( GetAbsOrigin(), GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
			}

			// Necessary to get the next loop working
			m_flOverlayPrevEventCycle[j] = -0.01;
		}

		for (int i = 0; i < (int)seqdesc.numevents; i++)
		{
			if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
			{
				if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
					 continue;
			}
			else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
				continue;

			if ( (pevent[i].cycle > m_flOverlayPrevEventCycle[j] && pevent[i].cycle <= m_AnimOverlay[j].m_flCycle) )
			{
				if ( watch )
				{
					Msg( "%i (seq: %d) FE %i Normal cycle %f, prev %f ev %f (time %.3f)\n",
						gpGlobals->tickcount,
						m_AnimOverlay[j].m_nSequence,
						pevent[i].event,
						pevent[i].cycle,
						m_flOverlayPrevEventCycle[j],
						(float)m_AnimOverlay[j].m_flCycle,
						gpGlobals->curtime );
				}

				FireEvent( GetAbsOrigin(), GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
			}
		}

		m_flOverlayPrevEventCycle[j] = m_AnimOverlay[j].m_flCycle;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CStudioHdr *C_BaseAnimatingOverlay::OnNewModel()
{
	CStudioHdr *hdr = BaseClass::OnNewModel();

	// Clear out animation layers
	for ( int i=0; i < m_AnimOverlay.Count(); i++ )
	{
		m_AnimOverlay[i].Reset();
		m_AnimOverlay[i].m_nOrder = MAX_OVERLAYS;
	}

	return hdr;
}