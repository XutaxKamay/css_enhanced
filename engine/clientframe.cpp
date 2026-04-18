//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "clientframe.h"
#include "framesnapshot.h"

#include "tier0/memdbgon.h"

CClientFrame::CClientFrame( CFrameSnapshot *pSnapshot )
{
	last_entity = 0;
	transmit_always = NULL;
	from_baseline = NULL;
	tick_count = pSnapshot->m_nTickCount;
	m_pSnapshot = NULL;
	m_pNext = NULL;
	SetSnapshot( pSnapshot );
}

CClientFrame::CClientFrame( int tickcount )
{
	last_entity = 0;
	transmit_always = NULL;
	from_baseline = NULL;
	tick_count = tickcount;
	m_pSnapshot = NULL;
	m_pNext = NULL;
}

CClientFrame::CClientFrame( void )
{
	last_entity = 0;
	transmit_always = NULL;
	from_baseline = NULL;
	tick_count = 0;
	m_pSnapshot = NULL;
	m_pNext = NULL;
}

void CClientFrame::Init( int tickcount )
{
	tick_count = tickcount;
}

void CClientFrame::Init( CFrameSnapshot *pSnapshot )
{
	tick_count = pSnapshot->m_nTickCount;
	SetSnapshot( pSnapshot );
}

CClientFrame::~CClientFrame()
{
	SetSnapshot( NULL );

	if ( transmit_always != NULL )
	{
		delete transmit_always;
		transmit_always = NULL;
	}
}

void CClientFrame::SetSnapshot( CFrameSnapshot *pSnapshot )
{
	if ( m_pSnapshot == pSnapshot )
		return;

	if( pSnapshot )
		pSnapshot->AddReference();

	if ( m_pSnapshot )
		m_pSnapshot->ReleaseReference();

	m_pSnapshot = pSnapshot;
}

void CClientFrame::CopyFrame( CClientFrame &frame )
{
	tick_count = frame.tick_count;	
	last_entity = frame.last_entity;
	
	SetSnapshot( frame.GetSnapshot() );

	transmit_entity = frame.transmit_entity;

	if ( frame.transmit_always )
	{
		Assert( transmit_always == NULL );
		transmit_always = new CBitVec<MAX_EDICTS>;
		*transmit_always = *(frame.transmit_always);
	}
}

CClientFrame *CClientFrameManager::GetClientFrame( int nTick, bool bExact )
{
	if ( nTick < 0 )
		return NULL;

	if ( m_Frames.Count() == 0 )
		return NULL;

	CClientFrame *lastFrame = nullptr;

	for ( int i = 0; i < m_Frames.Count(); ++i )
	{
		CClientFrame *frame = m_Frames[i];
		if ( !frame )
			continue;

		if ( frame->tick_count >= nTick )
		{
			if ( frame->tick_count == nTick )
				return frame;
			
			if ( bExact )
				return NULL;

			return lastFrame;
		}

		lastFrame = frame;
	}

	if ( bExact )
		return NULL;
	
	return lastFrame;
}

int CClientFrameManager::CountClientFrames( void )
{
	return m_Frames.Count();
}

void CClientFrameManager::UpdateLinks()
{
	for ( int i = 0; i < m_Frames.Count(); ++i )
	{
		CClientFrame *frame = m_Frames[i];
		if ( frame )
		{
			frame->m_pNext = ( i + 1 < m_Frames.Count() ) ? m_Frames[i + 1] : nullptr;
		}
	}
}

int CClientFrameManager::AddClientFrame( CClientFrame *frame )
{
	Assert( frame->tick_count > 0 );
	
	frame->m_pNext = nullptr;

	if ( m_Frames.Count() >= MAX_CLIENT_FRAMES )
	{
		RemoveOldestFrame();
	}

	m_Frames.AddToTail( frame );
	
	UpdateLinks();
	
	return m_Frames.Count();
}

void CClientFrameManager::RemoveOldestFrame( void )
{
	if ( m_Frames.Count() == 0 )
		return;

	CClientFrame *frame = m_Frames[0];
	FreeFrame( frame );
	m_Frames.Remove( 0 );
	
	UpdateLinks();
}

void CClientFrameManager::DeleteClientFrames( int nTick )
{
	if ( nTick < 0 )
	{
		while ( m_Frames.Count() > 0 )
		{
			RemoveOldestFrame();
		}
	}
	else
	{
		for ( int i = m_Frames.Count() - 1; i >= 0; --i )
		{
			CClientFrame *frame = m_Frames[i];
			if ( frame && frame->tick_count < nTick )
			{
				FreeFrame( frame );
				m_Frames.Remove( i );
			}
		}
		
		UpdateLinks();
	}
}

CClientFrame* CClientFrameManager::AllocateFrame()
{
	CClientFrame *frame = m_ClientFramePool.Alloc();
	frame->m_pNext = nullptr;
	return frame;
}

void CClientFrameManager::FreeFrame( CClientFrame* pFrame )
{
	if ( !pFrame )
	{
		Warning( "CClientFrameManager::FreeFrame: null frame pointer\n" );
		return;
	}

	if ( pFrame->IsMemPoolAllocated() )
	{
		m_ClientFramePool.Free( pFrame );
	}
	else
	{
		delete pFrame;
	}
}

CClientFrameManager::CClientFrameManager( void )
:	m_ClientFramePool( MAX_CLIENT_FRAMES, CUtlMemoryPool::GROW_SLOW )
{
	m_Frames.EnsureCapacity( MAX_CLIENT_FRAMES );
}

CClientFrameManager::~CClientFrameManager( void )
{
	DeleteClientFrames( -1 );
	Assert( m_Frames.Count() == 0 );
}