/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/rpc/TCPChannel.h"

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

static void processRPCBuffer_InPlace(Channel* channel, const ChunkBuffer& buf)
{
	while (true)
	{
		//Sleep(rand() % 10); // #TODO Remove this
		auto size = Channel::hasFullRPC(buf);
		if (size == 0)
			return; // Not enough data for an RPC yet
		channel->onReceivedData(buf);
	}
}

// #TODO : Remove all the unnecessary parameters that I was using for debugging
static void processRPCBuffer(Channel* channel, const ChunkBuffer& buf, WorkQueue* rcvQueue,
							 cz::ZeroSemaphore* queuedOps)
{
	if (rcvQueue) // Put the required action in a queue, if the queue was provided
	{
		auto tmp = std::make_shared<ChunkBuffer>();
		while (true)
		{
			auto size = Channel::hasFullRPC(buf);
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
			rcvQueue->emplace([channel, buf = std::move(tmp), queuedOps]() 
			{
				//Sleep(rand() % 10); // #TODO : Remove this
				processRPCBuffer_InPlace(channel, *buf.get());
				queuedOps->decrement();
			});
		}
	}
	else // Queue not provided, so execute right now
	{
		processRPCBuffer_InPlace(channel, buf);
	}
}

//////////////////////////////////////////////////////////////////////////
//
// TCPChannel
//
//////////////////////////////////////////////////////////////////////////

TCPChannel::TCPChannel(const std::string& ip, int port, net::CompletionPort& iocp,
					   WorkQueue* rcvQueue) : m_rcvQueue(rcvQueue)
{
	LOGENTRY();
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

TCPChannel::~TCPChannel()
{
	LOGENTRY();
	m_socket.reset();
	m_queuedOps.wait();
	LOGEXIT();
}

void TCPChannel::onSocketReceive(const ChunkBuffer& buf)
{
	if (m_rcvQueue)
		m_queuedOps.increment();
	// #TODO : Remove all the unnecessary parameters I was using for debugging
	processRPCBuffer(this, buf, m_rcvQueue, &m_queuedOps);
}

void TCPChannel::onSocketShutdown(int code, const std::string& msg)
{
	onDisconnected();
}

ChunkBuffer TCPChannel::prepareSend()
{
	ChunkBuffer out;
	auto pos = out.writeReserve(sizeof(uint32_t));
	return std::move(out);
}

bool TCPChannel::send(ChunkBuffer&& data)
{
	ChunkBuffer::WritePos pos;
	pos.block = 0;
	pos.write = 0;
	uint32_t size = data.calcSize();
	data.writeAt(pos, static_cast<uint32_t>(size - sizeof(size)));
	return m_socket->send(std::move(data));
}

const std::string& TCPChannel::getCustomID() const
{
	return m_customID;
}

//////////////////////////////////////////////////////////////////////////
//
// TCPServerChannelClientInfo
//
//////////////////////////////////////////////////////////////////////////

class TCPServerChannelConnection : public net::TCPServerConnection
{
public:
  TCPServerChannelConnection(net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket, class TCPServerChannel* channelOwner,
							 WorkQueue* rcvQueue = nullptr)
	  : net::TCPServerConnection(owner, std::move(socket)), m_channelOwner(channelOwner), m_rcvQueue(rcvQueue)
	{
		LOGENTRY();
		LOGEXIT();
	}

	virtual ~TCPServerChannelConnection()
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
		m_channelOwner->onClientRemoved(this);
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
		TCPServerConnection::onSocketReceive(buf); // Call base class

		if (m_rcvQueue)
			m_queuedOps.increment();
		// #TODO : Remove all the unnecessary parameters I was using for debugging
		processRPCBuffer(m_channel, buf, m_rcvQueue, &m_queuedOps);
	}

	friend class TCPServerChannel;
	Channel* m_channel;
	class TCPServerChannel* m_channelOwner;
	WorkQueue* m_rcvQueue;
	cz::ZeroSemaphore m_queuedOps;
};


//////////////////////////////////////////////////////////////////////////
//
// TCPServerSideChannel
//
//////////////////////////////////////////////////////////////////////////

class TCPServerSideChannel : public Channel
{
public:
	TCPServerSideChannel(TCPServerChannelConnection* clientInfo)
		: m_clientInfo(clientInfo)
	{
		LOGENTRY();
		m_customID = m_clientInfo->getSocket()->getRemoteAddress().toString(true);
		LOGEXIT();
	}
	virtual ~TCPServerSideChannel()
	{
		LOGENTRY();
		m_clientInfo->getOwner()->removeClient(m_clientInfo);
		LOGEXIT();
	}

protected:
	// czrpc::Channel interface
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

	virtual const std::string& getCustomID() const override
	{
		return m_customID;
	}

	TCPServerChannelConnection* m_clientInfo;
	std::string m_customID;
};

//////////////////////////////////////////////////////////////////////////
//
// TCPServerChannel
//
//////////////////////////////////////////////////////////////////////////
TCPServerChannel::TCPServerChannel(int listenPort, net::CompletionPort& iocp,
								   WorkQueue* rcvQueue)
	: m_tcpServer(listenPort, iocp, [this, rcvQueue](net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket)
				  {
					  return createConnection(owner, std::move(socket), rcvQueue);
				  })
{
}

TCPServerChannel::~TCPServerChannel()
{
	m_tcpServer.shutdown();
}

std::unique_ptr<net::TCPServerConnection> TCPServerChannel::createConnection(
	net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket,
	WorkQueue* rcvQueue)
{
	auto clientInfo = std::make_unique<TCPServerChannelConnection>(owner, std::move(socket), this, rcvQueue);
	auto channel = std::make_unique<TCPServerSideChannel>(clientInfo.get());
	clientInfo->m_channel = channel.get();
	m_onNewClient(std::move(channel));
	return std::move(clientInfo);
}

void TCPServerChannel::onClientRemoved(TCPServerChannelConnection* clientInfo)
{
	m_onClientDisconnect(clientInfo->m_channel);
}

int TCPServerChannel::getNumClients()
{
	return m_tcpServer.getNumClients();
}

} // namespace rpc
} // namespace cz
