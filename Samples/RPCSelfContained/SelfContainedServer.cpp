#include "SelfContainedPCH.h"
#include "SelfContainedServer.h"

using namespace cz;

struct ClientInfo : public rpc::ConnectionUserData
{
	ClientInfo()
	{
		printf("%s, %p\n", __FUNCTION__, this);
	}
	~ClientInfo()
	{
		printf("%s, %p\n", __FUNCTION__, this);
	}
};

namespace testserver
{

SelfContainedServer::SelfContainedServer()
{
}

SelfContainedServer::~SelfContainedServer()
{
	m_rpc.reset();
}

void SelfContainedServer::init(net::CompletionPort& iocp, int listeningPort)
{
	auto transport = std::make_unique<rpc::TCPServerTransport>(listeningPort, iocp);
	m_rpc = std::make_unique<rpc::Server<SelfContainedServer>>(*this, std::move(transport));
	
	m_rpc->setClientConnectCallback([](rpc::BaseConnection* client)
	{
		printf("Client connected: %s\n", client->getTransport()->getPropertyAsString("peer_addr"));
		client->setUserData(std::make_shared<ClientInfo>());
	});

	m_rpc->setClientDisconnectCallback([](rpc::BaseConnection* client)
	{
		printf("Client disconnected: %s\n", client->getTransport()->getPropertyAsString("peer_addr"));
	});
}

//
// RPC Calls
//
const char* SelfContainedServer::getName()
{
	return "SelfContainedServer";
}

std::vector<std::string> SelfContainedServer::getClients()
{
	std::vector<std::string> v;
	m_rpc->iterateClients([&](auto client)
	{
		v.push_back(client->getTransport()->getPropertyAsString("peer_addr"));
	});
	return v;
}

}
