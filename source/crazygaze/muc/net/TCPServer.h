/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	This is a way to hold together all the clients that connect to a server socket
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/Semaphore.h"

namespace cz
{
namespace net
{

class TCPServer;

class TCPServerClientInfo
{
public:
	explicit TCPServerClientInfo(TCPServer* owner, std::unique_ptr<TCPSocket> socket);
	virtual ~TCPServerClientInfo();
	TCPSocket* getSocket();
	void setSocket(std::unique_ptr<TCPSocket> socket);
	TCPServer* getOwner();
protected:

	virtual void removeClient();

	//
	// TCPSocket callbacks
	//
	virtual void onSocketReceive(const ChunkBuffer& buf);
	virtual void onSocketShutdown(int code, const std::string &msg);
	virtual void onSocketSendCompleted();

	friend class TCPServer;
	TCPServer* m_owner;
	std::unique_ptr<TCPSocket> m_socket;
};

typedef std::function<std::unique_ptr<TCPServerClientInfo>(class TCPServer*, std::unique_ptr<TCPSocket>)>
	TCPServerClientInfoFactory;

/*!

\note
	The destructor blocks waiting for all clients to disconnect.
	If you want to drop all clients, call "shutdown" before destroying.
*/
class TCPServer
{
public:
	TCPServer(int serverPort, int numIOThreads,
		TCPServerClientInfoFactory clientInfoFactory = [](TCPServer* owner, std::unique_ptr<TCPSocket> socket)
		{
			return std::make_unique<TCPServerClientInfo>(owner, std::move(socket));
		},
		uint32_t numPendingReads = 0, uint32_t pendingReadSize = 0);
	TCPServer(int serverPort, CompletionPort& iocp,
		TCPServerClientInfoFactory clientInfoFactory = [](TCPServer* owner, std::unique_ptr<TCPSocket> socket)
		{
			return std::make_unique<TCPServerClientInfo>(owner, std::move(socket));
		},
		uint32_t numPendingReads = 0, uint32_t pendingReadSize = 0);
	virtual ~TCPServer();

	// Manually stops listening for new incoming connections, and disconnects all clients
	// This is necessary if you want to destroy the server without blocking while waiting for the clients to disconnect
	void shutdown();
	int getNumClients();

	void removeClient(TCPServerClientInfo* client);

private:

	std::mutex m_mtx;
	cz::ZeroSemaphore m_clientsCount;

	CompletionPort* m_iocp;
	std::unique_ptr<CompletionPort> m_owniocp; // This is only set if we created the completion port on our own

	std::unique_ptr<TCPAcceptor> m_listenSocket;
	TCPServerClientInfoFactory m_clientInfoFactory;
	friend TCPServerClientInfo;
	std::unordered_map<TCPServerClientInfo*, std::unique_ptr<TCPServerClientInfo>> m_clients;
	void init(int serverPort, uint32_t numPendingReads, uint32_t pendingReadSize);

	// TCPAcceptor callbacks
	void onAccept(std::unique_ptr<TCPSocket> socket);

};

} // namespace net
} // namespace cz

