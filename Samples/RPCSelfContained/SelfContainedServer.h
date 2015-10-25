//
// Self contained RPC server
//
// The idea here is that instead of having the user specify the channel, the class itself
// knows it's being used for RPCs, and knows about its clients, etc
//

#pragma once

// Putting everything inside a namespace to test if it works
namespace testserver 
{
	using namespace cz;

	class SelfContainedServer
	{
	public:
		SelfContainedServer();
		virtual ~SelfContainedServer();
		void init(net::CompletionPort& iocp, int listeningPort);
	public:
		const char* getName();
		std::vector<std::string> getClients();
		typedef rpc::Client<void, SelfContainedServer> ClientType;
		std::unique_ptr<class rpc::Server<SelfContainedServer>> m_rpc;
	};
}


#define RPCTABLE_CLASS testserver::SelfContainedServer
#define RPCTABLE_CONTENTS \
		REGISTERRPC(getName) \
		REGISTERRPC(getClients)
#include "crazygaze/rpc/RPCGenerate.h"
