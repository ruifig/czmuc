#include "czlibPCH.h"
#include "crazygaze/StringUtils.h"
#include "crazygaze/net/SocketAddress.h"

namespace cz
{
namespace net
{

const char* SocketAddress::toString(bool includePort) const
{
	if (includePort)
		return formatString("%d.%d.%d.%d:%d", ip.bytes.b1, ip.bytes.b2, ip.bytes.b3, ip.bytes.b4, port);
	else
		return formatString("%d.%d.%d.%d", ip.bytes.b1, ip.bytes.b2, ip.bytes.b3, ip.bytes.b4);
}

void SocketAddress::constructFrom(const sockaddr_in* sa)
{
	ip.full = sa->sin_addr.s_addr;
	port = ntohs(sa->sin_port);
}

SocketAddress::SocketAddress(const sockaddr& addr)
{
	CZ_ASSERT(addr.sa_family == AF_INET);
	constructFrom((const sockaddr_in*)&addr);
}

SocketAddress::SocketAddress(const sockaddr_in& addr)
{
	CZ_ASSERT(addr.sin_family == AF_INET);
	constructFrom(&addr);
}

SocketAddress::SocketAddress(const char* ip_, int port_)
{
	inet_pton(AF_INET, ip_, &ip.full);
	port = port_;
}

SocketAddress::SocketAddress(const std::string& ipAndPort)
{
	constructFrom(ipAndPort.c_str());
}

SocketAddress::SocketAddress(const char* ipAndPort)
{
	constructFrom(ipAndPort);
}

void SocketAddress::constructFrom(const char* ipAndPort)
{
	auto ptr = ipAndPort;
	char* ip_ = getTemporaryString();
	while(*ptr)
	{
		if (*ptr == ':' || *ptr == '|')
		{
			ip_[ptr-ipAndPort] = 0;
			inet_pton(AF_INET, ip_, &ip.full);
			port = std::atoi(ptr+1);
			return;
		}
		ip_[ptr - ipAndPort] = *ptr++;
	}

	throw std::runtime_error("String is not a valid IP:PORT");
}

SocketAddress::SocketAddress(const std::string& ip_, int port_)
{
	inet_pton(AF_INET, ip_.c_str(), &ip.full);
	port = port_;
}


} // net
} // cz

