//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#include "baseentity_shared.h"
#include "c_baseentity.h"
#include "const.h"
#include "datamodel/dmelementhandle.h"
#include "platform.h"
#include "touchlink.h"
#include "utlvector.h"
#if !defined( PREDICTION_H )
#define PREDICTION_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "iprediction.h"
#include "c_baseplayer.h"
#include "cdll_bounded_cvars.h"

class CMoveData;
class CUserCmd;

//-----------------------------------------------------------------------------
// Purpose: Implements prediction in the client .dll
//-----------------------------------------------------------------------------
class CPrediction : public IPrediction
{
// Construction
public:
	DECLARE_CLASS_GAMEROOT( CPrediction, IPrediction );

					CPrediction( void );
	virtual			~CPrediction( void );

	virtual void	Init( void );
	virtual void	Shutdown( void );

// Implement IPrediction
public:

	virtual void	Update
					( 
						int startframe,		// World update ( un-modded ) most recently received
						bool validframe,		// Is frame data valid
						int incoming_acknowledged, // Last command acknowledged to have been run by server (un-modded)
						int outgoing_command	// Last command (most recent) sent to server (un-modded)
					);

	virtual void	OnReceivedUncompressedPacket( void );

	virtual void	PreEntityPacketReceived( int nCmdSequencesAck );
	virtual void	PostEntityPacketReceived( void );
	virtual void	PostNetworkDataReceived( int nCmdSequencesAck );

	virtual bool	InPrediction( void ) const;
	virtual bool	IsFirstTimePredicted( void ) const;

	float			GetIdealPitch( void ) const 
	{
		return m_flIdealPitch;
	}

	// The engine needs to be able to access a few predicted values
	virtual void	GetViewOrigin( Vector& org );
	virtual void	SetViewOrigin( Vector& org );
	virtual void	GetViewAngles( QAngle& ang );
	virtual void	SetViewAngles( QAngle& ang );

	virtual void	GetLocalViewAngles( QAngle& ang );
	virtual void	SetLocalViewAngles( QAngle& ang );

	virtual void	RunCommand( C_BasePlayer *player, CUserCmd *ucmd, IMoveHelper *moveHelper );

// Internal
protected:
	virtual void	SetupMove( C_BasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move );
	virtual void	FinishMove( C_BasePlayer *player, CUserCmd *ucmd, CMoveData *move );
	virtual void	SetIdealPitch ( C_BasePlayer *player, const Vector& origin, const QAngle& angles, const Vector& viewheight );

	void			CheckError( int nCmdSequencesAck );

	// Called before and after any movement processing
	void			StartCommand( C_BasePlayer *player, CUserCmd *cmd );
	void			FinishCommand( C_BasePlayer *player );

	// Helpers to call pre and post think for player, and to call think if a think function is set
	void			RunPreThink( C_BasePlayer *player );
	void			RunThink (C_BasePlayer *ent, double frametime );
	void			RunPostThink( C_BasePlayer *player );
    void 			CheckMovingGround( CBasePlayer *player, double frametime );
private:
	virtual void	_Update
					( 
						bool received_new_world_update,
						bool validframe,		// Is frame data valid
						int incoming_acknowledged, // Last command acknowledged to have been run by server (un-modded)
						int outgoing_command	// Last command (most recent) sent to server (un-modded)
					);

	// Actually does the prediction work, returns false if an error occurred
	bool			PerformPrediction( bool received_new_world_update, C_BasePlayer *localPlayer, int incoming_acknowledged, int outgoing_command );

	void			RestoreEntityToPredictedFrame( int predicted_frame );
	int				ComputeFirstCommandToExecute( bool received_new_world_update, int incoming_acknowledged, int outgoing_command );

	void			DumpEntity( C_BaseEntity *ent, int nCmdSequencesAck );

	void			ShutdownPredictables( void );
	void			ReinitPredictables( void );

	void			RemoveStalePredictedEntities( int last_command_packet );
	void			RestoreOriginalEntityState( void );
	void			RunSimulation( int current_command, float curtime, CUserCmd *cmd, C_BasePlayer *localPlayer );
	void            RestorePredictedTouched( int current_command );
	void            StorePredictedTouched( int current_command );
	void			Untouch( void );
	void			StorePredictionResults( int predicted_frame );
	bool			ShouldDumpEntity( C_BaseEntity *ent );

	void			SmoothViewOnMovingPlatform( C_BasePlayer *pPlayer, Vector& offset );

#if !defined( NO_ENTITY_PREDICTION )
// Data
protected:
	// Last object the player was standing on
	CHandle< C_BaseEntity > m_hLastGround;
private:
	bool			m_bInPrediction;
	bool			m_bFirstTimePredicted;
	bool			m_bOldCLPredictValue;
	bool			m_bEnginePaused;

	// Last network origin for local player
	int				m_nPreviousStartFrame;
	int				m_bPreviousAckHadErrors;

#endif
	float			m_flIdealPitch;

    struct SavedTouch_t
	{
		int nEntityTouched;
		int touchStamp;
		int flags;
	};

	struct TouchedHistory
	{
		CUtlVector< SavedTouch_t > savedTouches;
		CUtlVector< EHANDLE > touchedTriggerEntities;
		int nEntityIndex;
	};

	CUtlVector< TouchedHistory > m_TouchedHistory[MULTIPLAYER_BACKUP];
	C_EventQueue m_EventQueueHistory[MULTIPLAYER_BACKUP];

  public:
	CGlobalVarsBase m_saveVars;
};
 
extern CPrediction *prediction;

#endif // PREDICTION_H