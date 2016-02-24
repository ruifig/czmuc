#include "czlibPCH.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
#include "crazygaze/Json.h"
#include "crazygaze/Logging.h"
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
namespace net
{

namespace details
{
	WSAInstance::WSAInstance()
	{
		WORD wVersionRequested = MAKEWORD(2, 2);
		WSADATA wsaData;
		int err = WSAStartup(wVersionRequested, &wsaData);
		if (err != 0)
			throw std::runtime_error(getLastWin32ErrorMsg());
		if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
		{
			WSACleanup();
			throw std::runtime_error("Could not find a usable version of Winsock.dll");
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
		if (m_socket != INVALID_SOCKET)
		{
			::shutdown(m_socket, SD_BOTH);
			closesocket(m_socket);
		}
	}

	void SocketWrapper::shutdown()
	{
		if (m_socket != INVALID_SOCKET)
		{
			::shutdown(m_socket, SD_BOTH);
			closesocket(m_socket);
		}
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
		m_socket = other.m_socket;
		other.m_socket = INVALID_SOCKET;
		return *this;
	}


} // namespace details

//////////////////////////////////////////////////////////////////////////
//
// Utility code
//
//////////////////////////////////////////////////////////////////////////

// #TODO Delete these when not needed
static std::vector<std::string> logs;
void logStr(const char* str)
{
	static std::mutex mtx;
	std::lock_guard<std::mutex> lk(mtx);
	logs.push_back(str);
}

#ifndef NDEBUG
	//#define LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
	//#define LOG(fmt, ...) logStr(formatString(fmt, ##__VA_ARGS__))
	#define LOG(...) ((void)0)
#else
	#define LOG(...) ((void)0)
#endif

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

//
// std::priority_queue doesn't allow moving out elements, since top() returns a const&
// Casting out the const is ok, if the element is removed right after moving it, to avoid calling the
// comparator.
template <typename Q, typename T>
static bool movepop(Q& q, T& dst)
{
	if (q.size())
	{
		dst = std::move(const_cast<T&>(q.top()));
		q.pop();
		return true;
	}
	else
	{
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////
//
// Shared Data
//
//////////////////////////////////////////////////////////////////////////

// NOTE: The order of the enums needs to be the order things happen with typical use
enum class SocketState
{
	None,
	WaitingAccept,  // When being used on the server
	Connecting,		// When it's a client connecting to a server
	Connected,
	Disconnected
};


struct TCPServerSocketData
{
	explicit TCPServerSocketData(CompletionPort& iocp) : iocp(iocp)
	{
	}
	virtual ~TCPServerSocketData()
	{
	}
	CompletionPort& iocp;
	SocketState state = SocketState::None;
	details::SocketWrapper listenSocket;
	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
};

struct TCPSocketData
{
	CompletionPort& iocp;
	details::SocketWrapper socket;
	SocketState state = SocketState::None;
	SocketAddress localAddr;
	SocketAddress remoteAddr;
	TCPSocket* owner;
	TCPSocketData(TCPSocket* owner, CompletionPort& iocp);
	virtual ~TCPSocketData();
};

//////////////////////////////////////////////////////////////////////////
//
// Shared Data
//
//////////////////////////////////////////////////////////////////////////

TCPSocketData::TCPSocketData(TCPSocket* owner, CompletionPort& iocp)
	: iocp(iocp)
	, owner(owner)
{
}
TCPSocketData::~TCPSocketData()
{
}

struct AsyncAcceptOperation : public CompletionPortOperation
{
	AsyncAcceptOperation(TCPServerSocket* owner) : owner(owner) { }
	virtual void execute(unsigned bytesTransfered, uint64_t completionKey) override
	{
		owner->execute(this, bytesTransfered, completionKey);
	}

	enum
	{
		AddrLen = sizeof(sockaddr_in) + 16
	};
	TCPServerSocket* owner;
	SocketCompletionHandler handler;
	char outputBuffer[2 * AddrLen];
	TCPSocket* socket = nullptr;
};

struct AsyncReceiveOperation : public CompletionPortOperation
{
	AsyncReceiveOperation(TCPSocket* owner) : owner(owner)
	{
	}
	virtual ~AsyncReceiveOperation()
	{
	}
	virtual void execute(unsigned bytesTransfered, uint64_t completionKey) override
	{
		owner->execute(this, bytesTransfered, completionKey);
	}
	TCPSocket* owner;
	SocketCompletionHandler handler;
	Error err;
	Buffer buf;
};

struct AsyncSendOperation : public CompletionPortOperation
{
	AsyncSendOperation(TCPSocket* owner) : owner(owner) { }
	virtual void execute(unsigned bytesTransfered, uint64_t completionKey) override
	{
		owner->execute(this, bytesTransfered, completionKey);
	}
	TCPSocket* owner;
	SocketCompletionHandler handler;
	Error err;
	Buffer buf;
};
//////////////////////////////////////////////////////////////////////////
//
// TCPServerSocket
//
//////////////////////////////////////////////////////////////////////////

TCPServerSocket::TCPServerSocket(CompletionPort& iocp, int listenPort)
{
	LOG("TCPServerSocket %p: Enter\n", this);
	m_data = std::make_shared<TCPServerSocketData>(iocp);
	m_data->listenSocket = details::SocketWrapper(
		WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
	if (!m_data->listenSocket.isValid())
		CZ_LOG(logDefault, Fatal, "Error creating listen socket: %s", getLastWin32ErrorMsg());

	// Set this, so no other applications can bind to the same port
	int iOptval = 1;
	if (setsockopt(m_data->listenSocket.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&iOptval, sizeof(iOptval)) ==
		SOCKET_ERROR)
		throw std::runtime_error(formatString("Error setting SO_EXCLUSIVEADDRUSE: %s", getLastWin32ErrorMsg()));

	sockaddr_in serverAddress;
	ZeroMemory(&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(listenPort);

	if (bind(m_data->listenSocket.get(), (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR ||
		listen(m_data->listenSocket.get(), SOMAXCONN) == SOCKET_ERROR)
	{
		CZ_LOG(logDefault, Fatal, "Error initializing listen socket: %s", getLastWin32ErrorMsg());
	}

	// Load the AcceptEx function into memory using WSAIoctl.
	// The WSAIoctl function is an extension of the ioctlsocket()
	// function that can use overlapped I/O. The function's 3rd
	// through 6th parameters are input and output buffers where
	// we pass the pointer to our AcceptEx function. This is used
	// so that we can call the AcceptEx function directly, rather
	// than refer to the Mswsock.lib library.
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD dwBytes;
	int res1 = WSAIoctl(m_data->listenSocket.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx),
						&m_data->lpfnAcceptEx, sizeof(m_data->lpfnAcceptEx), &dwBytes, NULL, NULL);
	int res2 = WSAIoctl(m_data->listenSocket.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockaddrs,
						sizeof(GuidGetAcceptExSockaddrs), &m_data->lpfnGetAcceptExSockaddrs,
						sizeof(m_data->lpfnGetAcceptExSockaddrs), &dwBytes, NULL, NULL);
	if (res1 == SOCKET_ERROR || res2 == SOCKET_ERROR)
		throw std::runtime_error(formatString("Error initializing listen socket: %s", getLastWin32ErrorMsg()));

	if (CreateIoCompletionPort(
			reinterpret_cast<HANDLE>(m_data->listenSocket.get()), // FileHandle
			m_data->iocp.getHandle(), // ExistingCompletionPort
			(ULONG_PTR)0, // CompletionKey
			0 // NumberOfConcurrentThreads
		) == NULL)
	{
		CZ_LOG(logDefault, Fatal, "Error initializing listen socket: %s", getLastWin32ErrorMsg());
	}

	m_data->state = SocketState::Connected; // #TODO : Change this to state Listen
	debugData.serverSocketCreated(this);
	LOG("TCPServerSocket %p: Exit\n", this);
}

TCPServerSocket::~TCPServerSocket()
{
	LOG("~TCPServerSocket %p: Enter\n", this);
	shutdown();
	debugData.serverSocketDestroyed(this);
	LOG("~TCPServerSocket %p: Exit\n", this);
}

void TCPServerSocket::shutdown()
{

	if (m_data->state == SocketState::Disconnected)
		return;

	// Notes:
	// CancelIo - Cancels I/O operations that are issued by calling thread
	// CancelIoEx Cancels all I/O regardless of the thread that created the I/O operation.
	auto res = CancelIoEx(
		(HANDLE)m_data->listenSocket.get(), // hFile
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
			CZ_LOG(logDefault, Warning, "CancelIoEx failed: %d:%s", err, getLastWin32ErrorMsg(err));
		}
	}

	m_data->listenSocket.shutdown();
	m_data->state = SocketState::Disconnected;
}

Error TCPServerSocket::accept(TCPSocket& socket, unsigned timeoutMs)
{
	FD_SET set;
	TIMEVAL t;
	t.tv_sec = timeoutMs / 1000;
	t.tv_usec = (timeoutMs % 1000) * 1000;

	FD_ZERO(&set);
	FD_SET(m_data->listenSocket.get(), &set);
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
		SOCKET s = ::accept(m_data->listenSocket.get(), NULL, NULL);
		// Since select passed, accept should not fail
		CZ_ASSERT(s != INVALID_SOCKET);
		socket.init(s, socket.getIOCP());
		socket.fillLocalAddr();
		socket.fillRemoteAddr();
		return Error(Error::Code::Success);
	}
}

void TCPServerSocket::asyncAccept(TCPSocket& socket, SocketCompletionHandler handler)
{
	DWORD dwBytes;

	auto op = std::make_unique<AsyncAcceptOperation>(this);
	op->handler = std::move(handler);
	op->socket = &socket;

	auto res = m_data->lpfnAcceptEx(
		m_data->listenSocket.get(), // sListenSocket
		socket.m_data->socket.get(), // sAcceptSocket
		&op->outputBuffer, // lpOutputBuffer
		0, // dwReceiveDataLength
		AsyncAcceptOperation::AddrLen, // dwLocalAddressLength
		AsyncAcceptOperation::AddrLen, // dwRemoteAddressLength
		&dwBytes /*Unused for asynchronous AcceptEx*/,
		&op->overlapped
		);

	int err = WSAGetLastError();
	if (!(res == FALSE && err == ERROR_IO_PENDING))
	{
		CZ_LOG(logDefault, Fatal, "Error calling AcceptEx: %s", getLastWin32ErrorMsg());
	}

	socket.m_data->state = SocketState::WaitingAccept;
	m_data->iocp.add(std::move(op));
}

void TCPServerSocket::execute(struct AsyncAcceptOperation* op, unsigned bytesTransfered, uint64_t completionKey)
{
	if (completionKey!=0)
	{
		// Implement this
		CZ_UNEXPECTED();
	}

	sockaddr* localSockAddr = NULL;
	int localSockAddrSize;
	sockaddr* remoteSockAddr = NULL;
	int remoteSockAddrSize;
	m_data->lpfnGetAcceptExSockaddrs(&op->outputBuffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &localSockAddr,
		&localSockAddrSize, &remoteSockAddr, &remoteSockAddrSize);
	LOG("New connection:\n\tlocal = %s\n\tremote = %s\n", getAddr(localSockAddr), getAddr(remoteSockAddr));
	op->socket->m_data->localAddr = SocketAddress(*localSockAddr);
	op->socket->m_data->remoteAddr = SocketAddress(*remoteSockAddr);
	op->socket->m_data->state = SocketState::Connected;
	op->handler(Error(), bytesTransfered);
}

//////////////////////////////////////////////////////////////////////////
//
// TCPSocket
//
//////////////////////////////////////////////////////////////////////////

TCPSocket::TCPSocket(CompletionPort& iocp)
{
	LOG("TCPSocket %p: Enter\n", this);
	debugData.socketCreated(this);
	init(INVALID_SOCKET, iocp);
	LOG("TCPSocket %p: Exit\n", this);
}

void TCPSocket::init(SOCKET s, CompletionPort& iocp)
{
	m_data = std::make_shared<TCPSocketData>(this, iocp);
	m_data->socket = details::SocketWrapper(
		(s==INVALID_SOCKET) ? WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED) : s);
	if (!m_data->socket.isValid())
	{
		CZ_LOG(logDefault, Fatal, "Error creating client socket: %s", getLastWin32ErrorMsg());
	}

	if (CreateIoCompletionPort(
			(HANDLE)m_data->socket.get(), // FileHandle
			m_data->iocp.getHandle(), // ExistingCompletionPort
			(ULONG_PTR)0, // CompletionKey
			0 // NumberOfConcurrentThreads
		) == NULL) 
	{
		CZ_LOG(logDefault, Fatal, "Error initializing accept socket: %s", getLastWin32ErrorMsg());
	}

	// Set non-blocking, so sendSome/receiveSome doesn't block forever
	setBlocking(false);

	// This disables IOCP notification if the request completes immediately at the point of call.
	/*
	if (SetFileCompletionNotificationModes((HANDLE)m_socket.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)==FALSE)
		throw std::runtime_error(formatString("Error initializing accept socket: %s", getLastWin32ErrorMsg()));
	*/
}

TCPSocket::~TCPSocket()
{
	LOG("~TCPSocket %p: Enter\n", this);
	shutdown();
	LOG("~TCPSocket %p: Exit\n", this);
}

void TCPSocket::setBlocking(bool blocking)
{
	 // 0: Blocking. !=0 : Non-blocking
	u_long mode = blocking ? 0 : 1;
	int res = ioctlsocket(m_data->socket.get(), FIONBIO, &mode);
	if (res != 0)
	{
		CZ_LOG(logDefault, Fatal, "Error set non-blocking mode: %s", getLastWin32ErrorMsg());
	}

}

void TCPSocket::shutdown()
{
	if (m_data->state == SocketState::Disconnected)
		return;

	// Notes:
	// CancelIo - Cancels I/O operations that are issued by calling thread
	// CancelIoEx Cancels all I/O regardless of the thread that created the I/O operation.
	auto res = CancelIoEx(
		(HANDLE)m_data->socket.get(), // hFile
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
			CZ_LOG(logDefault, Warning, "CancelIoEx failed: %d:%s", err, getLastWin32ErrorMsg(err));
		}
	}

	debugData.socketDestroyed(this);

	m_data->socket.shutdown();
	m_data->state = SocketState::Disconnected;
	m_userData.reset();
}

Error TCPSocket::fillLocalAddr()
{
	sockaddr_in addr;
	int size = sizeof(addr);
	if (getsockname(m_data->socket.get(), (SOCKADDR*)&addr, &size) == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logDefault, Error, "Could not get address information. Error '%s'", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	m_data->localAddr = SocketAddress(addr);
	return Error();
}

Error TCPSocket::fillRemoteAddr()
{
	sockaddr_in addr;
	int size = sizeof(addr);
	if (getpeername(m_data->socket.get(), (SOCKADDR*)&addr, &size) == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logDefault, Error, "Could not get address information. Error '%s'", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	m_data->remoteAddr = SocketAddress(addr);
	return Error();
}

Error TCPSocket::connect(const std::string& ip, int port)
{
	CZ_ASSERT(m_data->state == SocketState::None);
	LOG("%p: state_Connecting: addr=%s:%d\n", this, ip.c_str(), port);

	SocketAddress remoteAddr(ip.c_str(), port);
	m_data->remoteAddr = remoteAddr;
	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = remoteAddr.ip.full;
	addr.sin_port = htons(remoteAddr.port);

	setBlocking(true);
	SCOPE_EXIT{ setBlocking(false); };
	auto res = WSAConnect(m_data->socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);

	if (res == SOCKET_ERROR)
	{
		auto errMsg = getLastWin32ErrorMsg(WSAGetLastError());
		CZ_LOG(logDefault, Warning, "Could not connect: %s", errMsg);
		return Error(Error::Code::Other, errMsg);
	}

	Error ec = fillLocalAddr();
	if (ec)
		return std::move(ec);

	m_data->state = SocketState::Connected;
	return Error();
}

struct AsyncConnectOperation : public CompletionPortOperation
{
	AsyncConnectOperation(TCPSocket* owner) : owner(owner) { }
	virtual void execute(unsigned bytesTransfered, uint64_t completionKey) override
	{
		owner->execute(this, bytesTransfered, completionKey);
	}
	TCPSocket* owner;
	SocketCompletionHandler handler;
	Error err;
};

void TCPSocket::asyncConnect(const std::string& ip, int port, SocketCompletionHandler handler)
{
	CZ_ASSERT(m_data->state == SocketState::None);
	LOG("%p: state_Connecting: addr=%s:%d\n", this, ip.c_str(), port);

	//
	// Get ConnectEx pointer
	LPFN_CONNECTEX lpfnConnectEx = NULL;
	{
		GUID guid = WSAID_CONNECTEX;
		DWORD dwBytes = 0;
		int res = WSAIoctl(
			m_data->socket.get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
			&lpfnConnectEx, sizeof(lpfnConnectEx), &dwBytes, NULL, NULL);

		if (res == SOCKET_ERROR)
		{
			CZ_LOG(logDefault, Fatal, "Error getting pointer for ConnectEx : %s", getLastWin32ErrorMsg());
			return;
		}
	}

	// ConnectEx required the socket to be bound
	{
		sockaddr_in addr;
		ZeroMemory(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = 0;
		if (bind(m_data->socket.get(), (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			CZ_LOG(logDefault, Fatal, "Error binding socket: %s", getLastWin32ErrorMsg());
			return;
		}
	}

	m_data->state = SocketState::Connecting;

	auto op = std::make_unique<AsyncConnectOperation>(this);
	op->handler = std::move(handler);

	// Connect
	SocketAddress remoteAddr(ip.c_str(), port);
	m_data->remoteAddr = remoteAddr;
	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = remoteAddr.ip.full;
	addr.sin_port = htons(remoteAddr.port);
	BOOL res = lpfnConnectEx(m_data->socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, &op->overlapped);
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
		m_data->iocp.post(std::move(op), 0, 0);
		return;
	}

	m_data->iocp.add(std::move(op));
}

void TCPSocket::execute(struct AsyncConnectOperation* op, unsigned bytesTransfered, uint64_t completionKey)
{
	int seconds;
	int bytes = sizeof(seconds);
	int res = getsockopt(m_data->socket.get(), SOL_SOCKET, SO_CONNECT_TIME, (char*)&seconds, (PINT)&bytes);

	if (res!=NO_ERROR || seconds==-1)
	{
		op->err = Error(Error::Code::Other, "Connect failed");
	}
	else
	{
		m_data->state = SocketState::Connected;
	}

	op->handler(op->err, bytesTransfered);
}

const SocketAddress& TCPSocket::getLocalAddress() const
{
	return m_data->localAddr;
}

const SocketAddress& TCPSocket::getRemoteAddress() const
{
	return m_data->remoteAddr;
}

CompletionPort& TCPSocket::getIOCP()
{
	return m_data->iocp;
}

void TCPSocket::asyncReceiveSome(Buffer buf, SocketCompletionHandler handler)
{
	// According to the documentation for WSARecv, it is safe to have the WSABUF on the stack, since it will copied
	int const numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	memset(&wsabufs, 0, sizeof(wsabufs));
	wsabufs[0].buf = buf.ptr;
	wsabufs[0].len = static_cast<DWORD>(buf.size);

	auto op = std::make_unique<AsyncReceiveOperation>(this);
	op->handler = std::move(handler);
	op->buf = buf;

	DWORD rcvFlags = 0;
	DWORD bytesTransfered = 0;
	int res = WSARecv(
		m_data->socket.get(), // Socket
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
				op->err.code = Error::Code::Other;
				m_data->state = SocketState::Disconnected;
				break;
			default: // Any other error we just consider as disconnection
				op->err.code = Error::Code::Other;
				m_data->state = SocketState::Disconnected;
			}
			// Manually queue the handler
			auto res = PostQueuedCompletionStatus(
				m_data->iocp.getHandle(), // CompletionPort
				0, // dwNumberOfBytesTransfered
				1, // CompletionKey
				&op->overlapped // lpOverlapped
				);
			CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		}
	}

	m_data->iocp.add(std::move(op));
}



void TCPSocket::prepareRecvUntil(std::shared_ptr<char> tmpbuf, int tmpBufSize,
                                 std::pair<RingBuffer::Iterator, bool> res, RingBuffer& buf,
                                 SocketCompletionUntilHandler untilHandler, SocketCompletionHandler handler)
{
	asyncReceiveSome(
		Buffer(tmpbuf.get(), tmpBufSize),
		[this, tmpbuf, tmpBufSize, res, &buf,
#if CZ_RINGBUFFER_DEBUG
		bufReadCounter = buf.getReadCounter(),
#endif
		untilHandler=std::move(untilHandler), handler=std::move(handler)](const Error& err, unsigned bytesTransfered) mutable
	{

#if CZ_RINGBUFFER_DEBUG
		CZ_ASSERT_F(bufReadCounter == buf.getReadCounter(), "You Should not perform any read operations on the buffer while there are pending read-until operations");
#endif

		if (bytesTransfered==0)
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

void TCPSocket::asyncReceiveUntil(RingBuffer& buf, SocketCompletionUntilHandler untilHandler,
                                  SocketCompletionHandler handler, int tmpBufSize)
{
	auto tmpbuf = make_shared_array<char>(tmpBufSize);
	auto res = std::make_pair<RingBuffer::Iterator, bool>(buf.begin(), false);

	if (buf.getUsedSize()==0)
	{
		prepareRecvUntil(tmpbuf, tmpBufSize, res, buf, std::move(untilHandler), std::move(handler));
	}
	else
	{
		// If the buffer is not empty, then we manually queue the operation.
		// This deals with the case where the buffer already has enough data to match the condition.
		auto op = std::make_unique<AsyncReceiveOperation>(this);
		op->handler = [
			this, tmpbuf, &buf, tmpBufSize, untilHandler=std::move(untilHandler), handler=std::move(handler),
			err = Error()
#if CZ_RINGBUFFER_DEBUG
			, bufReadCounter = buf.getReadCounter()
#endif
		](const Error& err, unsigned bytesTransfered)
		{

#if CZ_RINGBUFFER_DEBUG
			CZ_ASSERT_F(bufReadCounter == buf.getReadCounter(), "You Should not perform any read operations on the buffer while there are pending read-until operations");
#endif
			if (bytesTransfered == 0 || err)
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
		op->buf = Buffer(tmpbuf.get(), tmpBufSize);

		auto res = PostQueuedCompletionStatus(
			m_data->iocp.getHandle(), // CompletionPort
			buf.getUsedSize(), // dwNumberOfBytesTransfered
			0, // CompletionKey
			&op->overlapped // lpOverlapped
			);
		CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		m_data->iocp.add(std::move(op));
	}

}

void TCPSocket::execute(struct AsyncReceiveOperation* op, unsigned bytesTransfered, uint64_t completionKey)
{
	if (completionKey==0) // The operation was queued by WSARecv (or a manual handler triggered by asyncReceiveUntil)
	{
		if (bytesTransfered==0) // When a receive gives us 0 bytes, it means the peer disconnected
		{
			m_data->state = SocketState::Disconnected;
			if (!op->err) // Set the error if not set
				op->err = Error(Error::Code::Other, "Disconnected");
		}
	}
	else if (completionKey==1) // The operation failed, and we queued it manually
	{
		CZ_ASSERT(bytesTransfered == 0);
	}
	else
	{
		CZ_UNEXPECTED();
	}
	
	op->handler(op->err, bytesTransfered);
}

void TCPSocket::asyncSend(Buffer buf, SocketCompletionHandler handler)
{
	//printf("TCPSocket::asyncSend(%d bytes)\n", (int)buf.size);
	CZ_ASSERT(buf.ptr != nullptr);

	LOG("%p: Sending %d bytes\n", this, buf.capacity);

	auto op = std::make_unique<AsyncSendOperation>(this);
	op->handler = std::move(handler);
	op->buf = buf;

	// WSABUF is safe to keep on the stack, according to the documentation
	const int numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	wsabufs[0].buf = op->buf.ptr;
	wsabufs[0].len = static_cast<DWORD>(op->buf.size);

	int res = WSASend(
		m_data->socket.get(), // Socket
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
				m_data->state = SocketState::Disconnected;
				break;
			default: // Any other error we just consider as disconnection
				op->err.code = Error::Code::Other;
				m_data->state = SocketState::Disconnected;
			}
			// Manually queue the handler
			auto res = PostQueuedCompletionStatus(
				m_data->iocp.getHandle(), // CompletionPort
				0, // dwNumberOfBytesTransfered
				1, // CompletionKey
				&op->overlapped // lpOverlapped
				);
			CZ_ASSERT_F(res == TRUE, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
		}
	}
	else
	{
		CZ_UNEXPECTED();
	}

	m_data->iocp.add(std::move(op));
}

void TCPSocket::execute(struct AsyncSendOperation* op, unsigned bytesTransfered, uint64_t completionKey)
{
	if (completionKey==0) // Queued by WSASend
	{
	}
	else if (completionKey==1) // Queued manually, because WSASend failed
	{
		CZ_ASSERT(bytesTransfered == 0);
	}
	else
	{
		CZ_UNEXPECTED();
	}
	op->handler(op->err, bytesTransfered);
}

int TCPSocket::sendSome(const void* data, int size, Error& ec)
{
	int sent = ::send(m_data->socket.get(), (const char*)data, size, 0);
	if (sent != SOCKET_ERROR)
	{
		ec = Error();
		return sent;
	}

	int err = WSAGetLastError();
	if (err == WSAENOBUFS || err == WSAEWOULDBLOCK)
	{
		ec = Error();
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
		FD_SET(m_data->socket.get(), &set);
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

	int received = ::recv(m_data->socket.get(), (char*)data, size, 0);

	if (received != SOCKET_ERROR) // Data received
	{
		ec = Error();
		return received;
	}

	if (received==0) // Connection was gracefully closed
	{
		shutdown();
		return 0;
	}

	CZ_ASSERT(received == SOCKET_ERROR);
	int err = WSAGetLastError();
	if (err == WSAENOBUFS || err == WSAEWOULDBLOCK)
	{
		ec = Error();
		return 0;
	}
	else
	{
		shutdown();
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
		FD_SET(m_data->socket.get(), &set);
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

std::string to_json(const net::SocketAddress& val)
{
	return to_json(val.toString(true));
}

} // namespace cz
