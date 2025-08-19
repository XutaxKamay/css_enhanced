//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef INTERPOLATEDVAR_H
#define INTERPOLATEDVAR_H
#ifdef _WIN32
#pragma once
#endif

constexpr auto MAX_INTERPOLATION_TICK_HISTORY = 1024;

#include <string>

#include "tier1/utllinkedlist.h"
#include "rangecheckedvar.h"
#include "lerp_functions.h"
#include "convar.h"
#include "utlvector.h"
#include "shareddefs.h"
#include "tier0/dbg.h"

#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
class C_AnimationLayer;
#else
class LayerRecord;
#endif

namespace std
{
	inline std::string to_string( QAngle& obj )
	{
		return "[" + std::to_string( obj.x ) + ", " + std::to_string( obj.y ) + ", " + std::to_string( obj.z ) + "]";
	}

	inline std::string to_string( Vector& obj )
	{
		return "[" + std::to_string( obj.x ) + ", " + std::to_string( obj.y ) + ", " + std::to_string( obj.z ) + "]";
	}

	inline std::string to_string( Quaternion& obj )
	{
		return "[" + std::to_string( obj.x ) + ", " + std::to_string( obj.y ) + ", " + std::to_string( obj.z ) + ", "
			   + std::to_string( obj.w ) + "]";
	}

#ifdef CLIENT_DLL
	std::string to_string( C_AnimationLayer& obj );
#else
	std::string to_string( LayerRecord& obj );
#endif
};

template < typename T >
inline T Interpolate_Linear( float frac, T& start, T& end, bool bLooping = false )
{
	Assert( frac >= 0.0f && frac <= 1.0f );

	if ( start == end )
	{
		return start;
	}

	if ( frac <= 0.0f )
	{
		return start;
	}

	if ( frac >= 1.0f )
	{
		return end;
	}

	auto out = bLooping ? LoopingLerp( frac, start, end ) : Lerp( frac, start, end );

	Lerp_Clamp( out );

	return out;
}

template < typename T >
inline T Interpolate_Hermite( float frac, T& prev, T& start, T& end, bool bLooping = false )
{
	Assert( frac >= 0.0f && frac <= 1.0f );

	if ( start == end )
	{
		return start;
	}

	if ( frac <= 0.0f )
	{
		return start;
	}

	if ( frac >= 1.0f )
	{
		return end;
	}

	auto out = bLooping ? LoopingLerp_Hermite( frac, prev, start, end ) : Lerp_Hermite( frac, prev, start, end );

	Lerp_Clamp( out );

	return out;
}

enum InterpolatedVarType
{
	LATCH_SIMULATION_VAR,
	LATCH_ANIMATION_VAR
};

struct IInterpolatedVar
{
	virtual void Push()																   = 0;
	virtual void Push( void* pData, size_t size )									   = 0;
	virtual void ClearHistory()														   = 0;
	virtual void SetLooping( bool bLooping )										   = 0;
	virtual bool IsLooping()														   = 0;
	virtual void SetHermite( bool bIsHermite )										   = 0;
	virtual bool IsHermite()														   = 0;
	virtual bool Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac ) = 0;
	virtual void RestoreToLastKnownValue()											   = 0;
	virtual void SaveLastKnownValue()												   = 0;
	virtual void DisableInterpolation()												   = 0;
	virtual void EnableInterpolation()												   = 0;
	virtual bool IsInterpolationEnabled()											   = 0;
	virtual void* ReferenceData()													   = 0;
	virtual size_t ReferenceSize()													   = 0;
	virtual void SetReferenceData( void* pData, size_t size )						   = 0;
	virtual void SetDebugName( const std::string& szDebugName )						   = 0;
	virtual std::string DebugName()													   = 0;
	virtual size_t MaxHistory()														   = 0;
	virtual size_t CurrentHistorySize()												   = 0;
	virtual void Copy( IInterpolatedVar* varDst )									   = 0;
	virtual void Disable()															   = 0;
	virtual void Enable()															   = 0;
	virtual bool IsEnabled()														   = 0;
	virtual void Reset()															   = 0;
	virtual InterpolatedVarType Type()												   = 0;
	virtual void SetType( const InterpolatedVarType& type )							   = 0;
	virtual std::string DebugValue()												   = 0;
};

template < typename T, size_t MAX_HISTORY = MAX_INTERPOLATION_TICK_HISTORY >
class CInterpolatedVar : public IInterpolatedVar
{
  public:
	CInterpolatedVar()
	 : m_szDebugName( {} ),
	   m_pReferencedVariable( &m_LastKnownValue ),
	   m_IVType( LATCH_SIMULATION_VAR ),
	   m_bLooping( false ),
	   m_bHermite( true ),
	   m_bDisabledInterpolation( false ),
	   m_bEnabled( true )
	{
	}

	CInterpolatedVar( const std::string& szDebugName,
					  const InterpolatedVarType& IVtype,
					  bool bLooping				  = false,
					  bool bHermite				  = true,
					  bool bDisabledInterpolation = false )
	 : m_szDebugName( szDebugName ),
	   m_pReferencedVariable( &m_LastKnownValue ),
	   m_IVType( IVtype ),
	   m_bLooping( bLooping ),
	   m_bHermite( bHermite ),
	   m_bDisabledInterpolation( bDisabledInterpolation ),
	   m_bEnabled( true )
	{
	}

	CInterpolatedVar( const std::string& szDebugName,
					  T* pReferenceVariable,
					  const InterpolatedVarType& IVtype,
					  bool bLooping				  = false,
					  bool bHermite				  = true,
					  bool bDisabledInterpolation = false )
	 : m_szDebugName( szDebugName ),
	   m_pReferencedVariable( pReferenceVariable ),
	   m_IVType( IVtype ),
	   m_bLooping( bLooping ),
	   m_bHermite( bHermite ),
	   m_bDisabledInterpolation( bDisabledInterpolation ),
	   m_bEnabled( true )
	{
	}

	void Push( T&& value )
	{
		if ( !IsEnabled() )
		{
			return;
		}

		m_History.Push( std::move( value ) );
	}

	void Push( const T& value )
	{
		if ( !IsEnabled() )
		{
			return;
		}

		m_History.Push( value );
	}

	virtual void Push() override
	{
		Push( *m_pReferencedVariable );
	}

	virtual void Push( void* pData, size_t size ) override
	{
		ErrorIfNot(
		  size == sizeof( T ),
		  ( "CInterpolatedVar::%s pushing bad variable %li != %li", m_szDebugName.c_str(), size, sizeof( T ) ) );

		Push( *reinterpret_cast< T* >( pData ) );
	}

	virtual void ClearHistory() override
	{
		m_History.Clear();
	}

	virtual void SetLooping( bool bLooping ) override
	{
		m_bLooping = bLooping;
	}

	virtual bool IsLooping() override
	{
		return m_bLooping;
	}

	virtual void SetHermite( bool bIsHermite ) override
	{
		m_bHermite = bIsHermite;
	}

	virtual bool IsHermite() override
	{
		return m_bHermite;
	}

	bool Interpolate_Linear( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		ErrorIfNot( nAmountOfTicks >= 1 || nAmountOfTicks - 1 < MAX_HISTORY,
					( "Need atleast 1 tick of history for CInterpolatedVar::%s", m_szDebugName.c_str() ) );

		auto start = m_History.Get( nAmountOfTicks );
		auto end   = m_History.Get( nAmountOfTicks - 1 );

		if ( !start || !end )
		{
			return false;
		}

		*out = ::Interpolate_Linear( flInterpolationAmountFrac, *start, *end, m_bLooping );

		return true;
	}

	bool Interpolate_Hermite( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		ErrorIfNot( nAmountOfTicks >= 2 || nAmountOfTicks - 2 < MAX_HISTORY,
					( "Need atleast 2 ticks of history for CInterpolatedVar::%s", m_szDebugName.c_str() ) );

		auto prev  = m_History.Get( nAmountOfTicks );
		auto start = m_History.Get( nAmountOfTicks - 1 );
		auto end   = m_History.Get( nAmountOfTicks - 2 );

		if ( !prev || !start || !end )
		{
			return false;
		}

		*out = ::Interpolate_Hermite( flInterpolationAmountFrac, *prev, *start, *end, m_bLooping );

		return true;
	}

	bool InterpolateCopy( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		return m_bHermite ? Interpolate_Hermite( nAmountOfTicks, flInterpolationAmountFrac, out ) :
							Interpolate_Linear( nAmountOfTicks, flInterpolationAmountFrac, out );
	}

	virtual bool Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac ) override
	{
		if ( !IsInterpolationEnabled() || !IsEnabled() )
		{
			return false;
		}

		return InterpolateCopy( nAmountOfTicks, flInterpolationAmountFrac, m_pReferencedVariable );
	}

	virtual void RestoreToLastKnownValue() override
	{
		*m_pReferencedVariable = m_LastKnownValue;
	}

	virtual void SaveLastKnownValue() override
	{
		m_LastKnownValue = *m_pReferencedVariable;
	}

	virtual void DisableInterpolation() override
	{
		m_bDisabledInterpolation = true;
	}

	virtual void EnableInterpolation() override
	{
		m_bDisabledInterpolation = false;
	}

	virtual bool IsInterpolationEnabled() override
	{
		return !m_bDisabledInterpolation;
	}

	virtual void* ReferenceData() override
	{
		return reinterpret_cast< void* >( m_pReferencedVariable );
	}

	virtual size_t ReferenceSize() override
	{
		return sizeof( T );
	}

	virtual void SetReferenceData( void* pData, size_t size ) override
	{
		ErrorIfNot( size == sizeof( T ),
					( "CInterpolatedVar::%s setting bad reference variable %li != %li",
					  m_szDebugName.c_str(),
					  size,
					  sizeof( T ) ) );

		m_pReferencedVariable = reinterpret_cast< T* >( pData );
	}

	virtual void SetDebugName( const std::string& szDebugName ) override
	{
		m_szDebugName = szDebugName;
	}

	virtual std::string DebugName() override
	{
		return m_szDebugName;
	}

	virtual size_t MaxHistory() override
	{
		return MAX_HISTORY;
	}

	virtual size_t CurrentHistorySize() override
	{
		return m_History.FillCount();
	}

	virtual void Copy( IInterpolatedVar* varDst ) override
	{
		varDst->ClearHistory();

		for ( size_t i = 0; i < m_History.FillCount(); i++ )
		{
			varDst->Push( m_History.Get( i ), sizeof( T ) );
		}
	}

	virtual void Enable() override
	{
		m_bEnabled = true;
	}

	virtual void Disable() override
	{
		m_bEnabled = true;
	}

	virtual bool IsEnabled() override
	{
		return m_bEnabled;
	}

	virtual void Reset() override
	{
		ClearHistory();
		m_LastKnownValue = *m_pReferencedVariable;
	}

	virtual InterpolatedVarType Type() override
	{
		return m_IVType;
	}

	virtual void SetType( const InterpolatedVarType& type ) override
	{
		m_IVType = type;
	}

	virtual std::string DebugValue() override
	{
		return "interpolated = " + std::to_string( *m_pReferencedVariable )
			   + "  |  networked = " + std::to_string( m_LastKnownValue );
	}

	inline T& GetLastKnownValue()
	{
		return m_LastKnownValue;
	}

	inline T* Get( size_t nSlot = 0 )
	{
		return m_History.Get( nSlot );
	}

  private:
	CUtlCircularBuffer< T, MAX_HISTORY > m_History;
	std::string m_szDebugName;
	T* m_pReferencedVariable;
	bool m_bLooping;
	bool m_bHermite;
	bool m_bDisabledInterpolation;
	bool m_bEnabled;
	InterpolatedVarType m_IVType;
	T m_LastKnownValue;
};

template < typename T, size_t ARRAY_SIZE, size_t MAX_HISTORY = MAX_INTERPOLATION_TICK_HISTORY >
class CInterpolatedVarArray
{
  public:
	using Element = CInterpolatedVar< T, MAX_HISTORY >;

	CInterpolatedVarArray()
	{
		for ( size_t i = 0; i < ARRAY_SIZE; i++ )
		{
			m_Array[i].SetDebugName( "unknown_" + std::to_string( i ) );
			m_Array[i].SetReferenceData( &m_Array[i].GetLastKnownValue(), sizeof( T ) );
			m_Array[i].SetType( LATCH_SIMULATION_VAR );
			m_Array[i].SetLooping( false );
			m_Array[i].SetHermite( true );
			m_Array[i].Enable();
			m_Array[i].EnableInterpolation();
		}
	}

	CInterpolatedVarArray( const std::string& szDebugName,
						   const InterpolatedVarType& IVtype,
						   bool bLooping			   = false,
						   bool bHermite			   = true,
						   bool bDisabledInterpolation = false )
	{
		for ( size_t i = 0; i < ARRAY_SIZE; i++ )
		{
			m_Array[i].SetDebugName( szDebugName + "_" + std::to_string( i ) );
			m_Array[i].SetReferenceData( &m_Array[i].GetLastKnownValue(), sizeof( T ) );
			m_Array[i].SetType( IVtype );
			m_Array[i].SetLooping( bLooping );
			m_Array[i].SetHermite( bHermite );
			m_Array[i].Enable();
			bDisabledInterpolation ? m_Array[i].DisableInterpolation() : m_Array[i].EnableInterpolation();
		}
	}

	CInterpolatedVarArray( const std::string& szDebugName,
						   T ( &pReferenceVariable )[ARRAY_SIZE],
						   const InterpolatedVarType& IVtype,
						   bool bLooping			   = false,
						   bool bHermite			   = true,
						   bool bDisabledInterpolation = false )
	{
		for ( size_t i = 0; i < ARRAY_SIZE; i++ )
		{
			m_Array[i].SetDebugName( szDebugName + "_" + std::to_string( i ) );
			m_Array[i].SetReferenceData( &pReferenceVariable[i], sizeof( T ) );
			m_Array[i].SetType( IVtype );
			m_Array[i].SetLooping( bLooping );
			m_Array[i].SetHermite( bHermite );
			m_Array[i].Enable();
			bDisabledInterpolation ? m_Array[i].DisableInterpolation() : m_Array[i].EnableInterpolation();
		}
	}

	inline Element& operator[]( size_t i )
	{
		return m_Array[i];
	}

  private:
	Element m_Array[ARRAY_SIZE];
};

struct CInterpolatedVarList
{
	CUtlVector< IInterpolatedVar* > variables;

	inline void Push()
	{
		for ( auto&& variable : variables )
		{
			variable->Push();
		}
	}

	inline void Reset()
	{
		for ( auto&& variable : variables )
		{
			variable->Reset();
		}
	}

	inline void ClearHistory()
	{
		for ( auto&& variable : variables )
		{
			variable->ClearHistory();
		}
	}

	inline bool Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac )
	{
		bool bCouldInterpolate = true;

		for ( auto&& variable : variables )
		{
			if ( !variable->Interpolate( nAmountOfTicks, flInterpolationAmountFrac ) )
			{
				bCouldInterpolate = false;
			}
		}

		return bCouldInterpolate;
	}

	inline void RestoreToLastKnownValue()
	{
		for ( auto&& variable : variables )
		{
			variable->RestoreToLastKnownValue();
		}
	}

	inline void SaveLastKnownValue()
	{
		for ( auto&& variable : variables )
		{
			variable->SaveLastKnownValue();
		}
	}

	inline void AddVar( IInterpolatedVar* variable )
	{
		if ( variables.Find( variable ) != -1 )
		{
			return;
		}

		variables.AddToTail( variable );
	}

	inline void RemoveVar( IInterpolatedVar* variable )
	{
		variables.FindAndFastRemove( variable );
	}

	template < typename T >
	inline void RemoveVar( T* ref )
	{
		size_t i;

		for ( i = 0; i < variables.Size(); i++ )
		{
			if ( variables[i]->ReferenceData() == ref )
			{
				break;
			}
		}

		variables.FastRemove( i );
	}
};

#include "tier0/memdbgoff.h"

#endif // INTERPOLATEDVAR_H
