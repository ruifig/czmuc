/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/rpc/TCPTransport.h"

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
namespace rpc
{

static void processRPCBuffer_InPlace(Transport* transport, const ChunkBuffer& buf)
{
	while (true)
	{
		//Sleep(rand() % 10); // #TODO Remove this
		auto size = Transport::hasFullRPC(buf);
		if (size == 0)
			return; // Not enough data for an RPC yet
		transport->onReceivedData(buf);
	}
}

// #TODO : Remove all the unnecessary parameters that I was using for debugging
static void processRPCBuffer(Transport* transport, const ChunkBuffer& buf, WorkQueue* rcvQueue,
							 cz::ZeroSemaphore* queuedOps)
{
	if (rcvQueue) // Put the required action in a queue, if the queue was provided
	{
		auto tmp = std::make_shared<ChunkBuffer>();
		while (true)
		{
			auto size = Transport::hasFullRPC(buf);
			if (size == 0)
				break;

			auto ptr = std::unique_ptr<char[]>(new char[sizeof(size) + size]);
			memcpy(ptr.get(), &size, sizeof(size));
			buf.read(ptr.get() + sizeof(size), size);
			tmp->writeBlock(std::move(ptr), sizeof(size)+size, sizeof(size)+size);
		}

		if (tmp->numBlocks())
		{
			// #TODO : Remove all the unnecessary parameters I was using for debugging
			rcvQueue->emplace([transport, buf = std::move(tmp), queuedOps]() 
			{
				//Sleep(rand() % 10); // #TODO : Remove this
				processRPCBuffer_InPlace(transport, *buf.get());
				queuedOps->decrement();
			});
		}
	}
	else // Queue not provided, so execute right now
	{
		processRPCBuffer_InPlace(transport, buf);
	}
}

//////////////////////////////////////////////////////////////////////////
//
// TCPTransport
//
//////////////////////////////////////////////////////////////////////////

TCPTransport::TCPTransport(const char* ip, int port, net::CompletionPort& iocp, WorkQueue* rcvQueue)
{
	init(ip, port, iocp, rcvQueue);
}

TCPTransport::TCPTransport(const net::SocketAddress& address, net::CompletionPort& iocp,
					   WorkQueue* rcvQueue)
{
	init(address.toString(false), address.port, iocp, rcvQueue);
}

TCPTransport::~TCPTransport()
{
	LOGENTRY();
	m_socket.reset();
	m_queuedOps.wait();
	LOGEXIT();
}

void TCPTransport::init(const char* ip, int port, net::CompletionPort& iocp, WorkQueue* rcvQueue /*= nullptr*/)
{
	LOGENTRY();
	m_rcvQueue = rcvQueue;
	m_socket = std::make_unique<net::TCPSocket>(iocp);
	m_socket->setOnReceive([this](const ChunkBuffer& buf)
	{
		onSocketReceive(buf);
	});
	m_socket->setOnShutdown([this](int code, const std::string& msg)
	{
		onSocketShutdown(code, msg);
	});

	auto res = m_socket->connect(ip, port);

	// #TODO If connect failed, handle it properly
	if (!res.get())
		throw std::runtime_error("Could not connect to server socket");
	m_customID = m_socket->getRemoteAddress().toString(true);
	LOGEXIT();
}

void TCPTransport::onSocketReceive(const ChunkBuffer& buf)
{
	if (m_rcvQueue)
		m_queuedOps.increment();
	// #TODO : Remove all the unnecessary parameters I was using for debugging
	processRPCBuffer(this, buf, m_rcvQueue, &m_queuedOps);
}

void TCPTransport::onSocketShutdown(int code, const std::string& msg)
{
	onDisconnected();
}

ChunkBuffer TCPTransport::prepareSend()
{
	ChunkBuffer out;
	auto pos = out.writeReserve(sizeof(uint32_t));
	return std::move(out);
}

bool TCPTransport::send(ChunkBuffer&& data)
{
	ChunkBuffer::WritePos pos;
	pos.block = 0;
	pos.write = 0;
	uint32_t size = data.calcSize();
	data.writeAt(pos, static_cast<uint32_t>(size - sizeof(size)));
	return m_socket->send(std::move(data));
}

//////////////////////////////////////////////////////////////////////////
//
// TCPServerConnection
//
//////////////////////////////////////////////////////////////////////////

class TCPServerConnection : public net::TCPServerClientInfo
{
public:
  TCPServerConnection(net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket, class TCPServerTransport* transportOwner,
							 WorkQueue* rcvQueue = nullptr)
	  : net::TCPServerClientInfo(owner, std::move(socket)), m_transportOwner(transportOwner), m_rcvQueue(rcvQueue)
	{
		LOGENTRY();
		LOGEXIT();
	}

	virtual ~TCPServerConnection()
	{
		LOGENTRY();
		waitToFinish();
		LOGEXIT();
	}

protected:
	virtual void removeClient() override
	{
		LOGENTRY();
		waitToFinish();
		m_transportOwner->onClientRemoved(this);
		//cz::TCPServerConnection::removeClient();
		LOGEXIT();
	}

	void waitToFinish()
	{
		m_socket->resetCallbacks();
		m_queuedOps.wait();
	}
	
	virtual void onSocketReceive(const ChunkBuffer& buf) override
	{
		net::TCPServerClientInfo::onSocketReceive(buf); // Call base class

		if (m_rcvQueue)
			m_queuedOps.increment();
		// #TODO : Remove all the unnecessary parameters I was using for debugging
		processRPCBuffer(m_transport, buf, m_rcvQueue, &m_queuedOps);
	}

	friend class TCPServerTransport;
	Transport* m_transport;
	class TCPServerTransport* m_transportOwner;
	WorkQueue* m_rcvQueue;
	cz::ZeroSemaphore m_queuedOps;
};


//////////////////////////////////////////////////////////////////////////
//
// TCPServerSideTransport
//
//////////////////////////////////////////////////////////////////////////

class TCPServerSideTransport : public Transport
{
public:
	TCPServerSideTransport(TCPServerConnection* clientInfo)
		: m_clientInfo(clientInfo)
	{
		LOGENTRY();
		setProperty("peer_ip", m_clientInfo->getSocket()->getRemoteAddress().toString(false));
		setProperty("peer_port", m_clientInfo->getSocket()->getRemoteAddress().port);
		setProperty("peer_addr", m_clientInfo->getSocket()->getRemoteAddress().toString(true));
		LOGEXIT();
	}
	virtual ~TCPServerSideTransport()
	{
		LOGENTRY();
		m_clientInfo->getOwner()->removeClient(m_clientInfo);
		LOGEXIT();
	}

protected:
	virtual ChunkBuffer prepareSend() override
	{
		ChunkBuffer out;
		auto pos = out.writeReserve(sizeof(uint32_t));
		return std::move(out);
	}
	virtual bool send(ChunkBuffer&& data) override
	{
		ChunkBuffer::WritePos pos;
		pos.block = 0;
		pos.write = 0;
		uint32_t size = data.calcSize();
		data.writeAt(pos, static_cast<uint32_t>(size - sizeof(size)));
		return m_clientInfo->getSocket()->send(std::move(data));
	}

	TCPServerConnection* m_clientInfo;
};

//////////////////////////////////////////////////////////////////////////
//
// TCPServerTransport
//
//////////////////////////////////////////////////////////////////////////
TCPServerTransport::TCPServerTransport(int listenPort, net::CompletionPort& iocp,
								   WorkQueue* rcvQueue)
	: m_tcpServer(listenPort, iocp, [this, rcvQueue](net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket)
				  {
					  return createConnection(owner, std::move(socket), rcvQueue);
				  })
{
}

TCPServerTransport::~TCPServerTransport()
{
	m_tcpServer.shutdown();
}

std::unique_ptr<net::TCPServerClientInfo> TCPServerTransport::createConnection(
	net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket,
	WorkQueue* rcvQueue)
{
	auto clientInfo = std::make_unique<TCPServerConnection>(owner, std::move(socket), this, rcvQueue);
	auto transport = std::make_unique<TCPServerSideTransport>(clientInfo.get());
	clientInfo->m_transport = transport.get();
	m_onNewClient(std::move(transport));
	return std::move(clientInfo);
}

void TCPServerTransport::onClientRemoved(TCPServerConnection* clientInfo)
{
	m_onClientDisconnect(clientInfo->m_transport);
}

int TCPServerTransport::getNumClients()
{
	return m_tcpServer.getNumClients();
}

} // namespace rpc
} // namespace cz