//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef BONE_MERGE_CACHE_H
#define BONE_MERGE_CACHE_H
#include "mathlib/mathlib.h"
#include "studio.h"
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
class C_BaseAnimating;
#ifndef CBaseAnimating
#define CBaseAnimating C_BaseAnimating
#endif
#else
class CBaseAnimating;
#endif

class CStudioHdr;


#include "mathlib/vector.h"


class CBoneMergeCache
{
public:

	CBoneMergeCache();
	
	void Init( CBaseAnimating *pOwner );

	// Updates the lookups that let it merge bones quickly.
	void UpdateCache();
	
	// This copies the transform from all bones in the followed entity that have 
	// names that match our bones.
	void MergeMatchingBones( int boneMask, matrix3x4_t mergedbones[MAXSTUDIOBONES] );

	// copy bones instead of matrices
	void CopyParentToChild( const Vector parentPos[], const Quaternion parentQ[], Vector childPos[], Quaternion childQ[], int boneMask );
	void CopyChildToParent( const Vector childPos[], const Quaternion childQ[], Vector parentPos[], Quaternion parentQ[], int boneMask );

	// Returns true if the specified bone is one that gets merged in MergeMatchingBones.
	int IsBoneMerged( int iBone ) const;

	// Gets the origin for the first merge bone on the parent.
	bool GetAimEntOrigin( Vector *pAbsOrigin, QAngle *pAbsAngles );

	bool GetRootBone( matrix3x4_t &rootBone );

private:

	// This is the entity that we're keeping the cache updated for.
	CBaseAnimating *m_pOwner;

	// All the cache data is based off these. When they change, the cache data is regenerated.
	// These are either all valid pointers or all NULL.
	CBaseAnimating *m_pFollow;
	CStudioHdr		*m_pFollowHdr;
	const studiohdr_t	*m_pFollowRenderHdr;
	CStudioHdr		*m_pOwnerHdr;
#ifndef CLIENT_DLL
	CStudioHdr		*m_pFollowWorldHdr;
#endif
	// This is the mask we need to use to set up bones on the followed entity to do the bone merge
	int				m_nFollowBoneSetupMask;

	// Cache data.
	class CMergedBone
	{
	public:
		unsigned short m_iMyBone;
		unsigned short m_iParentBone;
	};

	CUtlVector<CMergedBone> m_MergedBones;
	CUtlVector<unsigned char> m_BoneMergeBits;	// One bit for each bone. The bit is set if the bone gets merged.
};


inline int CBoneMergeCache::IsBoneMerged( int iBone ) const
{
	if ( m_pOwnerHdr )
		return m_BoneMergeBits[iBone >> 3] & ( 1 << ( iBone & 7 ) );
	else
		return 0;
}


#endif // BONE_MERGE_CACHE_H
