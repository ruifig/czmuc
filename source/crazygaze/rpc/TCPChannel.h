/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCChannel.h"
#include "crazygaze/rpc/RPCClient.h"
#include "crazygaze/rpc/RPCServer.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/TCPServer.h"

namespace cz
{
namespace rpc
{

class TCPChannel : public Channel
{
public:
  TCPChannel(const std::string& ip, int port, net::CompletionPort& iocp,
			WorkQueue* rcvQueue = nullptr);
  virtual ~TCPChannel();

protected:
	//
	// Channel interface
	virtual ChunkBuffer prepareSend() override;
	virtual bool send(ChunkBuffer&& data) override;
	virtual const std::string& getCustomID() const override;

	//
	// TCPSocket callbacks
	//
	void onSocketReceive(const ChunkBuffer& buf);
	void onSocketShutdown(int code, const std::string& msg);

	//! If this is a client side channel, we created the socket, and we control the lifetime
	std::unique_ptr<net::TCPSocket> m_socket;
	std::string m_customID;
	WorkQueue* m_rcvQueue;
	cz::ZeroSemaphore m_queuedOps;
};

class TCPServerChannelConnection;

class TCPServerChannel : public ServerChannel
{
public:
	TCPServerChannel(int listenPort, net::CompletionPort& iocp, WorkQueue* rcvQueue = nullptr);
	virtual ~TCPServerChannel();
	void onClientRemoved(TCPServerChannelConnection* clientInfo);
protected:
	virtual int getNumClients() override;
	std::unique_ptr<net::TCPServerConnection> createConnection(net::TCPServer* owner, std::unique_ptr<net::TCPSocket> socket,
		WorkQueue* rcvQueue);
	net::TCPServer m_tcpServer;
};

} // namespace rpc
} // namespace cz