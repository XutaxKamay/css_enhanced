//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef CLIENTFRAME_H
#define CLIENTFRAME_H
#ifdef _WIN32
#pragma once
#endif

#include <bitvec.h>
#include <const.h>
#include <tier1/mempool.h>
#include <tier1/utlvector.h>

class CFrameSnapshot;

#define MAX_CLIENT_FRAMES	2048

class CClientFrame
{
public:

	CClientFrame( void );
	CClientFrame( int tickcount );
	CClientFrame( CFrameSnapshot *pSnapshot );
	virtual ~CClientFrame();
	void Init( CFrameSnapshot *pSnapshot );
	void Init( int tickcount );

	inline CFrameSnapshot*	GetSnapshot() const { return m_pSnapshot; };
	void					SetSnapshot( CFrameSnapshot *pSnapshot );
	void					CopyFrame( CClientFrame &frame );
	virtual bool		IsMemPoolAllocated() { return true; }

public:

	int					last_entity;	// highest entity index
	int					tick_count;		// server tick of this snapshot

	CBitVec<MAX_EDICTS>	transmit_entity; 
	CBitVec<MAX_EDICTS>	*from_baseline;	
	CBitVec<MAX_EDICTS>	*transmit_always; 

	CClientFrame		*m_pNext;	// for HLTV/Replay frame iteration

private:

	CFrameSnapshot		*m_pSnapshot;
};

class CClientFrameManager
{
public:
	CClientFrameManager(void);
	virtual ~CClientFrameManager(void);

	int				AddClientFrame( CClientFrame *pFrame );
	CClientFrame	*GetClientFrame( int nTick, bool bExact = true );
	void			DeleteClientFrames( int nTick );
	int				CountClientFrames( void );
	void			RemoveOldestFrame( void );

	CClientFrame*	AllocateFrame();

	void			FreeFrame( CClientFrame* pFrame );
	void			UpdateLinks();

	CUtlVector<CClientFrame*>	m_Frames;
	CClassMemoryPool< CClientFrame >	m_ClientFramePool;
};

#endif // CLIENTFRAME_H