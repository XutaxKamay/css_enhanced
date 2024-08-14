#ifndef C_ENTITYINPUT_H
#define C_ENTITYINPUT_H

#ifdef WIN32
#pragma once
#endif

#include "cbase.h"
#include "c_entityoutput.h"
#include "c_entitylistpool.h"

class variant_t;

//-----------------------------------------------------------------------------
// Purpose: Used to request a value, or a set of values, from a set of entities.
//			used when a multi-input variable needs to refresh it's inputs
//-----------------------------------------------------------------------------
class C_MultiInputVar
{
public:
	C_MultiInputVar() : m_InputList(NULL) {}
	~C_MultiInputVar();

	struct inputitem_t
	{
		variant_t value;	// local copy of variable (maybe make this a variant?) 
		int	outputID;		// the ID number of the output that sent this
		inputitem_t *next;

		// allocate and free from MPool memory
		static void *operator new( size_t stAllocBlock );	
		static void *operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
		static void operator delete( void *pMem );
		static void operator delete( void *pMem, int nBlockUse, const char *pFileName, int nLine ) { operator delete(pMem); }
	};

	inputitem_t *m_InputList;	// list of data
	int m_bUpdatedThisFrame;

	void AddValue( variant_t newVal, int outputID );

	DECLARE_SIMPLE_DATADESC();
};

#endif