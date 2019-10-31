/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Miscellaneous code for network, sockets, etc 
*********************************************************************/

#pragma  once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

//! Checks if the given ip is in the specified ip range.
// Example:
//		isIPInRange("192.168.0.1", "192.168.0.0", "255.255.255.0") == true
//
bool isIPInRange(const std::string& ip, const std::string& network, const std::string& mask);

//! Check if the given ip a private IP address, as specified in https://en.wikipedia.org/wiki/Private_network
// Private IPV4 addresses fall in the following ranges:
// 10.0.0.0 to 10.255.255.255 , subnet mask 255.0.0.0
// 172.16.0.0 to 172.31.255.255, subnet mask 255.240.0.0
// 192.168.0.0 to 192.168.255.255, subnet mask 255.255.0.0
bool isPrivateIP(const std::string& ip);


//! Given a host name, it returns its IP
// This is not production ready. It's very basic, and certainly doesn't handle
// all the cases
// Returns the IP, or "" on error
std::string resolveHostName(const std::string hostName);

struct NetworkAdapterInfo
{
	struct Address
	{
		std::string str;
		bool isIPV6;
		union
		{
			IN6_ADDR ipv6;
			IN_ADDR ipv4;
		};
	};

	std::string name;
	std::vector<Address> unicast;
	std::vector<Address> anycast;
	std::vector<Address> multicast;
	std::vector<Address> gateways;
};

//! Returns the addresses all all available network adapters
//
// \param onlyStatusUp
//	If true, it will return only the adapters that are up (able to pass packets)
// \param includeIPV6
//	If true it will include also ipv6 addresses, instead of just upv4
//
std::vector<NetworkAdapterInfo> getAdaptersAddresses(bool onlyStatusUp, bool includeIPV6);


}

