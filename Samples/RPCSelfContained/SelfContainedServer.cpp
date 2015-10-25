#include "SelfContainedPCH.h"
#include "SelfContainedServer.h"

using namespace cz;

struct ClientInfo : public rpc::ClientUserData
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
	auto channel = std::make_unique<rpc::TCPServerChannel>(listeningPort, iocp);
	m_rpc = std::make_unique<rpc::Server<SelfContainedServer>>(*this, std::move(channel));
	
	m_rpc->setClientConnectCallback([](rpc::BaseClient* client)
	{
		printf("Client connected: %s\n", client->getChannel()->getCustomID().c_str());
		client->setUserData(std::make_shared<ClientInfo>());
	});

	m_rpc->setClientDisconnectCallback([](rpc::BaseClient* client)
	{
		printf("Client connected: %s\n", client->getChannel()->getCustomID().c_str());
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
		v.push_back(client->getChannel()->getCustomID());
	});
	return v;
}

}
