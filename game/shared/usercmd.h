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

struct SimulationData
{
	// TODO_ENHANCED:
	// For now we send the last received update for animations.
	// We might optimize this by sending a base counter and round the other entities values to it.
	float sim_time;
	float anim_time;
};

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
		command_number = 0;
		viewangles.Init();
		forwardmove = 0.0f;
		sidemove = 0.0f;
		upmove = 0.0f;
		buttons = 0;
		impulse = 0;
		weaponselect = 0;
		weaponsubtype = 0;
		random_seed = 0;
		mousedx = 0;
		mousedy = 0;

        hasbeenpredicted = false;

        for (int i = 0; i < MAX_EDICTS; i++)
        {
            simulationdata[i] = {};
        }
		debug_hitboxes = DEBUG_HITBOXES_OFF;
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
		entitygroundcontact.RemoveAll();
#endif

        interpolated_amount = 0.0f;
	}

	CUserCmd& operator =( const CUserCmd& src )
	{
		if ( this == &src )
			return *this;

		command_number		= src.command_number;
		viewangles			= src.viewangles;
		forwardmove			= src.forwardmove;
		sidemove			= src.sidemove;
		upmove				= src.upmove;
		buttons				= src.buttons;
		impulse				= src.impulse;
		weaponselect		= src.weaponselect;
		weaponsubtype		= src.weaponsubtype;
		random_seed			= src.random_seed;
		mousedx				= src.mousedx;
		mousedy				= src.mousedy;

		hasbeenpredicted	= src.hasbeenpredicted;

        for (int i = 0; i < MAX_EDICTS; i++)
        {
            simulationdata[i] = src.simulationdata[i];
		}
		debug_hitboxes = src.debug_hitboxes;
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
		entitygroundcontact			= src.entitygroundcontact;
#endif

        interpolated_amount = src.interpolated_amount;
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
		CRC32_ProcessBuffer( &crc, &command_number, sizeof( command_number ) );
		CRC32_ProcessBuffer( &crc, &viewangles, sizeof( viewangles ) );    
		CRC32_ProcessBuffer( &crc, &forwardmove, sizeof( forwardmove ) );   
		CRC32_ProcessBuffer( &crc, &sidemove, sizeof( sidemove ) );      
		CRC32_ProcessBuffer( &crc, &upmove, sizeof( upmove ) );         
		CRC32_ProcessBuffer( &crc, &buttons, sizeof( buttons ) );		
		CRC32_ProcessBuffer( &crc, &impulse, sizeof( impulse ) );        
		CRC32_ProcessBuffer( &crc, &weaponselect, sizeof( weaponselect ) );	
		CRC32_ProcessBuffer( &crc, &weaponsubtype, sizeof( weaponsubtype ) );
		CRC32_ProcessBuffer( &crc, &random_seed, sizeof( random_seed ) );
		CRC32_ProcessBuffer( &crc, simulationdata, sizeof( simulationdata ) );
		CRC32_ProcessBuffer( &crc, &debug_hitboxes, sizeof( debug_hitboxes ) );
		CRC32_ProcessBuffer( &crc, &interpolated_amount, sizeof( interpolated_amount ) );
		CRC32_Final( &crc );

		return crc;
	}

	// Allow command, but negate gameplay-affecting values
	void MakeInert( void )
	{
		Reset();
	}

	// For matching server and client commands for debugging
	int		command_number;
	
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

	int		random_seed;	// For shared random functions

	short	mousedx;		// mouse accum in x from create move
	short	mousedy;		// mouse accum in y from create move

	// Client only, tracks whether we've predicted this command at least once
	bool	hasbeenpredicted;

	// TODO_ENHANCED: Lag compensate also other entities when needed.
	// Send simulation times for each players for lag compensation.
	SimulationData simulationdata[MAX_EDICTS];

	enum debug_hitboxes_t : uint8
	{
		DEBUG_HITBOXES_OFF,
		DEBUG_HITBOXES_ON_FIRE = 1 << 0,
		DEBUG_HITBOXES_ON_HIT  = 1 << 1
	};

	uint8 debug_hitboxes;

	// TODO_ENHANCED: check README_ENHANCED in host.cpp!
	float interpolated_amount;

	// Back channel to communicate IK state
#if defined( HL2_DLL ) || defined( HL2_CLIENT_DLL )
	CUtlVector< CEntityGroundContact > entitygroundcontact;
#endif

};

void ReadUsercmd( bf_read *buf, CUserCmd *move, CUserCmd *from );
void WriteUsercmd( bf_write *buf, const CUserCmd *to, const CUserCmd *from );

#endif // USERCMD_H
