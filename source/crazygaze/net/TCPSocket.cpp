#include "czlibPCH.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/Semaphore.h"
#include "crazygaze/net/details/TCPSocketDebug.h"
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

const char* SocketAddress::toString(bool includePort) const
{
	if (includePort)
		return formatString("%d.%d.%d.%d:%d", ip.bytes.b1, ip.bytes.b2, ip.bytes.b3, ip.bytes.b4, port);
	else
		return formatString("%d.%d.%d.%d", ip.bytes.b1, ip.bytes.b2, ip.bytes.b3, ip.bytes.b4);
}

void SocketAddress::constructFrom(const sockaddr_in* sa)
{
	ip.full = sa->sin_addr.s_addr;
	port = ntohs(sa->sin_port);
}

SocketAddress::SocketAddress(const sockaddr& addr)
{
	CZ_ASSERT(addr.sa_family == AF_INET);
	constructFrom((const sockaddr_in*)&addr);
}

SocketAddress::SocketAddress(const sockaddr_in& addr)
{
	CZ_ASSERT(addr.sin_family == AF_INET);
	constructFrom(&addr);
}

SocketAddress::SocketAddress(const char* ip_, int port_)
{
	inet_pton(AF_INET, ip_, &ip.full);
	port = port_;
}

SocketAddress::SocketAddress(const std::string& ip_, int port_)
{
	inet_pton(AF_INET, ip_.c_str(), &ip.full);
	port = port_;
}

//////////////////////////////////////////////////////////////////////////
//
// Shared Data
//
//////////////////////////////////////////////////////////////////////////

struct TCPBaseSocketData : public CompletionPortOperationBaseData
{
	explicit TCPBaseSocketData(CompletionPort& iocp) : CompletionPortOperationBaseData(iocp)
	{
	}
	virtual ~TCPBaseSocketData()
	{
	}

	uint32_t pendingReceivesCount;
	uint32_t pendingReceivesSize;
};

// Forward declaration
struct ReadOperation;

struct TCPServerSocketData : public TCPBaseSocketData
{
	explicit TCPServerSocketData(CompletionPort& iocp) : TCPBaseSocketData(iocp) {}
	virtual ~TCPServerSocketData()
	{
	}
	SocketWrapper listenSocket;
	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
	std::function<void(std::unique_ptr<TCPSocket>)> onAccept;
	bool shuttingDown = false;
	ZeroSemaphore pendingAcceptsCount;
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

struct TCPSocketData : public TCPBaseSocketData
{
	SocketWrapper socket;
	SocketState state = SocketState::None;
	std::shared_future<bool> connectingFt;
	SocketAddress localAddr;
	SocketAddress remoteAddr;
	TCPSocket* owner;

	std::vector<ReadOperation*> activeReadOperations;
	std::atomic_uint64_t pendingSendBytes = 0;

	std::function<void(const ChunkBuffer&)> onReceive;
	std::function<void(int, const std::string&)> onShutdown;
	std::function<void()> onSendCompleted;

	//
	// Members to keep track of data we received
	//
	struct Receive
	{
		// Putting the counter outside the ReadOperation object, to make it more cache friendly
		typedef std::pair<uint64_t, std::unique_ptr<ReadOperation>> PendingReadOperation;
		// This is used to help process out of order notifications for the WSARecv calls
		struct PendingReadOperationCompare
		{
			bool operator()(const PendingReadOperation& a, const PendingReadOperation& b)
			{
				return a.first > b.first;
			}
		};
		// Reads that Windows completed. We need to hold them, so we can can process out-of-order notifications
		// Deriving from priority_queue, so we can access "Container c".
		// This is required, so we can move the top element out of the queue, since we are storing unique_ptr elements, and
		// priority_queue::top returns a const_reference therefore having no way to move unique_ptr elements out of the queue.
		typedef std::priority_queue<PendingReadOperation, std::vector<PendingReadOperation>, PendingReadOperationCompare>
			ReadyReadsQueue;
		ReadyReadsQueue ready;

		// #TODO have to way to handle wrap around of the counter. Although it's very unlikely it will wrap around a full 64 bits
		// number
		uint64_t counter = 0;
		uint64_t lastDeliveredCounter = 0;
		ChunkBuffer buf;
	} rcv;

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//	OLD STUFF
	//////////////////////////////////////////////////////////////////////////
	struct
	{
		int code = 0;
		std::string msg;
	} disconnectInfo;

	//////////////////////////////////////////////////////////////////////////

	TCPSocketData(TCPSocket* owner, CompletionPort& iocp);
	virtual ~TCPSocketData();
	void state_WaitingAccept();
	void state_Connecting(const std::string& ip, int port);
	void state_Connected(const SocketAddress& local, const SocketAddress& remote);
	void state_Disconnected(int code, const std::string& reason);
	void addReadyRead(ReadOperation* op);
};

//////////////////////////////////////////////////////////////////////////
//
// Socket operations
//
//////////////////////////////////////////////////////////////////////////

struct AcceptOperation : public CompletionPortOperation
{
	std::unique_ptr<TCPSocket> acceptSocket;
	char outputBuffer[2 * (sizeof(sockaddr_in) + 16)];
	explicit AcceptOperation(std::shared_ptr<TCPServerSocketData> sharedData_);
	virtual ~AcceptOperation();
	virtual void onSuccess(unsigned bytesTransfered) override;
	virtual void onError() override;
	TCPServerSocketData* getSharedData();
	static void prepareAccept(const std::shared_ptr<TCPServerSocketData>& sharedData);
};

struct ReadOperation : public CompletionPortOperation
{
	WSABUF wsabuf;
	struct
	{
		std::unique_ptr<char[]> ptr;
		unsigned capacity;
		unsigned size = 0;  // Used space. This is filled once we receive the IOCP notification
	} buf;
	uint64_t counter;
	DWORD rcvFlags = 0;

	explicit ReadOperation(unsigned initialCapacity, std::shared_ptr<TCPSocketData> sharedData_);
	virtual ~ReadOperation();
	ReadOperation(ReadOperation&) = delete;
	ReadOperation& operator=(ReadOperation&) = delete;
	virtual void onSuccess(unsigned bytesTransfered) override;
	virtual void onError() override;
	virtual void destroy() override;
	TCPSocketData* getSharedData();

	static bool prepareRecv(const std::shared_ptr<TCPSocketData>& sharedData)
	{
		if (sharedData->state != SocketState::Connected)
			return false;

		auto operation = std::make_unique<ReadOperation>(sharedData->pendingReceivesSize, sharedData);
		DWORD bytesTransfered = 0;
		int res = WSARecv(sharedData->socket.get(), &operation->wsabuf, 1, NULL, &operation->rcvFlags, &operation->overlapped, NULL);
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
				sharedData->state_Disconnected(err, getLastWin32ErrorMsg(err));
				return false;
			}
		}

		operation.release();
		return true;
	}
};

struct WriteOperation : public CompletionPortOperation
{
	std::vector<WSABUF> wsabufs;
	ChunkBuffer buffer;
	uint64_t totalSize=0;
	explicit WriteOperation(ChunkBuffer buffer_, std::shared_ptr<TCPSocketData> sharedData_);
	virtual ~WriteOperation();
	WriteOperation(WriteOperation&) = delete;
	WriteOperation& operator=(WriteOperation&) = delete;
	virtual void onSuccess(unsigned bytesTransfered) override;
	virtual void onError() override;
	TCPSocketData* getSharedData();
};

//////////////////////////////////////////////////////////////////////////
//
// Shared Data
//
//////////////////////////////////////////////////////////////////////////

TCPSocketData::TCPSocketData(TCPSocket* owner, CompletionPort& iocp)
	: TCPBaseSocketData(iocp)
	, owner(owner)
{
	//printf("TCPSocketData: %p\n", this);
}
TCPSocketData::~TCPSocketData()
{
	//printf("~TCPSocketData: %p\n", this);
}
void TCPSocketData::state_WaitingAccept()
{
	LOG("%p: state_WaitingAccept:\n", this);
	CZ_ASSERT(state == SocketState::None);
	state = SocketState::WaitingAccept;
}

void TCPSocketData::state_Connecting(const std::string& ip, int port)
{
	LOG("%p: state_Connecting: addr=%s:%d\n", this, ip.c_str(), port);
	CZ_ASSERT(state == SocketState::None);
	state = SocketState::Connecting;

	SocketAddress remoteAddr(ip.c_str(), port);
	connectingFt = std::async(std::launch::async, [this, remoteAddr]
	{
		sockaddr_in addr;
		ZeroMemory(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = remoteAddr.ip.full;
		addr.sin_port = htons(remoteAddr.port);
		auto res =
			WSAConnect(socket.get(), (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);

		auto lk = this->lock();

		if (res == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			state_Disconnected(err, getLastWin32ErrorMsg(err));
			return false;
		}

		sockaddr_in localinfo;
		int size = sizeof(localinfo);
		if (getsockname(socket.get(), (SOCKADDR*)&localinfo, &size) == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			state_Disconnected(err, getLastWin32ErrorMsg(err));
			return false;
		}

		SocketAddress localAddr(localinfo);
		state_Connected(localAddr, remoteAddr);
		// Changing the state to connected might itself detect other errors, and set it to
		// disconnected, so
		// returning true/false by checking against the state, instead of explicit "true"
		return state == SocketState::Connected;
	});
}

void TCPSocketData::state_Connected(const SocketAddress& local, const SocketAddress& remote)
{
	LOG("%p: state_Connected: local=%s, remote=%s\n", this, local.toString(true), remote.toString(true));
	CZ_ASSERT(state == SocketState::Connecting || state == SocketState::WaitingAccept);
	localAddr = local;
	remoteAddr = remote;
	state = SocketState::Connected;

	for (uint32_t i = 0; i < pendingReceivesCount;i++)
		ReadOperation::prepareRecv(std::static_pointer_cast<TCPSocketData>(shared_from_this()));
}

void TCPSocketData::state_Disconnected(int code, const std::string& reason)
{
	LOG("%p: state_Disconnected:\n", this);
	if (state == SocketState::Disconnected)
		return;
	CZ_ASSERT(state == SocketState::Connecting || state==SocketState::Connected || state == SocketState::WaitingAccept);
	state = SocketState::Disconnected;
	disconnectInfo.code = code;
	disconnectInfo.msg = reason;
	onShutdown(disconnectInfo.code, disconnectInfo.msg);
}

void TCPSocketData::addReadyRead(ReadOperation* op)
{
	rcv.ready.push(std::make_pair(op->counter, std::unique_ptr<ReadOperation>(op)));

	// Try to deliver any finished read operations, in order
	while (rcv.ready.size() > 0 && rcv.ready.top().first == rcv.lastDeliveredCounter + 1)
	{
		CZ_ASSERT(rcv.ready.top().second != nullptr);
		Receive::PendingReadOperation pendingRead;
		movepop(rcv.ready, pendingRead);
		debugData.receivedData(this, pendingRead.second->buf.ptr.get(), pendingRead.second->buf.size);
		rcv.buf.writeBlock(std::move(pendingRead.second->buf.ptr), pendingRead.second->buf.capacity,
							  pendingRead.second->buf.size);
		rcv.lastDeliveredCounter++;
		onReceive(rcv.buf);
	}
}

//////////////////////////////////////////////////////////////////////////
//
// Socket operations
//
//////////////////////////////////////////////////////////////////////////


AcceptOperation::AcceptOperation(std::shared_ptr<TCPServerSocketData> sharedData_)
	: CompletionPortOperation(std::move(sharedData_))
{
	LOG("AcceptOperation: %p\n", this);
	auto data = getSharedData();
	memset(&overlapped, 0, sizeof(overlapped));
	acceptSocket = std::make_unique<TCPSocket>(data->iocp, data->pendingReceivesCount, data->pendingReceivesSize);
	acceptSocket->m_data->state_WaitingAccept();
	data->pendingAcceptsCount.increment();
}

AcceptOperation::~AcceptOperation()
{
	LOG("~AcceptOperation: %p\n", this);

	// Delete our pending accept socket before signaling that there are no more Accept operations,
	// so that there is no chance of our accept socket having the last strong reference to the CompletionPort,
	// and trying it to delete from here
	acceptSocket.reset();
	auto data = getSharedData();
	data->pendingAcceptsCount.decrement();
}

TCPServerSocketData* AcceptOperation::getSharedData()
{
	return static_cast<TCPServerSocketData*>(sharedData.get());
}

void AcceptOperation::prepareAccept(const std::shared_ptr<TCPServerSocketData>& sharedData)
{
	auto op = std::make_unique<AcceptOperation>(sharedData);

	DWORD dwBytes;
	auto res = sharedData->lpfnAcceptEx(sharedData->listenSocket.get(), op->acceptSocket->m_data->socket.get(), &op->outputBuffer, 0,
		sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
		&dwBytes /*Unused for asynchronous AcceptEx*/, &op->overlapped);
	int err = WSAGetLastError();
	// In our case, since the AcceptEx is asynchronous, it should return FALSE and give an error of "PENDING"
	if (!(res == FALSE && err == ERROR_IO_PENDING))
		throw std::runtime_error(formatString("Error calling AcceptEx: %s", getLastWin32ErrorMsg()));

	auto data = op->getSharedData();
	op.release();
}

void AcceptOperation::onSuccess(unsigned bytesTransfered)
{
	auto data = getSharedData();
	SOCKET optval = data->listenSocket.get();
	auto res =
		setsockopt(acceptSocket->m_data->socket.get(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&optval, sizeof(optval));

	if (res != 0)
		throw std::runtime_error(formatString("Error calling setsockopt: %s", getLastWin32ErrorMsg()));

	sockaddr* localSockAddr = NULL;
	int localSockAddrSize;
	sockaddr* remoteSockAddr = NULL;
	int remoteSockAddrSize;

	data->lpfnGetAcceptExSockaddrs(&outputBuffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &localSockAddr,
								   &localSockAddrSize, &remoteSockAddr, &remoteSockAddrSize);

	LOG("New connection:\n\tlocal = %s\n\tremote = %s\n", getAddr(localSockAddr), getAddr(remoteSockAddr));

	acceptSocket->m_data->state_Connected(SocketAddress(*localSockAddr), SocketAddress(*remoteSockAddr));
	data->onAccept(std::move(acceptSocket));

	if (!data->shuttingDown)
		AcceptOperation::prepareAccept(std::static_pointer_cast<TCPServerSocketData>(sharedData));
}

void AcceptOperation::onError()
{
}

//////////////////////////////////////////////////////////////////////////
//	ReadOperation
//////////////////////////////////////////////////////////////////////////

ReadOperation::ReadOperation(unsigned initialCapacity, std::shared_ptr<TCPSocketData> sharedData_)
	: CompletionPortOperation(std::move(sharedData_))
{
	LOG("ReadOperation: %p : Counter %d\n", this, (int)counter);
	auto data = getSharedData();
	counter = ++data->rcv.counter;
	data->activeReadOperations.push_back(this);
	memset(&overlapped, 0, sizeof(overlapped));
	memset(&wsabuf, 0, sizeof(wsabuf));
	buf.ptr = std::unique_ptr<char[]>(new char[initialCapacity]);
	buf.capacity = initialCapacity;
	buf.size = 0;
	wsabuf.buf = buf.ptr.get();
	wsabuf.len = buf.capacity;
}

ReadOperation::~ReadOperation()
{
	LOG("~ReadOperation: %p\n", this);
	auto data = getSharedData();
	for (auto it = data->activeReadOperations.begin(); it != data->activeReadOperations.end(); it++)
	{
		if (*it == this)
		{
			data->activeReadOperations.erase(it);
			break;
		}
	}
}

TCPSocketData* ReadOperation::getSharedData()
{
	return static_cast<TCPSocketData*>(sharedData.get());
}

void ReadOperation::onSuccess(unsigned bytesTransfered)
{
	LOG("ReadOperation::onSuccess(%d): %p\n", bytesTransfered, this);

	auto data = getSharedData();
	if (bytesTransfered == 0)
	{
		data->state_Disconnected(0, "Connection closed");
		return;
	}

	buf.size = bytesTransfered;
}

void ReadOperation::onError()
{
	LOG("ReadOperation::onError: %p\n", this);
	auto data = getSharedData();
	data->state_Disconnected(0, "Connection closed");
}

void ReadOperation::destroy()
{
	LOG("ReadOperation::destroy: %p\n", this);
	auto data = getSharedData();
	if (buf.size)
	{
		ReadOperation::prepareRecv(std::static_pointer_cast<TCPSocketData>(sharedData));
		data->addReadyRead(this);
	}
	else
	{
		delete this;
	}
}

WriteOperation::WriteOperation(ChunkBuffer buffer_, std::shared_ptr<TCPSocketData> sharedData_)
	: CompletionPortOperation(std::move(sharedData_))
	, buffer(std::move(buffer_))
{
	LOG("WriteOperation: %p\n", this);
	// Prepare WSABUF
	wsabufs.reserve(buffer.numBlocks());
	buffer.iterateBlocks(
		[&](const char* ptr, unsigned size)
		{
			WSABUF buf;
			buf.buf = const_cast<char*>(ptr);
			buf.len = size;
			totalSize += size;
			wsabufs.push_back(buf);
		});

	auto data = getSharedData();
	data->pendingSendBytes += totalSize;
}

WriteOperation::~WriteOperation()
{
	auto data = getSharedData();
	data->pendingSendBytes -= totalSize;
	data->onSendCompleted();
	LOG("~WriteOperation: %p\n", this);
}

TCPSocketData* WriteOperation::getSharedData()
{
	return static_cast<TCPSocketData*>(sharedData.get());
}

std::atomic<uint64_t> gWriteOperationBytesTransfered = 0;
void WriteOperation::onSuccess(unsigned bytesTransfered)
{
	gWriteOperationBytesTransfered += bytesTransfered;
}

std::atomic<int> gWriteOperationErrors = 0;
void WriteOperation::onError()
{
	gWriteOperationErrors++;
}

//////////////////////////////////////////////////////////////////////////
//
// CompletionPort
//
//////////////////////////////////////////////////////////////////////////

void initializeSocketsLib()
{
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != NO_ERROR)
		throw std::runtime_error(formatString("Error initializing WinSock: %s", getLastWin32ErrorMsg()));
}

void shutdownSocketsLib()
{
	WSACleanup();
}

//////////////////////////////////////////////////////////////////////////
//
// TCPServerSocket
//
//////////////////////////////////////////////////////////////////////////

TCPServerSocket::TCPServerSocket(CompletionPort& iocp, int listenPort,
								 std::function<void(std::unique_ptr<TCPSocket>)> onAccept, uint32_t numPendingReads,
								 uint32_t pendingReadSize)
{
	LOG("TCPServerSocket %p: Enter\n", this);
	m_data = std::make_shared<TCPServerSocketData>(iocp);
	m_data->pendingReceivesCount = numPendingReads ? numPendingReads : TCPSocket::DefaultPendingReads;
	m_data->pendingReceivesSize = pendingReadSize ? pendingReadSize : TCPSocket::DefaultReadSize;
	m_data->onAccept = std::move(onAccept);

	// Need a lock even here in the constructor, because once we add stuff to the IOCP, we might get other threads touching
	// this object before we are even done with the constructor
	auto lk = m_data->lock();

	m_data->listenSocket = SocketWrapper(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
	if (!m_data->listenSocket.isValid())
		throw std::runtime_error(formatString("Error creating listen socket: %s", getLastWin32ErrorMsg()));

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
		throw std::runtime_error(formatString("Error initializing listen socket: %s", getLastWin32ErrorMsg()));
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
		throw std::runtime_error(formatString("Error initializing listen socket: %s", getLastWin32ErrorMsg()));
	}

	AcceptOperation::prepareAccept(m_data);

	debugData.serverSocketCreated(this);
	LOG("TCPServerSocket %p: Exit\n", this);
}

TCPServerSocket::~TCPServerSocket()
{
	LOG("~TCPServerSocket %p: Enter\n", this);

	{
		auto lk = m_data->lock();
		m_data->shuttingDown = true;
	}

	CancelIoEx(reinterpret_cast<HANDLE>(m_data->listenSocket.get()), NULL);

	// Block until no more AcceptOperations running
	m_data->pendingAcceptsCount.wait();

	debugData.serverSocketDestroyed(this);
	LOG("~TCPServerSocket %p: Exit\n", this);
}

//////////////////////////////////////////////////////////////////////////
//
// TCPSocket
//
//////////////////////////////////////////////////////////////////////////

void TCPSocket::createSocket()
{
	m_data->socket = SocketWrapper(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED));
	if (!m_data->socket.isValid())
		throw std::runtime_error(formatString("Error creating client socket: %s", getLastWin32ErrorMsg()));

	if (CreateIoCompletionPort((HANDLE)m_data->socket.get(), m_data->iocp.getHandle(),
							   (ULONG_PTR)this, 0) == NULL)
	{
		throw std::runtime_error(formatString("Error initializing accept socket: %s", getLastWin32ErrorMsg()));
	}

	// This disables IOCP notification if the request completes immediately at the point of call.
	/*
	if (SetFileCompletionNotificationModes((HANDLE)m_socket.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)==FALSE)
		throw std::runtime_error(formatString("Error initializing accept socket: %s", getLastWin32ErrorMsg()));
	*/
}

TCPSocket::TCPSocket(CompletionPort& iocp, uint32_t numPendingReads, uint32_t pendingReadSize)
{
	LOG("TCPSocket %p: Enter\n", this);
	debugData.socketCreated(this);
	m_data = std::make_shared<TCPSocketData>(this, iocp);
	m_data->pendingReceivesCount = numPendingReads ? numPendingReads : TCPSocket::DefaultPendingReads;
	m_data->pendingReceivesSize = pendingReadSize ? pendingReadSize : TCPSocket::DefaultReadSize;
	resetCallbacks();
	createSocket();
	LOG("TCPSocket %p: Exit\n", this);
}

TCPSocket::~TCPSocket()
{
	LOG("~TCPSocket %p: Enter\n", this);

	if (m_data->state == SocketState::Connecting)
		m_data->connectingFt.get();

	auto lk = m_data->lock();

	// Notes:
	// CancelIo - Cancels I/O operations that are issued by calling thread
	// CancelIoEx Cancels all I/O regardless of the thread that created the I/O operation.

	// Cancel only read operations, so we leave pending sending operations to finish
	for (auto&& i : m_data->activeReadOperations)
	{
		auto res = CancelIoEx((HANDLE)m_data->socket.get(), &i->overlapped);
		if (res == 0)
		{
			auto err = GetLastError();
			if (err != ERROR_NOT_FOUND)  // This means no elements were found to cancel, and it's expected
			{
				// If we get any other errors, probably the user called WSACleanup already
				LOG(formatString("CancelIoEx failed: %d:%s", err, getLastWin32ErrorMsg(err)));
			}
		}
	}

	// Remove the callbacks, so we don't try to use any user objects from the pending operations
	resetCallbacks();

	if (m_data->state != SocketState::Disconnected)
	{
		m_data->state_Disconnected(0, "TCPSocket destroyed");
	}

	debugData.socketDestroyed(this);
	LOG("~TCPSocket %p: Exit\n", this);
}

void TCPSocket::resetCallbacks()
{
	auto lk = m_data->lock();
	m_data->onReceive = [](const ChunkBuffer&) { };
	m_data->onShutdown = [](int, const std::string&) { };
	m_data->onSendCompleted = []() {};
}

void TCPSocket::setOnReceive(std::function<void(const ChunkBuffer&)> fn)
{
	auto lk = m_data->lock();
	m_data->onReceive = std::move(fn);
}

void TCPSocket::setOnShutdown(std::function<void(int, const std::string&)> fn)
{
	auto lk = m_data->lock();
	m_data->onShutdown = std::move(fn);
}

void TCPSocket::setOnSendCompleted(std::function<void()> fn)
{
	auto lk = m_data->lock();
	m_data->onSendCompleted = std::move(fn);
}

std::shared_future<bool> TCPSocket::connect(const std::string& ip, int port)
{
	CZ_ASSERT(m_data->state == SocketState::None);
	m_data->state_Connecting(ip, port);
	return m_data->connectingFt;
}

uint64_t TCPSocket::getPendingSendBytes() const
{
	return m_data->pendingSendBytes;
}

const SocketAddress& TCPSocket::getRemoteAddress()
{
	return m_data->remoteAddr;
}

void TCPSocket::setUserData(std::shared_ptr<TCPSocketUserData> userData)
{
	m_userData = std::move(userData);
}

const std::shared_ptr<TCPSocketUserData>& TCPSocket::getUserData()
{
	return m_userData;
}

bool TCPSocket::send(ChunkBuffer&& data)
{
	if (m_data->state != SocketState::Connected)
		return false;
	if (data.numBlocks() == 0)
		return true;

	auto op = std::make_unique<WriteOperation>(std::move(data), m_data);

	LOG("%p: Sending %d bytes\n", this, op->data.calcSize());
	DWORD bytesSent = 0;
	int res = WSASend(m_data->socket.get(), &op->wsabufs[0], static_cast<DWORD>(op->wsabufs.size()), &bytesSent, 0,
					  &op->overlapped, NULL);
	int err = WSAGetLastError();

	if (res == 0)  // Send operation completed immediately, so nothing else to do
	{
		op.release();
		return true;
	}

	CZ_ASSERT(res == SOCKET_ERROR);
	if (err == WSA_IO_PENDING)
	{
		// Send is being done asynchronously, so save the pointer, and it will be released once we get the IOCP notification
		// that the send completed
		// Nothing to do
	}
	else
	{
		//throw std::runtime_error(formatString("WSASend failed: %s", getLastWin32ErrorMsg(err)));
		return false;
	}

	op.release();
	return true;
}

bool TCPSocket::isConnected() const
{
	return m_data->state == SocketState::Connected;
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
} // namespace cz
