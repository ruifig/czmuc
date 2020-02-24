
#include "czmucPCH.h"
#include "crazygaze/muc/NetworkUtils.h"
#include "crazygaze/muc/StringUtils.h"
#include "crazygaze/muc/Logging.h"
#include "crazygaze/muc/ScopeGuard.h"
#include <iphlpapi.h>

namespace cz
{

namespace 
{

	std::optional<uint32_t> ipToUint(const std::string& ip)
	{
		unsigned int a,b,c,d;
		if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) !=4)
			return std::nullopt;

		if ((a > 255) || (b > 255) || (c > 255) || (d > 255))
			return std::nullopt;
		else
			return (a << 24) | (b << 16) | (c << 8) | d;
	}

}


bool isIPInRange(const std::string& ip, const std::string& network, const std::string& mask)
{
	std::optional<uint32_t> ip_addr = ipToUint(ip);
	std::optional<uint32_t> network_addr = ipToUint(network);
	std::optional<uint32_t> mask_addr = ipToUint(mask);

	// Check if all input parameters are valid ip addresses
	if (!ip_addr.has_value() || !network_addr.has_value() || !mask_addr.has_value())
		return false;

	uint32_t net_lower = network_addr.value() & mask_addr.value();
	uint32_t net_upper = net_lower | (~mask_addr.value());
	if (ip_addr.value() >= net_lower && ip_addr.value() <= net_upper)
		return true;
	else
		return false;
}

bool isPrivateIP(const std::string& ip)
{
	return
		isIPInRange(ip, "10.0.0.0", "255.0.0.0") ||
		isIPInRange(ip, "172.16.0.0", "255.240.0.0") ||
		isIPInRange(ip, "192.168.0.0", "255.255.0.0");
}

std::string resolveHostName(const std::string hostName)
{
	CZ_LOG(logDefault, Log, "Resolving host %s... ", hostName.c_str());
	struct WSAInstance
	{
		WSAInstance()
		{
			WSADATA wsaData;
			int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
			if (iResult != 0)
			{
				CZ_LOG(logDefault, Fatal, "Could not initialize WinSock: %d", iResult);
			}
		}
		~WSAInstance()
		{
			WSACleanup();
		}
	};

	WSAInstance wsa;

	struct hostent* remoteHost = gethostbyname(hostName.c_str());
	if (remoteHost == NULL)
	{
		DWORD dwError = WSAGetLastError();
		if (dwError != 0)
		{
			if (dwError == WSAHOST_NOT_FOUND)
			{
				CZ_LOG(logDefault, Error, "Host %s not found", hostName.c_str());
				return "";
			}
			else if (dwError == WSANO_DATA)
			{
				CZ_LOG(logDefault, Error, "No data record found");
				return "";
			}
			else
			{
				CZ_LOG(logDefault, Error, "Function failed with error: %ld", dwError);
				return "";
			}
		}
	}

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = *(u_long*)remoteHost->h_addr_list[0];
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(addr.sin_addr), str, INET_ADDRSTRLEN);
	CZ_LOG(logDefault, Log, "Resolved to %s", str);
	return str;
}

// Enable disable full logging for getAdaptersAddress
#if 0
	#define getAdaptersAddressesLog printf
#else
	#define getAdaptersAddressesLog(...) ((void)0)
#endif

namespace 
{
template<typename T>
static std::vector<NetworkAdapterInfo::Address> walkAddresses(T pFirstAddr, const char* addrType, bool includeIPV6)
{
	// To silence the compiler warning about unused parameter when logging is disabled
	addrType = addrType;

	std::vector<NetworkAdapterInfo::Address> res;
	char buff[100];
	DWORD bufflen = 100;

	std::string log;

	auto pAddr = pFirstAddr;
	if (pAddr != NULL)
	{
		for (int i = 0; pAddr != NULL; i++)
		{
			NetworkAdapterInfo::Address addr;
			if (pAddr->Address.lpSockaddr->sa_family == AF_INET)
			{
				sockaddr_in* sa_in = (sockaddr_in*)pAddr->Address.lpSockaddr;
				addr.isIPV6 = false;
				addr.ipv4 = sa_in->sin_addr;
				addr.str = inet_ntop(AF_INET, &(sa_in->sin_addr), buff, bufflen);
				res.push_back(addr);
				log += formatString("\t\tIPV4:%s\n", addr.str.c_str());
			}
			else if (pAddr->Address.lpSockaddr->sa_family == AF_INET6)
			{
				sockaddr_in6* sa_in6 = (sockaddr_in6*)pAddr->Address.lpSockaddr;
				if (includeIPV6)
				{
					addr.isIPV6 = true;
					addr.ipv6 = sa_in6->sin6_addr;
					addr.str = inet_ntop(AF_INET6, &(sa_in6->sin6_addr), buff, bufflen);
					res.push_back(addr);
				}
				log += formatString("\t\tIPV6:%s\n", addr.str.c_str());
			}
			else
			{
				log += "\t\tUNSPEC";
			}
			pAddr = pAddr->Next;
		}
	}

	getAdaptersAddressesLog("\tNumber of %s Addresses: %d\n", addrType, (int)res.size());
	if (log.size())
		getAdaptersAddressesLog(log.c_str());

	return res;
}

}

std::vector<NetworkAdapterInfo> getAdaptersAddresses(bool onlyStatusUp, bool includeIPV6)
{
	std::vector<NetworkAdapterInfo> res;

	// Declare and initialize variables
	DWORD dwRetVal = 0;

	unsigned int i = 0;

	// Set the flags to pass to GetAdaptersAddresses
	ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
	flags |= GAA_FLAG_INCLUDE_GATEWAYS;

	// default to unspecified address family (both)
	ULONG family = AF_UNSPEC;

	PIP_ADAPTER_ADDRESSES pAddresses = NULL;
	ULONG outBufLen = 0;

	PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
	IP_ADAPTER_PREFIX* pPrefix = NULL;

	getAdaptersAddressesLog("Calling GetAdaptersAddresses function with family = ");

	// First, check how much memory we need to allocate
	dwRetVal = GetAdaptersAddresses(family, flags, NULL, NULL, &outBufLen);
	CZ_ASSERT(dwRetVal == ERROR_BUFFER_OVERFLOW); // This error is expected when passing NULL insteadl of pAddresses.

	pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, outBufLen);

	if (pAddresses == NULL) {
		CZ_LOG(logDefault, Fatal, "Memory allocation failed for IP_ADAPTER_ADDRESSES struct");
		return {};
	}
	SCOPE_EXIT{ HeapFree(GetProcessHeap(), 0, pAddresses); };

	dwRetVal = GetAdaptersAddresses(family, flags, NULL, pAddresses, &outBufLen);
	if (dwRetVal != NO_ERROR)
	{
		CZ_LOG(logDefault, Error, cz::getWin32Error(dwRetVal, "GetAdaptersAddresses").c_str());
		return {};
	}


	pCurrAddresses = pAddresses;
	while (pCurrAddresses)
	{
		NetworkAdapterInfo adapter;

		getAdaptersAddressesLog("\tLength of the IP_ADAPTER_ADDRESS struct: %ld\n", pCurrAddresses->Length);
		getAdaptersAddressesLog("\tIfIndex (IPv4 interface): %u\n", pCurrAddresses->IfIndex);
		getAdaptersAddressesLog("\tAdapter name: %s\n", pCurrAddresses->AdapterName);

		adapter.name = cz::narrow(pCurrAddresses->FriendlyName);
		adapter.unicast = walkAddresses(pCurrAddresses->FirstUnicastAddress, "Unicast", includeIPV6);
		adapter.anycast = walkAddresses(pCurrAddresses->FirstAnycastAddress, "Anycast", includeIPV6);
		adapter.multicast = walkAddresses(pCurrAddresses->FirstMulticastAddress, "Multicast", includeIPV6);
		adapter.gateways = walkAddresses(pCurrAddresses->FirstGatewayAddress, "Default Gateway", includeIPV6);

		auto unused = walkAddresses(pCurrAddresses->FirstDnsServerAddress, "DNS Server", includeIPV6);

		getAdaptersAddressesLog("\tDNS Suffix: %wS\n", pCurrAddresses->DnsSuffix);
		getAdaptersAddressesLog("\tDescription: %wS\n", pCurrAddresses->Description);
		getAdaptersAddressesLog("\tFriendly name: %wS\n", pCurrAddresses->FriendlyName);

		if (pCurrAddresses->PhysicalAddressLength != 0)
		{
			getAdaptersAddressesLog("\tPhysical address: ");
			for (i = 0; i < (int)pCurrAddresses->PhysicalAddressLength; i++)
			{
				if (i == (pCurrAddresses->PhysicalAddressLength - 1))
					getAdaptersAddressesLog("%.2X\n", (int)pCurrAddresses->PhysicalAddress[i]);
				else
					getAdaptersAddressesLog("%.2X-", (int)pCurrAddresses->PhysicalAddress[i]);
			}
		}
		getAdaptersAddressesLog("\tFlags: %ld\n", pCurrAddresses->Flags);
		getAdaptersAddressesLog("\tMtu: %lu\n", pCurrAddresses->Mtu);
		getAdaptersAddressesLog("\tIfType: %ld\n", pCurrAddresses->IfType);
		getAdaptersAddressesLog("\tOperStatus: %ld\n", pCurrAddresses->OperStatus);
		getAdaptersAddressesLog("\tIpv6IfIndex (IPv6 interface): %u\n", pCurrAddresses->Ipv6IfIndex);
		getAdaptersAddressesLog("\tZoneIndices (hex): ");
		for (i = 0; i < 16; i++) getAdaptersAddressesLog("%lx ", pCurrAddresses->ZoneIndices[i]);
		getAdaptersAddressesLog("\n");

		pPrefix = pCurrAddresses->FirstPrefix;
		if (pPrefix)
		{
			for (i = 0; pPrefix != NULL; i++) pPrefix = pPrefix->Next;
			getAdaptersAddressesLog("\tNumber of IP Adapter Prefix entries: %d\n", i);
		}
		else
			getAdaptersAddressesLog("\tNumber of IP Adapter Prefix entries: 0\n");

		getAdaptersAddressesLog("\n");

		if (pCurrAddresses->OperStatus==IfOperStatusUp || onlyStatusUp==false)
			res.push_back(adapter);

		pCurrAddresses = pCurrAddresses->Next;
	}

	return res;
}


}
