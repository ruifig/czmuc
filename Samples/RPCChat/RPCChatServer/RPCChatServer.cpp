// ChatServer.cpp : Defines the entry point for the console application.
//

#include "RPCChatServerPCH.h"

// Disable deprecation warnings
#pragma warning(disable : 4996)

using namespace cz;
using namespace cz::net;
using namespace cz::rpc;

struct ClientInfo : public ConnectionUserData
{
	ClientInfo()
	{
		static uint32_t counter = 0;
		id = ++counter;
	}
	~ClientInfo()
	{
	}
	uint32_t id=0;
	Service<ChatServerInterface>::ServerConnection* client;
	std::string name;
	bool admin = false;
};

class ChatServer : public ChatServerInterface
{
public:
	ChatServer(WorkQueue& workQueue)
		: m_workQueue(workQueue)
	{
	}

	void setRPCServer(Server<ChatServerInterface,ChatClientInterface>* server)
	{
		m_server = server;
		m_server->setClientConnectCallback([this](BaseConnection* client)
		{
			onClientConnect(client);
		} );
		m_server->setClientDisconnectCallback([this](BaseConnection* client)
		{
			onClientDisconnect(client);
		} );
	}
private:

	void disconnectUser(ClientInfo* user)
	{
		m_workQueue.emplace([this, id=user->id]()
		{
			auto user = getUser(id);
			if (user)
				m_server->disconnectClient(user->client);
		});
	}

	//
	// ChatServerInterface
	//
	virtual std::string login(const std::string& user, const std::string& pass) override
	{
		auto current = getCurrent();
		printf("%s:%s\n", __FUNCTION__, user.c_str());

		if (getUser(user))
		{
			disconnectUser(current);
			return formatString("User with name %s already logged in", user.c_str());
		}

		if (pass != "pass")
		{
			disconnectUser(current);
			return "Wrong password";
		}

		current->name = user;
		if (user == "Admin")
			current->admin = true;

		sendSystemMsg(formatString("%s joined the chat", user.c_str()));
		return "OK";
	}

	virtual void sendMsg(const std::string& msg) override
	{
		auto current = getCurrent();
		printf("%s:%s:%s\n", __FUNCTION__, current->name.c_str(), msg.c_str());
		broadcastMsg(getCurrent()->name, msg);
	}
	
	virtual void kick(const std::string& user) override
	{
		auto current = getCurrent();
		printf("%s:%s:%s\n", __FUNCTION__, current->name.c_str(), user.c_str());

		if (!current->admin)
		{
			sendSystemMsg("You do not have admin rights", current);
			return;
		}

		auto info = getUser(user);
		if (!info)
		{
			sendSystemMsg(formatString("User %s not found", user.c_str()), current);
			return;
		}

		sendSystemMsg(formatString("You were kicked by %s", current->name.c_str()), info);
		sendSystemMsg(formatString("User %s was kicked", user.c_str()));
		disconnectUser(info);
	}

private:
	//
	// Other functions
	//
	ClientInfo* getCurrent()
	{
		auto current = Service<ChatServerInterface>::ServerConnection::getCurrent();
		return std::static_pointer_cast<ClientInfo>(current->getUserData()).get();
	}

	ClientInfo* getUser(const std::string& user)
	{
		auto info = m_server->findUserData<ClientInfo>([&](auto info)
		{
			return info->name == user;
		});
		return info;
	}

	ClientInfo* getUser(uint32_t id)
	{
		auto info = m_server->findUserData<ClientInfo>([&](auto info)
		{
			return info->id == id;
		});
		return info;
	}

	void onClientConnect(BaseConnection* client)
	{
		auto info = std::make_shared<ClientInfo>();
		info->client = static_cast<Service<ChatServerInterface>::ServerConnection*>(client);
		printf("%s connected.\n", info->client->getTransport()->getPropertyAsString("peer_addr"));
		client->setUserData(std::move(info));
	}
	void onClientDisconnect(BaseConnection* client)
	{
		auto info = static_cast<ClientInfo*>(client->getUserData().get());
		if (info->name != "")
		{
			printf("User %s disconnected.\n", info->name.c_str());
			sendSystemMsg(formatString("User %s disconnected", info->name.c_str()));
		}
		printf("%s disconnected.\n", info->client->getTransport()->getPropertyAsString("peer_addr"));
	}

	void broadcastMsg(const std::string& from, const std::string& msg)
	{
		BROADCAST_CALLRPC(*m_server, nullptr, onMsg, from, msg);
	}

	void sendSystemMsg(const std::string& msg, ClientInfo* recipient = nullptr)
	{
		if (recipient)
			CALLRPC(*recipient->client, onSystemMsg, msg);
		else
			BROADCAST_CALLRPC(*m_server, nullptr, onSystemMsg, msg);
	}

	Service<ChatServerInterface>::Server* m_server = nullptr;
	//std::unordered_map<ClientType*, std::unique_ptr<ClientInfo>> m_clients;
	WorkQueue& m_workQueue;
};

int main()
{
	initializeSocketsLib();
	try
	{
		WorkQueue workQueue;
		ChatServer chat(workQueue);
		CompletionPort iocp(1);

		bool workThreadFinish = false;
		auto workThread = std::thread([&workQueue, &workThreadFinish]()
		{
			while (!workThreadFinish)
			{
				std::function<void()> fn;
				workQueue.wait_and_pop(fn);
				fn();
			}
		});

		Service<ChatServerInterface>::Server server(chat, std::make_unique<TCPServerTransport>(28000, iocp, &workQueue));
		chat.setRPCServer(&server);
		printf("Chat server running...\n");
		printf("Press any key to exit\n");
		while (true)
		{
			if (getch())
					break;
		}

		workQueue.emplace([&]() { workThreadFinish = true;});
		workThread.join();
	}
	catch (std::exception& e)
	{
		printf("Exception: %s\n", e.what());
	}
	shutdownSocketsLib();
    return 0;
}
