#pragma once

#include "crazygaze/czlib.h"

namespace cz
{
namespace net
{

struct SocketAddress
{
	struct IP
	{
		union
		{
			struct bytes_st
			{
				uint8_t b1, b2, b3, b4;
			} bytes;
			uint32_t full = 0;
		};
	} ip;
	int port = 0;

	const char* toString(bool includePort) const;
	SocketAddress() {}
	explicit SocketAddress(const sockaddr& addr);
	explicit SocketAddress(const sockaddr_in& addr);
	explicit SocketAddress(const char* ip, int port);
	explicit SocketAddress(const std::string& ipAndPort);
	explicit SocketAddress(const char* ipAndPort);
	explicit SocketAddress(const std::string& ip, int port);

	bool operator==( const SocketAddress& right ) const
	{
		return (ip.full == right.ip.full) && (port == right.port);
	}
	bool operator!=( const SocketAddress& right ) const
	{
		return (*this == right) == false;
	}
	bool operator > ( const SocketAddress& right ) const
	{
		if (port == right.port)
			return ip.full > right.ip.full;
		else
			return port > right.port;
	}
	bool operator < ( const SocketAddress& right ) const
	{
		if (port == right.port)
			return ip.full < right.ip.full;
		else
			return port < right.port;
	}

	bool isValid() const
	{
		return !(port == 0 && ip.full == 0);
	}

  private:
	void constructFrom(const sockaddr_in* sa);
	void constructFrom(const char* ipAndPort);
};

} // net
} // cz

