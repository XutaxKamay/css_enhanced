//=============================================================================//
//
// Purpose: Server-side masterserver HTTPS API client.
//          Queries the masterserver to retrieve player information
//          (e.g., default clan tag) using their masterserver session id.
//
//=============================================================================//

#include "cbase.h"
#include "sv_masterserver.h"

#ifdef CSTRIKE_DLL

#include "cs_player.h"
#include "cs_shareddefs.h"
#include "vstdlib/jobthread.h"
#include "utlvector.h"
#include "curl/curl.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#undef AddJob

#define MASTERSERVER_HTTPS_HOST "cssserv.xutaxkamay.com"
#define MASTERSERVER_HTTPS_PORT 27011
#define MASTERSERVER_CLAN_DEFAULT_URL "https://" MASTERSERVER_HTTPS_HOST ":27011/clan/default/"

#define MASTERSERVER_POLL_INTERVAL 10.0f

//-----------------------------------------------------------------------------
// Response structure queued from worker threads
//-----------------------------------------------------------------------------
struct MasterServerResponse_t
{
	enum ResponseType_t
	{
		RESPONSE_CLAN_TAG,
	};

	ResponseType_t type;
	int playerIndex;
	char clanTag[MAX_CLAN_TAG_LENGTH];
};

//-----------------------------------------------------------------------------
// Curl write callback - appends data to a buffer
//-----------------------------------------------------------------------------
struct CurlWriteBuffer_t
{
	char data[1024];
	int pos;
};

static size_t CurlWriteCallback( void *contents, size_t size, size_t nmemb, void *userp )
{
	CurlWriteBuffer_t *buf = (CurlWriteBuffer_t *)userp;
	size_t totalSize = size * nmemb;
	size_t remaining = sizeof( buf->data ) - buf->pos - 1;
	size_t toCopy = MIN( totalSize, remaining );
	Q_memcpy( buf->data + buf->pos, contents, toCopy );
	buf->pos += (int)toCopy;
	buf->data[buf->pos] = '\0';
	return totalSize;
}

//-----------------------------------------------------------------------------
// Thread-safe response queue
//-----------------------------------------------------------------------------
static CThreadMutex s_ResponseMutex;
static CUtlVector<MasterServerResponse_t> s_PendingResponses;

//-----------------------------------------------------------------------------
// Per-player polling state
//-----------------------------------------------------------------------------
static float s_flNextPollTime[MAX_PLAYERS + 1];

//-----------------------------------------------------------------------------
// Worker thread parameters
//-----------------------------------------------------------------------------
struct ClanTagRequestParams_t
{
	int playerIndex;
	char sessionID[128];
};

//-----------------------------------------------------------------------------
// Parse a simple JSON string value: {"tag": "value"}
// Returns true if found and copies into outValue.
//-----------------------------------------------------------------------------
static bool ParseJSONStringField( const char *json, const char *field, char *outValue, int outSize )
{
	char searchKey[64];
	Q_snprintf( searchKey, sizeof( searchKey ), "\"%s\"", field );

	const char *fieldStart = Q_strstr( json, searchKey );
	if ( !fieldStart )
		return false;

	const char *colon = Q_strstr( fieldStart + Q_strlen( searchKey ), ":" );
	if ( !colon )
		return false;

	// Skip whitespace after colon
	const char *p = colon + 1;
	while ( *p == ' ' || *p == '\t' )
		p++;

	if ( *p != '"' )
		return false;

	p++; // skip opening quote
	const char *valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
		return false;

	int len = MIN( (int)( valueEnd - p ), outSize - 1 );
	Q_strncpy( outValue, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// Worker thread: queries masterserver for default clan tag
//-----------------------------------------------------------------------------
static uintp ClanTagWorkerThread( void *pParam )
{
	ClanTagRequestParams_t *req = (ClanTagRequestParams_t *)pParam;

	CURL *curl = curl_easy_init();
	if ( !curl )
	{
		delete req;
		return 0;
	}

	char url[512];
	Q_snprintf( url, sizeof( url ), "%s%s", MASTERSERVER_CLAN_DEFAULT_URL, req->sessionID );

	CurlWriteBuffer_t response;
	response.pos = 0;
	response.data[0] = '\0';

	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
	curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 1L );
	curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
	curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 10L );
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 15L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 0L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 0L );
	curl_easy_setopt( curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );

	CURLcode res = curl_easy_perform( curl );

	if ( res == CURLE_OK )
	{
		long httpCode = 0;
		curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

		if ( httpCode == 200 )
		{
			char clanTag[MAX_CLAN_TAG_LENGTH];
			if ( ParseJSONStringField( response.data, "tag", clanTag, sizeof( clanTag ) ) )
			{
				MasterServerResponse_t resp;
				resp.type = MasterServerResponse_t::RESPONSE_CLAN_TAG;
				resp.playerIndex = req->playerIndex;
				Q_strncpy( resp.clanTag, clanTag, sizeof( resp.clanTag ) );

				s_ResponseMutex.Lock();
				s_PendingResponses.AddToTail( resp );
				s_ResponseMutex.Unlock();
			}
		}
		else
		{
			DevMsg( "[MasterServer] Clan tag query returned HTTP %ld for player %d\n", httpCode, req->playerIndex );
		}
	}
	else
	{
		Warning( "[MasterServer] Failed to query clan tag: %s\n", curl_easy_strerror( res ) );
	}

	curl_easy_cleanup( curl );
	delete req;
	return 0;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

void MasterServer_RequestDefaultClanTag( int playerIndex, const char *sessionID )
{
	if ( !sessionID || !sessionID[0] || Q_strcmp( sessionID, "0" ) == 0 )
		return;

	ClanTagRequestParams_t *req = new ClanTagRequestParams_t;
	req->playerIndex = playerIndex;
	Q_strncpy( req->sessionID, sessionID, sizeof( req->sessionID ) );

	g_pThreadPool->AddJob( new CFunctorJob( CreateFunctor( ClanTagWorkerThread, req ) ) );
}

void MasterServer_SetDefaultClanTag( void )
{
	s_ResponseMutex.Lock();

	for ( int i = 0; i < s_PendingResponses.Count(); i++ )
	{
		const MasterServerResponse_t& resp = s_PendingResponses[i];

		switch ( resp.type )
		{
			case MasterServerResponse_t::RESPONSE_CLAN_TAG:
			{
				CCSPlayer* pPlayer = ToCSPlayer( UTIL_PlayerByIndex( resp.playerIndex ) );
				if ( pPlayer && pPlayer->IsConnected() )
				{
					pPlayer->SetClanTag( resp.clanTag );
					DevMsg( "[MasterServer] Set clan tag '%s' for player '%s'\n",
							resp.clanTag,
							pPlayer->GetPlayerName() );
				}
				break;
			}
		}
	}

	s_PendingResponses.RemoveAll();

	s_ResponseMutex.Unlock();

	// Periodic polling: re-request clan tags for all connected players
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CCSPlayer *pPlayer = ToCSPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pPlayer || !pPlayer->IsConnected() || pPlayer->IsBot() )
			continue;

		if ( gpGlobals->curtime < s_flNextPollTime[i] )
			continue;

		s_flNextPollTime[i] = gpGlobals->curtime + MASTERSERVER_POLL_INTERVAL;

		const char *pszSessionID = engine->GetClientConVarValue( i, "cl_masterserver_session_id" );
		if ( pszSessionID && pszSessionID[0] && Q_strcmp( pszSessionID, "0" ) != 0 )
		{
			MasterServer_RequestDefaultClanTag( i, pszSessionID );
		}
	}
}

void MasterServer_ProcessResponses()
{
	g_pThreadPool->AddJob( new CFunctorJob( CreateFunctor( MasterServer_SetDefaultClanTag ) ) );
}

#endif // CSTRIKE_DLL
