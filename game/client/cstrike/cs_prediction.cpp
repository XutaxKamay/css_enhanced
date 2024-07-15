//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "iclientvehicle.h"
#include "prediction.h"
#include "c_cs_player.h"
#include "igamemovement.h"


static CMoveData g_MoveData;
CMoveData *g_pMoveData = &g_MoveData;


class CCSPrediction : public CPrediction
{
DECLARE_CLASS( CCSPrediction, CPrediction );

public:
	virtual void    StartCommand( CBasePlayer *player, CUserCmd *cmd );
	virtual void	SetupMove( C_BasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move );
	virtual void	FinishMove( C_BasePlayer *player, CUserCmd *ucmd, CMoveData *move );
};

void CCSPrediction::StartCommand(CBasePlayer* player, CUserCmd* cmd)
{
    CCSPlayer* pPlayer = ToCSPlayer(player);

	BaseClass::StartCommand( player, cmd );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCSPrediction::SetupMove( C_BasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, 
	CMoveData *move )
{
	player->AvoidPhysicsProps( ucmd );

	// Call the default SetupMove code.
	BaseClass::SetupMove( player, ucmd, pHelper, move );
	
	IClientVehicle *pVehicle = player->GetVehicle();
	if (pVehicle && gpGlobals->frametime != 0)
	{
		pVehicle->SetupMove( player, ucmd, pHelper, move ); 
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCSPrediction::FinishMove( C_BasePlayer *player, CUserCmd *ucmd, CMoveData *move )
{
	// Call the default FinishMove code.
	BaseClass::FinishMove( player, ucmd, move );

	IClientVehicle *pVehicle = player->GetVehicle();
	if (pVehicle && gpGlobals->frametime != 0)
	{
		pVehicle->FinishMove( player, ucmd, move );
	}
}


// Expose interface to engine
// Expose interface to engine
static CCSPrediction g_Prediction;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CCSPrediction, IPrediction, VCLIENT_PREDICTION_INTERFACE_VERSION, g_Prediction );

CPrediction *prediction = &g_Prediction;

