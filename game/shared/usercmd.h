//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//
#include "const.h"
#ifdef CLIENT_DLL
#include "cbase.h"
#endif
#include "shareddefs.h"
#if !defined( USERCMD_H )
#define USERCMD_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "utlvector.h"
#include "imovehelper.h"
#include "checksum_crc.h"

#ifndef CLIENT_DLL
#include "baseanimating.h"
#include "BaseAnimatingOverlay.h"
#else
#include "c_baseanimating.h"
#include "c_baseanimatingoverlay.h"
#endif

#define MAX_LAYER_RECORDS (CBaseAnimatingOverlay::MAX_OVERLAYS)
#define MAX_POSE_PARAMETERS (MAXSTUDIOPOSEPARAM)
#define MAX_ENCODED_CONTROLLERS (MAXSTUDIOBONECTRLS)

class bf_read;
class bf_write;

// #define USERCMD_DEBUG_SIMULATION_DATA

#ifdef USERCMD_DEBUG_SIMULATION_DATA
struct SimulationData
{
	uint64 sim_tick_count;
	uint64 anim_tick_count;
	bool is_sim_interpolated;
	bool is_anim_interpolated;

	// TODO_ENHANCED:
	// For now we send the last received update for animations.
	// We might optimize this by sending a base counter and round the other entities values to it.
	uint64 interpolated_sim_tick_count;
	uint64 interpolated_anim_tick_count;
	uint64 end_sim_tick_count;
	uint64 end_anim_tick_count;
};
#endif

class CEntityGroundContact
{
public:
	int					entindex;
	float				minheight;
	float				maxheight;
};

class CUserCmd
{
public:
	CUserCmd()
	{
		Reset();
	}

	virtual ~CUserCmd() { };

	void Reset()
	{
		viewangles.Init();
		forwardmove	  = 0.0f;
		sidemove	  = 0.0f;
		upmove		  = 0.0f;
		buttons		  = 0;
		impulse		  = 0;
		weaponselect  = 0;
		weaponsubtype = 0;
		mousedx		  = 0;
		mousedy		  = 0;

		hasbeenpredicted = false;
#ifdef USERCMD_DEBUG_SIMULATION_DATA
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			simulationdata[i] = {};
		}
#endif
		debug_flags = DEBUG_FLAG_NONE;
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
		entitygroundcontact.RemoveAll();
#endif
		snapshot_tickcount		 = 0;
		interpolated_amount_frac = 0.0f;
	}

	CUserCmd& operator =( const CUserCmd& src )
	{
		if ( this == &src )
			return *this;

		viewangles			= src.viewangles;
		forwardmove			= src.forwardmove;
		sidemove			= src.sidemove;
		upmove				= src.upmove;
		buttons				= src.buttons;
		impulse				= src.impulse;
		weaponselect		= src.weaponselect;
		weaponsubtype		= src.weaponsubtype;
		mousedx				= src.mousedx;
		mousedy				= src.mousedy;
		hasbeenpredicted	= src.hasbeenpredicted;
#ifdef USERCMD_DEBUG_SIMULATION_DATA
		for ( int i = 0; i < MAX_EDICTS; i++ )
		{
			simulationdata[i] = src.simulationdata[i];
		}
#endif
		debug_flags = src.debug_flags;
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
		entitygroundcontact = src.entitygroundcontact;
#endif

		interpolated_amount_frac = src.interpolated_amount_frac;
		snapshot_tickcount		 = src.snapshot_tickcount;
		return *this;
	}

	CUserCmd( const CUserCmd& src )
	{
		*this = src;
	}

	CRC32_t GetChecksum( void ) const
	{
		CRC32_t crc;

		CRC32_Init( &crc );
		CRC32_ProcessBuffer( &crc, &viewangles, sizeof( viewangles ) );    
		CRC32_ProcessBuffer( &crc, &forwardmove, sizeof( forwardmove ) );   
		CRC32_ProcessBuffer( &crc, &sidemove, sizeof( sidemove ) );      
		CRC32_ProcessBuffer( &crc, &upmove, sizeof( upmove ) );         
		CRC32_ProcessBuffer( &crc, &buttons, sizeof( buttons ) );		
		CRC32_ProcessBuffer( &crc, &impulse, sizeof( impulse ) );        
		CRC32_ProcessBuffer( &crc, &weaponselect, sizeof( weaponselect ) );	
		CRC32_ProcessBuffer( &crc, &weaponsubtype, sizeof( weaponsubtype ) );
#ifdef USERCMD_DEBUG_SIMULATION_DATA
		CRC32_ProcessBuffer( &crc, simulationdata, sizeof( simulationdata ) );
#endif
		CRC32_ProcessBuffer( &crc, &debug_flags, sizeof( debug_flags ) );
		CRC32_ProcessBuffer( &crc, &interpolated_amount_frac, sizeof( interpolated_amount_frac ) );
		CRC32_ProcessBuffer( &crc, &snapshot_tickcount, sizeof( snapshot_tickcount ) );
		CRC32_Final( &crc );

		return crc;
	}

	// Allow command, but negate gameplay-affecting values
	void MakeInert( void )
	{
		Reset();
	}
	
	// Player instantaneous view angles.
	QAngle	viewangles;     
	// Intended velocities
	//	forward velocity.
	float	forwardmove;   
	//  sideways velocity.
	float	sidemove;      
	//  upward velocity.
	float	upmove;         
	// Attack button states
	int		buttons;		
	// Impulse command issued.
	byte    impulse;        
	// Current weapon id
	int		weaponselect;	
	int		weaponsubtype;

	short	mousedx;		// mouse accum in x from create move
	short	mousedy;		// mouse accum in y from create move

	// Client only, tracks whether we've predicted this command at least once
	bool	hasbeenpredicted;

#ifdef USERCMD_DEBUG_SIMULATION_DATA
	// TODO_ENHANCED: Lag compensate also other entities when needed.
	// Send simulation times for each players for lag compensation.
	SimulationData simulationdata[MAX_EDICTS];
#endif

	enum debug_flags_t : uint8
	{
		DEBUG_FLAG_NONE              = 0,
		DEBUG_FLAG_FIRE        = 1 << 0,
		DEBUG_FLAG_HIT         = 1 << 1
	};

	uint8 debug_flags;

	// TODO_ENHANCED: check README_ENHANCED in host.cpp!
	float interpolated_amount_frac;
	uint64 snapshot_tickcount;

	// Back channel to communicate IK state
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
	CUtlVector< CEntityGroundContact > entitygroundcontact;
#endif

};

void ReadUsercmd( bf_read *buf, CUserCmd *move, CUserCmd *from );
void WriteUsercmd( bf_write *buf, const CUserCmd *to, const CUserCmd *from );

#endif // USERCMD_H
