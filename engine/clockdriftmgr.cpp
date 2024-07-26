//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "client.h"
#include "clockdriftmgr.h"
#include "demo.h"
#include "server.h"
#include "enginethreads.h"


ConVar cl_clock_correction( "cl_clock_correction", "1", FCVAR_CHEAT, "Enable/disable clock correction on the client." );

ConVar cl_clockdrift_max_ms( "cl_clockdrift_max_ms", "150", FCVAR_CHEAT, "Maximum number of milliseconds the clock is allowed to drift before the client snaps its clock to the server's." );
ConVar cl_clockdrift_max_ms_threadmode( "cl_clockdrift_max_ms_threadmode", "0", FCVAR_CHEAT, "Maximum number of milliseconds the clock is allowed to drift before the client snaps its clock to the server's." );

ConVar cl_clock_showdebuginfo( "cl_clock_showdebuginfo", "0", FCVAR_CHEAT, "Show debugging info about the clock drift. ");

ConVar cl_clock_correction_force_server_tick( "cl_clock_correction_force_server_tick", "999", FCVAR_CHEAT, "Force clock correction to match the server tick + this offset (-999 disables it)."  );
	 

// -------------------------------------------------------------------------------------------------- /
// CClockDriftMgr implementation.
// -------------------------------------------------------------------------------------------------- /

CClockDriftMgr::CClockDriftMgr()
{
	Clear();
}


void CClockDriftMgr::Clear()
{
    m_nClientTick = 0;
	m_nServerTick = 0;
    m_nOldServerTick  = 0;
    m_nLaggedClientTick = 0;
    m_flServerHostFrametime = 0.0f;
    m_flServerHostFrametimeStdDeviation = 0.0f;
}


// when running in threaded host mode, the clock drifts by a predictable algorithm
// because the client lags the server by one frame
// so at each update from the network we have lastframeticks-1 pending ticks to execute
// on the client.  If the clock has drifted by exactly that amount, allow it to drift temporarily
// NOTE: When the server gets paused the tick count is still incorrect for a frame
// NOTE: It should be possible to fix this by applying pause before the tick is incremented
// NOTE: or decrementing the client tick after receiving pause
// NOTE: This is due to the fact that currently pause is applied at frame start on the server
// NOTE: and frame end on the client
void CClockDriftMgr::SetServerTick( int nTick, int nLaggedTick, float flServerHostFrametime, float flServerHostFrametimeStdDeviation )
{
#if !defined( SWDS )
    m_nServerTick = nTick;
    m_nLaggedClientTick = nLaggedTick;
    m_flServerHostFrametime = flServerHostFrametime;
    m_flServerHostFrametimeStdDeviation = flServerHostFrametimeStdDeviation;

    int clientTick = m_nClientTick + g_ClientGlobalVariables.simTicksThisFrame - 1;
    int nMaxDriftTicks = IsEngineThreaded() ? TIME_TO_TICKS((cl_clockdrift_max_ms_threadmode.GetFloat() / 1000.0)) :
                                              TIME_TO_TICKS((cl_clockdrift_max_ms.GetFloat() / 1000.0));


	if (cl_clock_correction_force_server_tick.GetInt() == 999)
	{
        if (IsClockCorrectionEnabled())
        {
            // Take the difference between last sent client tick and server tick
            // This will give how much we are shifted from the server perfectly
            // If server fps is higher than tickrate.
            m_nLagDiff = m_nServerTick - m_nLaggedClientTick;
        }
    	// If this is the first tick from the server, or if we get further than cl_clockdrift_max_ticks off, then
		// use the old behavior and slam the server's tick into the client tick.
        else if (!IsClockCorrectionEnabled() || clientTick == 0 || abs(nTick - clientTick) > nMaxDriftTicks)
        {
            m_nClientTick = (nTick - (g_ClientGlobalVariables.simTicksThisFrame - 1));
            if (m_nClientTick < cl.oldtickcount)
            {
                cl.oldtickcount = m_nClientTick;
            }
        }
    }
	else
	{
		// Used for testing..
		m_nClientTick = (nTick + cl_clock_correction_force_server_tick.GetInt());
	}

    ShowDebugInfo();
    m_nOldServerTick = m_nServerTick;
#endif // SWDS
}

void CClockDriftMgr::IncrementCachedTickCount(bool bFinalTick)
{
    if (bFinalTick)
    {
        m_nCachedRealClientTick += m_nNumberOfTicks;
    }

    m_nClientTick = m_nCachedRealClientTick + m_nLagDiff;
}

void CClockDriftMgr::ShowDebugInfo()
{
#if !defined( SWDS )
	if ( !cl_clock_showdebuginfo.GetInt() )
		return;

	if ( IsClockCorrectionEnabled() )
    {
        int clientTick = m_nClientTick + g_ClientGlobalVariables.simTicksThisFrame - 1;

		ConMsg( "Clock drift: client sim tick: %i, client tick: %i, server tick: %i, lagged tick: %i, cached server tick: %i\n", clientTick, m_nClientTick, m_nServerTick, m_nLaggedClientTick, m_nCachedRealClientTick);
	}
	else
	{
		ConMsg( "Clock drift disabled.\n" );
    }
#endif
}

extern float NET_GetFakeLag();
extern ConVar net_usesocketsforloopback;

bool CClockDriftMgr::IsClockCorrectionEnabled()
{
#ifdef SWDS
	return false;
#else

	bool bIsMultiplayer = NET_IsMultiplayer();
	// Assume we always want it in multiplayer
	bool bWantsClockDriftMgr = bIsMultiplayer;
	// If we're a multiplayer listen server, we can back off of that if we have zero latency (faked or due to sockets)
	if ( bIsMultiplayer )
	{
		bool bIsListenServer = sv.IsActive();
		bool bLocalConnectionHasZeroLatency = ( NET_GetFakeLag() <= 0.0f ) && !net_usesocketsforloopback.GetBool();

		if ( bIsListenServer && bLocalConnectionHasZeroLatency )
		{
			bWantsClockDriftMgr = false;
		}
	}

	// Only in multi-threaded client/server OR in multi player, but don't use it if we're the listen server w/ no fake lag
	return cl_clock_correction.GetInt() && 
		( IsEngineThreaded() ||	bWantsClockDriftMgr );		
#endif
}
