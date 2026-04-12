#include "cbase.h"
#include "c_token_receiver.h"
#include "convar.h"
#include "checksum_crc.h"
#include "tier0/threadtools.h"

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
#include <cstring>
#define SOCKET		   int
#define INVALID_SOCKET ( -1 )
#define SOCKET_ERROR   ( -1 )
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TOKEN_RECEIVER_PORT 27080
#define TOKEN_MAX_LENGTH 128

static ConVar cl_masterserver_token( "cl_masterserver_token",
									 "0",
									 FCVAR_ARCHIVE | FCVAR_USERINFO,
									 "Master server token for clan identification" );

CTokenReceiver g_TokenReceiver;

//-----------------------------------------------------------------------------
// Thread-safe pending token
//-----------------------------------------------------------------------------
static CThreadMutex s_TokenMutex;
static char s_szPendingToken[TOKEN_MAX_LENGTH];
static bool s_bHasPendingToken = false;

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
	return ( c >= 'a' && c <= 'z' ) ||
		   ( c >= 'A' && c <= 'Z' ) ||
		   ( c >= '0' && c <= '9' ) ||
		   c == '-' || c == '_';
}

static bool ValidateToken( const char *token )
{
	if ( !token || !token[0] )
		return false;

	int len = Q_strlen( token );
	if ( len >= TOKEN_MAX_LENGTH )
		return false;

	for ( int i = 0; i < len; i++ )
	{
		if ( !IsValidTokenChar( token[i] ) )
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Parse token from a minimal HTTP POST body: {"token": "value"}
//-----------------------------------------------------------------------------
static bool ParseTokenFromBody( const char *body, char *outToken, int outSize )
{
	const char *tokenKey = Q_strstr( body, "\"token\"" );
	if ( !tokenKey )
		return false;

	const char *colon = Q_strstr( tokenKey + 7, ":" );
	if ( !colon )
		return false;

	const char *p = colon + 1;
	while ( *p == ' ' || *p == '\t' )
		p++;

	if ( *p != '"' )
		return false;

	p++;
	const char *valueEnd = Q_strstr( p, "\"" );
	if ( !valueEnd )
		return false;

	int len = MIN( (int)( valueEnd - p ), outSize - 1 );
	Q_strncpy( outToken, p, len + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// CTokenReceiver
//-----------------------------------------------------------------------------
CTokenReceiver::CTokenReceiver()
 : m_nPort( TOKEN_RECEIVER_PORT ),
   m_bRunning( false ),
   m_iSocket( -1 )
{
}

CTokenReceiver::~CTokenReceiver()
{
	Stop();
}

void CTokenReceiver::Start()
{
	if ( m_bRunning )
		return;

	m_bRunning = true;

	ThreadHandle_t hThread = CreateSimpleThread( ServerThreadProc, this );
	ReleaseThreadHandle( hThread );
}

void CTokenReceiver::Stop()
{
	if ( !m_bRunning )
		return;

	m_bRunning = false;

	if ( m_iSocket >= 0 )
	{
		CloseSocket( m_iSocket );
		m_iSocket = -1;
	}
}

void CTokenReceiver::ProcessPendingToken()
{
	s_TokenMutex.Lock();

	if ( s_bHasPendingToken )
	{
		cl_masterserver_token.SetValue( s_szPendingToken );

		CRC32_t crc;
		CRC32_Init( &crc );
		CRC32_ProcessBuffer( &crc, s_szPendingToken, Q_strlen( s_szPendingToken ) );
		CRC32_Final( &crc );
		Msg( "[TokenReceiver] Token set (CRC32: %08X)\n", crc );

		s_bHasPendingToken = false;
	}

	s_TokenMutex.Unlock();
}

void CTokenReceiver::RunServer()
{
#ifdef _WIN32
	WSADATA wsaData;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
	{
		Msg( "[TokenReceiver] WSAStartup failed\n" );
		return;
	}
#endif

	m_iSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( m_iSocket == INVALID_SOCKET )
	{
		Msg( "[TokenReceiver] Failed to create socket\n" );
		return;
	}

	int reuse = 1;
	setsockopt( m_iSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof( reuse ) );

	sockaddr_in serverAddr;
	serverAddr.sin_family	   = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	serverAddr.sin_port		   = htons( m_nPort );

	if ( bind( m_iSocket, (sockaddr *)&serverAddr, sizeof( serverAddr ) ) == SOCKET_ERROR )
	{
		Msg( "[TokenReceiver] Failed to bind to port %d\n", m_nPort );
		CloseSocket( m_iSocket );
		m_iSocket = -1;
#ifdef _WIN32
		WSACleanup();
#endif
		return;
	}

	if ( listen( m_iSocket, 1 ) == SOCKET_ERROR )
	{
		Msg( "[TokenReceiver] Failed to listen\n" );
		CloseSocket( m_iSocket );
		m_iSocket = -1;
#ifdef _WIN32
		WSACleanup();
#endif
		return;
	}

	Msg( "[TokenReceiver] Listening on 127.0.0.1:%d\n", m_nPort );

	while ( m_bRunning )
	{
		sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );
		SOCKET clientSocket = accept( m_iSocket, (sockaddr *)&clientAddr, &clientLen );

		if ( clientSocket == INVALID_SOCKET )
			continue;

		char buffer[4096] = { 0 };
		int recvSize = recv( clientSocket, buffer, sizeof( buffer ) - 1, 0 );

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
		const char *body = Q_strstr( buffer, "\r\n\r\n" );
		if ( !body )
		{
			CloseSocket( clientSocket );
			continue;
		}
		body += 4;

		char token[TOKEN_MAX_LENGTH];
		if ( !ParseTokenFromBody( body, token, sizeof( token ) ) || !ValidateToken( token ) )
		{
			const char *badResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"status\":\"invalid token\"}";
			send( clientSocket, badResponse, Q_strlen( badResponse ), 0 );
			CloseSocket( clientSocket );
			continue;
		}

		// Queue token for main thread
		s_TokenMutex.Lock();
		Q_strncpy( s_szPendingToken, token, sizeof( s_szPendingToken ) );
		s_bHasPendingToken = true;
		s_TokenMutex.Unlock();

		const char *response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}";
		send( clientSocket, response, Q_strlen( response ), 0 );

		CloseSocket( clientSocket );
	}

	CloseSocket( m_iSocket );
	m_iSocket = -1;

#ifdef _WIN32
	WSACleanup();
#endif

	Msg( "[TokenReceiver] Server stopped\n" );
}

uintp CTokenReceiver::ServerThreadProc( void *pParam )
{
	CTokenReceiver *pReceiver = static_cast<CTokenReceiver *>( pParam );
	if ( pReceiver )
	{
		pReceiver->RunServer();
	}
	return 0;
}
