//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "cdll_bounded_cvars.h"
#include "cdll_client_int.h"
#include "convar_serverbounded.h"
#include "icvar.h"
#include "shareddefs.h"
#include "tier0/icommandline.h"


bool g_bForceCLPredictOff = false;

// ------------------------------------------------------------------------------------------ //
// cl_predict.
// ------------------------------------------------------------------------------------------ //

class CBoundedCvar_Predict : public ConVar_ServerBounded
{
public:
	CBoundedCvar_Predict() :
	  ConVar_ServerBounded( "cl_predict", 
		  "1.0",
#if defined(DOD_DLL) || defined(CSTRIKE_DLL)
		  FCVAR_USERINFO | FCVAR_CHEAT, 
#else
		  FCVAR_USERINFO | FCVAR_NOT_CONNECTED, 
#endif
		  "Perform client side prediction." )
	  {
	  }

	  virtual float GetFloat() const
	  {
		  // Used temporarily for CS kill cam.
		  if ( g_bForceCLPredictOff )
			  return 0;

		  static const ConVar *pClientPredict = g_pCVar->FindVar( "sv_client_predict" );
		  if ( pClientPredict && pClientPredict->GetInt() != -1 )
		  {
			  // Ok, the server wants to control this value.
			  return pClientPredict->GetFloat();
		  }
		  else
		  {
			  return GetBaseFloatValue();
		  }
	  }
};

static CBoundedCvar_Predict cl_predict_var;
ConVar_ServerBounded *cl_predict = &cl_predict_var;



// ------------------------------------------------------------------------------------------ //
// cl_interp_ratio.
// ------------------------------------------------------------------------------------------ //

ConVar cl_interp_ratio("cl_interp_ratio", "2");


// ------------------------------------------------------------------------------------------ //
// cl_interp
// ------------------------------------------------------------------------------------------ //

class CBoundedCvar_Interp : public ConVar_ServerBounded
{
public:
	CBoundedCvar_Interp() :
	  ConVar_ServerBounded( "cl_interp", 
		  "-1.0",
		  FCVAR_USERINFO,
		  "Sets the interpolation amount (bounded on low side by server interp ratio settings).", true, -1.0f, true, 1.0f )
	  {
	  }

	  virtual float GetFloat() const
	  {
		  float value = GetBaseFloatValue();

		  if (value < 0.0f)
		  {
			  value = TICK_INTERVAL;
		  }

		  static const ConVar *pUpdateRate = g_pCVar->FindVar( "cl_updaterate" );
		  if ( pUpdateRate )
		  {
			  return MAX( value, cl_interp_ratio.GetFloat() / pUpdateRate->GetFloat() );
		  }
		  else
		  {
			  return value;
		  }
	  }
};

static CBoundedCvar_Interp cl_interp_var;
ConVar_ServerBounded *cl_interp = &cl_interp_var;

float GetClientInterpAmount()
{
	static const ConVar *cl_interpolate = g_pCVar->FindVar("cl_interpolate");
	static const ConVar *pUpdateRate = g_pCVar->FindVar( "cl_updaterate" );

	if (!cl_interpolate->GetBool())
	{
		return 0.0f;
	}

	if ( pUpdateRate )
	{
		// #define FIXME_INTERP_RATIO
		return MAX( cl_interp->GetFloat(), cl_interp_ratio.GetFloat() / pUpdateRate->GetFloat() );
	}
	else
	{
		if (!CommandLine()->FindParm("-hushasserts"))
		{
			AssertMsgOnce( false, "GetInterpolationAmount: can't get cl_updaterate cvar." );
		}
	
		return 0.1f;
	}
}

