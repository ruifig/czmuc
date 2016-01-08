#include "czlibPCH.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
#include "crazygaze/Json.h"
#include "crazygaze/Logging.h"
#include "crazygaze/StringUtils.h"

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

	struct AsyncAcceptData
	{
		enum
		{
			AddrLen = sizeof(sockaddr_in) + 16
		};
		char outputBuffer[2 * AddrLen];
		CompletionHandler handler;
		TCPSocket* socket = nullptr;
	};

	struct AsyncReceiveData
	{
		std::unique_ptr<char[]> buf;
		int capacity;
		CompletionHandler handler;
	};

	struct AsyncSendData
	{
		std::vector<char> buf;
		CompletionHandler handler;
	};

} // namespace details

//////////////////////////////////////////////////////////////////////////
//
// Utility code
//
//////////////////////////////////////////////////////////////////////////

static const char* getLastWin32ErrorMsg(int err = 0)
{
	DWORD errCode = (err == 0) ? GetLastError() : err;
	thread_local char buf[512];
	// http://msdn.microsoft.com/en-us/library/ms679351(VS.85).aspx
	int size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,  // use windows internal message table
							  0,						   // 0 since source is internal message table
							  errCode,					   // this is the error code returned by WSAGetLastError()
							  // Could just as well have been an error code from generic
							  // Windows errors from GetLastError()
							  0,		// auto-determine language to use
							  &buf[0],  // this is WHERE we want FormatMessage
							  sizeof(buf),
							  0);  // 0, since getting message from system tables
	return buf;
}

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
// SocketAddress
//
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
//
// Shared Data
//
//////////////////////////////////////////////////////////////////////////


struct TCPServerSocketData
{
	explicit TCPServerSocketData(CompletionPort& iocp) : iocp(iocp)
	{
	}
	virtual ~TCPServerSocketData()
	{
	}
	CompletionPort& iocp;
	SocketWrapper listenSocket;
	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
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

struct TCPSocketData
{
	CompletionPort& iocp;
	SocketWrapper socket;
	SocketState state = SocketState::None;
	Future<bool> connectingFt;
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

//////////////////////////////////////////////////////////////////////////
//
// TCPServerSocket
//
//////////////////////////////////////////////////////////////////////////

TCPServerSocket::TCPServerSocket(CompletionPort& iocp, int listenPort)
{
	LOG("TCPServerSocket %p: Enter\n", this);
	m_data = std::make_shared<TCPServerSocketData>(iocp);
	m_data->listenSocket = SocketWrapper(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
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

	if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_data->listenSocket.get()), m_data->iocp.getHandle(),
							   (ULONG_PTR) this, 0) == NULL)
	{
		CZ_LOG(logDefault, Fatal, "Error initializing listen socket: %s", getLastWin32ErrorMsg());
	}

	debugData.serverSocketCreated(this);
	LOG("TCPServerSocket %p: Exit\n", this);
}

TCPServerSocket::~TCPServerSocket()
{
	LOG("~TCPServerSocket %p: Enter\n", this);
	CancelIoEx(reinterpret_cast<HANDLE>(m_data->listenSocket.get()), NULL);
	debugData.serverSocketDestroyed(this);
	LOG("~TCPServerSocket %p: Exit\n", this);
}


void TCPServerSocket::asyncAccept(TCPSocket& socket, CompletionHandler handler)
{
	DWORD dwBytes;

	auto data = std::make_shared<details::AsyncAcceptData>();
	data->handler = std::move(handler);
	data->socket = &socket;

	auto op = std::make_unique<CompletionPortOperation>([data,this](unsigned bytesTransfered)
	{
		sockaddr* localSockAddr = NULL;
		int localSockAddrSize;
		sockaddr* remoteSockAddr = NULL;
		int remoteSockAddrSize;
		m_data->lpfnGetAcceptExSockaddrs(&data->outputBuffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &localSockAddr,
									   &localSockAddrSize, &remoteSockAddr, &remoteSockAddrSize);
		LOG("New connection:\n\tlocal = %s\n\tremote = %s\n", getAddr(localSockAddr), getAddr(remoteSockAddr));
		data->socket->m_data->localAddr = SocketAddress(*localSockAddr);
		data->socket->m_data->remoteAddr = SocketAddress(*remoteSockAddr);
		data->socket->m_data->state = SocketState::Connected;
		data->handler(bytesTransfered);
	});

	auto res = m_data->lpfnAcceptEx(
		m_data->listenSocket.get(), // sListenSocket
		socket.m_data->socket.get(), // sAcceptSocket
		&data->outputBuffer, // lpOutputBuffer
		0, // dwReceiveDataLength
		details::AsyncAcceptData::AddrLen, // dwLocalAddressLength
		details::AsyncAcceptData::AddrLen, // dwRemoteAddressLength
		&dwBytes /*Unused for asynchronous AcceptEx*/,
		&op->overlapped
		);

	int err = WSAGetLastError();
	// In our case, since the AcceptEx is asynchronous, it should return FALSE and give an error of "PENDING"
	if (!(res == FALSE && err == ERROR_IO_PENDING))
	{
		CZ_LOG(logDefault, Fatal, "Error calling AcceptEx: %s", getLastWin32ErrorMsg());
	}

	socket.m_data->state = SocketState::WaitingAccept;
	m_data->iocp.add(std::move(op));
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
	m_data = std::make_shared<TCPSocketData>(this, iocp);

	m_data->socket = SocketWrapper(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
	if (!m_data->socket.isValid())
	{
		CZ_LOG(logDefault, Fatal, "Error creating client socket: %s", getLastWin32ErrorMsg());
	}

	if (CreateIoCompletionPort((HANDLE)m_data->socket.get(), m_data->iocp.getHandle(),
							   (ULONG_PTR)this, 0) == NULL)
	{
		CZ_LOG(logDefault, Fatal, "Error initializing accept socket: %s", getLastWin32ErrorMsg());
	}

	// This disables IOCP notification if the request completes immediately at the point of call.
	/*
	if (SetFileCompletionNotificationModes((HANDLE)m_socket.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)==FALSE)
		throw std::runtime_error(formatString("Error initializing accept socket: %s", getLastWin32ErrorMsg()));
	*/
	LOG("TCPSocket %p: Exit\n", this);
}

TCPSocket::~TCPSocket()
{
	shutdown();
}

void TCPSocket::shutdown()
{
	LOG("~TCPSocket %p: Enter\n", this);

	// If we are connecting, there is an async operation in fly which reference us, so we need for it to finish
	if (m_data->connectingFt.valid())
		m_data->connectingFt.get();

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

	m_userData.reset();
	LOG("~TCPSocket %p: Exit\n", this);
}

Future<bool> TCPSocket::connect(const std::string& ip, int port)
{
	CZ_ASSERT(m_data->state == SocketState::None);

	LOG("%p: state_Connecting: addr=%s:%d\n", this, ip.c_str(), port);
	CZ_ASSERT(m_data->state == SocketState::None);
	m_data->state = SocketState::Connecting;

	m_data->connectingFt = cz::async([this, ip, port]
	{
		SocketAddress remoteAddr(ip.c_str(), port);
		sockaddr_in addr;
		ZeroMemory(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = remoteAddr.ip.full;
		addr.sin_port = htons(remoteAddr.port);
		auto res =
			WSAConnect(m_data->socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);

		if (res == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			CZ_LOG(logDefault, Log, "Could not connect. Error '%s'", getLastWin32ErrorMsg(err));
			m_data->state = SocketState::Disconnected;
			return false;
		}

		sockaddr_in localinfo;
		int size = sizeof(localinfo);
		if (getsockname(m_data->socket.get(), (SOCKADDR*)&localinfo, &size) == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			CZ_LOG(logDefault, Log, "Could not get address information. Error '%s'", getLastWin32ErrorMsg(err));
			m_data->state = SocketState::Disconnected;
			return false;
		}

		m_data->localAddr = SocketAddress(localinfo);
		m_data->remoteAddr = remoteAddr;
		m_data->state = SocketState::Connected;
		return true;
	});

	return m_data->connectingFt;
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

bool TCPSocket::asyncReceive(std::unique_ptr<char[]> buf, int capacity, CompletionHandler handler)
{
	if (m_data->state != SocketState::Connected)
		return false;

	// According to the documentation for WSARecv, it is safe to have the WSABUF on the stack, since it will copied
	int const numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	memset(&wsabufs, 0, sizeof(wsabufs));
	wsabufs[0].buf = buf.get();
	wsabufs[0].len = capacity;


	// Note: Need to wrap the buffer and handler with our own stuff, since we need to keep things alive until the
	// handler is executed
	auto data = std::make_shared<details::AsyncReceiveData>();
	data->handler = std::move(handler);
	data->buf = std::move(buf);
	data->capacity = capacity;
	auto op = std::make_unique<CompletionPortOperation>(
		[data=std::move(data)](unsigned bytesTransfered)
	{
		data->handler(bytesTransfered);
	});

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
			// Expected
		}
		else  // Any other error, we close this connection
		{
			m_data->state = SocketState::Disconnected;
			CZ_LOG(logDefault, Log, "%s failed with '%s'", __FUNCTION__, getLastWin32ErrorMsg(err));
			return false;
		}
	}

	m_data->iocp.add(std::move(op));
	return true;
}

bool TCPSocket::asyncSend(std::vector<char> buf, CompletionHandler handler)
{
	if (m_data->state != SocketState::Connected)
		return false;

	LOG("%p: Sending %d bytes\n", this, toSend);

	auto data = std::make_shared<details::AsyncSendData>();
	data->buf = std::move(buf);
	data->handler = std::move(handler);

	// WSABUF is safe to keep on the stack, according to the documentation
	const int numbuffers = 1;
	WSABUF wsabufs[numbuffers];
	wsabufs[0].buf = data->buf.data();
	wsabufs[0].len = static_cast<DWORD>(data->buf.size());

	auto op = std::make_unique<CompletionPortOperation>([data=std::move(data)](unsigned bytesTransfered)
	{
		data->handler(bytesTransfered);
	});

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
			return false;
		}
	}
	else
	{
		CZ_UNEXPECTED();
	}

	m_data->iocp.add(std::move(op));
	return true;
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
		shutdown(m_socket, SD_BOTH);
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

} // namespace net

std::string to_json(const net::SocketAddress& val)
{
	return to_json(val.toString(true));
}

} // namespace cz
