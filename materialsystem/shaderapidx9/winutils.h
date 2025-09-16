//========= Copyright Valve Corporation, All rights reserved. ============//
//
//
//
//==================================================================================================
#ifndef WINUTILS_H
#define WINUTILS_H

#ifdef DXVK_ENABLED
	#include <windows.h>
	#include <d3d9types.h>
#else
	#include "togl/rendermechanism.h" // for win types
#endif

#if !defined(_WIN32)

	void Sleep( unsigned int ms );
	void* GetCurrentThread();
	void SetThreadAffinityMask( void *hThread, int nMask );
	void GlobalMemoryStatus( MEMORYSTATUS *pOut );

	#ifdef DXVK_ENABLED
		bool IsIconic( HWND hWnd );
		BOOL ClientToScreen( HWND hWnd, LPPOINT pPoint );
		BOOL GetClientRect(HWND hWnd, LPRECT pRect);
	#else 
		bool IsIconic( VD3DHWND hWnd );
		BOOL ClientToScreen( VD3DHWND hWnd, LPPOINT pPoint );
	#endif // DXVK_ENABLED

#endif !_WIN32

#endif // WINUTILS_H
