//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

// #include "BaseAnimating.h"

#ifndef BASE_ANIMATING_OVERLAY_H
#define BASE_ANIMATING_OVERLAY_H
#ifdef _WIN32
#pragma once
#endif

class CBaseAnimatingOverlay;


class CBaseAnimatingOverlay : public CBaseAnimating
{
	DECLARE_CLASS( CBaseAnimatingOverlay, CBaseAnimating );

public:
	enum 
	{
		MAX_OVERLAYS = 15,
	};

protected:
	CUtlVector< CAnimationLayer	> m_AnimOverlay;
	//int				m_nActiveLayers;
	//int				m_nActiveBaseLayers;

public:
	
	virtual void	OnRestore();

	virtual void	StudioFrameAdvance();
	virtual	void	DispatchAnimEvents ( CBaseAnimating *eventHandler );
	virtual void	GetSkeleton( CStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], int boneMask );

	int		AddGestureSequence( int sequence, bool autokill = true );
	int		AddGestureSequence( int sequence, float flDuration, bool autokill = true );
	int		AddGesture( Activity activity, bool autokill = true );
	int		AddGesture( Activity activity, float flDuration, bool autokill = true );
	bool	IsPlayingGesture( Activity activity );
	void	RestartGesture( Activity activity, bool addifmissing = true, bool autokill = true );
	void	RemoveGesture( Activity activity );
	void	RemoveAllGestures( void );

	int		AddLayeredSequence( int sequence, int iPriority );

	void	SetLayerPriority( int iLayer, int iPriority );

	bool	IsValidLayer( int iLayer );

	void	SetLayerDuration( int iLayer, float flDuration );
	float	GetLayerDuration( int iLayer );

	void	SetLayerCycle( int iLayer, float flCycle );
	void	SetLayerCycle( int iLayer, float flCycle, float flPrevCycle );
	float	GetLayerCycle( int iLayer );

	void	SetLayerPlaybackRate( int iLayer, float flPlaybackRate );
	void	SetLayerWeight( int iLayer, float flWeight );
	float	GetLayerWeight( int iLayer );
	void	SetLayerBlendIn( int iLayer, float flBlendIn );
	void	SetLayerBlendOut( int iLayer, float flBlendOut );
	void	SetLayerAutokill( int iLayer, bool bAutokill );
	void	SetLayerLooping( int iLayer, bool bLooping );
	void	SetLayerNoRestore( int iLayer, bool bNoRestore );

	Activity	GetLayerActivity( int iLayer );
	int			GetLayerSequence( int iLayer );

	int		FindGestureLayer( Activity activity );

	void	RemoveLayer( int iLayer, float flKillRate = 0.2, float flKillDelay = 0.0 );
	void	FastRemoveLayer( int iLayer );

	CAnimationLayer *GetAnimOverlay( int iIndex );
	int GetNumAnimOverlays() const;
	void SetNumAnimOverlays( int num );

	void VerifyOrder( void );

	bool	HasActiveLayer( void );

private:
	int		AllocateLayer( int iPriority = 0 ); // lower priorities are processed first

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_PREDICTABLE();
};

EXTERN_SEND_TABLE(DT_BaseAnimatingOverlay);

inline int CBaseAnimatingOverlay::GetNumAnimOverlays() const
{
	return m_AnimOverlay.Count();
}

// ------------------------------------------------------------------------------------------ //
// CAnimationLayer inlines.
// ------------------------------------------------------------------------------------------ //

inline void CAnimationLayer::SetOrder( int nOrder )
{
	m_nOrder = nOrder;
}

inline void CAnimationLayer::NetworkStateChanged()
{
	if ( m_pOwnerEntity )
		m_pOwnerEntity->NetworkStateChanged();
}

inline void CAnimationLayer::NetworkStateChanged( void *pVar )
{
	if ( m_pOwnerEntity )
		m_pOwnerEntity->NetworkStateChanged();
}

#endif // BASE_ANIMATING_OVERLAY_H
