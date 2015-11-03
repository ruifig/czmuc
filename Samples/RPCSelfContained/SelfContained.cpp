//
// Sample of for an RPC class that is fully contained (it contains and owns it's own RPC classes)
//

#include "SelfContainedPCH.h"
#include "SelfContainedServer.h"

using namespace cz;

void work()
{
	net::CompletionPort iocp(1);
	using namespace testserver;
	SelfContainedServer server;
	server.init(iocp, 28000);

	rpc::Connection<SelfContainedServer> client1(std::make_unique<rpc::TCPTransport>("127.0.0.1", 28000, iocp));
	// Another client, so we can get a vector of the clients, just to test
	rpc::Connection<SelfContainedServer> client2(std::make_unique<rpc::TCPTransport>("127.0.0.1", 28000, iocp));

	// Test RPC
	{
		auto res = CALLRPC(client1, getName);
		printf("res=%s\n", res.get().c_str());
	}

	// Test RPC
	{
		auto res = CALLRPC(client1, getClients);
		printf("{");
		for (auto&& i : res.get())
			printf("%s,", i.c_str());
		printf("}\n");
	}
}

int main()
{
	net::initializeSocketsLib();
	try
	{
		work();
	}
	catch(std::exception& e)
	{
		printf("%s\n", e.what());
	}
	net::shutdownSocketsLib();

    return 0;
}

