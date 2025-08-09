#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <fstream>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

constexpr uint32_t CONNECTIONLESS_HEADER = 0xFFFFFFFF;
constexpr uint8_t C2M_CLIENTQUERY		 = 0x31;
constexpr uint8_t M2C_QUERY				 = 0x4A;
constexpr int PORT						 = 27010;
constexpr int MAX_BUFFER_SIZE			 = 2048;

std::string ipaddr_to_str( uint32_t ip )
{
	uint8_t byte3 = ( ip >> 24 ) & 0xFF;
	uint8_t byte2 = ( ip >> 16 ) & 0xFF;
	uint8_t byte1 = ( ip >> 8 ) & 0xFF;
	uint8_t byte0 = ip & 0xFF;

	return std::to_string( byte0 ) + '.' + std::to_string( byte1 ) + '.' + std::to_string( byte2 ) + '.'
		   + std::to_string( byte3 );
}

uint32_t get_ip( const std::string& host )
{
	in_addr ip_addr {};

	// Attempt direct conversion if it's an IPv4 address
	if ( inet_pton( AF_INET, host.c_str(), &ip_addr ) == 1 )
	{
		return ip_addr.s_addr;
	}

	// Resolve domain using DNS
	addrinfo hints {};
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family	 = AF_INET; // IPv4 only
	addrinfo* result = nullptr;

	int status = getaddrinfo( host.c_str(), nullptr, &hints, &result );
	if ( status != 0 )
	{
		throw std::runtime_error( "DNS resolution failed: " + std::string( gai_strerror( status ) ) );
	}

	// Extract the first IPv4 address
	for ( addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next )
	{
		if ( ptr->ai_family == AF_INET )
		{
			auto sockaddr_ipv4 = reinterpret_cast< sockaddr_in* >( ptr->ai_addr );
			ip_addr			   = sockaddr_ipv4->sin_addr;
			freeaddrinfo( result );
			return ip_addr.s_addr;
		}
	}

	freeaddrinfo( result );
	throw std::runtime_error( "No IPv4 address found" );
}

struct ServerEntry
{
	uint32_t ip;
	uint16_t port;
};

class MasterServer
{
  public:
	MasterServer()
	 : sockfd_( -1 )
	{
	}

	~MasterServer()
	{
		if ( sockfd_ >= 0 )
		{
			close( sockfd_ );
		}
	}

	void AddServer( const std::string& ip_str, uint16_t port )
	{
		auto ip = get_ip( ip_str );

		std::cout << "Adding server: " << ipaddr_to_str( ip ) << ":" << port << '\n';

		servers_.push_back( { ntohl( ip ), port } );
	}

	void Run()
	{
		CreateSocket();
		BindSocket();
		ListenForRequests();
	}

  private:
	std::vector< uint8_t > server_list_payload_;
	std::vector< ServerEntry > servers_;
	int sockfd_;

	void CreateSocket()
	{
		sockfd_ = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if ( sockfd_ < 0 )
		{
			throw std::runtime_error( "Failed to create socket: " + std::string( strerror( errno ) ) );
		}

		// Enable address reuse
		int reuse = 1;
		if ( setsockopt( sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) ) < 0 )
		{
			throw std::runtime_error( "Set SO_REUSEADDR failed: " + std::string( strerror( errno ) ) );
		}
	}

	void BindSocket()
	{
		struct sockaddr_in servaddr;
		memset( &servaddr, 0, sizeof( servaddr ) );
		servaddr.sin_family		 = AF_INET;
		servaddr.sin_addr.s_addr = INADDR_ANY;
		servaddr.sin_port		 = htons( PORT );

		if ( bind( sockfd_, reinterpret_cast< sockaddr* >( &servaddr ), sizeof( servaddr ) ) < 0 )
		{
			throw std::runtime_error( "Bind failed: " + std::string( strerror( errno ) ) );
		}

		std::cout << "Master server listening on port " << PORT << std::endl;
	}

	void ListenForRequests()
	{
		struct sockaddr_in cliaddr;
		socklen_t len = sizeof( cliaddr );
		char buffer[MAX_BUFFER_SIZE];

		while ( true )
		{
			ssize_t n = recvfrom( sockfd_, buffer, MAX_BUFFER_SIZE, 0, reinterpret_cast< sockaddr* >( &cliaddr ), &len );
			if ( n < 0 )
			{
				std::cerr << "Recvfrom error: " << strerror( errno ) << std::endl;
				continue;
			}

			ProcessPacket( buffer, n, cliaddr );
		}
	}

	void ProcessPacket( char* buffer, ssize_t size, sockaddr_in& client_addr )
	{
		if ( size < 1 )
		{
			return;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop( AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN );

		// Check for client query command, ignore game id
		if ( buffer[0] == C2M_CLIENTQUERY )
		{
			char client_ip[INET_ADDRSTRLEN];
			inet_ntop( AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN );

			std::cout << "Received query from " << client_ip << ":" << ntohs( client_addr.sin_port ) << std::endl;

			SendServerList( client_addr );
		}
	}

	void BuildServerList()
	{
		// Clear existing list
		server_list_payload_.clear();

		// Add header + query command
		uint32_t header = CONNECTIONLESS_HEADER;
		server_list_payload_.insert( server_list_payload_.end(),
									 reinterpret_cast< uint8_t* >( &header ),
									 reinterpret_cast< uint8_t* >( &header ) + sizeof( header ) );
		server_list_payload_.push_back( M2C_QUERY );

		// Add servers to response
		for ( auto&& server : servers_ )
		{
			auto server_ip = reinterpret_cast< uint8_t* >( &server.ip );
			server_list_payload_.insert( server_list_payload_.end(), server_ip, server_ip + 4 );

			uint8_t port_bytes[2];
			auto server_port = server.port;
			memcpy( port_bytes, &server_port, sizeof( server_port ) );
			server_list_payload_.insert( server_list_payload_.end(), port_bytes, port_bytes + 2 );
		}

		// Add termination marker
		uint8_t terminator[6] = { 0, 0, 0, 0, 0, 0 };
		server_list_payload_.insert( server_list_payload_.end(), terminator, terminator + 6 );
	}

	void SendServerList( sockaddr_in& client_addr )
	{
		BuildServerList();

		ssize_t sent = sendto( sockfd_,
							   server_list_payload_.data(),
							   server_list_payload_.size(),
							   0,
							   reinterpret_cast< sockaddr* >( &client_addr ),
							   sizeof( client_addr ) );

		if ( sent < 0 )
		{
			std::cerr << "Sendto failed: " << strerror( errno ) << std::endl;
		}
	}
};

void LoadServersFromFile( MasterServer& server, const std::string& filePath )
{
	std::ifstream file( filePath );

	if ( !file.is_open() )
	{
		throw std::runtime_error( "Could not open server list file: " + filePath );
	}

	std::string line;
	int line_num = 0;

	while ( std::getline( file, line ) )
	{
		line_num++;

		// Skip empty lines and comments
		if ( line.empty() || line[0] == '#' )
		{
			continue;
		}

		// Remove potential carriage return characters
		if ( !line.empty() && line.back() == '\r' )
		{
			line.pop_back();
		}

		// Find colon separator
		size_t colon_pos = line.find( ':' );

		if ( colon_pos == std::string::npos )
		{
			std::cerr << "Warning [Line " << line_num << "]: Invalid format - missing colon in '" << line << "'"
					  << std::endl;
			continue;
		}

		try
		{
			// Extract IP part
			std::string ip = line.substr( 0, colon_pos );

			// Extract and convert port part
			std::string port_str = line.substr( colon_pos + 1 );
			int port			 = std::stoi( port_str );

			if ( port < 1 || port > 65535 )
			{
				throw std::out_of_range( "Port number out of range (1-65535)" );
			}

			server.AddServer( ip, static_cast< uint16_t >( port ) );
		}
		catch ( const std::exception& e )
		{
			std::cerr << "Error [Line " << line_num << "]: Processing '" << line << "' - " << e.what() << std::endl;
		}
	}
}

int main()
{
	try
	{
		MasterServer server;

		// Add servers to respond with (IP bytes and port)
		server.AddServer( "cssserv.xutaxkamay.com", 27015 );
		// Could be generated by a SQL database or something similar.
		LoadServersFromFile( server, "./server.list" );
		server.Run();
	}
	catch ( const std::exception& e )
	{
		std::cerr << "Fatal error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
