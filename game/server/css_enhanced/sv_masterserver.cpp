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
#include "inetchannelinfo.h" // for GetPlayerNetInfo

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#undef AddJob

#define MASTERSERVER_HTTPS_HOST		  "cssserv.xutaxkamay.com"
#define MASTERSERVER_HTTPS_PORT		  27011
#define MASTERSERVER_CLAN_DEFAULT_URL "https://" MASTERSERVER_HTTPS_HOST ":27011/clan/default/"
#define MASTERSERVER_AUTH_VERIFY_URL  "https://" MASTERSERVER_HTTPS_HOST ":27011/auth/"

#define MASTERSERVER_POLL_INTERVAL	  10.0f

static ConVar sv_masterserver_auth_enforce( "sv_masterserver_auth_enforce",
											"0",
											FCVAR_NOTIFY,
											"Whether to kick players with invalid auth" );

//-----------------------------------------------------------------------------
// Response structure queued from worker threads
//-----------------------------------------------------------------------------
struct MasterServerResponse_t
{
	enum ResponseType_t
	{
		RESPONSE_AUTH,
		RESPONSE_CLAN_TAG,
	};

	ResponseType_t type;
	int playerIndex;
	bool accepted;
	char error[128];
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

static size_t CurlWriteCallback( void* contents, size_t size, size_t nmemb, void* userp )
{
	CurlWriteBuffer_t* buf = ( CurlWriteBuffer_t* )userp;
	size_t totalSize	   = size * nmemb;
	size_t remaining	   = sizeof( buf->data ) - buf->pos - 1;
	size_t toCopy		   = MIN( totalSize, remaining );
	Q_memcpy( buf->data + buf->pos, contents, toCopy );
	buf->pos			+= ( int )toCopy;
	buf->data[buf->pos]	 = '\0';
	return totalSize;
}

//-----------------------------------------------------------------------------
// Thread-safe response queue
//-----------------------------------------------------------------------------
static CThreadMutex s_ResponseMutex;
static CUtlVector< MasterServerResponse_t > s_PendingResponses;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static float s_flNextProcessTime = 0.0f;

#define MASTERSERVER_PROCESS_INTERVAL 1.0f

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Worker thread parameters
//-----------------------------------------------------------------------------
struct RequestParams_t
{
	int playerIndex;
	char sessionID[128];
};

//-----------------------------------------------------------------------------
// Parse a simple JSON string value: {"tag": "value"}
// Returns true if found and copies into outValue.
//-----------------------------------------------------------------------------
static bool ParseJSONStringField( const char* json, const char* field, char* outValue, int outSize )
{
	char searchKey[64];
	Q_snprintf( searchKey, sizeof( searchKey ), "\"%s\"", field );

	const char* fieldStart = Q_strstr( json, searchKey );
	if ( !fieldStart )
	{
		return false;
	}

	const char* colon = Q_strstr( fieldStart + Q_strlen( searchKey ), ":" );
	if ( !colon )
	{
		return false;
	}

	// Skip whitespace after colon
	const char* p = colon + 1;
	while ( *p == ' ' || *p == '\t' )
	{
		p++;
	}

	if ( *p != '"' )
	{
		return false;
	}

	p++; // skip opening quote
	const char* valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
	{
		return false;
	}

	int len = MIN( ( int )( valueEnd - p ), outSize - 1 );
	Q_strncpy( outValue, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// Helper: verify session with masterserver
// Returns true if session is valid and accepted
//-----------------------------------------------------------------------------
static bool VerifySession( const char* sessionID, char* outError, int errorSize )
{
	CURL* curl = curl_easy_init();
	if ( !curl )
	{
		return false;
	}

	char url[512];
	Q_snprintf( url, sizeof( url ), "%s%s", MASTERSERVER_AUTH_VERIFY_URL, sessionID );

	DevMsg( "[MasterServer] Verify URL: %s\n", url );

	CurlWriteBuffer_t response;
	response.pos	 = 0;
	response.data[0] = '\0';

	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_POST, 1 );
	curl_easy_setopt( curl, CURLOPT_POSTFIELDS, "{}" );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
	curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 1L );
	curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
	curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 10L );
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 15L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 0L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 0L );
	curl_easy_setopt( curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
	curl_easy_setopt( curl, CURLOPT_COPYPOSTFIELDS, "{}" );
	struct curl_slist* headers = NULL;
	headers					   = curl_slist_append( headers, "Content-Type: application/json" );
	curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

	bool accepted = false;
	if ( outError && errorSize > 0 )
	{
		outError[0] = '\0';
	}

	CURLcode res = curl_easy_perform( curl );

	DevMsg( "[MasterServer] CURL result: %d\n", res );

	if ( res == CURLE_OK )
	{
		long httpCode = 0;
		curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

		DevMsg( "[MasterServer] HTTP code: %ld\n", httpCode );
		DevMsg( "[MasterServer] Raw response: '%s'\n", response.data );

		if ( httpCode == 200 )
		{
			char acceptedStr[16];
			if ( ParseJSONStringField( response.data, "accepted", acceptedStr, sizeof( acceptedStr ) ) )
			{
				accepted = Q_strcmp( acceptedStr, "true" ) == 0;
			}

			if ( !accepted && outError && errorSize > 0 )
			{
				ParseJSONStringField( response.data, "error", outError, errorSize );
			}
		}
	}

	curl_slist_free_all( headers );
	curl_easy_cleanup( curl );
	return accepted;
}

//-----------------------------------------------------------------------------
// Helper: fetch clan tag from masterserver
// Returns true if clan tag found
//-----------------------------------------------------------------------------
static bool FetchClanTag( const char* sessionID, char* outClanTag, int clanTagSize, char* outError, int errorSize )
{
	CURL* curl = curl_easy_init();
	if ( !curl )
	{
		return false;
	}

	char url[512];
	Q_snprintf( url, sizeof( url ), "%s%s", MASTERSERVER_CLAN_DEFAULT_URL, sessionID );

	DevMsg( "[MasterServer] ClanTag URL: %s\n", url );

	CurlWriteBuffer_t response;
	response.pos	 = 0;
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

	DevMsg( "[MasterServer] ClanTag CURL result: %d\n", res );

	bool found = false;

	if ( outError && errorSize > 0 )
	{
		outError[0] = '\0';
	}

	if ( res == CURLE_OK )
	{
		long httpCode = 0;
		curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

		DevMsg( "[MasterServer] ClanTag HTTP code: %ld\n", httpCode );
		DevMsg( "[MasterServer] ClanTag Raw response: '%s'\n", response.data );

		if ( httpCode == 200 )
		{
			if ( ParseJSONStringField( response.data, "tag", outClanTag, clanTagSize ) )
			{
				found = true;
			}

			if ( !found && outError && errorSize > 0 )
			{
				ParseJSONStringField( response.data, "error", outError, errorSize );
			}
		}
		else if ( outError && errorSize > 0 )
		{
			ParseJSONStringField( response.data, "error", outError, errorSize );
		}
	}

	curl_easy_cleanup( curl );
	return found;
}

//-----------------------------------------------------------------------------
// Worker thread: queries masterserver for default clan tag
//-----------------------------------------------------------------------------
static uintp WorkerThread( void* pParam )
{
	char clanTag[MAX_CLAN_TAG_LENGTH];
	char error[128];

	RequestParams_t* req = ( RequestParams_t* )pParam;
	bool verified		 = VerifySession( req->sessionID, error, sizeof( error ) );

	DevMsg( "[MasterServer] Verify result: session=%s accepted=%d error='%s'\n", req->sessionID, verified, error );

	if ( !verified )
	{
		MasterServerResponse_t resp;
		resp.type		 = MasterServerResponse_t::RESPONSE_AUTH;
		resp.playerIndex = req->playerIndex;
		resp.accepted	 = false;
		Q_strncpy( resp.error, error, sizeof( resp.error ) );

		s_ResponseMutex.Lock();
		s_PendingResponses.AddToTail( resp );
		s_ResponseMutex.Unlock();

		return 0;
	}

	DevMsg( "[MasterServer] Verification passed, fetching clan tag...\n" );

	if ( FetchClanTag( req->sessionID, clanTag, sizeof( clanTag ), error, sizeof( error ) ) )
	{
		MasterServerResponse_t resp;
		resp.type		 = MasterServerResponse_t::RESPONSE_CLAN_TAG;
		resp.playerIndex = req->playerIndex;
		Q_strncpy( resp.clanTag, clanTag, sizeof( resp.clanTag ) );

		s_ResponseMutex.Lock();
		s_PendingResponses.AddToTail( resp );
		s_ResponseMutex.Unlock();

		DevMsg( "[MasterServer] Failed to fetch clan tag: session=%s\n", req->sessionID );

		return 0;
	}

	DevMsg( "[MasterServer] Clan tag not found for session=%s, error='%s'\n", req->sessionID, error );

	return 0;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

void MasterServer_RequestAuth( int playerIndex, const char* sessionID )
{
	DevMsg( "[MasterServer] RequestAuth: player=%d session=%s\n", playerIndex, sessionID ? sessionID : "(null)" );

	if ( !sessionID || !sessionID[0] || Q_strcmp( sessionID, "0" ) == 0 )
	{
		return;
	}

	static RequestParams_t reqs[MAX_PLAYERS + 1];

	auto req = &reqs[playerIndex];

	req->playerIndex = playerIndex;
	Q_strncpy( req->sessionID, sessionID, sizeof( req->sessionID ) );

	INetChannelInfo* pNetChan = engine->GetPlayerNetInfo( playerIndex );
	CCSPlayer* pPlayer		  = ToCSPlayer( UTIL_PlayerByIndex( playerIndex ) );

	if ( pNetChan )
	{
		const char* pszIP = pNetChan->GetAddress();

		DevMsg( "[MasterServer] Player %s with IP %s requesting auth\n",
				pPlayer->GetPlayerName(),
				pszIP ? pszIP : "(null)" );

		if ( pszIP )
		{
			g_pThreadPool->AddJob( new CFunctorJob( CreateFunctor( WorkerThread, req ) ) );
		}
	}
	else
	{
		DevMsg( "[MasterServer] NULL network channel for player %s, can't request auth\n", pPlayer->GetPlayerName() );
	}
}

void MasterServer_Auth( void )
{
	s_ResponseMutex.Lock();

	for ( int i = 0; i < s_PendingResponses.Count(); i++ )
	{
		const MasterServerResponse_t& resp = s_PendingResponses[i];

		switch ( resp.type )
		{
			case MasterServerResponse_t::RESPONSE_AUTH:
			{
				if ( !resp.accepted )
				{
					CCSPlayer* pPlayer	   = ToCSPlayer( UTIL_PlayerByIndex( resp.playerIndex ) );
					const char* playerName = pPlayer ? pPlayer->GetPlayerName() : "unknown";
					DevMsg( "[MasterServer] Kicking player %d (%s): reason='%s'\n",
							resp.playerIndex,
							playerName,
							resp.error );

					if ( sv_masterserver_auth_enforce.GetBool() )
					{
						CCSPlayer* pPlayer = ToCSPlayer( UTIL_PlayerByIndex( resp.playerIndex ) );
						if ( pPlayer && pPlayer->IsConnected() )
						{
							engine->ServerCommand( UTIL_VarArgs( "kickid %d %s\n", pPlayer->GetUserID(), resp.error ) );
						}
					}
				}
				else if ( sv_masterserver_auth_enforce.GetBool() )
				{
					DevMsg( "[MasterServer] Player %d accepted\n", resp.playerIndex );
				}
				break;
			}
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
}

void MasterServer_ProcessResponses()
{
	if ( gpGlobals->curtime < s_flNextProcessTime )
	{
		return;
	}

	s_flNextProcessTime = gpGlobals->curtime + MASTERSERVER_PROCESS_INTERVAL;

	g_pThreadPool->AddJob( new CFunctorJob( CreateFunctor( MasterServer_Auth ) ) );
}

#endif // CSTRIKE_DLL
