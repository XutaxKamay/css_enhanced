#include "tier1/utlvector.h"
#include "mathlib/vmatrix.h"
#include "locald3dtypes.h"

#define DXABSTRACT_BREAK_ON_ERROR() *( int* )nullptr = 0

// ------------------------------------------------------------------------------------------------------------------------------
// // D3DX stuff.
// ------------------------------------------------------------------------------------------------------------------------------
// //

#define UNDEFINED                                                                                                      \
	{                                                                                                                  \
		*( int* )nullptr = 0;                                                                                          \
	}

// matrix stack...
#include <vector>
#include <d3dx9.h>

class CD3DXMatrixStack : public ID3DXMatrixStack
{
  private:
	std::vector< D3DXMATRIX > m_stack;
	ULONG m_refCount;

  public:
	CD3DXMatrixStack()
	 : m_refCount( 1 )
	{
		D3DXMATRIX identity;
		D3DXMatrixIdentity( &identity );
		m_stack.push_back( identity );
	}

	STDMETHOD( QueryInterface )( REFIID riid, void** ppvObj ) override
	{
		if ( riid == IID_IUnknown || riid == IID_ID3DXMatrixStack )
		{
			*ppvObj = static_cast< ID3DXMatrixStack* >( this );
			AddRef();
			return S_OK;
		}
		*ppvObj = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHOD_( ULONG, AddRef )() override
	{
		return ThreadInterlockedIncrement( &m_refCount );
	}

	STDMETHOD_( ULONG, Release )() override
	{
		ULONG ref = ThreadInterlockedDecrement( &m_refCount );
		if ( ref == 0 )
		{
			delete this;
		}
		return ref;
	}

	STDMETHOD( Pop )() override
	{
		if ( m_stack.size() <= 1 )
		{
			return D3DERR_INVALIDCALL;
		}
		m_stack.pop_back();
		return S_OK;
	}

	STDMETHOD( Push )() override
	{
		m_stack.push_back( m_stack.back() );
		return S_OK;
	}

	STDMETHOD( LoadIdentity )() override
	{
		D3DXMatrixIdentity( &m_stack.back() );
		return S_OK;
	}

	STDMETHOD( LoadMatrix )( const D3DXMATRIX* pM ) override
	{
		m_stack.back() = *pM;
		return S_OK;
	}

	STDMETHOD( MultMatrix )( const D3DXMATRIX* pM ) override
	{
		D3DXMatrixMultiply( &m_stack.back(), pM, &m_stack.back() );
		return S_OK;
	}

	STDMETHOD( MultMatrixLocal )( const D3DXMATRIX* pM ) override
	{
		D3DXMatrixMultiply( &m_stack.back(), &m_stack.back(), pM );
		return S_OK;
	}

	STDMETHOD( RotateAxis )( const D3DXVECTOR3* pV, FLOAT Angle ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixRotationAxis( &mat, pV, Angle );
		return MultMatrix( &mat );
	}

	STDMETHOD( RotateAxisLocal )( const D3DXVECTOR3* pV, FLOAT Angle ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixRotationAxis( &mat, pV, Angle );
		return MultMatrixLocal( &mat );
	}

	STDMETHOD( RotateYawPitchRoll )( FLOAT Yaw, FLOAT Pitch, FLOAT Roll ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixRotationYawPitchRoll( &mat, Yaw, Pitch, Roll );
		return MultMatrix( &mat );
	}

	STDMETHOD( RotateYawPitchRollLocal )( FLOAT Yaw, FLOAT Pitch, FLOAT Roll ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixRotationYawPitchRoll( &mat, Yaw, Pitch, Roll );
		return MultMatrixLocal( &mat );
	}

	STDMETHOD( Scale )( FLOAT x, FLOAT y, FLOAT z ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixScaling( &mat, x, y, z );
		return MultMatrix( &mat );
	}

	STDMETHOD( ScaleLocal )( FLOAT x, FLOAT y, FLOAT z ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixScaling( &mat, x, y, z );
		return MultMatrixLocal( &mat );
	}

	STDMETHOD( Translate )( FLOAT x, FLOAT y, FLOAT z ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixTranslation( &mat, x, y, z );
		return MultMatrix( &mat );
	}

	STDMETHOD( TranslateLocal )( FLOAT x, FLOAT y, FLOAT z ) override
	{
		D3DXMATRIX mat;
		D3DXMatrixTranslation( &mat, x, y, z );
		return MultMatrixLocal( &mat );
	}

	STDMETHOD_( D3DXMATRIX*, GetTop )() override
	{
		return &m_stack.back();
	}
};

HRESULT D3DXCreateMatrixStack( DWORD Flags, LPD3DXMATRIXSTACK* ppStack )
{
	*ppStack = new ( std::nothrow ) CD3DXMatrixStack();
	return ( *ppStack ) ? S_OK : E_OUTOFMEMORY;
}

const char* D3DXGetPixelShaderProfile( IDirect3DDevice9* pDevice )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return "";
}

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4701 ) // potentially uninitialized local variable 'temp' used
#endif
D3DXMATRIX* D3DXMatrixMultiply( D3DXMATRIX* pOut, CONST D3DXMATRIX* pM1, CONST D3DXMATRIX* pM2 )
{
	D3DXMATRIX temp;

	for ( int i = 0; i < 4; i++ )
	{
		for ( int j = 0; j < 4; j++ )
		{
			temp.m[i][j] = ( pM1->m[i][0] * pM2->m[0][j] ) + ( pM1->m[i][1] * pM2->m[1][j] )
						   + ( pM1->m[i][2] * pM2->m[2][j] ) + ( pM1->m[i][3] * pM2->m[3][j] );
		}
	}
	*pOut = temp;
	return pOut;
}
#ifdef _MSC_VER
#pragma warning( pop )
#endif

// Transform a 3D vector by a given matrix, projecting the result back into w = 1
// http://msdn.microsoft.com/en-us/library/ee417622(VS.85).aspx
D3DXVECTOR3* D3DXVec3TransformCoord( D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DXMATRIX* pM )
{
	D3DXVECTOR3 vOut;

	float norm = ( pM->m[0][3] * pV->x ) + ( pM->m[1][3] * pV->y ) + ( pM->m[2][3] * pV->z ) + pM->m[3][3];
	if ( norm )
	{
		float norm_inv = 1.0f / norm;
		vOut.x		   = ( pM->m[0][0] * pV->x + pM->m[1][0] * pV->y + pM->m[2][0] * pV->z + pM->m[3][0] ) * norm_inv;
		vOut.y		   = ( pM->m[0][1] * pV->x + pM->m[1][1] * pV->y + pM->m[2][1] * pV->z + pM->m[3][1] ) * norm_inv;
		vOut.z		   = ( pM->m[0][2] * pV->x + pM->m[1][2] * pV->y + pM->m[2][2] * pV->z + pM->m[3][2] ) * norm_inv;
	}
	else
	{
		vOut.x = vOut.y = vOut.z = 0.0f;
	}

	*pOut = vOut;

	return pOut;
}

D3DXMATRIX* D3DXMatrixTranslation( D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z )
{
	D3DXMatrixIdentity( pOut );
	pOut->m[3][0] = x;
	pOut->m[3][1] = y;
	pOut->m[3][2] = z;
	return pOut;
}

D3DXMATRIX* D3DXMatrixInverse( D3DXMATRIX* pOut, FLOAT* pDeterminant, CONST D3DXMATRIX* pM )
{
	Assert( sizeof( D3DXMATRIX ) == ( 16 * sizeof( float ) ) );
	Assert( sizeof( VMatrix ) == ( 16 * sizeof( float ) ) );
	Assert( pDeterminant == NULL ); // homey don't play that

	VMatrix* origM = ( VMatrix* )pM;
	VMatrix* destM = ( VMatrix* )pOut;

	bool success = MatrixInverseGeneral( *origM, *destM );
	( void )success;
	Assert( success );

	return pOut;
}

D3DXMATRIX* D3DXMatrixTranspose( D3DXMATRIX* pOut, CONST D3DXMATRIX* pM )
{
	if ( pOut != pM )
	{
		for ( int i = 0; i < 4; i++ )
		{
			for ( int j = 0; j < 4; j++ )
			{
				pOut->m[i][j] = pM->m[j][i];
			}
		}
	}
	else
	{
		D3DXMATRIX temp = *pM;
		D3DXMatrixTranspose( pOut, &temp );
	}

	return NULL;
}

D3DXPLANE* D3DXPlaneNormalize( D3DXPLANE* pOut, CONST D3DXPLANE* pP )
{
	// not very different from normalizing a vector.
	// figure out the square root of the sum-of-squares of the x,y,z components
	// make sure that's non zero
	// then divide all four components by that value
	// or return some dummy plane like 0,0,1,0 if it fails

	float len = sqrt( ( pP->a * pP->a ) + ( pP->b * pP->b ) + ( pP->c * pP->c ) );
	if ( len > 1e-10 ) // FIXME need a real epsilon here ?
	{
		pOut->a = pP->a / len;
		pOut->b = pP->b / len;
		pOut->c = pP->c / len;
		pOut->d = pP->d / len;
	}
	else
	{
		pOut->a = 0.0f;
		pOut->b = 0.0f;
		pOut->c = 1.0f;
		pOut->d = 0.0f;
	}
	return pOut;
}

D3DXVECTOR4* D3DXVec4Transform( D3DXVECTOR4* pOut, CONST D3DXVECTOR4* pV, CONST D3DXMATRIX* pM )
{
	VMatrix* mat   = ( VMatrix* )pM;
	Vector4D* vIn  = ( Vector4D* )pV;
	Vector4D* vOut = ( Vector4D* )pOut;

	Vector4DMultiplyTranspose( *mat, *vIn, *vOut );

	return pOut;
}

D3DXVECTOR4* D3DXVec4Normalize( D3DXVECTOR4* pOut, CONST D3DXVECTOR4* pV )
{
	Vector4D* vIn  = ( Vector4D* )pV;
	Vector4D* vOut = ( Vector4D* )pOut;

	*vOut = *vIn;
	Vector4DNormalize( *vOut );

	return pOut;
}

D3DXMATRIX* D3DXMatrixOrthoOffCenterRH( D3DXMATRIX* pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return NULL;
}

D3DXMATRIX* D3DXMatrixPerspectiveRH( D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return NULL;
}

D3DXMATRIX* D3DXMatrixPerspectiveOffCenterRH( D3DXMATRIX* pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return NULL;
}

D3DXPLANE* D3DXPlaneTransform( D3DXPLANE* pOut, CONST D3DXPLANE* pP, CONST D3DXMATRIX* pM )
{
	float* out = &pOut->a;

	// dot dot dot
	for ( int x = 0; x < 4; x++ )
	{
		out[x] = ( pM->m[0][x] * pP->a ) + ( pM->m[1][x] * pP->b ) + ( pM->m[2][x] * pP->c ) + ( pM->m[3][x] * pP->d );
	}

	return pOut;
}

void D3DPERF_SetOptions( DWORD dwOptions )
{
}

HRESULT D3DXCompileShader( LPCSTR pSrcData,
						   UINT SrcDataLen,
						   CONST D3DXMACRO* pDefines,
						   LPD3DXINCLUDE pInclude,
						   LPCSTR pFunctionName,
						   LPCSTR pProfile,
						   DWORD Flags,
						   LPD3DXBUFFER* ppShader,
						   LPD3DXBUFFER* ppErrorMsgs,
						   LPD3DXCONSTANTTABLE* ppConstantTable )
{
	DXABSTRACT_BREAK_ON_ERROR(); // is anyone calling this ?
	return S_OK;
}

DWORD WINAPI D3DXGetShaderVersion( const DWORD* byte_code )
{
	return 2;
}

uint64 GetVidMemBytes( void )
{
	return UINT64_MAX;
}

// Thanks to wine
D3DXMATRIX* WINAPI D3DXMatrixRotationYawPitchRoll( D3DXMATRIX* out, FLOAT yaw, FLOAT pitch, FLOAT roll )
{
	FLOAT sroll, croll, spitch, cpitch, syaw, cyaw;

	sroll  = sinf( roll );
	croll  = cosf( roll );
	spitch = sinf( pitch );
	cpitch = cosf( pitch );
	syaw   = sinf( yaw );
	cyaw   = cosf( yaw );

	out->m[0][0] = sroll * spitch * syaw + croll * cyaw;
	out->m[0][1] = sroll * cpitch;
	out->m[0][2] = sroll * spitch * cyaw - croll * syaw;
	out->m[0][3] = 0.0f;
	out->m[1][0] = croll * spitch * syaw - sroll * cyaw;
	out->m[1][1] = croll * cpitch;
	out->m[1][2] = croll * spitch * cyaw + sroll * syaw;
	out->m[1][3] = 0.0f;
	out->m[2][0] = cpitch * syaw;
	out->m[2][1] = -spitch;
	out->m[2][2] = cpitch * cyaw;
	out->m[2][3] = 0.0f;
	out->m[3][0] = 0.0f;
	out->m[3][1] = 0.0f;
	out->m[3][2] = 0.0f;
	out->m[3][3] = 1.0f;

	return out;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationAxis( D3DXMATRIX* out, const D3DXVECTOR3* v, FLOAT angle )
{
	D3DXVECTOR3 nv;
	FLOAT sangle, cangle, cdiff;

	D3DXVec3Normalize( &nv, v );
	sangle = sinf( angle );
	cangle = cosf( angle );
	cdiff  = 1.0f - cangle;

	out->m[0][0] = cdiff * nv.x * nv.x + cangle;
	out->m[1][0] = cdiff * nv.x * nv.y - sangle * nv.z;
	out->m[2][0] = cdiff * nv.x * nv.z + sangle * nv.y;
	out->m[3][0] = 0.0f;
	out->m[0][1] = cdiff * nv.y * nv.x + sangle * nv.z;
	out->m[1][1] = cdiff * nv.y * nv.y + cangle;
	out->m[2][1] = cdiff * nv.y * nv.z - sangle * nv.x;
	out->m[3][1] = 0.0f;
	out->m[0][2] = cdiff * nv.z * nv.x - sangle * nv.y;
	out->m[1][2] = cdiff * nv.z * nv.y + sangle * nv.x;
	out->m[2][2] = cdiff * nv.z * nv.z + cangle;
	out->m[3][2] = 0.0f;
	out->m[0][3] = 0.0f;
	out->m[1][3] = 0.0f;
	out->m[2][3] = 0.0f;
	out->m[3][3] = 1.0f;

	return out;
}

D3DXVECTOR3* WINAPI D3DXVec3Normalize( D3DXVECTOR3* pout, const D3DXVECTOR3* pv )
{
	FLOAT norm;

	norm = D3DXVec3Length( pv );
	if ( !norm )
	{
		pout->x = 0.0f;
		pout->y = 0.0f;
		pout->z = 0.0f;
	}
	else
	{
		pout->x = pv->x / norm;
		pout->y = pv->y / norm;
		pout->z = pv->z / norm;
	}

	return pout;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling( D3DXMATRIX* pout, FLOAT sx, FLOAT sy, FLOAT sz )
{
	D3DXMatrixIdentity( pout );
	pout->m[0][0] = sx;
	pout->m[1][1] = sy;
	pout->m[2][2] = sz;
	return pout;
}