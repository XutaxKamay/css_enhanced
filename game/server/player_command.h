//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef PLAYER_COMMAND_H
#define PLAYER_COMMAND_H
#pragma once


#include "edict.h"
#include "usercmd.h"


class IMoveHelper;
class CMoveData;
class CBasePlayer;

//-----------------------------------------------------------------------------
// Purpose: Server side player movement
//-----------------------------------------------------------------------------
class CPlayerMove
{
public:
	DECLARE_CLASS_NOBASE( CPlayerMove );
	
	// Construction/destruction
					CPlayerMove( void );
	virtual			~CPlayerMove( void ) {}

	// Public interfaces:
	// Run a movement command from the player
	void			RunCommand ( CBasePlayer *player, CUserCmd *ucmd, IMoveHelper *moveHelper );

protected:
	// Prepare for running movement
	virtual void	SetupMove( CBasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move );

	// Finish movement
	virtual void	FinishMove( CBasePlayer *player, CUserCmd *ucmd, CMoveData *move );

	// Called before and after any movement processing
	virtual void	StartCommand( CBasePlayer *player, CUserCmd *cmd );
	void			FinishCommand( CBasePlayer *player );

	// Helper to determine if the user is standing on ground
	void			CheckMovingGround( CBasePlayer *player, double frametime );

	// Helpers to call pre and post think for player, and to call think if a think function is set
	void			RunPreThink( CBasePlayer *player );
	void			RunThink (CBasePlayer *ent, double frametime );
	void			RunPostThink( CBasePlayer *player );
	void 			StartInterpolatingPlayer( CBasePlayer* player );
	void 			FinishInterpolatingPlayer( CBasePlayer* player );

	// TODO_ENHANCED: checks if this affects vehicles properly too! It should.
	enum INTERPOLATION_CONTEXT
	{
		BEFORE_MOVEMENT,
		AFTER_MOVEMENT,
		INTERPOLATION_CONTEXT_MAX
	};

	struct InterpolationContext
	{
		Vector m_vecLocalOrigin;
		Vector m_vecViewOffset;
	} InterpolationContexts[INTERPOLATION_CONTEXT_MAX];
};


//-----------------------------------------------------------------------------
// Singleton accessor
//-----------------------------------------------------------------------------
CPlayerMove *PlayerMove();


#endif // PLAYER_COMMAND_H
