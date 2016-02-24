/*!
TCP Sockets inspired Boost asio
*/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/CompletionPort.h"
#include "crazygaze/net/SocketAddress.h"
#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
#include "crazygaze/Future.h"
#include "crazygaze/Buffer.h"
#include "crazygaze/RingBuffer.h"

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

	//! Simple SOCKET wrapper, to have the socket closed
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

	// NOTE: The order of the enums needs to be the order things happen with typical use
	enum class SocketState
	{
		None,
		WaitingAccept,  // When being used on the server
		Connecting,		// When it's a client connecting to a server
		Connected,
		Disconnected
	};
}

struct Error
{
	enum class Code
	{
		Success,
		Timeout,
		Other
	};

	Error(Code c=Code::Success) : code(c)
	{
	}

	Error(Code c, const char* msg)
		: code(c)
	{
		setMsg(msg);
	}

	const char* msg() const
	{
		if (optionalMsg)
			return optionalMsg->c_str();
		switch(code)
		{
			case Code::Success: return "Success";
			case Code::Timeout: return "Timeout";
			case Code::Other: return "Other";
			default: return "Unknown";
		}
	}

	void setMsg(const char* msg)
	{
		// Always create a new one, since it might be being shared by other Error instances
		optionalMsg = std::make_shared<std::string>(msg);
	}

	//! Checks if there is an error.
	// Note that this means failure, not success, so it's shorter to type for error handling. E.g:
	//
	// if (ec) handleError(...);
	//
	operator bool() const
	{
		return code != Code::Success;
	}

	Code code;
	std::shared_ptr<std::string> optionalMsg;
};


class TCPSocket;
using SocketCompletionHandler = std::function<void(const Error& err, unsigned)>;
using SocketCompletionUntilHandler = std::function<std::pair<RingBuffer::Iterator,bool>(RingBuffer::Iterator begin, RingBuffer::Iterator end)>;

class TCPAcceptor
{
  public:
	TCPAcceptor(CompletionPort& iocp, int listenPort);
	~TCPAcceptor();


	//! Synchronous accept
	Error accept(TCPSocket& socket, unsigned timeoutMs);
	void asyncAccept(TCPSocket& socket, SocketCompletionHandler handler);
	void shutdown();

  protected:
	  friend struct AsyncAcceptOperation;
	  void execute(struct AsyncAcceptOperation* op, unsigned bytesTransfered, uint64_t completionKey);

  private:
	CompletionPort& m_iocp;
	details::WSAInstance m_wsainstance;
	details::SocketState m_state = details::SocketState::None;
	details::SocketWrapper m_listenSocket;
	LPFN_ACCEPTEX m_lpfnAcceptEx = NULL;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs = NULL;
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

	//! Synchronous connect
	Error connect(const std::string& ip, int port);

	//! Asynchronous connect
	void asyncConnect(const std::string& ip, int port, SocketCompletionHandler handler);


	//! Perform a synchronous send
	// 
	// \return
	//	 The number of bytes sent. Not that it might be less than than the requested number of bytes.
	int sendSome(const void* data, int size, Error& ec);

	//! Performs a synchronous send
	//
	//	Unlike #sendSome, this blocks until all the data is sent, a timeout occurs (due to waiting too long to be able
	// to send), or an error occurs
	//
	// \param data
	//	Data to send
	//
	// \param size
	//	Size of the data to send
	//
	// \param timeoutMs
	//	Time in milliseconds to wait to be able to send more data, before it considers a timeout.
	//	This is useful in situations where the receiver is slower than the sender. The sender sends data in multiple
	//	steps. Whenever it can't send, it will block waiting for the socket to be ready to send again.
	//	Possible values:
	//	0: No blocking. It will try to send what it can, and return right away without trying again.
	//	>0: Waiting time in milliseconds between sends.
	//	0xFFFFFFFF : Block forever. Use this carefully, since if the other end misbehaves (e.g: Doesn't read data and
	//		doesn't close the connection), this will indeed BLOCK FOREVER since you have no way to break out of this
	// call.
	//
	// \return
	//	The number of bytes sent. This can be lower than the requested number of bytes, if a timeout occurred.
	int send(void* data, int size, unsigned timeoutMs, Error& ec);

	//! Performs a synchronous receive
	//
	// \return
	//	The number of bytes received. This might be lower than the number of bytes requested
	int receiveSome(void* data, int size, Error& ec);

	//! Performs a synchronous receive
	//
	// Unlike #receiveSome, this blocks until it received the requested number of bytes, a timeout occurs, or an error
	// occurs.
	//
	// \param timeoutMs
	//	Time milliseconds to wait for the socket to have incoming data. If no data arrived it will timeout.
	//	0: No blocking. It will try to send what it can, and return right away without trying again.
	//	>0: Waiting time in milliseconds between sends.
	//	0xFFFFFFFF : Block forever. Use this carefully, since if the other end misbehaves (e.g: Doesn't read data and
	//		doesn't close the connection), this will indeed BLOCK FOREVER since you have no way to break out of this
	// call.
	//
	// \return
	//	The number of bytes received. This can be lower than the requested number of bytes, if a timeout or an error
	//	occurred.
	int receive(void* data, int size, unsigned timeoutMs, Error& ec);

	//! Sends the specified buffer in its entirety.
	// The supplied buffer must remain valid until the handler is executed.
	// \note
	//	The handler is only executed when all the data is sent, or an error occurs.
	void asyncSend(Buffer buf, SocketCompletionHandler handler);

	//! Sends the specified buffer in its entirety.
	// The supplied buffer, is copied, so it doesn't need to remain valid 
	// 
	template<typename HANDLER>
	void asyncSend(const void* data, int size, HANDLER handler )
	{
		auto buf = make_shared_array<char>(size);
		memcpy(buf.get(), data, size);
		asyncSend(Buffer(buf.get(), size),
			[buf, handler=std::move(handler)](auto err, auto bytesTransfered)
		{
			handler(err, bytesTransfered);
		});
	}

	//! Starts an asynchronous receive.
	// The supplied buffer must remain valid until the handler is invoked
	// \note
	//	The operation might not receive all the requested number of bytes
	void asyncReceiveSome(Buffer buf, SocketCompletionHandler handler);

	template<typename HANDLER>
	void processAsyncReceive(Buffer buf, HANDLER handler, const Error& err, unsigned bytesTransfered,
		unsigned done, unsigned expected)
	{
		if (bytesTransfered == 0)
		{
			handler(err, bytesTransfered);
			return;
		}

		done += bytesTransfered;
		if (done == expected)
		{
			handler(err, done);
			return;
		}

		buf = Buffer(buf.ptr + bytesTransfered, buf.size - bytesTransfered);
		asyncReceiveSome(
			buf, [this, buf, done, expected, handler=std::move(handler)](auto ec, auto bytesTransfered)
		{
			processAsyncReceive(buf, std::move(handler), ec, bytesTransfered, done, expected);
		});
	}

	//! Starts an asynchronous receive.
	// The supplied buffer must remain valid until the handler is invoked
	// \note
	// Unlike #asyncReceiveSome, this operation only calls the handler once it receives the requested number of bytes
	// or an error occurs.
	//
	template<typename HANDLER>
	void asyncReceive(Buffer buf, HANDLER handler)
	{
		unsigned done = 0;
		unsigned expected = static_cast<unsigned>(buf.size);
		asyncReceiveSome(
			buf,
			[this, buf, handler=std::move(handler), done, expected](auto ec, auto bytesTransfered) mutable
		{
			processAsyncReceive(buf, std::move(handler), ec, bytesTransfered, done, expected);
		});
	}

	//! 
	void asyncReceiveUntil(RingBuffer& buf, SocketCompletionUntilHandler untilHandler, SocketCompletionHandler handler,
	                       int tmpBufSize = 2048);

	//! 
	void asyncReceiveUntil(RingBuffer& buf, char delim, SocketCompletionHandler handler,
	                       int tmpBufSize = 2048)
	{
		asyncReceiveUntil(
			buf,
			[this, delim](auto begin, auto end) mutable -> std::pair<RingBuffer::Iterator, bool>
			{
				std::pair<RingBuffer::Iterator, bool> res;
				while(begin!=end)
				{
					auto ch = *begin;
					if (ch == delim)
					{
						res.first = ++begin; res.second = true;
						return res;
					}
					else
						++begin;
				}
				res.first = begin; res.second = false;
				return res;
			},
			std::move(handler), tmpBufSize);
	}
	const SocketAddress& getLocalAddress() const;
	const SocketAddress& getRemoteAddress() const;
	CompletionPort& getIOCP();
	void shutdown();
  protected:

	friend struct AsyncConnectOperation;
	friend struct AsyncReceiveOperation;
	friend struct AsyncSendOperation;
	friend class TCPAcceptor;
	void init(SOCKET s, CompletionPort& iocp);
	Error fillLocalAddr();
	Error fillRemoteAddr();
	void setBlocking(bool blocking);
	void execute(struct AsyncConnectOperation* op, unsigned bytesTransfered, uint64_t completionKey);
	void execute(struct AsyncReceiveOperation* op, unsigned bytesTransfered, uint64_t completionKey);
	void execute(struct AsyncSendOperation* op, unsigned bytesTransfered, uint64_t completionKey);
	void prepareRecvUntil(std::shared_ptr<char> tmpbuf, int tmpBufSize, std::pair<RingBuffer::Iterator, bool> res,
	                      RingBuffer& buf, SocketCompletionUntilHandler untilHandler, SocketCompletionHandler handler);

	CompletionPort& m_iocp;
	details::SocketWrapper m_socket;
	details::SocketState m_state = details::SocketState::None;
	SocketAddress m_localAddr;
	SocketAddress m_remoteAddr;
	std::shared_ptr<TCPSocketUserData> m_userData;
	details::WSAInstance m_wsainstance;
};

} // namespace net


std::string to_json(const net::SocketAddress& val);

} // namespace cz