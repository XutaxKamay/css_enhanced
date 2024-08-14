#ifndef C_VARIANT_T_H
#define C_VARIANT_T_H
#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "isaverestore.h"
#include "mathlib/vmatrix.h"
#include "c_baseentity.h"

//
// A variant class for passing data in entity input/output connections.
//
class variant_t
{
	union
	{
		bool bVal;
		string_t iszVal;
		int iVal;
		float flVal;
		float vecVal[3];
		color32 rgbaVal;
	};
	CHandle<CBaseEntity> eVal; // this can't be in the union because it has a constructor.

	fieldtype_t fieldType;

public:

	// constructor
	variant_t() : fieldType(FIELD_VOID), iVal(0) {}

	inline bool Bool( void ) const						{ return( fieldType == FIELD_BOOLEAN ) ? bVal : false; }
	inline const char *String( void ) const				{ return( fieldType == FIELD_STRING ) ? STRING(iszVal) : ToString(); }
	inline string_t StringID( void ) const				{ return( fieldType == FIELD_STRING ) ? iszVal : NULL_STRING; }
	inline int Int( void ) const						{ return( fieldType == FIELD_INTEGER ) ? iVal : 0; }
	inline float Float( void ) const					{ return( fieldType == FIELD_FLOAT ) ? flVal : 0; }
	inline const CHandle<CBaseEntity> &Entity(void) const;
	inline color32 Color32(void) const					{ return rgbaVal; }
	inline void Vector3D(Vector &vec) const;

	fieldtype_t FieldType( void ) { return fieldType; }

	void SetBool( bool b ) { bVal = b; fieldType = FIELD_BOOLEAN; }
	void SetString( string_t str ) { iszVal = str, fieldType = FIELD_STRING; }
	void SetInt( int val ) { iVal = val, fieldType = FIELD_INTEGER; }
	void SetFloat( float val ) { flVal = val, fieldType = FIELD_FLOAT; }
	inline void SetEntity( CBaseEntity *val )
	{
		eVal = val;
		fieldType = FIELD_EHANDLE; 
	}
	void SetVector3D( const Vector &val ) { vecVal[0] = val[0]; vecVal[1] = val[1]; vecVal[2] = val[2]; fieldType = FIELD_VECTOR; }
	void SetPositionVector3D( const Vector &val ) { vecVal[0] = val[0]; vecVal[1] = val[1]; vecVal[2] = val[2]; fieldType = FIELD_POSITION_VECTOR; }
	void SetColor32( color32 val ) { rgbaVal = val; fieldType = FIELD_COLOR32; }
	void SetColor32( int r, int g, int b, int a ) { rgbaVal.r = r; rgbaVal.g = g; rgbaVal.b = b; rgbaVal.a = a; fieldType = FIELD_COLOR32; }
	void Set( fieldtype_t ftype, void *data );
	void SetOther( void *data );
	bool Convert( fieldtype_t newType );

	static typedescription_t m_SaveBool[];
	static typedescription_t m_SaveInt[];
	static typedescription_t m_SaveFloat[];
	static typedescription_t m_SaveEHandle[];
	static typedescription_t m_SaveString[];
	static typedescription_t m_SaveColor[];
	static typedescription_t m_SaveVector[];
	static typedescription_t m_SavePositionVector[];
	static typedescription_t m_SaveVMatrix[];
	static typedescription_t m_SaveVMatrixWorldspace[];
	static typedescription_t m_SaveMatrix3x4Worldspace[];

protected:

	//
	// Returns a string representation of the value without modifying the variant.
	//
	const char *ToString( void ) const;

	friend class CVariantSaveDataOps;
};

class CEventsSaveDataOps : public ISaveRestoreOps
{
public:
	virtual void Save(const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave);
	virtual void Restore(const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore);
	virtual bool IsEmpty(const SaveRestoreFieldInfo_t &fieldInfo);
	virtual void MakeEmpty(const SaveRestoreFieldInfo_t &fieldInfo);
	virtual bool Parse(const SaveRestoreFieldInfo_t &fieldInfo, char const* szValue);
};

class CVariantSaveDataOps : public CDefSaveRestoreOps
{
	// saves the entire array of variables
	virtual void Save(const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave);

	// restores a single instance of the variable
	virtual void Restore(const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore);
	virtual bool IsEmpty(const SaveRestoreFieldInfo_t &fieldInfo);
	virtual void MakeEmpty(const SaveRestoreFieldInfo_t &fieldInfo);
};

extern ISaveRestoreOps *variantFuncs;	// function pointer set for save/restoring variants
extern ISaveRestoreOps *eventFuncs;		// function pointer set for save/restoring events


//-----------------------------------------------------------------------------
// Purpose: Returns this variant as a vector.
//-----------------------------------------------------------------------------
inline void variant_t::Vector3D(Vector &vec) const
{
	if (( fieldType == FIELD_VECTOR ) || ( fieldType == FIELD_POSITION_VECTOR ))
	{
		vec[0] =  vecVal[0];
		vec[1] =  vecVal[1];
		vec[2] =  vecVal[2];
	}
	else
	{
		vec = vec3_origin;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns this variant as an EHANDLE.
//-----------------------------------------------------------------------------
inline const CHandle<CBaseEntity> &variant_t::Entity(void) const
{
	if ( fieldType == FIELD_EHANDLE )
		return eVal;

	static CHandle<CBaseEntity> hNull;
	hNull.Set(NULL);
	return(hNull);
}

#endif // VARIANT_T_H
