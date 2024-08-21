//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "cbase.h"
#include "bone_merge_cache.h"
#include "bone_setup.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "mathlib/mathlib.h"
#include "studio.h"
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// CBoneMergeCache
//-----------------------------------------------------------------------------

CBoneMergeCache::CBoneMergeCache()
{
	m_pOwner = NULL;
	m_pFollow = NULL;
	m_pFollowHdr = NULL;
	m_pFollowRenderHdr = NULL;
	m_pOwnerHdr = NULL;
	m_nFollowBoneSetupMask = 0;
}

void CBoneMergeCache::Init( CBaseAnimating *pOwner )
{
	m_pOwner = pOwner;
	m_pFollow = NULL;
	m_pFollowHdr = NULL;
	m_pFollowRenderHdr = NULL;
	m_pOwnerHdr = NULL;
	m_nFollowBoneSetupMask = 0;
}

void CBoneMergeCache::UpdateCache()
{
	CStudioHdr *pOwnerHdr = m_pOwner ? m_pOwner->GetModelPtr() : NULL;
	if ( !pOwnerHdr || !m_pOwner->GetFollowedEntity())
	{
		if ( m_pOwnerHdr )
		{
			// Owner's model got swapped out
			m_MergedBones.Purge();
			m_BoneMergeBits.Purge();
			m_pFollow = NULL;
			m_pFollowHdr = NULL;
			m_pFollowRenderHdr = NULL;
			m_pOwnerHdr = NULL;
			m_nFollowBoneSetupMask = 0;
		}
		return;
	}

#ifdef CLIENT_DLL
    C_BaseAnimating* pTestFollow = m_pOwner->FindFollowedEntity();
#else
    CBaseAnimating* pTestFollow = dynamic_cast<CBaseAnimating*>(m_pOwner->GetFollowedEntity());
#endif
    CStudioHdr* pTestHdr = (pTestFollow ? pTestFollow->GetModelPtr() : NULL);

    // TODO_ENHANCED: We really need a better way to do this ...
#ifndef CLIENT_DLL
    CBaseCombatWeapon* pWeapon = dynamic_cast<CBaseCombatWeapon*>(pTestFollow);
    pTestHdr                   = pWeapon ? pWeapon->m_pStudioWorldHdr : pTestHdr;
    pWeapon = dynamic_cast<CBaseCombatWeapon*>(m_pOwner);
    pOwnerHdr = pWeapon ? pWeapon->m_pStudioWorldHdr : pOwnerHdr;
#endif

	const studiohdr_t *pTestStudioHDR = (pTestHdr ? pTestHdr->GetRenderHdr() : NULL);
	if ( pTestFollow != m_pFollow || pTestHdr != m_pFollowHdr || pTestStudioHDR != m_pFollowRenderHdr || pOwnerHdr != m_pOwnerHdr )
	{
		m_MergedBones.Purge();
		m_BoneMergeBits.Purge();
	
		// Update the cache.
		if ( pTestFollow && pTestHdr && pOwnerHdr )
		{
			m_pFollow = pTestFollow;
			m_pFollowHdr = pTestHdr;
			m_pFollowRenderHdr = pTestStudioHDR;
			m_pOwnerHdr = pOwnerHdr;

			m_BoneMergeBits.SetSize( pOwnerHdr->numbones() / 8 + 1 );
			memset( m_BoneMergeBits.Base(), 0, m_BoneMergeBits.Count() );

			mstudiobone_t *pOwnerBones = m_pOwnerHdr->pBone( 0 );
			
			m_nFollowBoneSetupMask = BONE_USED_BY_BONE_MERGE;
			for ( int i = 0; i < m_pOwnerHdr->numbones(); i++ )
			{
				int parentBoneIndex = Studio_BoneIndexByName( m_pFollowHdr, pOwnerBones[i].pszName() );
				if ( parentBoneIndex < 0 )
					continue;
#ifdef CLIENT_DLL
                printf("client bone attach: (%i - %i) %s %s %i %s\n", m_pFollow->entindex(), m_pOwner->entindex(), m_pFollowHdr->pszName(), m_pOwnerHdr->pszName(), i, m_pFollowHdr->pBone(i)->pszName());
#else
                printf("server bone attach: (%i - %i) %s %s %i %s\n", m_pFollow->entindex(), m_pOwner->entindex(), m_pFollowHdr->pszName(), m_pOwnerHdr->pszName(), i, m_pFollowHdr->pBone(i)->pszName());
#endif
				// Add a merged bone here.
				CMergedBone mergedBone;
				mergedBone.m_iMyBone = i;
				mergedBone.m_iParentBone = parentBoneIndex;
				m_MergedBones.AddToTail( mergedBone );

				m_BoneMergeBits[i>>3] |= ( 1 << ( i & 7 ) );

				if ( ( m_pFollowHdr->boneFlags( parentBoneIndex ) & BONE_USED_BY_BONE_MERGE ) == 0 )
				{
					m_nFollowBoneSetupMask = BONE_USED_BY_ANYTHING;
//					Warning("Performance warning: Merge with '%s'. Mark bone '%s' in model '%s' as being used by bone merge in the .qc!\n",
//						pOwnerHdr->pszName(), m_pFollowHdr->pBone( parentBoneIndex )->pszName(), m_pFollowHdr->pszName() ); 
				}
			}

			// No merged bones found? Slam the mask to 0
			if ( !m_MergedBones.Count() )
			{
				m_nFollowBoneSetupMask = 0;
			}
		}
		else
		{
			m_pFollow = NULL;
			m_pFollowHdr = NULL;
			m_pFollowRenderHdr = NULL;
			m_pOwnerHdr = NULL;
			m_nFollowBoneSetupMask = 0;
		}
	}
}


#ifdef STAGING_ONLY
ConVar r_captain_canteen_is_angry ( "r_captain_canteen_is_angry", "1" );
#endif

void CBoneMergeCache::MergeMatchingBones( int boneMask , matrix3x4_t mergedbones[MAXSTUDIOBONES] )
{
	UpdateCache();

	// If this is set, then all the other cache data is set.
	if ( !m_pOwnerHdr || m_MergedBones.Count() == 0 )
		return;

    matrix3x4_t bones[MAXSTUDIOBONES];
    // Have the entity we're following setup its bones.
#ifdef CLIENT_DLL
    m_pFollow->SetupBones(bones, MAXSTUDIOBONES, m_nFollowBoneSetupMask, gpGlobals->curtime);
#else
    m_pFollow->SetupBones(m_pFollowHdr, bones, m_nFollowBoneSetupMask);
#endif

	// Now copy the bone matrices.
	for ( int i=0; i < m_MergedBones.Count(); i++ )
	{
		int iOwnerBone = m_MergedBones[i].m_iMyBone;
		int iParentBone = m_MergedBones[i].m_iParentBone;
	
		// Only update bones reference by the bone mask.
		if ( !( m_pOwnerHdr->boneFlags( iOwnerBone ) & boneMask ) )
			continue;
// #ifdef CLIENT_DLL
//         printf("client bone attach: (%i - %i) %i", m_pFollow->entindex(), m_pOwner->entindex(), i);
//         for (int j = 0; j < 12; j++)
//         {
//             printf(" %f ", bones[iParentBone].Base()[i]);
//         }
//         printf("\n");
// #else
//         printf("server bone attach: (%i - %i) %i", m_pFollow->entindex(), m_pOwner->entindex(), i);
//         for (int j = 0; j < 12; j++)
//         {
//             printf(" %f ", bones[iParentBone].Base()[i]);
//         }
//         printf("\n");
// #endif
                
#ifdef CLIENT_DLL
		MatrixCopy( bones[ iParentBone ], mergedbones[ iOwnerBone ] );
#else
        MatrixCopy( bones[ iParentBone ], mergedbones[ iOwnerBone ] );
#endif
	}
}


	// copy bones instead of matrices
void CBoneMergeCache::CopyParentToChild( const Vector parentPos[], const Quaternion parentQ[], Vector childPos[], Quaternion childQ[], int boneMask )
{
	UpdateCache();

	// If this is set, then all the other cache data is set.
	if ( !m_pOwnerHdr || m_MergedBones.Count() == 0 )
		return;

	// Now copy the bone matrices.
	for ( int i=0; i < m_MergedBones.Count(); i++ )
	{
		int iOwnerBone = m_MergedBones[i].m_iMyBone;
		int iParentBone = m_MergedBones[i].m_iParentBone;
		
		if ( m_pOwnerHdr->boneParent( iOwnerBone ) == -1 || m_pFollowHdr->boneParent( iParentBone ) == -1 )
			continue;

		// Only update bones reference by the bone mask.
		if ( !( m_pOwnerHdr->boneFlags( iOwnerBone ) & boneMask ) )
			continue;

		childPos[ iOwnerBone ] = parentPos[ iParentBone ];
		childQ[ iOwnerBone ] = parentQ[ iParentBone ];
	}
}

void CBoneMergeCache::CopyChildToParent( const Vector childPos[], const Quaternion childQ[], Vector parentPos[], Quaternion parentQ[], int boneMask )
{
	UpdateCache();

	// If this is set, then all the other cache data is set.
	if ( !m_pOwnerHdr || m_MergedBones.Count() == 0 )
		return;

	// Now copy the bone matrices.
	for ( int i=0; i < m_MergedBones.Count(); i++ )
	{
		int iOwnerBone = m_MergedBones[i].m_iMyBone;
		int iParentBone = m_MergedBones[i].m_iParentBone;
		
		if ( m_pOwnerHdr->boneParent( iOwnerBone ) == -1 || m_pFollowHdr->boneParent( iParentBone ) == -1 )
			continue;

		// Only update bones reference by the bone mask.
		if ( !( m_pOwnerHdr->boneFlags( iOwnerBone ) & boneMask ) )
			continue;

		parentPos[ iParentBone ] = childPos[ iOwnerBone ];
		parentQ[ iParentBone ] = childQ[ iOwnerBone ];
	}
}


bool CBoneMergeCache::GetAimEntOrigin( Vector *pAbsOrigin, QAngle *pAbsAngles )
{
	UpdateCache();

	// If this is set, then all the other cache data is set.
	if ( !m_pOwnerHdr || m_MergedBones.Count() == 0 )
		return false;

	// We want the abs origin such that if we put the entity there, the first merged bone
	// will be aligned. This way the entity will be culled in the correct position.
	//
	// ie: mEntity * mBoneLocal = mFollowBone
	// so: mEntity = mFollowBone * Inverse( mBoneLocal )
	//
	// Note: the code below doesn't take animation into account. If the attached entity animates
	// all over the place, then this won't get the right results.
	
	// Get mFollowBone.
    matrix3x4_t bones[MAXSTUDIOBONES];
    // Have the entity we're following setup its bones.
#ifdef CLIENT_DLL
    m_pFollow->SetupBones(bones, MAXSTUDIOBONES, m_nFollowBoneSetupMask, gpGlobals->curtime);
#else
    m_pFollow->SetupBones(m_pFollowHdr, bones, m_nFollowBoneSetupMask);
#endif
	const matrix3x4_t &mFollowBone = bones[ m_MergedBones[0].m_iParentBone ];

	// Get Inverse( mBoneLocal )
	matrix3x4_t mBoneLocal, mBoneLocalInv;
	SetupSingleBoneMatrix( m_pOwnerHdr, m_pOwner->GetSequence(), 0, m_MergedBones[0].m_iMyBone, mBoneLocal );
	MatrixInvert( mBoneLocal, mBoneLocalInv );

	// Now calculate mEntity = mFollowBone * Inverse( mBoneLocal )
	matrix3x4_t mEntity;
	ConcatTransforms( mFollowBone, mBoneLocalInv, mEntity );
	MatrixAngles( mEntity, *pAbsAngles, *pAbsOrigin );

	return true;
}

bool CBoneMergeCache::GetRootBone( matrix3x4_t &rootBone )
{
	UpdateCache();

	// If this is set, then all the other cache data is set.
	if ( !m_pOwnerHdr || m_MergedBones.Count() == 0 )
		return false;

	// Get mFollowBone.
    matrix3x4_t bones[MAXSTUDIOBONES];
    // Have the entity we're following setup its bones.
#ifdef CLIENT_DLL
    m_pFollow->SetupBones(bones, MAXSTUDIOBONES, m_nFollowBoneSetupMask, gpGlobals->curtime);
#else
    m_pFollow->SetupBones(m_pFollowHdr, bones, m_nFollowBoneSetupMask);
#endif
	rootBone = bones[ m_MergedBones[0].m_iParentBone ];
	return true;
}


