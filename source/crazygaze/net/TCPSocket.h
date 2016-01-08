/*!

#TODO - At the moment, TCPServerSocket only has one pending accept call. This means while we are processing one accept,
other connection requests are denied. Ideally, we should have a couple of accept calls queued up.
*/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/net/CompletionPort.h"
#include "crazygaze/net/SocketAddress.h"
#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
#include "crazygaze/Future.h"

namespace cz
{
namespace net
{

namespace details
{
	// Instead of having a function to initialize the library, I call WSAStartup and WSACleanup as
	// required, since those functions can be called several times
	struct WSAInstance
	{
		WSAInstance();
		~WSAInstance();
		WSAInstance(const WSAInstance&) = delete;
		WSAInstance& operator=(const WSAInstance&) = delete;
	};
}


//! Simple SOCKET wrapper, to have the socket closed even if an exception occurs
//! when
// constructing a TCPSocketServer or TCPSocket.
class SocketWrapper
{
  public:
	SocketWrapper();
	explicit SocketWrapper(SOCKET socket);
	SocketWrapper(const SocketWrapper&) = delete;
	SocketWrapper& operator=(const SocketWrapper&) = delete;
	SocketWrapper& operator=(SocketWrapper&& other);
	~SocketWrapper();
	SOCKET get();
	bool isValid() const;

  private:
	SOCKET m_socket = INVALID_SOCKET;
};

class TCPSocket;

class TCPServerSocket
{
  public:
	TCPServerSocket(CompletionPort& iocp, int listenPort);
	~TCPServerSocket();
	void asyncAccept(TCPSocket& socket, CompletionHandler handler);
  private:
	std::shared_ptr<struct TCPServerSocketData> m_data;
	details::WSAInstance m_wsainstance;
};

struct TCPSocketUserData
{
	virtual ~TCPSocketUserData() {}
};

class TCPSocket
{
  public:
	explicit TCPSocket(CompletionPort& iocp);
	virtual ~TCPSocket();
	Future<bool> connect(const std::string& ip, int port);
	bool asyncSend(std::vector<char> buf, CompletionHandler handler);
	bool asyncReceive(std::unique_ptr<char[]> buf, int capacity, CompletionHandler handler);
	const SocketAddress& getLocalAddress() const;
	const SocketAddress& getRemoteAddress() const;
	CompletionPort& getIOCP();
  protected:
	void shutdown();
	friend class TCPServerSocket;
	std::shared_ptr<TCPSocketData> m_data;
	std::shared_ptr<TCPSocketUserData> m_userData;
	details::WSAInstance m_wsainstance;
};

} // namespace net


std::string to_json(const net::SocketAddress& val);

} // namespace cz