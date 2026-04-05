#ifndef HUD_LAGCOMP_DEBUG_H
#define HUD_LAGCOMP_DEBUG_H

#include "hudelement.h"
#include <vgui_controls/Panel.h>

struct DebugAnimLayer
{
	int m_nSequence;
	float m_flCycle;
	float m_flWeight;
	int m_nOrder;
	int m_fFlags;
};

struct DebugPlayerState
{
	Vector m_vecOrigin;
	QAngle m_angAngles;
	int m_nSequence;
	float m_flCycle;
	float m_flPoseParams[MAXSTUDIOPOSEPARAM];
	float m_flBoneCtrls[MAXSTUDIOBONECTRLS];
	DebugAnimLayer m_animLayers[C_BaseAnimatingOverlay::MAX_OVERLAYS];
	int m_nNumPoseParams;
	int m_nNumBoneCtrls;
	int m_nNumOverlays;

	Vector m_bonePositions[MAXSTUDIOBONES];
	QAngle m_boneAngles[MAXSTUDIOBONES];
	int m_nNumHitboxBones;
	int m_hitboxBoneIndexes[MAXSTUDIOBONES];
};

struct LagCompDebugEntry
{
	int playerIndex;
	char playerName[64];

	DebugPlayerState server;
	DebugPlayerState serverComputed;
	DebugPlayerState prediction;
	DebugPlayerState predictionComputed;

	bool bHasPrediction;

	bool bSrvPredVarsMatch;
	bool bSrvPredBonesMatch;
	bool bSrvSrvComputedBonesMatch;
	bool bSrvPredComputedBonesMatch;
	bool bSrvComputedPredComputedBonesMatch;
	bool bOverallMatch;
	int nSrvPredVarsDiffs;
	int nSrvPredBonesDiffs;
	int nSrvSrvComputedBonesDiffs;
	int nSrvPredComputedBonesDiffs;
	int nSrvComputedPredComputedBonesDiffs;

	CUtlVector< CUtlString > mismatchDetails;
};

class CHudLagCompDebug : public CHudElement,
						 public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudLagCompDebug, vgui::Panel );

  public:
	CHudLagCompDebug( const char* pElementName );

	void Init( void );
	void Reset( void );
	bool ShouldDraw( void );

	void SetEntry( const LagCompDebugEntry& entry );
	void ClearEntry( void );

  private:
	void Paint();
	void ApplySchemeSettings( vgui::IScheme* pScheme );

	LagCompDebugEntry m_Entry;
	vgui::HFont m_hFont;
	vgui::HFont m_hFontSmall;
};

DECLARE_HUDELEMENT( CHudLagCompDebug );

#endif // HUD_LAGCOMP_DEBUG_H
