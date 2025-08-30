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

struct CIVLatchType
{
	enum : int
	{
		SIMULATION,
		ANIMATION
	};

	inline CIVLatchType( int value = SIMULATION )
	 : m_value( value )
	{
	}

	explicit inline operator int&()
	{
		return m_value;
	}

	inline operator int() const
	{
		return m_value;
	}

	int m_value;
};

struct CInterpolationType
{
	enum : int
	{
		LINEAR,
		HERMITE,
		MAX_AND_NOT_SET
	};

	inline CInterpolationType( int value = MAX_AND_NOT_SET )
	 : m_value( value )
	{
	}

	explicit inline operator int&()
	{
		return m_value;
	}

	inline operator int() const
	{
		return m_value;
	}

	int m_value;
};

struct IInterpolatedVar
{
	virtual void Push()																   = 0;
	virtual void Push( void* pData, size_t size )									   = 0;
	virtual void ClearHistory()														   = 0;
	virtual void SetLooping( bool bLooping )										   = 0;
	virtual bool IsLooping()														   = 0;
	virtual size_t Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac ) = 0;
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
	virtual CIVLatchType LatchType()												   = 0;
	virtual void SetLatchType( const CIVLatchType& LatchType )						   = 0;
	virtual std::string DebugValue()												   = 0;
	virtual CInterpolationType InterpolationType()									   = 0;
	virtual void SetInterpolationType( const CInterpolationType& InterpolationType )   = 0;
};

template < typename T, size_t MAX_HISTORY = MAX_INTERPOLATION_TICK_HISTORY >
class CInterpolatedVar : public IInterpolatedVar
{
  public:
	CInterpolatedVar()
	 : m_szDebugName( {} ),
	   m_pReferencedVariable( &m_LastKnownValue ),
	   m_LatchType( CIVLatchType::SIMULATION ),
	   m_bLooping( false ),
	   m_bDisabledInterpolation( false ),
	   m_bEnabled( true ),
	   m_InterpolationType( CInterpolationType::MAX_AND_NOT_SET )
	{
	}

	CInterpolatedVar( const std::string& szDebugName,
					  const CIVLatchType& LatchType,
					  bool bLooping				  = false,
					  bool bDisabledInterpolation = false )
	 : m_szDebugName( szDebugName ),
	   m_pReferencedVariable( &m_LastKnownValue ),
	   m_LatchType( LatchType ),
	   m_bLooping( bLooping ),
	   m_bDisabledInterpolation( bDisabledInterpolation ),
	   m_bEnabled( true ),
	   m_InterpolationType( CInterpolationType::MAX_AND_NOT_SET )
	{
	}

	CInterpolatedVar( const std::string& szDebugName,
					  T* pReferenceVariable,
					  const CIVLatchType& LatchType,
					  bool bLooping				  = false,
					  bool bDisabledInterpolation = false )
	 : m_szDebugName( szDebugName ),
	   m_pReferencedVariable( pReferenceVariable ),
	   m_LatchType( LatchType ),
	   m_bLooping( bLooping ),
	   m_bDisabledInterpolation( bDisabledInterpolation ),
	   m_bEnabled( true ),
	   m_InterpolationType( CInterpolationType::MAX_AND_NOT_SET )
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

	size_t Interpolate_Linear( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		if ( nAmountOfTicks >= MAX_HISTORY )
		{
			*out = m_LastKnownValue;
			return 0;
		}

		T* start = NULL;
		T* end	 = NULL;

		// Get the latest history data possible if we can't interpolate this amount
		while ( nAmountOfTicks >= 1 )
		{
			start = m_History.Get( nAmountOfTicks );
			end	  = m_History.Get( nAmountOfTicks - 1 );

			if ( start && end )
			{
				break;
			}

			// Keep searching in history
			nAmountOfTicks--;
			// Don't interpolate here though, it might look jittery, wait for the history to fill up.
			flInterpolationAmountFrac = 0;
		}

		if ( !start || !end )
		{
			return 0;
		}

		*out = ::Interpolate_Linear( flInterpolationAmountFrac, *start, *end, m_bLooping );

		return nAmountOfTicks;
	}

	size_t Interpolate_Hermite( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		if ( nAmountOfTicks >= MAX_HISTORY )
		{
			*out = m_LastKnownValue;
			return 0;
		}

		T* prev	 = NULL;
		T* start = NULL;
		T* end	 = NULL;

		// Get the latest history data possible if we can't interpolate this amount
		while ( nAmountOfTicks >= 2 )
		{
			prev  = m_History.Get( nAmountOfTicks );
			start = m_History.Get( nAmountOfTicks - 1 );
			end	  = m_History.Get( nAmountOfTicks - 2 );

			if ( prev && start && end )
			{
				break;
			}

			// Keep searching in history
			nAmountOfTicks--;
			// Don't interpolate here though, it might look jittery, wait for the history to fill up.
			flInterpolationAmountFrac = 0;
		}

		if ( !prev || !start || !end )
		{
			return 0;
		}

		*out = ::Interpolate_Hermite( flInterpolationAmountFrac, *prev, *start, *end, m_bLooping );

		return nAmountOfTicks;
	}

	size_t InterpolateCopy( size_t nAmountOfTicks, float flInterpolationAmountFrac, T* out )
	{
		size_t result = 0;

		switch ( m_InterpolationType )
		{
			case CInterpolationType::LINEAR:
			{
				result = Interpolate_Linear( nAmountOfTicks, flInterpolationAmountFrac, out );
				break;
			}

			case CInterpolationType::HERMITE:
			{
				result = Interpolate_Hermite( nAmountOfTicks, flInterpolationAmountFrac, out );
				break;
			}

			case CInterpolationType::MAX_AND_NOT_SET:
			{
				Error( "Interpolation must be set for CInterpolatedVar::%s", m_szDebugName.c_str() );
				break;
			}
		}

		return result;
	}

	virtual size_t Interpolate( size_t nAmountOfTicks, float flInterpolationAmountFrac ) override
	{
		if ( !IsInterpolationEnabled() || !IsEnabled() )
		{
			return 0;
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

	virtual CIVLatchType LatchType() override
	{
		return m_LatchType;
	}

	virtual void SetLatchType( const CIVLatchType& LatchType ) override
	{
		m_LatchType = LatchType;
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

	virtual CInterpolationType InterpolationType() override
	{
		return m_InterpolationType;
	}

	virtual void SetInterpolationType( const CInterpolationType& InterpolationType ) override
	{
		m_InterpolationType = InterpolationType;
	}

  private:
	CUtlCircularBuffer< T, MAX_HISTORY > m_History;
	std::string m_szDebugName;
	T* m_pReferencedVariable;
	bool m_bLooping;
	bool m_bDisabledInterpolation;
	bool m_bEnabled;
	CIVLatchType m_LatchType;
	T m_LastKnownValue;
	CInterpolationType m_InterpolationType;
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
		}
	}

	CInterpolatedVarArray( const std::string& szDebugName, const CIVLatchType& LatchType )
	{
		for ( size_t i = 0; i < ARRAY_SIZE; i++ )
		{
			m_Array[i].SetDebugName( szDebugName + "_" + std::to_string( i ) );
			m_Array[i].SetReferenceData( &m_Array[i].GetLastKnownValue(), sizeof( T ) );
			m_Array[i].SetLatchType( LatchType );
		}
	}

	CInterpolatedVarArray( const std::string& szDebugName,
						   T ( &pReferenceVariable )[ARRAY_SIZE],
						   const CIVLatchType& LatchType )
	{
		for ( size_t i = 0; i < ARRAY_SIZE; i++ )
		{
			m_Array[i].SetDebugName( szDebugName + "_" + std::to_string( i ) );
			m_Array[i].SetReferenceData( &pReferenceVariable[i], sizeof( T ) );
			m_Array[i].SetLatchType( LatchType );
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
			if ( variable->Interpolate( nAmountOfTicks, flInterpolationAmountFrac ) > 0 )
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
		for ( int i = 0; i < variables.Size(); i++ )
		{
			if ( variables[i]->ReferenceData() == ref )
			{
				variables.FastRemove( i );
				break;
			}
		}
	}

	inline void SetInterpolationType( const CInterpolationType& InterpolationType )
	{
		for ( auto&& variable : variables )
		{
			variable->SetInterpolationType( InterpolationType );
		}
	}
};

#include "tier0/memdbgoff.h"

#endif // INTERPOLATEDVAR_H
