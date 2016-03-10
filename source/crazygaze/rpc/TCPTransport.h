/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/rpc/RPCConnection.h"
#include "crazygaze/rpc/RPCServer.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/TCPServer.h"

namespace cz
{
namespace rpc
{

//! Note: Instead of having the rpc library using a SharedQueue directly, it uses an interface, so the
// user code can trigger other things whenever work is queued, if required
class RPCWorkQueue
{
public:
	virtual void push(std::function<void()> work) = 0;
};

class TCPTransport : public Transport
{
public:
  TCPTransport(const char* ip, int port, CompletionPort& iocp, RPCWorkQueue* rcvQueue = nullptr);
  TCPTransport(const net::SocketAddress& address, CompletionPort& iocp, RPCWorkQueue* rcvQueue = nullptr);
  virtual ~TCPTransport();

protected:

	void init(const char* ip, int port, CompletionPort& iocp, RPCWorkQueue* rcvQueue = nullptr);
	//
	// Transport interface
	virtual ChunkBuffer prepareSend() override;
	virtual bool send(ChunkBuffer&& data) override;

	//
	// TCPSocket callbacks
	//
	void onSocketReceive(const ChunkBuffer& buf);
	void onSocketShutdown(int code, const std::string& msg);

	//! If this is a client side transport, we created the socket, and we control the lifetime
	std::unique_ptr<net::TCPSocket> m_socket;
	RPCWorkQueue* m_rcvQueue;
	cz::ZeroSemaphore m_queuedOps;
};

class TCPServerTransport : public ServerTransport
{
public:
	TCPServerTransport(int listenPort, CompletionPort& iocp, RPCWorkQueue* rcvQueue = nullptr);
	virtual ~TCPServerTransport();
	void onClientRemoved(class TCPServerConnection* clientInfo);
protected:
	virtual int getNumClients() override;
	std::unique_ptr<net::TCPServerClientInfo> createConnection(net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket,
		RPCWorkQueue* rcvQueue);
	net::TCPServer m_tcpServer;
};

} // namespace rpc
} // namespace cz