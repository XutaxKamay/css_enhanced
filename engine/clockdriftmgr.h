//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef CLOCKDRIFTMGR_H
#define CLOCKDRIFTMGR_H
#ifdef _WIN32
#pragma once
#endif


class CClockDriftMgr
{
friend class CBaseClientState;

public:
	CClockDriftMgr();

	// Is clock correction even enabled right now?
	static bool IsClockCorrectionEnabled();

	// Clear our state.
	void Clear();

	// This is called each time a server packet comes in. It is used to correlate
	// where the server is in time compared to us.
	void SetServerTick( int iServerTick, int nLaggedTick, float flServerHostFrametime, float flServerHostFrametimeStdDeviation);
	void ApplyClockCorrection(bool bFinalTick);

private:

	void ShowDebugInfo();


public:
    int m_nLagDiff;
    int m_nOldServerTick;
	int m_nServerTick;		// Last-received tick from the server.
    int m_nClientTick;
    int m_nCachedRealClientTick; // The client's own tick counter (specifically, for interpolation during rendering).
							 // The server may be on a slightly different tick and the client will drift towards it.
    int m_nLaggedClientTick;
    float m_flServerHostFrametime;
    float m_flServerHostFrametimeStdDeviation;
};


#endif // CLOCKDRIFTMGR_H
