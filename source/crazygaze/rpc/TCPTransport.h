/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/rpc/RPCConnection.h"
#include "crazygaze/rpc/RPCServer.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/TCPServer.h"

namespace cz
{
namespace rpc
{

class TCPTransport : public Transport
{
public:
  TCPTransport(const char* ip, int port, net::CompletionPort& iocp, WorkQueue* rcvQueue = nullptr);
  TCPTransport(const net::SocketAddress& address, net::CompletionPort& iocp, WorkQueue* rcvQueue = nullptr);
  virtual ~TCPTransport();

protected:

	void init(const char* ip, int port, net::CompletionPort& iocp, WorkQueue* rcvQueue = nullptr);
	//
	// Transport interface
	virtual ChunkBuffer prepareSend() override;
	virtual bool send(ChunkBuffer&& data) override;
	virtual const std::string& getCustomID() const override;

	//
	// TCPSocket callbacks
	//
	void onSocketReceive(const ChunkBuffer& buf);
	void onSocketShutdown(int code, const std::string& msg);

	//! If this is a client side transport, we created the socket, and we control the lifetime
	std::unique_ptr<net::TCPSocket> m_socket;
	std::string m_customID;
	WorkQueue* m_rcvQueue;
	cz::ZeroSemaphore m_queuedOps;
};

class TCPServerTransport : public ServerTransport
{
public:
	TCPServerTransport(int listenPort, net::CompletionPort& iocp, WorkQueue* rcvQueue = nullptr);
	virtual ~TCPServerTransport();
	void onClientRemoved(class TCPServerConnection* clientInfo);
protected:
	virtual int getNumClients() override;
	std::unique_ptr<net::TCPServerClientInfo> createConnection(net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket,
		WorkQueue* rcvQueue);
	net::TCPServer m_tcpServer;
};

} // namespace rpc
} // namespace cz