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

TCPServerConnection::TCPServerConnection(TCPServer* owner, std::unique_ptr<TCPSocket> socket)
	: m_owner(owner)
{
	setSocket(std::move(socket));
	m_owner->m_clientsCount.increment();
}
TCPServerConnection::~TCPServerConnection()
{
	m_socket.reset();
	m_owner->m_clientsCount.decrement();
}

TCPSocket* TCPServerConnection::getSocket()
{
	return m_socket.get();
}

void TCPServerConnection::setSocket(std::unique_ptr<TCPSocket> socket)
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

TCPServer* TCPServerConnection::getOwner()
{
	return m_owner;
}

void TCPServerConnection::removeClient()
{
	m_owner->removeClient(this);
}

void TCPServerConnection::onSocketReceive(const ChunkBuffer& buf)
{
}

void TCPServerConnection::onSocketShutdown(int code, const std::string &msg)
{
	removeClient();
}

void TCPServerConnection::onSocketSendCompleted()
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
	//
	// Thread 1, with the user code, shutting down the RPC objects
	// Trys to lock TCPSocketData:	Tests_x64_Debug.exe!cz::TCPBaseSocketData::lock() Line 195	C++
	// 							Tests_x64_Debug.exe!cz::TCPSocket::~TCPSocket() Line 994	C++
	// 							Tests_x64_Debug.exe!cz::TCPServerConnection::~TCPServerConnection() Line 17	C++
	// 							Tests_x64_Debug.exe!czrpc::TCPServerChannelClientInfo::~TCPServerChannelClientInfo() Line 155	C++
	// Locks TCPServer::m_mtx:		Tests_x64_Debug.exe!cz::TCPServer::shutdown()
	// 							Tests_x64_Debug.exe!czrpc::TCPServerChannel::~TCPServerChannel() Line 252	C++
	// 							Tests_x64_Debug.exe!czrpc::Server<Calculator,void>::~Server<Calculator,void>() Line 29	C++
	// 							Tests_x64_Debug.exe!SuiteRPC::TestIgnoredFutured::RunImpl::__l2::<lambda>() Line 252	C++
	//
	// 							
	// Thread 2, servicing the ICOCP, removing a client because it disconnected
	// Trys to lock TCPServer::m_mtx:	Tests_x64_Debug.exe!cz::TCPServerConnection::removeClient() Line 20	C++
	// 								Tests_x64_Debug.exe!cz::TCPServerConnection::onSocketShutdown(cz::TCPSocket * socket, int code, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & msg) Line 26	C++
	// 								Tests_x64_Debug.exe!cz::TCPSocketData::state_Disconnected(int code, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & reason) Line 483	C++
	// 								Tests_x64_Debug.exe!cz::ReadOperation::onSuccess(unsigned int bytesTransfered) Line 652	C++
	// Locks TCPSocketData:			Tests_x64_Debug.exe!cz::CompletionPort::runImpl() Line 838	C++
	// 								Tests_x64_Debug.exe!cz::CompletionPort::run() Line 847	C++
	// 								Tests_x64_Debug.exe!cz::CompletionPort::{ctor}::__l9::<lambda>() Line 760	C++
	//
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

void TCPServer::removeClient(TCPServerConnection* client)
{
	std::unique_ptr<TCPServerConnection> info;

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


