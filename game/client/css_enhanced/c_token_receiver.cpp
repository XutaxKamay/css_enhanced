#include "cbase.h"
#include "c_token_receiver.h"
#include "convar.h"
#include "checksum_crc.h"
#include "tier0/threadtools.h"
#include "curl/curl.h"
#include "mathlib/mathlib.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment( lib, "ws2_32.lib" )
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#define SOCKET		   int
#define INVALID_SOCKET ( -1 )
#define SOCKET_ERROR   ( -1 )
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TOKEN_RECEIVER_PORT 27080
#define TOKEN_MAX_LENGTH	128

static ConVar cl_masterserver_token( "cl_masterserver_token",
									 "",
									 FCVAR_ARCHIVE,
									 "Master server token for authentification" );

static ConVar cl_masterserver_session_id( "cl_masterserver_session_id",
										  "",
										  FCVAR_ARCHIVE | FCVAR_USERINFO,
										  "Master server session for identification to servers" );

CTokenReceiver g_TokenReceiver;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static void CloseSocket( SOCKET sock )
{
#ifdef _WIN32
	closesocket( sock );
#else
	close( sock );
#endif
}

static bool IsValidTokenChar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_';
}

static bool ValidateToken( const char* token )
{
	if ( !token || !token[0] )
	{
		return false;
	}

	int len = Q_strlen( token );
	if ( len >= TOKEN_MAX_LENGTH )
	{
		return false;
	}

	for ( int i = 0; i < len; i++ )
	{
		if ( !IsValidTokenChar( token[i] ) )
		{
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Parse token from a minimal HTTP POST body: {"token": "value"}
//-----------------------------------------------------------------------------
static bool ParseTokenFromBody( const char* body, char* outToken, int outSize )
{
	const char* tokenKey = Q_strstr( body, "\"token\"" );
	if ( !tokenKey )
	{
		return false;
	}

	const char* colon = Q_strstr( tokenKey + 7, ":" );
	if ( !colon )
	{
		return false;
	}

	const char* p = colon + 1;
	while ( *p == ' ' || *p == '\t' )
	{
		p++;
	}

	if ( *p != '"' )
	{
		return false;
	}

	p++;
	const char* valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
	{
		return false;
	}

	int len = MIN( ( int )( valueEnd - p ), outSize - 1 );
	Q_strncpy( outToken, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// Helper for HTTP POST to masterserver
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
// Sends token to masterserver and gets back a session ID
// Returns true if successful
//-----------------------------------------------------------------------------
static bool GetSessionIDFromMasterServer( const char* token, char* outSessionID, int outSize )
{
	if ( !token || !token[0] )
	{
		return false;
	}

	CURL* curl = curl_easy_init();
	if ( !curl )
	{
		return false;
	}

	const char* url = "https://cssserv.xutaxkamay.com:27011/auth";
	char postData[256];
	Q_snprintf( postData, sizeof( postData ), "{\"token\":\"%s\"}", token );

	struct curl_slist* headers = NULL;
	headers					   = curl_slist_append( headers, "Content-Type: application/json" );

	CurlWriteBuffer_t response;
	response.pos	 = 0;
	response.data[0] = '\0';

	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_POSTFIELDS, postData );
	curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 10L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 0L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 0L );

	CURLcode res  = curl_easy_perform( curl );
	long httpCode = 0;
	curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

	curl_slist_free_all( headers );
	curl_easy_cleanup( curl );

	if ( res != CURLE_OK || httpCode != 200 )
	{
		return false;
	}

	// Parse session_id from response: {"session_id": "..."}
	const char* sessionIDKey = Q_strstr( response.data, "\"session_id\"" );
	if ( !sessionIDKey )
	{
		return false;
	}

	const char* colon = Q_strstr( sessionIDKey + 12, ":" );
	if ( !colon )
	{
		return false;
	}

	const char* p = colon + 1;
	while ( *p == ' ' || *p == '\t' )
	{
		p++;
	}

	if ( *p != '"' )
	{
		return false;
	}

	p++;
	const char* valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
	{
		return false;
	}

	int len = MIN( ( int )( valueEnd - p ), outSize - 1 );
	Q_strncpy( outSessionID, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// CTokenReceiver
//-----------------------------------------------------------------------------
CTokenReceiver::CTokenReceiver()
 : m_nPort( TOKEN_RECEIVER_PORT ),
   m_bRunning( false ),
   m_bInitialized( false ),
   m_iSocket( -1 ),
   m_hThread( ( ThreadHandle_t )0 )
{
}

CTokenReceiver::~CTokenReceiver()
{
	Stop();
}

void CTokenReceiver::Start()
{
	if ( m_bInitialized )
	{
		return;
	}

	m_bRunning	   = true;
	m_bInitialized = true;

	m_hThread = CreateSimpleThread( ServerThreadProc, this );
}

void CTokenReceiver::Stop()
{
	if ( !m_bInitialized )
	{
		return;
	}

	m_bRunning	   = false;
	m_bInitialized = false;

	if ( m_iSocket >= 0 )
	{
		CloseSocket( m_iSocket );
		m_iSocket = -1;
	}
}

void CTokenReceiver::RunServer()
{
#ifdef _WIN32
	WSADATA wsaData;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
	{
		DevMsg( "[TokenReceiver] WSAStartup failed\n" );
		return;
	}
#endif

	m_iSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( m_iSocket == INVALID_SOCKET )
	{
		DevMsg( "[TokenReceiver] Failed to create socket\n" );
		return;
	}

	int reuse = 1;
	setsockopt( m_iSocket, SOL_SOCKET, SO_REUSEADDR, ( const char* )&reuse, sizeof( reuse ) );

	sockaddr_in serverAddr;
	serverAddr.sin_family	   = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	serverAddr.sin_port		   = htons( m_nPort );

	if ( bind( m_iSocket, ( sockaddr* )&serverAddr, sizeof( serverAddr ) ) == SOCKET_ERROR )
	{
		DevMsg( "[TokenReceiver] Failed to bind to port %d\n", m_nPort );
		CloseSocket( m_iSocket );
		m_iSocket = -1;
#ifdef _WIN32
		WSACleanup();
#endif
		return;
	}

	if ( listen( m_iSocket, 1 ) == SOCKET_ERROR )
	{
		DevMsg( "[TokenReceiver] Failed to listen\n" );
		CloseSocket( m_iSocket );
		m_iSocket = -1;
#ifdef _WIN32
		WSACleanup();
#endif
		return;
	}

	DevMsg( "[TokenReceiver] Listening on 127.0.0.1:%d\n", m_nPort );

	float flLastAuthTime	   = 0.0f;
	const float flAuthInterval = 10.0f; // Re-auth every 10 seconds

	// Set socket to non-blocking so we can check for periodic re-auth
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket( m_iSocket, FIONBIO, &mode );
#else
	int flags = fcntl( m_iSocket, F_GETFL, 0 );
	fcntl( m_iSocket, F_SETFL, flags | O_NONBLOCK );
#endif

	while ( m_bRunning )
	{
		// Periodic re-auth: if we have a token, re-auth every 10 seconds
		float flNow = Plat_FloatTime();

		auto currentToken = cl_masterserver_token.GetString();

		if ( currentToken[0] && Q_strcmp( currentToken, "0" ) != 0 && ( flNow - flLastAuthTime >= flAuthInterval ) )
		{
			char sessionID[128] = { 0 };

			if ( GetSessionIDFromMasterServer( currentToken, sessionID, sizeof( sessionID ) ) )
			{
				cl_masterserver_session_id.SetValue( sessionID );
				DevMsg( "[TokenReceiver] Re-auth session ID: %s\n", sessionID );
			}
			else
			{
				DevMsg( "[TokenReceiver] Failed to re-auth session ID\n" );
			}

			flLastAuthTime = flNow;
		}

		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );
		SOCKET clientSocket = accept( m_iSocket, ( sockaddr* )&clientAddr, &clientLen );

		if ( clientSocket == INVALID_SOCKET )
		{
			ThreadSleep( 100 );
			continue;
		}

		char buffer[4096] = { 0 };
		int recvSize	  = recv( clientSocket, buffer, sizeof( buffer ) - 1, 0 );

		if ( recvSize <= 0 )
		{
			CloseSocket( clientSocket );
			continue;
		}

		buffer[recvSize] = '\0';

		// Only accept POST /set_token
		if ( Q_strstr( buffer, "POST /set_token" ) == NULL )
		{
			CloseSocket( clientSocket );
			continue;
		}

		// Find HTTP body after \r\n\r\n
		const char* body = Q_strstr( buffer, "\r\n\r\n" );
		if ( !body )
		{
			CloseSocket( clientSocket );
			continue;
		}
		body += 4;

		char token[TOKEN_MAX_LENGTH];
		if ( !ParseTokenFromBody( body, token, sizeof( token ) ) || !ValidateToken( token ) )
		{
			const char* badResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
									  "application/json\r\n\r\n{\"status\":\"invalid token\"}";
			send( clientSocket, badResponse, Q_strlen( badResponse ), 0 );
			CloseSocket( clientSocket );
			continue;
		}

		// Set the token cvar
		cl_masterserver_token.SetValue( token );

		CRC32_t crc;
		CRC32_Init( &crc );
		CRC32_ProcessBuffer( &crc, token, Q_strlen( token ) );
		CRC32_Final( &crc );
		DevMsg( "[TokenReceiver] Token set (CRC32: %08X)\n", crc );

		flLastAuthTime = Plat_FloatTime();

		const char* response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";
		send( clientSocket, response, Q_strlen( response ), 0 );

		CloseSocket( clientSocket );
	}

	CloseSocket( m_iSocket );
	m_iSocket = -1;

#ifdef _WIN32
	WSACleanup();
#endif

	m_bRunning = false;

	DevMsg( "[TokenReceiver] Server stopped\n" );
}

uintp CTokenReceiver::ServerThreadProc( void* pParam )
{
	CTokenReceiver* pReceiver = static_cast< CTokenReceiver* >( pParam );
	if ( pReceiver )
	{
		pReceiver->RunServer();
	}
	return 0;
}