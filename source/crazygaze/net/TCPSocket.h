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
#include "crazygaze/Buffer.h"

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
	void shutdown();

  private:
	SOCKET m_socket = INVALID_SOCKET;
};

enum class SocketOperationCode
{
	None,
	NotConnected,
	Disconnected
};

struct SocketCompletionError
{
	enum class Code
	{
		None,
		NotConnected, // The socket is not connected.
		Disconnected, // The socket was connected at some point, but it disconnected
		NoResources // Lack os OS resources caused the operation to fail. (e.g: Sending data too fast)
	};

	Code code;
	std::string msg; // Provides extra information (OS dependent), if there is an error

	SocketCompletionError()
		: code(Code::None) { }

	SocketCompletionError(Code code) :
		code(code) { }

	SocketCompletionError(Code code, std::string msg) :
		code(code), msg(std::move(msg)) { }

	bool isOk() const
	{
		return code != Code::Disconnected;
	}

};

class TCPSocket;
using SocketCompletionHandler = std::function<void(const SocketCompletionError& err, unsigned)>;
using SocketCompletionUntilHandler = std::function<bool(ChunkBuffer& buf)>;

class TCPServerSocket
{
  public:
	TCPServerSocket(CompletionPort& iocp, int listenPort);
	~TCPServerSocket();
	void asyncAccept(TCPSocket& socket, SocketCompletionHandler handler);

  protected:
	  friend struct AsyncAcceptOperation;
	  void execute(struct AsyncAcceptOperation* op, unsigned bytesTransfered, uint64_t completionKey);

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
	void asyncSend(Buffer buf, SocketCompletionHandler handler);
	void asyncReceive(Buffer buf, SocketCompletionHandler handler);
	void asyncReceiveUntil(ChunkBuffer& buf, SocketCompletionUntilHandler untilHandler, SocketCompletionHandler handler);
	const SocketAddress& getLocalAddress() const;
	const SocketAddress& getRemoteAddress() const;
	CompletionPort& getIOCP();
	void shutdown();
  protected:
	friend struct AsyncReceiveOperation;
	friend struct AsyncSendOperation;
	void execute(struct AsyncReceiveOperation* op, unsigned bytesTransfered, uint64_t completionKey);
	void execute(struct AsyncSendOperation* op, unsigned bytesTransfered, uint64_t completionKey);
	void prepareRecvUntil(ChunkBuffer& buf, SocketCompletionUntilHandler untilHandler, SocketCompletionHandler handler);
	friend class TCPServerSocket;
	std::shared_ptr<TCPSocketData> m_data;
	std::shared_ptr<TCPSocketUserData> m_userData;
	details::WSAInstance m_wsainstance;
};

} // namespace net


std::string to_json(const net::SocketAddress& val);

} // namespace cz