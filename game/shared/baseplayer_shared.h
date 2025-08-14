//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BASEPLAYER_SHARED_H
#define BASEPLAYER_SHARED_H
#ifdef _WIN32
#pragma once
#endif

// PlayerUse defines
#define	PLAYER_USE_RADIUS	80.f
#define CONE_45_DEGREES		0.707f
#define CONE_15_DEGREES		0.9659258f
#define CONE_90_DEGREES		0

#define TRAIN_ACTIVE	0x80 
#define TRAIN_NEW		0xc0
#define TRAIN_OFF		0x00
#define TRAIN_NEUTRAL	0x01
#define TRAIN_SLOW		0x02
#define TRAIN_MEDIUM	0x03
#define TRAIN_FAST		0x04 
#define TRAIN_BACK		0x05

// entity messages
#define PLAY_PLAYER_JINGLE	1
#define UPDATE_PLAYER_RADAR	2

#define DEATH_ANIMATION_TIME	3.0f

typedef struct 
{
	Vector		m_vecAutoAimDir;		// The direction autoaim wishes to point.
	Vector		m_vecAutoAimPoint;		// The point (world space) that autoaim is aiming at.
	EHANDLE		m_hAutoAimEntity;		// The entity that autoaim is aiming at.
	bool		m_bAutoAimAssisting;	// If this is true, autoaim is aiming at the target. If false, the player is naturally aiming.
	bool		m_bOnTargetNatural;		
	float		m_fScale;
	float		m_fMaxDist;
} autoaim_params_t;

enum stepsoundtimes_t
{
	STEPSOUNDTIME_NORMAL = 0,
	STEPSOUNDTIME_ON_LADDER,
	STEPSOUNDTIME_WATER_KNEE,
	STEPSOUNDTIME_WATER_FOOT,
};

void CopySoundNameWithModifierToken( char *pchDest, const char *pchSource, int nMaxLenInChars, const char *pchToken );

// TODO_ENHANCED: checks if this affects vehicles properly too! It should.
#if defined( CLIENT_DLL )
#define CBasePlayer C_BasePlayer
#endif

class CBasePlayer;

struct BasePlayerInterpolationCommandContext
{
	enum
	{
		BEFORE_MOVEMENT,
		APPLIED,
		AFTER_MOVEMENT,
		MAX
	};

	virtual void Start( CBasePlayer* player );
	virtual void Interpolate( CBasePlayer* player );
	virtual void Finish( CBasePlayer* player );

	// TODO_ENHANCED:
	// check if GetLocalAngles is used for camera. (if not can be ignored)
	// Every variables that are possibly interpolated and used by the camera
	// We need also to move punchangle code after game movement, but before interpolating.
	// Basically, there shouldn't be any networked variables being set in PostThink that affects player's camera.
	struct Data
	{
		QAngle m_angLocalRotation;
		Vector m_vecLocalOrigin;
		Vector m_vecViewOffset;
		QAngle m_vecPunchAngle;
		QAngle m_vecPunchAngleVel;
	};

	Data data[MAX];
};

// Shared header file for players
#if defined( CLIENT_DLL )
#include "c_baseplayer.h"
#else
#include "player.h"
#endif

#endif // BASEPLAYER_SHARED_H
