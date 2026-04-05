#include "cbase.h"
#include "hud_lagcomp_debug.h"
#include "iclientmode.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>
#include "c_cs_player.h"

#include "tier0/memdbgon.h"

using namespace vgui;

extern ConVar cl_debug_hitbox_enable;
extern ConVar cl_debug_hitbox_tolerance;

static void DrawText( vgui::HFont font, int x, int y, Color clr, const char* text )
{
	surface()->DrawSetTextFont( font );
	surface()->DrawSetTextPos( x, y );
	surface()->DrawSetTextColor( clr );

	wchar_t wbuf[512];
	V_snwprintf( wbuf, ARRAYSIZE( wbuf ), L"%S", text );
	surface()->DrawUnicodeString( wbuf );
}

static Color DiffColor( float a, float b, float tol )
{
	float diff = fabsf( a - b );
	if ( diff <= FLT_EPSILON )
		return Color( 0, 255, 0, 255 );
	if ( diff <= tol )
		return Color( 255, 255, 0, 255 );
	return Color( 255, 0, 0, 255 );
}

static Color DiffColorInt( int a, int b )
{
	if ( a == b )
		return Color( 0, 255, 0, 255 );
	return Color( 255, 0, 0, 255 );
}

CHudLagCompDebug::CHudLagCompDebug( const char* pElementName )
	: CHudElement( pElementName ), BaseClass( NULL, "HudLagCompDebug" )
{
	vgui::Panel* pParent = g_pClientMode->GetViewport();
	SetParent( pParent );
	SetHiddenBits( HIDEHUD_HEALTH );
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

void CHudLagCompDebug::Init( void )
{
	Reset();
}

void CHudLagCompDebug::Reset( void )
{
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

bool CHudLagCompDebug::ShouldDraw( void )
{
	return cl_debug_hitbox_enable.GetBool() && CHudElement::ShouldDraw();
}

void CHudLagCompDebug::SetEntry( const LagCompDebugEntry& entry )
{
	m_Entry = entry;
}

void CHudLagCompDebug::ClearEntry( void )
{
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

void CHudLagCompDebug::ApplySchemeSettings( vgui::IScheme* pScheme )
{
	m_hFont		 = pScheme->GetFont( "Default", true );
	m_hFontSmall = pScheme->GetFont( "DefaultSmall", true );
	BaseClass::ApplySchemeSettings( pScheme );
}

void CHudLagCompDebug::Paint()
{
	if ( !cl_debug_hitbox_enable.GetBool() )
		return;

	if ( m_Entry.playerIndex == 0 )
		return;

	float tol = cl_debug_hitbox_tolerance.GetFloat();
	int x = XRES( 10 );
	int y = YRES( 10 );
	int lh = YRES( 16 );
	int lhSmall = YRES( 14 );

	DrawText( m_hFont, x, y, Color( 255, 255, 255, 220 ), "=== LAG COMP DEBUG ===" );
	y += lh;

	char buf[256];
	V_sprintf_safe( buf, "Target: %s [%d]", m_Entry.playerName, m_Entry.playerIndex );
	DrawText( m_hFont, x, y, Color( 200, 200, 200, 200 ), buf );
	y += lh;

	// --- Raw animation/simulation vars: Server vs Prediction ---
	DrawText( m_hFont, x, y, Color( 255, 255, 255, 220 ), "--- Raw Vars ---" );
	y += lh;

	// Column headers
	int colLabel = x;
	int colSrv = x + 130;
	int colPred = x + 340;

	DrawText( m_hFontSmall, colLabel, y, Color( 180, 180, 180, 180 ), "Field" );
	DrawText( m_hFontSmall, colSrv, y, Color( 0, 100, 255, 200 ), "Server" );
	DrawText( m_hFontSmall, colPred, y, Color( 0, 255, 0, 200 ), "Prediction" );
	y += lhSmall;

	// Position
	DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), "Position" );
	V_sprintf_safe( buf, "%f %f %f", m_Entry.server.m_vecOrigin.x, m_Entry.server.m_vecOrigin.y, m_Entry.server.m_vecOrigin.z );
	DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
	if ( m_Entry.bHasPrediction )
	{
		V_sprintf_safe( buf, "%f %f %f", m_Entry.prediction.m_vecOrigin.x, m_Entry.prediction.m_vecOrigin.y, m_Entry.prediction.m_vecOrigin.z );
		DrawText( m_hFontSmall, colPred, y, DiffColor( m_Entry.server.m_vecOrigin.x, m_Entry.prediction.m_vecOrigin.x, tol ), buf );
	}
	else
		DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
	y += lhSmall;

	// Angles
	DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), "Angles" );
	V_sprintf_safe( buf, "%f %f %f", m_Entry.server.m_angAngles.x, m_Entry.server.m_angAngles.y, m_Entry.server.m_angAngles.z );
	DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
	if ( m_Entry.bHasPrediction )
	{
		V_sprintf_safe( buf, "%f %f %f", m_Entry.prediction.m_angAngles.x, m_Entry.prediction.m_angAngles.y, m_Entry.prediction.m_angAngles.z );
		DrawText( m_hFontSmall, colPred, y, DiffColor( m_Entry.server.m_angAngles.x, m_Entry.prediction.m_angAngles.x, tol ), buf );
	}
	else
		DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
	y += lhSmall;

	// Sequence
	DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), "Sequence" );
	V_sprintf_safe( buf, "%d", m_Entry.server.m_nSequence );
	DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
	if ( m_Entry.bHasPrediction )
	{
		V_sprintf_safe( buf, "%d", m_Entry.prediction.m_nSequence );
		DrawText( m_hFontSmall, colPred, y, DiffColorInt( m_Entry.server.m_nSequence, m_Entry.prediction.m_nSequence ), buf );
	}
	else
		DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
	y += lhSmall;

	// Cycle
	DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), "Cycle" );
	V_sprintf_safe( buf, "%f", m_Entry.server.m_flCycle );
	DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
	if ( m_Entry.bHasPrediction )
	{
		V_sprintf_safe( buf, "%f", m_Entry.prediction.m_flCycle );
		DrawText( m_hFontSmall, colPred, y, DiffColor( m_Entry.server.m_flCycle, m_Entry.prediction.m_flCycle, tol ), buf );
	}
	else
		DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
	y += lhSmall;

	// Pose params
	for ( int i = 0; i < m_Entry.server.m_nNumPoseParams; i++ )
	{
		char ppName[32];
		V_sprintf_safe( ppName, "PoseParam[%d]", i );
		DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), ppName );
		V_sprintf_safe( buf, "%f", m_Entry.server.m_flPoseParams[i] );
		DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
		if ( m_Entry.bHasPrediction )
		{
			V_sprintf_safe( buf, "%f", m_Entry.prediction.m_flPoseParams[i] );
			DrawText( m_hFontSmall, colPred, y, DiffColor( m_Entry.server.m_flPoseParams[i], m_Entry.prediction.m_flPoseParams[i], tol ), buf );
		}
		else
			DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
		y += lhSmall;
	}

	// Bone controllers
	for ( int i = 0; i < m_Entry.server.m_nNumBoneCtrls; i++ )
	{
		char bcName[32];
		V_sprintf_safe( bcName, "BoneCtrl[%d]", i );
		DrawText( m_hFontSmall, colLabel, y, Color( 200, 200, 200, 200 ), bcName );
		V_sprintf_safe( buf, "%f", m_Entry.server.m_flBoneCtrls[i] );
		DrawText( m_hFontSmall, colSrv, y, Color( 200, 200, 200, 200 ), buf );
		if ( m_Entry.bHasPrediction )
		{
			V_sprintf_safe( buf, "%f", m_Entry.prediction.m_flBoneCtrls[i] );
			DrawText( m_hFontSmall, colPred, y, DiffColor( m_Entry.server.m_flBoneCtrls[i], m_Entry.prediction.m_flBoneCtrls[i], tol ), buf );
		}
		else
			DrawText( m_hFontSmall, colPred, y, Color( 100, 100, 100, 150 ), "N/A" );
		y += lhSmall;
	}

	// --- Bone matrix comparisons ---
	if ( m_Entry.server.m_nNumHitboxBones > 0 )
	{
		y += lhSmall / 2;
		DrawText( m_hFont, x, y, Color( 255, 255, 255, 220 ), "--- Bone Matrices ---" );
		y += lh;

		int total = m_Entry.server.m_nNumHitboxBones;

		// SRV vs PRED bones (HitboxRecord cached)
		int srvPredMatch = 0;
		for ( int i = 0; i < total; i++ )
		{
			if ( VectorLength( m_Entry.server.m_bonePositions[i] - m_Entry.prediction.m_bonePositions[i] ) <= tol )
				srvPredMatch++;
		}
		V_sprintf_safe( buf, "SRV vs PRED cached:  %d/%d match", srvPredMatch, total );
		DrawText( m_hFontSmall, colLabel, y, srvPredMatch == total ? Color(0,255,0,255) : Color(255,0,0,255), buf );
		y += lhSmall;

		// SRV vs SRV Computed (SetupBones from server vars)
		int srvSrvCompMatch = 0;
		for ( int i = 0; i < total; i++ )
		{
			if ( VectorLength( m_Entry.server.m_bonePositions[i] - m_Entry.serverComputed.m_bonePositions[i] ) <= tol )
				srvSrvCompMatch++;
		}
		V_sprintf_safe( buf, "SRV vs SRV computed: %d/%d match", srvSrvCompMatch, total );
		DrawText( m_hFontSmall, colLabel, y, srvSrvCompMatch == total ? Color(0,255,0,255) : Color(255,0,0,255), buf );
		y += lhSmall;

		// SRV vs PRED Computed (SetupBones from prediction vars)
		if ( m_Entry.bHasPrediction )
		{
			int srvPredCompMatch = 0;
			for ( int i = 0; i < total; i++ )
			{
				if ( VectorLength( m_Entry.server.m_bonePositions[i] - m_Entry.predictionComputed.m_bonePositions[i] ) <= tol )
					srvPredCompMatch++;
			}
			V_sprintf_safe( buf, "SRV vs PRED computed: %d/%d match", srvPredCompMatch, total );
			DrawText( m_hFontSmall, colLabel, y, srvPredCompMatch == total ? Color(0,255,0,255) : Color(255,0,0,255), buf );
			y += lhSmall;
		}

		// SRV Computed vs PRED Computed
		if ( m_Entry.bHasPrediction )
		{
			int srvCompPredCompMatch = 0;
			for ( int i = 0; i < total; i++ )
			{
				if ( VectorLength( m_Entry.serverComputed.m_bonePositions[i] - m_Entry.predictionComputed.m_bonePositions[i] ) <= tol )
					srvCompPredCompMatch++;
			}
			V_sprintf_safe( buf, "SRV comp vs PRED comp: %d/%d match", srvCompPredCompMatch, total );
			DrawText( m_hFontSmall, colLabel, y, srvCompPredCompMatch == total ? Color(0,255,0,255) : Color(255,0,0,255), buf );
			y += lhSmall;
		}
	}

	// --- Comparison summary ---
	y += lhSmall / 2;
	DrawText( m_hFont, x, y, Color( 255, 255, 255, 220 ), "--- Comparison ---" );
	y += lh;

	V_sprintf_safe( buf, "SRV vs PRED vars:     %d diffs", m_Entry.nSrvPredVarsDiffs );
	DrawText( m_hFont, x, y, m_Entry.bSrvPredVarsMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
	y += lh;

	V_sprintf_safe( buf, "SRV vs PRED bones:    %d diffs", m_Entry.nSrvPredBonesDiffs );
	DrawText( m_hFont, x, y, m_Entry.bSrvPredBonesMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
	y += lh;

	V_sprintf_safe( buf, "SRV vs SRV computed:  %d diffs", m_Entry.nSrvSrvComputedBonesDiffs );
	DrawText( m_hFont, x, y, m_Entry.bSrvSrvComputedBonesMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
	y += lh;

	V_sprintf_safe( buf, "SRV vs PRED computed: %d diffs", m_Entry.nSrvPredComputedBonesDiffs );
	DrawText( m_hFont, x, y, m_Entry.bSrvPredComputedBonesMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
	y += lh;

	if ( m_Entry.bHasPrediction )
	{
		V_sprintf_safe( buf, "SRV comp vs PRED comp: %d diffs", m_Entry.nSrvComputedPredComputedBonesDiffs );
		DrawText( m_hFont, x, y, m_Entry.bSrvComputedPredComputedBonesMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
		y += lh;
	}

	V_sprintf_safe( buf, "Overall: %s", m_Entry.bOverallMatch ? "MATCH" : "MISMATCH" );
	DrawText( m_hFont, x, y, m_Entry.bOverallMatch ? Color(0,255,0,255) : Color(255,0,0,255), buf );
	y += lh;

	// Mismatch details
	if ( m_Entry.mismatchDetails.Count() > 0 )
	{
		y += lhSmall / 2;
		DrawText( m_hFont, x, y, Color( 255, 255, 255, 220 ), "--- Details ---" );
		y += lh;
		for ( int i = 0; i < m_Entry.mismatchDetails.Count(); i++ )
		{
			DrawText( m_hFontSmall, x, y, Color( 255, 100, 100, 220 ), m_Entry.mismatchDetails[i].Get() );
			y += lhSmall;
		}
	}
}
