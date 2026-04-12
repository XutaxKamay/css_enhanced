#include "cbase.h"
#include "c_hud_bullet_debugger.h"
#include "iclientmode.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>
#include "c_cs_player.h"

#include "tier0/memdbgon.h"

using namespace vgui;

ConVar cl_bullet_debugger_enable( "cl_bullet_debugger_enable",
								  "0",
								  FCVAR_ARCHIVE,
								  "Master toggle for bullet debugging (overlays + HUD)" );
ConVar cl_bullet_debugger_hud_enable( "cl_bullet_debugger_hud_enable",
									  "1",
									  FCVAR_ARCHIVE,
									  "Show bullet debugger HUD panel" );
ConVar cl_bullet_debugger_show_hitboxes( "cl_bullet_debugger_show_hitboxes",
										 "1",
										 FCVAR_ARCHIVE,
										 "Show hitboxes when firing (triggers bullet_player_hitboxes event)" );
ConVar cl_bullet_debugger_show_prediction( "cl_bullet_debugger_show_prediction",
										   "1",
										   FCVAR_ARCHIVE,
										   "Show client-predicted hitboxes" );
ConVar cl_bullet_debugger_show_server( "cl_bullet_debugger_show_server", "1", FCVAR_ARCHIVE, "Show server hitboxes" );
ConVar cl_bullet_debugger_show_prediction_computed( "cl_bullet_debugger_show_prediction_computed",
													"1",
													FCVAR_ARCHIVE,
													"Show prediction-computed hitboxes (recomputed from predicted "
													"vars)" );
ConVar cl_bullet_debugger_show_traces( "cl_bullet_debugger_show_traces",
									   "1",
									   FCVAR_ARCHIVE,
									   "Show bullet trace paths and hit player hitboxes" );
ConVar cl_bullet_debugger_show_only_on_error( "cl_bullet_debugger_show_only_on_error",
											  "0",
											  FCVAR_ARCHIVE,
											  "Only draw overlays when tolerance-exceeding mismatches detected" );
ConVar cl_bullet_debugger_tolerance( "cl_bullet_debugger_tolerance",
									 "2.0",
									 FCVAR_ARCHIVE,
									 "Distance tolerance for hit matching (units)" );
ConVar cl_bullet_debugger_duration( "cl_bullet_debugger_duration",
									"4.0",
									FCVAR_ARCHIVE,
									"Duration in seconds that debug overlays persist" );

static void DrawText( vgui::HFont font, int x, int y, Color clr, const char* text )
{
	surface()->DrawSetTextFont( font );
	surface()->DrawSetTextPos( x, y );
	surface()->DrawSetTextColor( clr );

	wchar_t wbuf[512];
	V_snwprintf( wbuf, ARRAYSIZE( wbuf ), L"%S", text );
	surface()->DrawUnicodeString( wbuf );
}

static Color MatchColor( bool match )
{
	return match ? Color( 0, 255, 0, 255 ) : Color( 255, 0, 0, 255 );
}

static Color DiffColor( float diff, float tol )
{
	if ( diff <= FLT_EPSILON )
	{
		return Color( 0, 255, 0, 255 );
	}
	if ( diff <= tol )
	{
		return Color( 255, 255, 0, 255 );
	}
	return Color( 255, 0, 0, 255 );
}

CHudBulletDebugger::~CHudBulletDebugger()
{
}

CHudBulletDebugger::CHudBulletDebugger( const char* pElementName )
 : CHudElement( pElementName ),
   BaseClass( NULL, "HudBulletDebugger" )
{
	vgui::Panel* pParent = g_pClientMode->GetViewport();
	SetParent( pParent );
	SetHiddenBits( HIDEHUD_HEALTH );
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

void CHudBulletDebugger::Init( void )
{
	Reset();
}

void CHudBulletDebugger::Reset( void )
{
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

bool CHudBulletDebugger::ShouldDraw( void )
{
	if ( !cl_bullet_debugger_enable.GetBool() )
	{
		return false;
	}

	if ( !cl_bullet_debugger_hud_enable.GetBool() )
	{
		return false;
	}

	return CHudElement::ShouldDraw();
}

void CHudBulletDebugger::SetEntry( const BulletDebugEntry& entry )
{
	m_Entry = entry;
}

void CHudBulletDebugger::UpdateTraceResult( bool bMatch, float srcDiff, float dstDiff )
{
	m_Entry.bHasBulletTrace	  = true;
	m_Entry.bBulletTraceMatch = bMatch;
	m_Entry.flTraceSrcDiff	  = srcDiff;
	m_Entry.flTraceDstDiff	  = dstDiff;
}

void CHudBulletDebugger::ClearEntry( void )
{
	memset( &m_Entry, 0, sizeof( m_Entry ) );
}

void CHudBulletDebugger::ApplySchemeSettings( vgui::IScheme* pScheme )
{
	m_hFont		 = pScheme->GetFont( "Default", true );
	m_hFontSmall = pScheme->GetFont( "DefaultSmall", true );
	BaseClass::ApplySchemeSettings( pScheme );
}

void CHudBulletDebugger::Paint()
{
	if ( !cl_bullet_debugger_enable.GetBool() )
	{
		return;
	}

	if ( m_Entry.playerIndex == 0 )
	{
		return;
	}

	const float tol = cl_bullet_debugger_tolerance.GetFloat();
	const Color white( 255, 255, 255, 220 );
	const Color gray( 180, 180, 180, 200 );
	const Color dimGray( 100, 100, 100, 150 );
	const Color srvColor( 0, 255, 255, 220 );
	const Color predColor( 0, 255, 0, 220 );

	int x  = XRES( 10 );
	int y  = YRES( 10 );
	int lh = YRES( 14 );

	char buf[256];

	// --- Header ---
	V_sprintf_safe( buf, "BULLET DEBUG - %s [%d]", m_Entry.playerName, m_Entry.playerIndex );
	DrawText( m_hFont, x, y, white, buf );
	y += lh;

	// --- Overall status ---
	if ( m_Entry.bOverallMatch )
	{
		DrawText( m_hFont, x, y, Color( 0, 255, 0, 255 ), "MATCH" );
	}
	else
	{
		int totalDiffs = m_Entry.nSrvPredVarsDiffs + m_Entry.nSrvPredBonesDiffs + m_Entry.nSrvSrvComputedBonesDiffs
						 + m_Entry.nSrvPredComputedBonesDiffs + m_Entry.nSrvComputedPredComputedBonesDiffs;
		V_sprintf_safe( buf, "MISMATCH (%d total diffs)", totalDiffs );
		DrawText( m_hFont, x, y, Color( 255, 0, 0, 255 ), buf );
	}
	y += lh + YRES( 2 );

	// --- Diagnosis ---
	if ( !m_Entry.bOverallMatch || ( m_Entry.bHasBulletTrace && !m_Entry.bBulletTraceMatch ) )
	{
		DrawText( m_hFontSmall, x, y, Color( 255, 200, 100, 220 ), "Diagnosis:" );
		y += lh;

		if ( m_Entry.bHasBulletTrace && !m_Entry.bBulletTraceMatch )
		{
			DrawText( m_hFontSmall,
					  x + XRES( 4 ),
					  y,
					  Color( 255, 150, 100, 220 ),
					  "Bullet path differs between client and server" );
			y += lh;
		}

		if ( !m_Entry.bSrvPredVarsMatch )
		{
			DrawText( m_hFontSmall,
					  x + XRES( 4 ),
					  y,
					  Color( 255, 150, 100, 220 ),
					  "Client had stale animation state (position/angles/sequence differ)" );
			y += lh;
		}
		else if ( !m_Entry.bSrvPredBonesMatch )
		{
			DrawText( m_hFontSmall,
					  x + XRES( 4 ),
					  y,
					  Color( 255, 150, 100, 220 ),
					  "Bone positions differ despite matching animation state" );
			y += lh;
		}

		if ( !m_Entry.bSrvSrvComputedBonesMatch )
		{
			DrawText( m_hFontSmall,
					  x + XRES( 4 ),
					  y,
					  Color( 255, 150, 100, 220 ),
					  "Server SetupBones non-deterministic (engine issue)" );
			y += lh;
		}
		y += YRES( 2 );
	}

	// --- Bullet trace status ---
	if ( m_Entry.bHasBulletTrace )
	{
		if ( m_Entry.bBulletTraceMatch )
		{
			V_sprintf_safe( buf,
							"Bullet Trace: MATCH (src d:%.2f, dst d:%.2f)",
							m_Entry.flTraceSrcDiff,
							m_Entry.flTraceDstDiff );
			DrawText( m_hFontSmall, x, y, Color( 0, 255, 0, 255 ), buf );
		}
		else
		{
			V_sprintf_safe( buf,
							"Bullet Trace: MISMATCH (src d:%.2f, dst d:%.2f)",
							m_Entry.flTraceSrcDiff,
							m_Entry.flTraceDstDiff );
			DrawText( m_hFontSmall, x, y, Color( 255, 0, 0, 255 ), buf );
		}
		y += lh;
	}

	// --- Comparison results ---
	if ( m_Entry.bHasPrediction )
	{
		DrawText( m_hFontSmall, x, y, gray, "Comparisons:" );
		y += lh;

		// Helper to format comparison line
		auto DrawComparison = [&]( const char* label, bool match, int diffs )
		{
			if ( match )
			{
				V_sprintf_safe( buf, "  %s OK", label );
			}
			else
			{
				V_sprintf_safe( buf, "  %s FAIL (%d)", label, diffs );
			}
			DrawText( m_hFontSmall, x, y, MatchColor( match ), buf );
			y += lh;
		};

		DrawComparison( "Anim Vars (Srv/Pred):     ", m_Entry.bSrvPredVarsMatch, m_Entry.nSrvPredVarsDiffs );
		DrawComparison( "Bones (Srv/Pred):         ", m_Entry.bSrvPredBonesMatch, m_Entry.nSrvPredBonesDiffs );
		DrawComparison( "Bones (Srv/SrvComp):      ",
						m_Entry.bSrvSrvComputedBonesMatch,
						m_Entry.nSrvSrvComputedBonesDiffs );
		DrawComparison( "Bones (Srv/PredComp):     ",
						m_Entry.bSrvPredComputedBonesMatch,
						m_Entry.nSrvPredComputedBonesDiffs );
		DrawComparison( "Bones (SrvComp/PredComp): ",
						m_Entry.bSrvComputedPredComputedBonesMatch,
						m_Entry.nSrvComputedPredComputedBonesDiffs );
	}
	else
	{
		DrawText( m_hFontSmall, x, y, dimGray, "No prediction data available" );
		y += lh;
	}

	y += YRES( 2 );

	// --- Key animation vars comparison ---
	int colLabel = x;
	int colSrv	 = x + XRES( 80 );
	int colPred	 = x + XRES( 250 );

	DrawText( m_hFontSmall, colLabel, y, gray, "Variable" );
	DrawText( m_hFontSmall, colSrv, y, srvColor, "Server" );
	DrawText( m_hFontSmall, colPred, y, predColor, "Prediction" );
	y += lh;

	// Position
	DrawText( m_hFontSmall, colLabel, y, gray, "Position" );
	V_sprintf_safe( buf,
					"%.2f %.2f %.2f",
					m_Entry.server.m_vecOrigin.x,
					m_Entry.server.m_vecOrigin.y,
					m_Entry.server.m_vecOrigin.z );
	DrawText( m_hFontSmall, colSrv, y, white, buf );
	if ( m_Entry.bHasPrediction )
	{
		float posDiff = VectorLength( m_Entry.server.m_vecOrigin - m_Entry.prediction.m_vecOrigin );
		V_sprintf_safe( buf,
						"%.2f %.2f %.2f (d:%.2f)",
						m_Entry.prediction.m_vecOrigin.x,
						m_Entry.prediction.m_vecOrigin.y,
						m_Entry.prediction.m_vecOrigin.z,
						posDiff );
		DrawText( m_hFontSmall, colPred, y, DiffColor( posDiff, tol ), buf );
	}
	else
	{
		DrawText( m_hFontSmall, colPred, y, dimGray, "N/A" );
	}
	y += lh;

	// Angles
	DrawText( m_hFontSmall, colLabel, y, gray, "Angles" );
	V_sprintf_safe( buf,
					"%.2f %.2f %.2f",
					m_Entry.server.m_angAngles.x,
					m_Entry.server.m_angAngles.y,
					m_Entry.server.m_angAngles.z );
	DrawText( m_hFontSmall, colSrv, y, white, buf );
	if ( m_Entry.bHasPrediction )
	{
		QAngle angDelta = m_Entry.server.m_angAngles - m_Entry.prediction.m_angAngles;
		float angDiff	= VectorLength( Vector( angDelta.x, angDelta.y, angDelta.z ) );
		V_sprintf_safe( buf,
						"%.2f %.2f %.2f (d:%.2f)",
						m_Entry.prediction.m_angAngles.x,
						m_Entry.prediction.m_angAngles.y,
						m_Entry.prediction.m_angAngles.z,
						angDiff );
		DrawText( m_hFontSmall, colPred, y, DiffColor( angDiff, tol ), buf );
	}
	else
	{
		DrawText( m_hFontSmall, colPred, y, dimGray, "N/A" );
	}
	y += lh;

	// Sequence / Cycle
	DrawText( m_hFontSmall, colLabel, y, gray, "Seq / Cycle" );
	V_sprintf_safe( buf, "%d / %.4f", m_Entry.server.m_nSequence, m_Entry.server.m_flCycle );
	DrawText( m_hFontSmall, colSrv, y, white, buf );
	if ( m_Entry.bHasPrediction )
	{
		bool seqMatch	= ( m_Entry.server.m_nSequence == m_Entry.prediction.m_nSequence );
		bool cycleMatch = ( fabsf( m_Entry.server.m_flCycle - m_Entry.prediction.m_flCycle ) <= FLT_EPSILON );
		V_sprintf_safe( buf, "%d / %.4f", m_Entry.prediction.m_nSequence, m_Entry.prediction.m_flCycle );
		DrawText( m_hFontSmall, colPred, y, MatchColor( seqMatch && cycleMatch ), buf );
	}
	else
	{
		DrawText( m_hFontSmall, colPred, y, dimGray, "N/A" );
	}
	y += lh;

	// --- Mismatch details ---
	if ( !m_Entry.bOverallMatch && m_Entry.mismatchDetails.Count() > 0 )
	{
		y += YRES( 2 );
		V_sprintf_safe( buf, "Details (%d):", m_Entry.mismatchDetails.Count() );
		DrawText( m_hFontSmall, x, y, Color( 255, 100, 100, 220 ), buf );
		y += lh;

		int maxDetails = MIN( m_Entry.mismatchDetails.Count(), 10 );
		for ( int i = 0; i < maxDetails; i++ )
		{
			V_sprintf_safe( buf, "  %s", m_Entry.mismatchDetails[i].Get() );
			DrawText( m_hFontSmall, x, y, Color( 255, 150, 150, 200 ), buf );
			y += lh;
		}
		if ( m_Entry.mismatchDetails.Count() > maxDetails )
		{
			V_sprintf_safe( buf, "  ... and %d more", m_Entry.mismatchDetails.Count() - maxDetails );
			DrawText( m_hFontSmall, x, y, dimGray, buf );
			y += lh;
		}
	}
}
