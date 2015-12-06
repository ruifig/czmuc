/*!

#TODO - At the moment, TCPServerSocket only has one pending accept call. This means while we are processing one accept,
other connection requests are denied. Ideally, we should have a couple of accept calls queued up.
*/

#pragma once

#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
#include "crazygaze/net/CompletionPort.h"

namespace cz
{
namespace net
{

struct SocketAddress
{
	struct IP
	{
		union
		{
			struct bytes_st
			{
				uint8_t b1, b2, b3, b4;
			} bytes;
			uint32_t full = 0;
		};
	} ip;
	int port = 0;

	const char* toString(bool includePort) const;
	SocketAddress() {}
	explicit SocketAddress(const sockaddr& addr);
	explicit SocketAddress(const sockaddr_in& addr);
	explicit SocketAddress(const char* ip, int port);
	explicit SocketAddress(const char* ipAndPort);
	explicit SocketAddress(const std::string& ip, int port);

	bool operator==( const SocketAddress& right ) const
	{
		return (ip.full == right.ip.full) && (port == right.port);
	}
	bool operator!=( const SocketAddress& right ) const
	{
		return (*this == right) == false;
	}
	bool operator > ( const SocketAddress& right ) const
	{
		if (port == right.port)
			return ip.full > right.ip.full;
		else
			return port > right.port;
	}
	bool operator < ( const SocketAddress& right ) const
	{
		if (port == right.port)
			return ip.full < right.ip.full;
		else
			return port < right.port;
	}

	bool isValid() const
	{
		return !(port == 0 && ip.full == 0);
	}

  private:
	void constructFrom(const sockaddr_in* sa);
};

void initializeSocketsLib();
void shutdownSocketsLib();


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

class TCPServerSocket
{
  public:
	/*!
	* \param numPendingReads
	*	If 0, it will assume the default
	* \param pendingReadSize 
	*	If 0, it will assume the default
	*/
	TCPServerSocket(CompletionPort& iocp, int listenPort, std::function<void(std::unique_ptr<TCPSocket>)> onAccept,
					uint32_t numPendingReads = 0, uint32_t pendingReadSize = 0);
	~TCPServerSocket();

  protected:
  private:
	std::shared_ptr<struct TCPServerSocketData> m_data;
};

struct TCPSocketUserData
{
	virtual ~TCPSocketUserData() {}
};

class TCPSocket
{
  public:

	enum
	{
		DefaultPendingReads = 10,
		DefaultReadSize = 1500
	};

	/*!
	* \param numPendingReads
	*	If 0, it will assume the default
	* \param pendingReadSize 
	*	If 0, it will assume the default
	*/
	explicit TCPSocket(CompletionPort& iocp, uint32_t numPendingReads = 0, uint32_t pendingReadSize = 0);
	~TCPSocket();

	void setOnReceive(std::function<void(const ChunkBuffer&)> fn);
	void setOnShutdown(std::function<void(int, const std::string&)> fn);
	void setOnSendCompleted(std::function<void()> fn);
	void resetCallbacks();

	void setUserData(std::shared_ptr<TCPSocketUserData> userData);
	const std::shared_ptr<TCPSocketUserData>& getUserData();
	bool send(ChunkBuffer&& data);
	bool isConnected() const;
	std::shared_future<bool> connect(const std::string& ip, int port);
	uint64_t getPendingSendBytes() const;

	const SocketAddress& getRemoteAddress();

	friend TCPServerSocketData;
	friend struct AcceptOperation;

  protected:
	void createSocket();

	std::shared_ptr<TCPSocketData> m_data;
	std::shared_ptr<TCPSocketUserData> m_userData;
};

} // namespace net
} // namespace cz