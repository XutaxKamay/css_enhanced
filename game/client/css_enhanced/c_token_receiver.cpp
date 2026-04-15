//=============================================================================//
//
// Purpose: Token receiver for masterserver authentication.
//          Listens on localhost for HTTP POST requests containing auth tokens
//          and periodically re-authenticates with the master server.
//          Runs in a separate thread to avoid blocking the main game thread.
//
//=============================================================================//

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

// ConVars for storing token and session ID
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
// Helper functions
//-----------------------------------------------------------------------------

// Platform-independent socket close
static void CloseSocket( SOCKET sock )
{
#ifdef _WIN32
	closesocket( sock );
#else
	close( sock );
#endif
}

// Check if a character is valid for a token (alphanumeric, hyphen, underscore)
static bool IsValidTokenChar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_';
}

// Validate token length and characters
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

// Parse token from a minimal HTTP POST body: {"token": "value"}
// Returns true if token was successfully extracted
static bool ParseTokenFromBody( const char* body, char* outToken, int outSize )
{
	// Find the "token" key in the JSON
	const char* tokenKey = Q_strstr( body, "\"token\"" );
	if ( !tokenKey )
	{
		return false;
	}

	// Find the colon after "token"
	const char* colon = Q_strstr( tokenKey + 7, ":" );
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

	// Expect opening quote
	if ( *p != '"' )
	{
		return false;
	}

	// Find the closing quote
	p++;
	const char* valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
	{
		return false;
	}

	// Copy token value (limited to outSize)
	int len = MIN( ( int )( valueEnd - p ), outSize - 1 );
	Q_strncpy( outToken, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// libcurl callback structures and functions
//-----------------------------------------------------------------------------

// Buffer for storing HTTP response data
struct CurlWriteBuffer_t
{
	char data[1024];
	int pos;
};

// libcurl callback to accumulate response data
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

// Sends token to masterserver and gets back a session ID
// Returns true if successful, session ID stored in outSessionID
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

	// Configure curl request
	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_POSTFIELDS, postData );
	curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 10L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 0L );
	curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 0L );

	// Perform the request
	CURLcode res  = curl_easy_perform( curl );
	long httpCode = 0;
	curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

	curl_slist_free_all( headers );
	curl_easy_cleanup( curl );

	// Check response
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
// CTokenReceiver - Main class implementation
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
	// Ensure thread is stopped on destruction
	Stop();
}

// Start the token receiver server in a new thread
void CTokenReceiver::Start()
{
	if ( m_bInitialized )
	{
		return;
	}

	// Lock mutex before modifying shared state
	AUTO_LOCK( m_Mutex );

	m_bRunning	   = true;
	m_bInitialized = true;

	// Create the server thread
	m_hThread = CreateSimpleThread( ServerThreadProc, this );
}

// Stop the token receiver server and wait for thread to finish
void CTokenReceiver::Stop()
{
	if ( !m_bInitialized )
	{
		return;
	}

	// Signal the thread to stop
	{
		AUTO_LOCK( m_Mutex );
		m_bRunning	   = false;
		m_bInitialized = false;
	}

	// Wait for the thread to finish (1 second timeout)
	if ( m_hThread )
	{
		ThreadJoin( m_hThread, 1000 );
		m_hThread = 0;
	}

	// Clean up socket
	if ( m_iSocket >= 0 )
	{
		CloseSocket( m_iSocket );
		m_iSocket = -1;
	}
}

// Main server loop - runs in a separate thread
void CTokenReceiver::RunServer()
{
#ifdef _WIN32
	// Initialize Windows sockets
	WSADATA wsaData;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
	{
		DevMsg( "[TokenReceiver] WSAStartup failed\n" );
		AUTO_LOCK( m_Mutex );
		m_bRunning = false;
		return;
	}
#endif

	// Create server socket
	m_iSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( m_iSocket == INVALID_SOCKET )
	{
		DevMsg( "[TokenReceiver] Failed to create socket\n" );
		AUTO_LOCK( m_Mutex );
		m_bRunning = false;
		return;
	}

	// Allow address reuse (prevents "Address already in use" errors on restart)
	int reuse = 1;
	setsockopt( m_iSocket, SOL_SOCKET, SO_REUSEADDR, ( const char* )&reuse, sizeof( reuse ) );

	// Bind to localhost:PORT
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
		AUTO_LOCK( m_Mutex );
		m_bRunning = false;
		return;
	}

	// Start listening for connections
	if ( listen( m_iSocket, 1 ) == SOCKET_ERROR )
	{
		DevMsg( "[TokenReceiver] Failed to listen\n" );
		CloseSocket( m_iSocket );
		m_iSocket = -1;
#ifdef _WIN32
		WSACleanup();
#endif
		AUTO_LOCK( m_Mutex );
		m_bRunning = false;
		return;
	}

	DevMsg( "[TokenReceiver] Listening on 127.0.0.1:%d\n", m_nPort );

	// Server configuration
	float flLastAuthTime	   = 0.0f;
	const float flAuthInterval = 10.0f; // Re-authenticate every 10 seconds

	// Set socket to non-blocking so we can periodically check for shutdown
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket( m_iSocket, FIONBIO, &mode );
#else
	int flags = fcntl( m_iSocket, F_GETFL, 0 );
	fcntl( m_iSocket, F_SETFL, flags | O_NONBLOCK );
#endif

	// Main server loop - runs until Stop() is called
	while ( true )
	{
		// Check if we've been asked to stop (with mutex held briefly)
		{
			AUTO_LOCK( m_Mutex );
			if ( !m_bRunning )
			{
				break;
			}
		}

		float flNow = Plat_FloatTime();

		// Copy current token under lock (brief lock)
		char currentToken[TOKEN_MAX_LENGTH];
		{
			AUTO_LOCK( m_Mutex );
			if ( !m_bRunning )
			{
				break;
			}
			Q_strncpy( currentToken, cl_masterserver_token.GetString(), sizeof( currentToken ) );
		}

		// Periodic re-authentication with master server
		if ( currentToken[0] && Q_strcmp( currentToken, "0" ) != 0 && ( flNow - flLastAuthTime >= flAuthInterval ) )
		{
			char sessionID[128] = { 0 };

			if ( GetSessionIDFromMasterServer( currentToken, sessionID, sizeof( sessionID ) ) )
			{
				// Update session ID under lock
				AUTO_LOCK( m_Mutex );
				if ( m_bRunning )
				{
					cl_masterserver_session_id.SetValue( sessionID );
					DevMsg( "[TokenReceiver] Re-auth session ID: %s\n", sessionID );
				}
			}
			else
			{
				DevMsg( "[TokenReceiver] Failed to re-auth session ID\n" );
			}

			flLastAuthTime = flNow;
		}

		// Accept incoming connections (non-blocking)
		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );
		SOCKET clientSocket = accept( m_iSocket, ( sockaddr* )&clientAddr, &clientLen );

		if ( clientSocket == INVALID_SOCKET )
		{
			// No pending connections, sleep briefly and loop
			ThreadSleep( 100 );
			continue;
		}

		// Read HTTP request
		char buffer[4096] = { 0 };
		int recvSize	  = recv( clientSocket, buffer, sizeof( buffer ) - 1, 0 );

		if ( recvSize <= 0 )
		{
			CloseSocket( clientSocket );
			continue;
		}

		buffer[recvSize] = '\0';

		// Only accept POST /set_token requests
		if ( Q_strstr( buffer, "POST /set_token" ) == NULL )
		{
			CloseSocket( clientSocket );
			continue;
		}

		// Find HTTP body (after blank line)
		const char* body = Q_strstr( buffer, "\r\n\r\n" );
		if ( !body )
		{
			CloseSocket( clientSocket );
			continue;
		}
		body += 4; // Skip the blank line

		// Parse and validate token from request body
		char token[TOKEN_MAX_LENGTH];
		if ( !ParseTokenFromBody( body, token, sizeof( token ) ) || !ValidateToken( token ) )
		{
			const char* badResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: "
									  "application/json\r\n\r\n{\"status\":\"invalid token\"}";
			send( clientSocket, badResponse, Q_strlen( badResponse ), 0 );
			CloseSocket( clientSocket );
			continue;
		}

		// Set the token under lock
		{
			AUTO_LOCK( m_Mutex );
			if ( m_bRunning )
			{
				cl_masterserver_token.SetValue( token );

				// Log CRC for debugging
				CRC32_t crc;
				CRC32_Init( &crc );
				CRC32_ProcessBuffer( &crc, token, Q_strlen( token ) );
				CRC32_Final( &crc );
				DevMsg( "[TokenReceiver] Token set (CRC32: %08X)\n", crc );
			}
		}

		flLastAuthTime = Plat_FloatTime();

		// Send success response
		const char* response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";
		send( clientSocket, response, Q_strlen( response ), 0 );

		CloseSocket( clientSocket );
	}

	// Cleanup
	CloseSocket( m_iSocket );
	m_iSocket = -1;

#ifdef _WIN32
	WSACleanup();
#endif

	// Signal that we're done
	AUTO_LOCK( m_Mutex );
	m_bRunning = false;

	DevMsg( "[TokenReceiver] Server stopped\n" );
}

// Thread entry point
uintp CTokenReceiver::ServerThreadProc( void* pParam )
{
	CTokenReceiver* pReceiver = static_cast< CTokenReceiver* >( pParam );
	if ( pReceiver )
	{
		pReceiver->RunServer();
	}
	return 0;
}