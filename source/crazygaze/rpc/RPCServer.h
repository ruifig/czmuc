/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCBase.h"
#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/rpc/RPCConnection.h"

namespace cz
{
namespace rpc
{

// Forward declarations
class Transport;

class BaseServer
{
public:
	explicit BaseServer(std::unique_ptr<ServerTransport> serverTransport)
		: m_serverTransport(std::move(serverTransport))
	{
		m_serverTransport->setOnNewClient([this](std::unique_ptr<Transport> ch)
		{
			onNewClient(std::move(ch));
		});

		m_serverTransport->setOnClientDisconnect([this](Transport* ch)
		{
			onClientDisconnect(ch);
		});
	}
	virtual ~BaseServer()
	{
		// First destroy this, so it does any necessary blocks to finish any callbacks we might have running on other threads
		m_serverTransport.reset();
	}

	void setClientConnectCallback(std::function<void(BaseConnection*)> func)
	{
		m_clientConnectCallback = std::move(func);
	}

	void setClientDisconnectCallback(std::function<void(BaseConnection*)> func)
	{
		m_clientDisconnectCallback = std::move(func);
	}

	void disconnectClient(BaseConnection* client)
	{
		// Only lock while we change our data, and call the callbacks outside the locking
		std::unique_ptr<BaseConnection> ptr;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			auto it = m_clients.find(client->getTransport());

			if (it == m_clients.end()) // #TODO: Once shutdown cleanup is refactored, this should not be needed, since we should not get this call if a client doesn't exist anymore?
				return;

			CZ_ASSERT(it != m_clients.end());
			ptr = std::move(it->second);
			m_clients.erase(client->getTransport());
		}

		// Call now, outside the lock
		m_clientDisconnectCallback(ptr.get());
	}


	template<typename F>
	void iterateClients(F f)
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto&& c : m_clients)
			f(c.second.get());
	}

	template<typename T, typename F>
	T* findUserData(F f)
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto&& c : m_clients)
		{
			auto userData = static_cast<T*>(c.second->getUserData().get());
			if (f(userData))
				return userData;
		}
		return nullptr;
	}
protected:
	typedef std::unordered_map<const Transport*, std::unique_ptr<BaseConnection>> ClientMap;
	std::unique_ptr<ServerTransport> m_serverTransport;
	std::mutex m_mtx;
	ClientMap m_clients;
	std::function<void(BaseConnection*)> m_clientConnectCallback = [](BaseConnection*) {} ;
	std::function<void(BaseConnection*)> m_clientDisconnectCallback = [](BaseConnection*) {};

	void onNewClient(std::unique_ptr<Transport> transport)
	{
		auto chPtr = transport.get(); // keep a raw pointer, because the std::move(transport) will set it to empty
		auto client = createClient(std::move(transport));
		auto clientPtr = client.get();

		// Only lock for the time we need
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_clients[chPtr] = std::move(client);
			m_clientConnectCallback(clientPtr);
		}
	}

	void onClientDisconnect(Transport* transport)
	{
		std::unique_ptr<BaseConnection> ptr;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			auto it = m_clients.find(transport);

			// #TODO : Once the shutdown is simplified, this should not be required, since we should not get this call
			// for non-existing transports
			if (it == m_clients.end())
				return;

			CZ_ASSERT(it != m_clients.end());
			ptr = std::move(it->second); // Move out the pointer, so we only delete the object after releasing the lock
			m_clients.erase(it);
		}

		m_clientDisconnectCallback(ptr.get());
	}

	virtual std::unique_ptr<BaseConnection> createClient(std::unique_ptr<Transport> transport) = 0;
};

template<typename LocalType_, typename RemoteType_=void>
class Server : public BaseServer
{
public:
	using LocalType = LocalType_;
	using RemoteType = RemoteType_;
	using ClientType = Connection<RemoteType, LocalType>;

	Server(LocalType& obj, std::unique_ptr<ServerTransport> serverTransport)
		: BaseServer(std::move(serverTransport))
		, m_obj(obj)
	{
	}

	virtual ~Server()
	{
	}

	static ClientType* castClient(BaseConnection* client)
	{
		return static_cast<ClientType*>(client);
	}

protected:

	virtual std::unique_ptr<BaseConnection> createClient(std::unique_ptr<Transport> transport) override
	{
		return std::make_unique<ClientType>(std::move(transport), m_obj);
	}

	LocalType& m_obj;
};

#define BROADCAST_CALLRPC(server, excludedClient, func, ...)                                \
	{                                                                                       \
		auto excluded = excludedClient;                                                     \
		(server).iterateClients([&](auto client) {                                          \
			if (client == excluded)                                                         \
				return;                                                                     \
			auto cl = static_cast<std::decay<decltype(server)>::type::ClientType*>(client); \
			CALLRPC(*cl, func, __VA_ARGS__);                                                \
		});                                                                                 \
	}

} // namespace rpc
} // namespace cz

