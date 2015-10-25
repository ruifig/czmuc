/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCBase.h"
#include "crazygaze/rpc/RPCChannel.h"
#include "crazygaze/rpc/RPCClient.h"

namespace cz
{
namespace rpc
{

// Forward declarations
class Channel;

class BaseServer
{
public:
	explicit BaseServer(std::unique_ptr<ServerChannel> serverChannel)
		: m_serverChannel(std::move(serverChannel))
	{
		m_serverChannel->setOnNewClient([this](std::unique_ptr<Channel> ch)
		{
			onNewClient(std::move(ch));
		});

		m_serverChannel->setOnClientDisconnect([this](Channel* ch)
		{
			onClientDisconnect(ch);
		});
	}
	virtual ~BaseServer()
	{
		// First destroy this, so it does any necessary blocks to finish any callbacks we might have running on other threads
		m_serverChannel.reset();
	}

	void setClientConnectCallback(std::function<void(BaseClient*)> func)
	{
		m_clientConnectCallback = std::move(func);
	}

	void setClientDisconnectCallback(std::function<void(BaseClient*)> func)
	{
		m_clientDisconnectCallback = std::move(func);
	}

	void disconnectClient(BaseClient* client)
	{
		// Only lock while we change our data, and call the callbacks outside the locking
		std::unique_ptr<BaseClient> ptr;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			auto it = m_clients.find(client->getChannel());

			if (it == m_clients.end()) // #TODO: Once shutdown cleanup is refactored, this should not be needed, since we should not get this call if a client doesn't exist anymore?
				return;

			CZ_ASSERT(it != m_clients.end());
			ptr = std::move(it->second);
			m_clients.erase(client->getChannel());
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
	typedef std::unordered_map<const Channel*, std::unique_ptr<BaseClient>> ClientMap;
	std::unique_ptr<ServerChannel> m_serverChannel;
	std::mutex m_mtx;
	ClientMap m_clients;
	std::function<void(BaseClient*)> m_clientConnectCallback = [](BaseClient*) {} ;
	std::function<void(BaseClient*)> m_clientDisconnectCallback = [](BaseClient*) {};

	void onNewClient(std::unique_ptr<Channel> channel)
	{
		auto chPtr = channel.get(); // keep a raw pointer, because the std::move(channel) will set it to empty
		auto client = createClient(std::move(channel));
		auto clientPtr = client.get();

		// Only lock for the time we need
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_clients[chPtr] = std::move(client);
			m_clientConnectCallback(clientPtr);
		}
	}

	void onClientDisconnect(Channel* channel)
	{
		std::unique_ptr<BaseClient> ptr;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			auto it = m_clients.find(channel);

			if (it == m_clients.end()) // #TODO : Once the shutdown is simplified, this should not be required, since we should not get this call for non-existing channels
				return;

			CZ_ASSERT(it != m_clients.end());
			ptr = std::move(it->second); // Move out the pointer, so we only delete the object after releasing the lock
			m_clients.erase(it);
		}

		m_clientDisconnectCallback(ptr.get());
	}

	virtual std::unique_ptr<BaseClient> createClient(std::unique_ptr<Channel> channel) = 0;
};

template<typename LocalType_, typename RemoteType_=void>
class Server : public BaseServer
{
public:
	typedef LocalType_ LocalType;
	typedef RemoteType_ RemoteType;
	typedef Client<RemoteType, LocalType> ClientType;

	Server(LocalType& obj, std::unique_ptr<ServerChannel> serverChannel)
		: BaseServer(std::move(serverChannel))
		, m_obj(obj)
	{
	}

	virtual ~Server()
	{
	}

	static ClientType* castClient(BaseClient* client)
	{
		return static_cast<ClientType*>(client);
	}

protected:

	virtual std::unique_ptr<BaseClient> createClient(std::unique_ptr<Channel> channel) override
	{
		return std::make_unique<ClientType>(std::move(channel), m_obj);
	}

	LocalType& m_obj;
};

} // namespace rpc
} // namespace cz

