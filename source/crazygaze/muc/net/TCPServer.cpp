#include "czlibPCH.h"
#include "crazygaze/net/TCPServer.h"
#include "crazygaze/net/details/TCPSocketDebug.h"

#ifndef NDEBUG
	//#define LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
	#define LOG(...) ((void)0)
#else
	#define LOG(...) ((void)0)
#endif

#define LOGENTRY() LOG("%s: %p: Enter, thread %p\n", __FUNCTION__, this, std::this_thread::get_id())
#define LOGEXIT()  LOG("%s: %p: Exit , thread %p\n", __FUNCTION__, this, std::this_thread::get_id())

namespace cz
{
namespace net
{

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

TCPServerClientInfo::TCPServerClientInfo(TCPServer* owner, std::unique_ptr<TCPSocket> socket)
	: m_owner(owner)
{
	setSocket(std::move(socket));
	m_owner->m_clientsCount.increment();
}
TCPServerClientInfo::~TCPServerClientInfo()
{
	m_socket->shutdown();
	m_socket.reset();
	m_owner->m_clientsCount.decrement();
}

TCPSocket* TCPServerClientInfo::getSocket()
{
	return m_socket.get();
}

void TCPServerClientInfo::setSocket(std::unique_ptr<TCPSocket> socket)
{
	m_socket = std::move(socket);

	// Set callbacks
	m_socket->setOnReceive([this](const ChunkBuffer& buf)
	{
		onSocketReceive(buf);
	});
	m_socket->setOnShutdown([this](int code, const std::string& msg)
	{
		onSocketShutdown(code, msg);
	});
	m_socket->setOnSendCompleted([this]()
	{
		onSocketSendCompleted();
	});
}

TCPServer* TCPServerClientInfo::getOwner()
{
	return m_owner;
}

void TCPServerClientInfo::removeClient()
{
	m_owner->removeClient(this);
}

void TCPServerClientInfo::onSocketReceive(const ChunkBuffer& buf)
{
}

void TCPServerClientInfo::onSocketShutdown(int code, const std::string &msg)
{
	removeClient();
}

void TCPServerClientInfo::onSocketSendCompleted()
{
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

TCPServer::TCPServer(
		int serverPort, int numIOThreads, TCPServerClientInfoFactory clientInfoFactory,
		uint32_t numPendingReads, uint32_t pendingReadSize)
	: m_clientInfoFactory(std::move(clientInfoFactory))
{
	m_owniocp = std::make_unique<CompletionPort>(numIOThreads);
	m_iocp = m_owniocp.get();
	init(serverPort, numPendingReads, pendingReadSize);
}

TCPServer::TCPServer(
		int serverPort, CompletionPort& iocp, TCPServerClientInfoFactory clientInfoFactory,
		uint32_t numPendingReads, uint32_t pendingReadSize)
	: m_iocp(&iocp), m_clientInfoFactory(clientInfoFactory)
{
	init(serverPort, numPendingReads, pendingReadSize);
}

void TCPServer::init(int serverPort, uint32_t numPendingReads, uint32_t pendingReadSize)
{
	m_listenSocket = std::make_unique<TCPServerSocket>(
		*m_iocp, serverPort,
		[this](std::unique_ptr<TCPSocket> socket)
		{
			onAccept(std::move(socket));
		},
		numPendingReads, pendingReadSize);
}

void TCPServer::onAccept(std::unique_ptr<TCPSocket> socket)
{
	auto client = m_clientInfoFactory(this, std::move(socket));
	client->m_owner = this;
	//client->setSocket(std::move(socket));

	// Only lock when we need to change our data
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_clients[client.get()] = std::move(client);
	}
}

TCPServer::~TCPServer()
{
	LOGENTRY();

	if (!m_listenSocket)
		return;

	// Destroy the listen socket first, so we don't accept any new clients
	m_listenSocket.reset();

	// Wait for all clients to disconnect
	m_clientsCount.wait();
	CZ_ASSERT(m_clients.size() == 0);

	LOGEXIT();
}

void TCPServer::shutdown()
{
	m_listenSocket.reset();

	// Moving the clients list to temporary list, so we don't end up calling the client
	// destructors while holding the lock, as it can cause deadlock.
	// The situation I got at some point was, due the inverse order threads end up locking mutexes:
	decltype(m_clients) tmp;
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		tmp = std::move(m_clients);
	}

	// Call removeClient on each client, since its a virtual, and might do shutdown work
	for(auto&& c : tmp)
		c.second->removeClient();
	tmp.clear();
}

int TCPServer::getNumClients()
{
	std::unique_lock<std::mutex> lk(m_mtx);
	return static_cast<int>(m_clients.size());
}

void TCPServer::removeClient(TCPServerClientInfo* client)
{
	std::unique_ptr<TCPServerClientInfo> info;

	// Only lock while we remove it from the map.
	// We don't delete the clientinfo object while holding the lock, since calling unknown code while holding locks is
	// prone to creating deadlocks
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		auto it = m_clients.find(client);
		if (it == std::end(m_clients))
			return;
		// Move out, so we release the lock, then delete the unique_ptr
		info = std::move(it->second);
		m_clients.erase(it);
	}
}

} // namespace net
} // namespace cz


