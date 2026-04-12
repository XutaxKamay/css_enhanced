//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "usercmd.h"
#include "bitbuf.h"
#include "checksum_md5.h"
#include "const.h"
#include "utlvector.h"
#include "shareddefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#ifdef CLIENT_DLL
#include "c_baseplayer.h"
#include "cliententitylist.h"
#else
#include "player.h"
#endif

#include "tier0/memdbgon.h"

// TF2 specific, need enough space for OBJ_LAST items from tf_shareddefs.h
#define WEAPON_SUBTYPE_BITS	6

//-----------------------------------------------------------------------------
// Purpose: Write a delta compressed user command.
// Input  : *buf - 
//			*to - 
//			*from - 
// Output : static
//-----------------------------------------------------------------------------
void WriteUsercmd( bf_write *buf, const CUserCmd *to, const CUserCmd *from )
{
	if ( to->viewangles[ 0 ] != from->viewangles[ 0 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 0 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->viewangles[ 1 ] != from->viewangles[ 1 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 1 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->viewangles[ 2 ] != from->viewangles[ 2 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 2 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->forwardmove != from->forwardmove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->forwardmove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->sidemove != from->sidemove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->sidemove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->upmove != from->upmove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->upmove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->buttons != from->buttons )
	{
		buf->WriteOneBit( 1 );
	  	buf->WriteUBitLong( to->buttons, 32 );
 	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->impulse != from->impulse )
	{
		buf->WriteOneBit( 1 );
	    buf->WriteUBitLong( to->impulse, 8 );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}


	if ( to->weaponselect != from->weaponselect )
	{
		buf->WriteOneBit( 1 );
		buf->WriteUBitLong( to->weaponselect, MAX_EDICT_BITS );

		if ( to->weaponsubtype != from->weaponsubtype )
		{
			buf->WriteOneBit( 1 );
			buf->WriteUBitLong( to->weaponsubtype, WEAPON_SUBTYPE_BITS );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

#ifdef USERCMD_DEBUG_SIMULATION_DATA
#ifdef CLIENT_DLL
	int highestEntityIndex = 0;
	if ( cl_entitylist )
	{
		highestEntityIndex = cl_entitylist->GetHighestEntityIndex();
	}
#else
	static constexpr auto highestEntityIndex = MAX_EDICTS - 1;
#endif

	// Write entity count
	buf->WriteUBitLong( highestEntityIndex, 11 );

	// Write finally simulation data with entity index
	for ( unsigned int i = 0; i <= highestEntityIndex; i++ )
	{
		if ( from->simulationdata[i].sim_tick_count != to->simulationdata[i].sim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].sim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}

		if ( from->simulationdata[i].anim_tick_count != to->simulationdata[i].anim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].anim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}

		buf->WriteOneBit( to->simulationdata[i].is_sim_interpolated );
		buf->WriteOneBit( to->simulationdata[i].is_anim_interpolated );

		if ( from->simulationdata[i].interpolated_sim_tick_count != to->simulationdata[i].interpolated_sim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].interpolated_sim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}

		if ( from->simulationdata[i].interpolated_anim_tick_count
			 != to->simulationdata[i].interpolated_anim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].interpolated_anim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}

		if ( from->simulationdata[i].end_sim_tick_count != to->simulationdata[i].end_sim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].end_sim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}

		if ( from->simulationdata[i].end_anim_tick_count != to->simulationdata[i].end_anim_tick_count )
		{
			buf->WriteOneBit( 1 );
			buf->WriteVarInt64( to->simulationdata[i].end_anim_tick_count );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}
	}
#endif

	if ( to->debug_flags != from->debug_flags )
	{
		buf->WriteOneBit( 1 );
		buf->WriteUBitLong( to->debug_flags, 2 );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( from->interpolated_amount_frac != to->interpolated_amount_frac )
	{
		buf->WriteOneBit( 1 );
		buf->WriteBitFloat( to->interpolated_amount_frac );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( from->snapshot_tickcount != to->snapshot_tickcount )
	{
		buf->WriteOneBit( 1 );
		buf->WriteVarInt64( to->snapshot_tickcount );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

#if defined( HL2_CLIENT_DLL )
	if ( to->entitygroundcontact.Count() != 0 )
	{
		buf->WriteOneBit( 1 );
		buf->WriteShort( to->entitygroundcontact.Count() );
		int i;
		for ( i = 0; i < to->entitygroundcontact.Count(); i++ )
		{
			buf->WriteUBitLong( to->entitygroundcontact[i].entindex, MAX_EDICT_BITS );
			buf->WriteBitCoord( to->entitygroundcontact[i].minheight );
			buf->WriteBitCoord( to->entitygroundcontact[i].maxheight );
		}
	}
	else
	{
		buf->WriteOneBit( 0 );
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Read in a delta compressed usercommand.
// Input  : *buf -
//			*move -
//			*from -
// Output : static void ReadUsercmd
//-----------------------------------------------------------------------------
void ReadUsercmd( bf_read* buf, CUserCmd* move, CUserCmd* from )
{
	// Assume no change
	*move = *from;

	// Read direction
	if ( buf->ReadOneBit() )
	{
		move->viewangles[0] = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->viewangles[1] = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->viewangles[2] = buf->ReadFloat();
	}

	// Moved value validation and clamping to CBasePlayer::ProcessUsercmds()

	// Read movement
	if ( buf->ReadOneBit() )
	{
		move->forwardmove = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->sidemove = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->upmove = buf->ReadFloat();
	}

	// read buttons
	if ( buf->ReadOneBit() )
	{
		move->buttons = buf->ReadUBitLong( 32 );
	}

	if ( buf->ReadOneBit() )
	{
		move->impulse = buf->ReadUBitLong( 8 );
	}

	if ( buf->ReadOneBit() )
	{
		move->weaponselect = buf->ReadUBitLong( MAX_EDICT_BITS );
		if ( buf->ReadOneBit() )
		{
			move->weaponsubtype = buf->ReadUBitLong( WEAPON_SUBTYPE_BITS );
		}
	}

#ifdef USERCMD_DEBUG_SIMULATION_DATA

	auto highestEntityIndex = buf->ReadUBitLong( 11 );

	highestEntityIndex = MIN( MAX_EDICTS - 1, highestEntityIndex );

	for ( unsigned int i = 0; i <= highestEntityIndex; i++ )
	{
		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].sim_tick_count = buf->ReadVarInt64();
		}

		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].anim_tick_count = buf->ReadVarInt64();
		}

		move->simulationdata[i].is_sim_interpolated	 = buf->ReadOneBit();
		move->simulationdata[i].is_anim_interpolated = buf->ReadOneBit();

		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].interpolated_sim_tick_count = buf->ReadVarInt64();
		}

		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].interpolated_anim_tick_count = buf->ReadVarInt64();
		}

		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].end_sim_tick_count = buf->ReadVarInt64();
		}

		if ( buf->ReadOneBit() )
		{
			move->simulationdata[i].end_anim_tick_count = buf->ReadVarInt64();
		}
	}
#endif

	if ( buf->ReadOneBit() )
	{
		move->debug_flags = (CUserCmd::debug_flags_t)buf->ReadUBitLong(2);
    }

    if ( buf->ReadOneBit() )
    {
        move->interpolated_amount_frac = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->snapshot_tickcount = buf->ReadVarInt64();
	}

#if defined( HL2_DLL )
	if ( buf->ReadOneBit() )
	{
		move->entitygroundcontact.SetCount( buf->ReadShort() );

		int i;
		for (i = 0; i < move->entitygroundcontact.Count(); i++)
		{
			move->entitygroundcontact[i].entindex = buf->ReadUBitLong( MAX_EDICT_BITS );
			move->entitygroundcontact[i].minheight = buf->ReadBitCoord( );
			move->entitygroundcontact[i].maxheight = buf->ReadBitCoord( );
		}
	}
#endif
}
