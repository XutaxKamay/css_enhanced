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
#include "interpolatedvar.h"

// 20 ticks should be enough
constexpr auto g_nDefaultTicksToInterpolate = 20;
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
ConVar_ServerBounded* cl_predict = &cl_predict_var;

ConVar cl_interpolation_amount( "cl_interpolation_amount",
								"0",
								FCVAR_ARCHIVE,
								"Number of ticks at least to interpolate entities with. 0 is default.",
								true,
								0.0,
								true,
								( float )( MAX_INTERPOLATION_TICK_HISTORY - 1 ) );

ConVar cl_interp_type( "cl_interp_type",
					   "0",
					   FCVAR_NOT_CONNECTED | FCVAR_ARCHIVE | FCVAR_USERINFO,
					   "0) Linear 1) Hermite",
					   true,
					   0.0f,
					   true,
					   ( float )( CInterpolationType::MAX_AND_NOT_SET - 1 ) );

int GetClientInterpolationAmountInTicks()
{
	static ConVarRef cl_interpolate( "cl_interpolate" );

	if ( !cl_interpolate.GetBool() )
	{
		return 0;
	}

	// Some sane defaults
	if ( cl_interpolation_amount.GetInt() <= 0 || cl_interpolation_amount.GetInt() >= MAX_INTERPOLATION_TICK_HISTORY )
	{
		return g_nDefaultTicksToInterpolate;
	}

	return cl_interpolation_amount.GetInt();
}
