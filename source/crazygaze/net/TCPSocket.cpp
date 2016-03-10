#include "czlibPCH.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/Json.h"
#include "crazygaze/StringUtils.h"
#include "crazygaze/ScopeGuard.h"

#pragma comment(lib, "ws2_32.lib")
// Disable deprecation warnings
#pragma warning(disable : 4996)

/*
IO Completion ports random notes:

IOCP Notifications:
http://stackoverflow.com/questions/28694559/can-wsarecv-return-data-immediately
http://support2.microsoft.com/default.aspx?scid=kb;en-us;Q192800
http://stackoverflow.com/questions/25334897/iocp-if-operation-returns-immediately-with-error-can-i-still-receive-completio
When performance operations (e,g: WSARecv/WSASend) with completion ports, if the operation is completed at the point of call
(instead of return ERROR_PENDING), the notification is still queued to the IOCP.
This means that whenever for example WSARecv completes immediately, we can do one of the following:
	- Deal with it immediately, and keep looping until WSARecv returns ERROR_PENDING. This means the IOCP notification is
	still queued, even if we already handled that data. This turns out a bit of a mess to handle.
	- Ignore the fact it completed immediately, and let it be handled whenever we get the IOCP event. This is easier, but
	means we are losing performance with extra context switches to handle it later.
	- Use SetFileCompletionNotificationModes on the socket, with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS. This skips the IOCP
	notification if the operation completes immediately.


Thread safety notes:
According to Microsoft documentation, calls to WSARecv/WSASend on the same socket from several threads is ok, but the
notifications might be out of order, but according to http://www.serverframework.com/asynchronousevents/ , it can corrupt
data, so for now I'm using a mutex for all WSARecv/WSASend calls on a socket

Client side IOCP:
http://stackoverflow.com/questions/10998504/winsock2-how-to-use-iocp-on-client-side

*/

/*
Links:

Use AcceptEx to accept connections using IO completion ports
https://msdn.microsoft.com/en-us/library/windows/desktop/ms737524%28v=vs.85%29.aspx
	The sample code has an example using completion ports
https://msdn.microsoft.com/en-us/magazine/cc302334.aspx
http://stackoverflow.com/questions/19956186/iocp-acceptex-not-creating-completion-upon-connect

Other links:
http://www.codeproject.com/Articles/20570/Scalable-Servers-with-IO-Completion-Ports-and-How
http://www.codeproject.com/Articles/13382/A-simple-application-using-I-O-Completion-Ports-an
http://www.serverframework.com/products---the-free-framework.html
*/

namespace cz
{

CZ_DEFINE_LOG_CATEGORY(logNet)

std::string to_json(const net::SocketAddress& val)
{
	return to_json(val.toString(true));
}

namespace net
{

namespace details
{

void setBlocking(SOCKET sock, bool blocking)
{
	CZ_ASSERT(sock != INVALID_SOCKET);

	// 0: Blocking. !=0 : Non-blocking
	u_long mode = blocking ? 0 : 1;
	int res = ioctlsocket(sock, FIONBIO, &mode);
	if (res != 0)
	{
		auto err = GetLastError();
		CZ_LOG(logNet, Fatal, "Error set non-blocking mode: %s", getLastWin32ErrorMsg());
	}
}

WSAInstance::WSAInstance()
{
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	int err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0)
	{
		CZ_LOG(logNet, Fatal, "Error calling WSAStartup: %s", getLastWin32ErrorMsg());
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		WSACleanup();
		CZ_LOG(logNet, Fatal, "Could not find a usable version of Winsock.dll");
	}
}

WSAInstance::~WSAInstance()
{
	WSACleanup();
}

//////////////////////////////////////////////////////////////////////////
//
// SocketWrapper
//
//////////////////////////////////////////////////////////////////////////

SocketWrapper::SocketWrapper() : m_socket(INVALID_SOCKET)
{
}

SocketWrapper::SocketWrapper(SOCKET socket) : m_socket(socket)
{
}

SocketWrapper::~SocketWrapper()
{
	close();
}

void SocketWrapper::close()
{
	if (m_socket == INVALID_SOCKET)
		return;
	::shutdown(m_socket, SD_BOTH);
	auto res = ::closesocket(m_socket);
	auto err = GetLastError();
	assert(res == 0);
	m_socket = INVALID_SOCKET;
}

SOCKET SocketWrapper::get()
{
	CZ_ASSERT(m_socket != INVALID_SOCKET);
	return m_socket;
}

bool SocketWrapper::isValid() const
{
	return m_socket != INVALID_SOCKET;
}

SocketWrapper& SocketWrapper::operator=(SocketWrapper&& other)
{
	close();
	m_socket = other.m_socket;
	other.m_socket = INVALID_SOCKET;
	return *this;
}


// Keeps pointers to functions that we need to get with WSAIoctl
struct WSAFuncs
{
	WSAFuncs()
	{
		// Create a dummy socket, so we can call WSAIoctl
		SocketWrapper sock(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
		CZ_ASSERT(sock.isValid());

		// Load the AcceptEx function into memory using WSAIoctl.
		// The WSAIoctl function is an extension of the ioctlsocket()
		// function that can use overlapped I/O. The function's 3rd
		// through 6th parameters are input and output buffers where
		// we pass the pointer to our AcceptEx function. This is used
		// so that we can call the AcceptEx function directly, rather
		// than refer to the Mswsock.lib library.
		int res;
		GUID guid;
		DWORD dwBytes;

		guid = WSAID_ACCEPTEX;
		res = WSAIoctl(sock.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
			&lpfnAcceptEx, sizeof(lpfnAcceptEx), &dwBytes, NULL, NULL);
		CZ_ASSERT(res != SOCKET_ERROR);

		guid = WSAID_GETACCEPTEXSOCKADDRS;
		res = WSAIoctl(sock.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
			&lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs), &dwBytes, NULL, NULL);
		CZ_ASSERT(res != SOCKET_ERROR);

		guid = WSAID_CONNECTEX;
		res = WSAIoctl(sock.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
			&lpfnConnectEx, sizeof(lpfnConnectEx), &dwBytes, NULL, NULL);
		CZ_ASSERT(res != SOCKET_ERROR);
	}

	static const WSAFuncs& get()
	{
		static WSAFuncs funcs;
		return funcs;
	}

	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
	LPFN_CONNECTEX lpfnConnectEx = NULL;
};

} // namespace details

//////////////////////////////////////////////////////////////////////////
//
// Utility code
//
//////////////////////////////////////////////////////////////////////////

static const char* getAddr(sockaddr* sa)
{
	void* ptr;
	int port;
	if (sa->sa_family == AF_INET)
	{
		ptr = &(((struct sockaddr_in*)sa)->sin_addr);
		port = ntohs(((struct sockaddr_in*)sa)->sin_port);
	}
	else
	{
		ptr = &(((struct sockaddr_in6*)sa)->sin6_addr);
		port = ntohs(((struct sockaddr_in6*)sa)->sin6_port);
	}

	thread_local char buf[INET6_ADDRSTRLEN];
	inet_ntop(sa->sa_family, ptr, buf, sizeof(buf));
	return formatString("%s:%d", buf, port);
}

struct AsyncAcceptOperation : public CompletionPortOperation
{
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override
	{
		CZ_ASSERT(bytesTransfered == 0);
		CZ_ASSERT(completionKey == 0);
		if (aborted)
		{
			err = Error(Error::Code::Cancelled);
		}
		else if (err)
		{
			// Do nothing
		}
		else
		{
			sockaddr* localSockAddr = NULL;
			int localSockAddrSize;
			sockaddr* remoteSockAddr = NULL;
			int remoteSockAddrSize;
			details::WSAFuncs::get().lpfnGetAcceptExSockaddrs(
				&outputBuffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &localSockAddr,
				&localSockAddrSize, &remoteSockAddr, &remoteSockAddrSize);
			CZ_LOG(logNet, Log, "New connection:\n\tlocal = %s\n\tremote = %s\n", getAddr(localSockAddr), getAddr(remoteSockAddr));
			sock->m_localAddr = SocketAddress(*localSockAddr);
			sock->m_remoteAddr = SocketAddress(*remoteSockAddr);
			sock->m_state = details::SocketState::Connected;
		}
		handler(err);
	}
	enum
	{
		AddrLen = sizeof(sockaddr_in) + 16
	};
	AcceptHandler handler;
	Error err;
	char outputBuffer[2 * AddrLen];
	TCPSocket* sock = nullptr;
};

struct AsyncConnectOperation : public CompletionPortOperation
{
	AsyncConnectOperation() {}
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override
	{
		if (aborted)
		{
			err = Error(Error::Code::Cancelled);
		}
		else if (err)
		{
			// Do nothing
		}
		else
		{
			int seconds;
			int bytes = sizeof(seconds);
			int res = getsockopt(sock, SOL_SOCKET, SO_CONNECT_TIME, (char*)&seconds, (PINT)&bytes);
			if (res != NO_ERROR || seconds == -1)
				err = Error(Error::Code::Other, "Connect failed");
		}

		handler(err);
	}

	// holding the SOCKET directly, instead of TCPSocket, so TCPSocket can be deleted while there are still handlers
	// in flight
	SOCKET sock; 
	ConnectHandler handler;
	Error err;
};

struct AsyncReceiveOperation : public CompletionPortOperation
{
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override
	{
		if (aborted)
		{
			err = Error(Error::Code::Cancelled);
		}
		else if (err)
		{
			CZ_ASSERT(bytesTransfered == 0);
		}
		else if (bytesTransfered == 0)
		{
			// When we don't have an error, but the bytes transfered are zero, it means the peer disconnected gracefully
			err = Error(Error::Code::ConnectionClosed);
		}

		handler(err, bytesTransfered);
	}
	ReceiveHandler handler;
	Error err;
};

struct AsyncSendOperation : public CompletionPortOperation
{
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override
	{
		if (aborted)
		{
			err = Error(Error::Code::Cancelled);
		}
		else if (err)
		{
			// Do nothing
		}
		else
		{
			// Do nothing
		}

		handler(err, bytesTransfered);
	}
	SendHandler handler;
	Error err;
};
//////////////////////////////////////////////////////////////////////////
//
// TCPAcceptor
//
//////////////////////////////////////////////////////////////////////////

Error TCPAcceptor::listen(int listenPort, int backlog)
{
	CZ_ASSERT(m_listening == false);

	sockaddr_in serverAddress;
	ZeroMemory(&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(listenPort);

	if (bind(m_listenSocket.get(), (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR ||
		::listen(m_listenSocket.get(), backlog) == SOCKET_ERROR)
	{
		CZ_LOG(logNet, Fatal, "Error initializing listen socket: %s", getLastWin32ErrorMsg());
	}

	if (CreateIoCompletionPort(
			reinterpret_cast<HANDLE>(m_listenSocket.get()), // FileHandle
			m_iocp.getHandle(), // ExistingCompletionPort
			(ULONG_PTR)0, // CompletionKey
			0 // NumberOfConcurrentThreads
		) == NULL)
	{
		CZ_LOG(logNet, Fatal, "Error initializing listen socket: %s", getLastWin32ErrorMsg());
	}

	m_listening = true;

	return Error();
}

TCPAcceptor::TCPAcceptor(CompletionPort& iocp)
	: m_iocp(iocp)
{
	m_listenSocket = details::SocketWrapper(
		WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
	if (!m_listenSocket.isValid())
		CZ_LOG(logNet, Fatal, "Error creating listen socket: %s", getLastWin32ErrorMsg());

	// Set this, so no other applications can bind to the same port
	int iOptval = 1;
	if (setsockopt(m_listenSocket.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&iOptval, sizeof(iOptval)) ==
		SOCKET_ERROR)
	{
		CZ_LOG(logNet, Fatal, "Error setting SO_EXCLUSIVEADDRUSE: %s", getLastWin32ErrorMsg());
	}
}

TCPAcceptor::~TCPAcceptor()
{
	close();
}

void TCPAcceptor::cancel()
{
	if (!m_listening)
		return;

	// Notes:
	// CancelIo - Cancels I/O operations that are issued by calling thread
	// CancelIoEx Cancels all I/O regardless of the thread that created the I/O operation.
	auto res = CancelIoEx(
		(HANDLE)m_listenSocket.get(), // hFile
		NULL // lpOverlapped - NULL means cancel all I/O requests for the specified hFile
		);

	if (res == FALSE)
	{
		auto err = GetLastError();
		if (err == ERROR_NOT_FOUND)
		{
			// This means no elements were found to cancel, and it's expected
		}
		else
		{
			// If we get any other errors, probably the user called WSACleanup already
			CZ_LOG(logNet, Warning, "CancelIoEx failed: %d:%s", err, getLastWin32ErrorMsg(err));
		}
	}

}
void TCPAcceptor::close()
{
	if (!m_listening)
		return;

	cancel();
	m_listenSocket.close();
	m_listening = false;
}

Error TCPAcceptor::accept(TCPSocket& socket, unsigned timeoutMs)
{
	FD_SET set;
	TIMEVAL t;
	t.tv_sec = timeoutMs / 1000;
	t.tv_usec = (timeoutMs % 1000) * 1000;

	FD_ZERO(&set);
	FD_SET(m_listenSocket.get(), &set);
	int res = select(0, &set, NULL, NULL, timeoutMs == 0xFFFFFFFF ? NULL : &t);

	if (res == 0)
	{
		return Error(Error::Code::Timeout, "");
	}
	else if (res==SOCKET_ERROR)
	{
		CZ_UNEXPECTED();
		return Error(Error::Code::Other, getLastWin32ErrorMsg());
	}
	else
	{
		SOCKET s = ::accept(m_listenSocket.get(), NULL, NULL);
		// Since select passed, accept should not fail
		CZ_ASSERT(s != INVALID_SOCKET);
		socket.init(s, socket.getIOCP());
		socket.fillLocalAddr();
		socket.fillRemoteAddr();
		return Error(Error::Code::Success);
	}
}

void TCPAcceptor::asyncAccept(TCPSocket& socket, AcceptHandler handler)
{
	CZ_ASSERT(m_listening);

	DWORD dwBytes;

	auto op = std::make_unique<AsyncAcceptOperation>();
	op->handler = std::move(handler);
	op->sock = &socket;

	auto res = details::WSAFuncs::get().lpfnAcceptEx(
		m_listenSocket.get(), // sListenSocket
		socket.m_socket.get(), // sAcceptSocket
		&op->outputBuffer, // lpOutputBuffer
		0, // dwReceiveDataLength : 0 means the outputBuffer will only contain the local and remote address, and not receive any data
		AsyncAcceptOperation::AddrLen, // dwLocalAddressLength
		AsyncAcceptOperation::AddrLen, // dwRemoteAddressLength
		&dwBytes /*Unused for asynchronous AcceptEx*/,
		&op->overlapped
		);

	int err = WSAGetLastError();
	if (res == FALSE && err == ERROR_IO_PENDING)
	{
		// Expected, so do nothing
	}
	else
	{
		// Common errors and usual causes
		// WSAEINVAL 10022 : An invalid argument was supplied
		//		This is normally caused by passing the same socket to multiple asyncAccept calls
		//	
		CZ_LOG(logNet, Fatal, "Error calling AcceptEx: %s", getLastWin32ErrorMsg());
	}

	socket.m_state = details::SocketState::WaitingAccept;
	m_iocp.add(std::move(op));
}

//////////////////////////////////////////////////////////////////////////
//
// TCPSocket
//
//////////////////////////////////////////////////////////////////////////

TCPSocket::TCPSocket(CompletionPort& iocp)
	: m_iocp(iocp)
{
	init(INVALID_SOCKET, iocp);
}

void TCPSocket::init(SOCKET s, CompletionPort& iocp)
{
	m_socket = details::SocketWrapper(
		(s==INVALID_SOCKET) ? WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED) : s);
	if (!m_socket.isValid())
	{
		CZ_LOG(logNet, Fatal, "Error creating client socket: %s", getLastWin32ErrorMsg());
	}

	if (CreateIoCompletionPort(
			(HANDLE)m_socket.get(), // FileHandle
			m_iocp.getHandle(), // ExistingCompletionPort
			(ULONG_PTR)0, // CompletionKey
			0 // NumberOfConcurrentThreads
		) == NULL) 
	{
		CZ_LOG(logNet, Fatal, "Error initializing accept socket: %s", getLastWin32ErrorMsg());
	}

	// Set non-blocking, so sendSome/receiveSome doesn't block forever
	setBlocking(false);

	// This disables IOCP notification if the request completes immediately at the point of call.
	/*
	if (SetFileCompletionNotificationModes((HANDLE)m_socket.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)==FALSE)
	{
		CZ_LOG(logNet, Fatal, "Error initializing accept socket: %s", getLastWin32ErrorMsg());
	}
	*/
}

TCPSocket::~TCPSocket()
{
	close();
}

void TCPSocket::setBlocking(bool blocking)
{
	details::setBlocking(m_socket.get(), blocking);
}

void TCPSocket::cancel()
{
	if (m_state == details::SocketState::Disconnected)
		return;

	// Notes:
	// CancelIo - Cancels I/O operations that are issued by calling thread
	// CancelIoEx Cancels all I/O regardless of the thread that created the I/O operation.
	auto res = CancelIoEx(
		(HANDLE)m_socket.get(), // hFile
		NULL // lpOverlapped - NULL means cancel all I/O requests for the specified hFile
		);

	if (res == FALSE)
	{
		auto err = GetLastError();
		if (err == ERROR_NOT_FOUND)
		{
			// This means no elements were found to cancel, and it's expected
		}
		else
		{
			// If we get any other errors, probably the user called WSACleanup already
			CZ_LOG(logNet, Warning, "CancelIoEx failed: %d:%s", err, getLastWin32ErrorMsg(err));
		}
	}

}

void TCPSocket::close()
{
	if (m_state == details::SocketState::Disconnected)
		return;

	cancel();
	m_socket.close();
	m_state = details::SocketState::Disconnected;
	m_userData.reset();
}

Error TCPSocket::fillLocalAddr()
{
	sockaddr_in addr;
	int size = sizeof(addr);
	if (getsockname(m_socket.get(), (SOCKADDR*)&addr, &size) == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logNet, Fatal, "Could not get address information. Error '%s'", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	m_localAddr = SocketAddress(addr);
	return Error();
}

Error TCPSocket::fillRemoteAddr()
{
	sockaddr_in addr;
	int size = sizeof(addr);
	if (getpeername(m_socket.get(), (SOCKADDR*)&addr, &size) == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logNet, Fatal, "Could not get address information. Error '%s'", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	m_remoteAddr = SocketAddress(addr);
	return Error();
}

Error TCPSocket::connect(const std::string& ip, int port)
{
	CZ_ASSERT(m_state == details::SocketState::None);

	SocketAddress remoteAddr(ip.c_str(), port);
	m_remoteAddr = remoteAddr;
	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = remoteAddr.ip.full;
	addr.sin_port = htons(remoteAddr.port);

	setBlocking(true);
	SCOPE_EXIT{ setBlocking(false); };
	auto res = WSAConnect(m_socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);

	if (res == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logNet, Warning, "Could not connect: %s", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	Error ec = fillLocalAddr();
	if (ec)
		return std::move(ec);

	m_state = details::SocketState::Connected;
	return Error();
}

void TCPSocket::asyncConnect(const std::string& ip, int port, ConnectHandler handler)
{
	CZ_ASSERT(m_state == details::SocketState::None);

	// ConnectEx required the socket to be bound
	{
		sockaddr_in addr;
		ZeroMemory(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = 0;
		if (bind(m_socket.get(), (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			CZ_LOG(logNet, Fatal, "Error binding socket: %s", getLastWin32ErrorMsg());
			return;
		}
	}

	m_state = details::SocketState::Connecting;

	auto op = std::make_unique<AsyncConnectOperation>();
	op->handler = std::move(handler);
	op->sock = m_socket.get();

	// Connect
	SocketAddress remoteAddr(ip.c_str(), port);
	m_remoteAddr = remoteAddr;
	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = remoteAddr.ip.full;
	addr.sin_port = htons(remoteAddr.port);
	BOOL res = details::WSAFuncs::get().lpfnConnectEx(
		m_socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, &op->overlapped);
	int err = WSAGetLastError();

	if (err==0 )
	{
		CZ_UNEXPECTED(); // #TODO : Check what do to here
	}
	else if (err == WSA_IO_PENDING)
	{
		// Nothing to do
	}
	else
	{
		op->err = Error(Error::Code::Other, getLastWin32ErrorMsg());
		// Manually queue the operation, so the handler is executed as part of the completion port
		m_iocp.post(std::move(op), 0, 0);
		return;
	}

	m_iocp.add(std::move(op));
}

const SocketAddress& TCPSocket::getLocalAddress() const
{
	return m_localAddr;
}

const SocketAddress& TCPSocket::getRemoteAddress() const
{
	return m_remoteAddr;
}

CompletionPort& TCPSocket::getIOCP()
{
	return m_iocp;
}

void TCPSocket::asyncReceiveSome(Buffer buf, ReceiveHandler handler)
{
	// According to the documentation for WSARecv, it is safe to have the WSABUF on the stack, since it will copied
	int const numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	memset(&wsabufs, 0, sizeof(wsabufs));
	wsabufs[0].buf = buf.ptr;
	wsabufs[0].len = static_cast<DWORD>(buf.size);

	auto op = std::make_unique<AsyncReceiveOperation>();
	op->handler = std::move(handler);

	DWORD rcvFlags = 0;
	DWORD bytesTransfered = 0;
	int res = WSARecv(
		m_socket.get(), // Socket
		&wsabufs[0], // lpBuffers
		numbuffers, // dwBufferCount
		NULL, // lpNumberOfBytesRecvd
		&rcvFlags, // lpFlags
		&op->overlapped, // lpOverlapped
		NULL // lpCompletionRoutine
		);
	int err = WSAGetLastError();

	if (res == 0)
	{
		// Operation completed immediately, but we will still get the iocp notification
	}
	else
	{
		CZ_ASSERT(res == SOCKET_ERROR);
		if (err == WSA_IO_PENDING)
		{
			// Expected. Handler was queued
		}
		else  // Any other error, we close this connection
		{
			op->err.setMsg(getLastWin32ErrorMsg());
			switch(err)
			{
			case WSAENOTCONN: // Socket was not connected at all;
				op->err.code = Error::Code::Other;
				break;
			case WSAENOBUFS:
				op->err.code = Error::Code::NoResources;
				m_state = details::SocketState::Disconnected;
				break;
			case WSAECONNRESET:
				op->err.code = Error::Code::ConnectionClosed;
				m_state = details::SocketState::Disconnected;
				break;
			default: // Any other error we just consider as disconnection
				op->err.code = Error::Code::Other;
				m_state = details::SocketState::Disconnected;
			}
			// Manually queue the handler
			auto res = PostQueuedCompletionStatus(
				m_iocp.getHandle(), // CompletionPort
				0, // dwNumberOfBytesTransfered
				0, // CompletionKey
				&op->overlapped // lpOverlapped
				);
			CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		}
	}

	m_iocp.add(std::move(op));
}

void TCPSocket::prepareRecvUntil(std::shared_ptr<char> tmpbuf, int tmpBufSize,
                                 std::pair<RingBuffer::Iterator, bool> res, RingBuffer& buf,
                                 MatchCondition matchCondition, ReceiveHandler handler)
{
	asyncReceiveSome(
		Buffer(tmpbuf.get(), tmpBufSize),
		[this, tmpbuf, tmpBufSize, res, &buf,
#if CZ_RINGBUFFER_DEBUG
		bufReadCounter = buf.getReadCounter(),
#endif
		untilHandler=std::move(matchCondition), handler=std::move(handler)](const Error& err, unsigned bytesTransfered) mutable
	{

#if CZ_RINGBUFFER_DEBUG
		CZ_ASSERT_F(bufReadCounter == buf.getReadCounter(), "You Should not perform any read operations on the buffer while there are pending read-until operations");
#endif

		if (err)
		{
			handler(err, bytesTransfered);
			return;
		}

		buf.write(tmpbuf.get(), bytesTransfered);
		res = untilHandler(res.first, buf.end());
		if (res.second)
			handler(err, res.first - buf.begin());
		else
			prepareRecvUntil(std::move(tmpbuf), tmpBufSize, res, buf, std::move(untilHandler), std::move(handler));
	});
}

void TCPSocket::asyncReceiveUntil(RingBuffer& buf, MatchCondition matchCondition,
                                  ReceiveHandler handler, int tmpBufSize)
{
	auto tmpbuf = make_shared_array<char>(tmpBufSize);
	auto res = std::make_pair<RingBuffer::Iterator, bool>(buf.begin(), false);

	if (buf.getUsedSize()==0)
	{
		prepareRecvUntil(tmpbuf, tmpBufSize, res, buf, std::move(matchCondition), std::move(handler));
	}
	else
	{
		// If the buffer is not empty, then we manually queue the operation.
		// This deals with the case where the buffer already has enough data to match the condition.
		auto op = std::make_unique<AsyncReceiveOperation>();
		op->handler = [
			this, tmpbuf, &buf, tmpBufSize, untilHandler=std::move(matchCondition), handler=std::move(handler),
			err = Error()
#if CZ_RINGBUFFER_DEBUG
			, bufReadCounter = buf.getReadCounter()
#endif
		](const Error& err, unsigned bytesTransfered)
		{

#if CZ_RINGBUFFER_DEBUG
			CZ_ASSERT_F(bufReadCounter == buf.getReadCounter(), "You Should not perform any read operations on the buffer while there are pending read-until operations");
#endif
			if (err)
			{
				handler(err, bytesTransfered);
				return;
			}

			auto res = untilHandler(buf.begin(), buf.end());
			if (res.second)
				handler(err, res.first - buf.begin());
			else
				prepareRecvUntil(std::move(tmpbuf), tmpBufSize, res, buf, std::move(untilHandler), std::move(handler));
		};

		auto res = PostQueuedCompletionStatus(
			m_iocp.getHandle(), // CompletionPort
			buf.getUsedSize(), // dwNumberOfBytesTransfered
			0, // CompletionKey
			&op->overlapped // lpOverlapped
			);
		CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		m_iocp.add(std::move(op));
	}

}

void TCPSocket::asyncSend(Buffer buf, SendHandler handler)
{
	CZ_ASSERT(buf.ptr != nullptr);

	auto op = std::make_unique<AsyncSendOperation>();
	op->handler = std::move(handler);

	// WSABUF is safe to keep on the stack, according to the documentation
	const int numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	wsabufs[0].buf = buf.ptr;
	wsabufs[0].len = static_cast<DWORD>(buf.size);

	int res = WSASend(
		m_socket.get(), // Socket
		&wsabufs[0], // lpBuffers
		numbuffers, // dwBufferCount
		NULL, // lpNumberOfBytesSent
		0, // dwFlags
		&op->overlapped, // lpOverlapped
		NULL // lpCompletionRoutine
		);

	int err = WSAGetLastError();

	if (res == 0)
	{
		// Send operation completed immediately, so nothing else to do.
		// Note that we still get the notification call. This gives us consistent behavior
	}
	else if (res==SOCKET_ERROR)
	{
		if (err == WSA_IO_PENDING)
		{
			// Send is being done asynchronously
		}
		else
		{
			// Operation failed
			op->err.setMsg(getLastWin32ErrorMsg());
			switch(err)
			{
			case WSAENOTCONN: // Socket was not connected at all;
				op->err.code = Error::Code::Other;
				break;
			case WSAENOBUFS:
				op->err.code = Error::Code::Other;
				m_state = details::SocketState::Disconnected;
				break;
			default: // Any other error we just consider as disconnection
				op->err.code = Error::Code::Other;
				m_state = details::SocketState::Disconnected;
			}
			// Manually queue the handler
			auto res = PostQueuedCompletionStatus(
				m_iocp.getHandle(), // CompletionPort
				0, // dwNumberOfBytesTransfered
				0, // CompletionKey
				&op->overlapped // lpOverlapped
				);
			CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		}
	}
	else
	{
		CZ_UNEXPECTED();
	}

	m_iocp.add(std::move(op));
}

int TCPSocket::sendSome(const void* data, int size, Error& ec)
{
	// NOTE: It's ok if size==0, since ::send will just treat it as a succefull send

	int sent = ::send(m_socket.get(), (const char*)data, size, 0);
	if (sent != SOCKET_ERROR)
	{
		ec = Error();
		return sent;
	}

	int err = WSAGetLastError();
	// This means we are sending data too fast, and the OS doesn't have any spare buffer space.
	// In this case we don't consider it an error, and just tell the caller it sent 0 bytes
	if (err == WSAENOBUFS || err == WSAEWOULDBLOCK)
	{
		ec = Error(Error::Code::NoResources);
		return 0;
	}
	else
	{
		ec = Error(Error::Code::Other, getLastWin32ErrorMsg());
		return 0;
	}
}

int TCPSocket::send(void* data, int size, unsigned timeoutMs, Error& ec)
{
	int done = 0;
	FD_SET set;

	if (timeoutMs == 0)
		return sendSome(data, size, ec);

	TIMEVAL t;
	t.tv_sec = timeoutMs / 1000;
	t.tv_usec = (timeoutMs % 1000) * 1000;

	while (true)
	{
		FD_ZERO(&set);
		FD_SET(m_socket.get(), &set);
		int res = select(0, NULL, &set, NULL, timeoutMs==0xFFFFFFFF ? NULL : &t);
		if (res == 1)
		{
			done += sendSome((const char*)data + done, size - done, ec);
			if (done == size || ec)
				return done;
		}
		else if (res==SOCKET_ERROR)
		{
			ec = Error(Error::Code::Other, getLastWin32ErrorMsg());
			return done;
		}
		else if (res==0) // timeout expired
		{
			ec = Error(Error::Code::Timeout, "");
			return done;
		}
		else
		{
			CZ_UNEXPECTED();
		}
	}

	return done;
}

int TCPSocket::receiveSome(void* data, int size, Error& ec)
{
	if (size == 0)
	{
		ec = Error();
		return 0;
	}

	int received = ::recv(m_socket.get(), (char*)data, size, 0);

	if (received != SOCKET_ERROR) // Data received
	{
		ec = Error();
		return received;
	}

	if (received==0) // Connection was gracefully closed
	{
		ec = Error(Error::Code::ConnectionClosed);
		return 0;
	}

	CZ_ASSERT(received == SOCKET_ERROR);
	int err = WSAGetLastError();
	if (err == WSAENOBUFS || err == WSAEWOULDBLOCK)
	{
		CZ_UNEXPECTED(); // Not sure we can ever get this
		ec = Error();
		return 0;
	}
	else
	{
		ec = Error(Error::Code::Other, getLastWin32ErrorMsg());
		return 0;
	}
}

int TCPSocket::receive(void* data, int size, unsigned timeoutMs, Error& ec)
{
	int done = 0;
	FD_SET set;

	if (timeoutMs == 0)
		return receiveSome(data, size, ec);

	TIMEVAL t;
	t.tv_sec = timeoutMs / 1000;
	t.tv_usec = (timeoutMs % 1000) * 1000;

	while (true)
	{
		FD_ZERO(&set);
		FD_SET(m_socket.get(), &set);
		int res = select(0, &set, NULL, NULL, timeoutMs==0xFFFFFFFF ? NULL : &t);
		if (res == 1)
		{
			done += receiveSome((char*)data + done, size - done, ec);
			if (done == size || ec)
				return done;
		}
		else if (res==SOCKET_ERROR)
		{
			ec = Error(Error::Code::Other, getLastWin32ErrorMsg());
			return done;
		}
		else if (res==0) // timeout expired
		{
			ec = Error(Error::Code::Timeout, "");
			return done;
		}
		else
		{
			CZ_UNEXPECTED();
		}
	}

	return done;

}

} // namespace net


} // namespace cz
