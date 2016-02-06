#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::net;

#define SERVER_PORT 28000

SUITE(TCPSocket)
{

void testConnection(int count)
{
	CompletionPort iocp;
	auto th  = std::thread([&iocp]()
	{
		iocp.run();
	});

	TCPServerSocket server(iocp, SERVER_PORT);
	std::vector<std::unique_ptr<TCPSocket>> s;
	std::vector<std::unique_ptr<TCPSocket>> c;
	ZeroSemaphore connected;
	ZeroSemaphore pendingSend;
	for (int i = 0; i < count; i++)
	{
		s.push_back(std::make_unique<TCPSocket>(iocp));
		c.push_back(std::make_unique<TCPSocket>(iocp));
		connected.increment();
		server.asyncAccept(*s.back().get(), [&connected, &pendingSend, &s, i](const SocketCompletionError& err, unsigned)
		{
			auto buf = make_shared_array<int>(1);
			*buf.get() = i;
			pendingSend.increment();
			s[i]->asyncSend(Buffer(buf.get(), 1), [buf, i, &s, &pendingSend](const SocketCompletionError& err, unsigned bytesTransfered)
			{
				CHECK_EQUAL(sizeof(int), bytesTransfered);
				s[i]->shutdown();
				pendingSend.decrement();
			});
			connected.decrement();
		});
	}

	std::vector<Future<bool>> fts;
	for (auto&& s : c)
		fts.push_back(s->connect("127.0.0.1", SERVER_PORT));
	for (auto&& ft : fts)
		CHECK_EQUAL(true, ft.get());
	std::unordered_map<int, bool> msgs;
	ZeroSemaphore activeClientSockets;
	for (size_t i = 0; i < c.size(); i++)
	{
		auto&& s = c[i];
		auto buf = make_shared_array<int>(1);
		activeClientSockets.increment();
		s->asyncReceive(Buffer(buf.get(), 1), [buf, &s, &activeClientSockets, &msgs](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			CHECK_EQUAL(sizeof(int), bytesTransfered);
			msgs[*buf.get()] = true;
			// Queue another receive, so we can detect socket shutdown
			s->asyncReceive(Buffer(buf.get(), 1), [buf, &activeClientSockets](const SocketCompletionError& err, unsigned bytesTransfered)
			{
				CHECK_EQUAL(0, bytesTransfered);
				activeClientSockets.decrement();
			});
		});
	}
	connected.wait(); // make sure all the handlers on the server were called

	for (int i = 0; i < count; i++)
	{
		CHECK_EQUAL(formatString("127.0.0.1:%d", SERVER_PORT), s[i]->getLocalAddress().toString(true));
		CHECK_EQUAL(formatString("127.0.0.1:%d", SERVER_PORT), c[i]->getRemoteAddress().toString(true));
	}

	pendingSend.wait();
	activeClientSockets.wait();

	// Check if we got the data from all sockets
	for (int i = 0; i < count; i++)
		CHECK(msgs[i]);

	// Stop iocp thread
	iocp.stop();
	th.join();
}

TEST(ConnectSimple)
{
	testConnection(1);
}

TEST(ConnectMultiple)
{
	testConnection(10);
}

class BigDataServer
{
public:
	BigDataServer()
	{
		m_s = std::make_unique<TCPSocket>(m_iocp);
		m_serverSocket = std::make_unique<TCPServerSocket>(m_iocp, 28000);
		m_serverSocket->asyncAccept(*m_s, [this](const SocketCompletionError& err, unsigned)
		{
			CZ_ASSERT(err.isOk());
			start();
		});

		m_th = std::thread([this]
		{
			CZ_LOG(logTestsVerbose, Log, "Server thread started");
			m_iocp.run();
			CZ_LOG(logTestsVerbose, Log, "Server thread finished");
		});

		m_checkThread = std::thread([this]
		{
			while(!m_finish)
			{
				std::function<void()> fn;
				m_checkThreadQ.wait_and_pop(fn);
				fn();
			}
		});

		m_timer.Start();
	}

	~BigDataServer()
	{
		m_pendingOps.wait();
		auto ms = m_timer.GetTimeInMs();
		m_checkThreadQ.push([this] {m_finish = true;});

		m_iocp.stop();
		m_th.join();
		m_checkThread.join();

		CZ_LOG(logTests, Log, "TCPSocket throughput (%lld bytes) %.2fMB received in %.2f ms. %fmbps/sec",
			m_received,
			m_received / (1000.0 * 1000.0),
			ms,
			((m_received * 8) / (1000.0 * 1000.0)) / (ms / 1000.0));
	}

private:

	void start()
	{
		CZ_LOG(logTestsVerbose, Log, "Server: Accepted. LocalAddr=%s, RemoteAddr=%s",
			m_s->getLocalAddress().toString(true), m_s->getRemoteAddress().toString(true));

		prepareReceive(1024*1024);
		prepareReceive(1024*1024);
		prepareReceive(1024*1024);
		prepareReceive(1024*1024);
	}

	void prepareReceive(int size)
	{
		m_pendingOps.increment();
		auto buf = make_shared_array<char>(size);
		m_s->asyncReceive(Buffer(buf.get(), size), [this,size, buf](const SocketCompletionError& err, unsigned bytesTransfered) mutable
		{
			CZ_LOG(logTestsVerbose, Log, "Server receive: %s, %d", err.isOk() ? "true" : "false", bytesTransfered);
			if (err.isOk())
			{
				CZ_ASSERT(bytesTransfered);
				m_received += bytesTransfered;
				prepareReceive(size);
				m_checkThreadQ.push([this, bytesTransfered, buf=std::move(buf)]
				{
					for (unsigned i = 0; i < bytesTransfered; i++)
					{
						CHECK_EQUAL(m_expected, (unsigned char)buf.get()[i]);
						m_expected++;
					}
				});
			}
			else
			{
				CZ_LOG(logTestsVerbose, Warning, "Receive failed with '%s'", err.msg.c_str());
			}
			m_pendingOps.decrement();
		});
	}

	CompletionPort m_iocp;
	std::thread m_th;

	// Data checking is done in a separate thread, so it doesn't interfere with our throughput
	std::thread m_checkThread;
	SharedQueue<std::function<void()>> m_checkThreadQ;
	bool m_finish = false;

	// Need to use pointers, since CompletionPort need to be initialized first
	std::unique_ptr<TCPServerSocket> m_serverSocket;
	std::unique_ptr<TCPSocket> m_s;
	uint64_t m_received;
	ZeroSemaphore m_pendingOps;
	UnitTest::Timer m_timer;
	uint8_t m_expected = 0;
};


struct IOCPThreads
{
	CompletionPort iocp;
	std::vector<std::thread> ths;

	void start(int nthreads)
	{
		for (int i = 0; i < nthreads; i++)
		{
			ths.push_back(std::thread([this]
			{
				CZ_LOG(logTestsVerbose, Log, "Thread started");
				iocp.run();
				CZ_LOG(logTestsVerbose, Log, "Thread finished");
			}));
		}
	}
		
	void stop()
	{
		iocp.stop();
		for (auto&& t : ths)
			t.join();
	}
};

TEST(TestThroughput)
{
	IOCPThreads ths;
	ths.start(1);
	auto server = std::make_unique<BigDataServer>();

	TCPSocket client(ths.iocp);

	auto ft = client.connect("127.0.0.1", 28000);
	CZ_LOG(logTestsVerbose, Log, "S2 accepted. LocalAddr=%s, RemoteAddr=%s", client.getLocalAddress().toString(true), client.getRemoteAddress().toString(true));

	std::vector<char> buf;
	for (unsigned i = 0; i < 256 * 4 * 1000; i++)
		buf.push_back(char(i & 0xFF));

	int count = 4;
	Semaphore sendDone;
	std::atomic<uint64_t> totalSent = 0;
	std::atomic<uint64_t> totalSentQueued = 0;
	while(count--)
	{
		client.asyncSend(buf, [&totalSent, &totalSentQueued, &sendDone](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			CZ_LOG(logTestsVerbose, Log, "Client send: %s, %d", err.isOk() ? "true" : "false", bytesTransfered);
			if (err.isOk())
			{
				totalSent += bytesTransfered;
				totalSentQueued -= bytesTransfered;
				sendDone.notify();
			}
			else
			{
				CZ_LOG(logTestsVerbose, Log, "Send failed with '%s'", err.msg.c_str());
			}

		});
		totalSentQueued += buf.size();
	}

	while(true)
	{
#if CZ_DEBUG
		int const MBCOUNT = 5;
#else
		int const MBCOUNT = 1000;
#endif

		sendDone.wait();
		if (totalSent >= (uint64_t)1000*1000*MBCOUNT)
			break;
		client.asyncSend(buf, [&totalSent, &totalSentQueued, &sendDone](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			CZ_LOG(logTestsVerbose, Log, "Client send: %s, %d", err.isOk() ? "true" : "false", bytesTransfered);
			if (err.isOk())
			{
				totalSent += bytesTransfered;
				totalSentQueued -= bytesTransfered;
				sendDone.notify();
			}
			else
			{
				CZ_LOG(logTestsVerbose, Log, "Send failed with '%s'", err.msg.c_str());
			}
		});
		totalSentQueued += buf.size();
	}

	// Shutdown the client socket, so the server pending reads can complete (with error)
	client.shutdown();
	server = nullptr;
	ths.stop();
}

class EchoServerConnection
{
public:
	EchoServerConnection(std::shared_ptr<TCPSocket> socket, int waitMs=0)
		: m_socket(std::move(socket))
		, m_waitMs(0)
	{
		setupRecv();
	}

	~EchoServerConnection()
	{
		CZ_LOG(logTests, Log, "EchoServerConnection: Destructor");
		m_socket->shutdown();
		m_pending.wait();
	}

private:
	void setupRecv()
	{
		m_pending.increment();
		m_socket->asyncReceiveUntil(m_buf, '\n',
			[this](const SocketCompletionError& err, unsigned bytesTransfered)
			{
				SCOPE_EXIT{ m_pending.decrement(); };
				if (bytesTransfered==0)
				{
					CZ_LOG(logTests, Log, "EchoServerConnection: Disconnected");
					m_socket->shutdown();
					return;
				}

				if (m_waitMs)
					spinMs(m_waitMs);

				std::string msg(m_buf.begin(), m_buf.begin() + bytesTransfered);
				m_buf.skip(bytesTransfered);
				CZ_LOG(logTests, Log, "EchoServerConnection:Received: %d bytes, %s,", bytesTransfered, msg.c_str());
				CZ_LOG(logTests, Log, "EchoServerConnection: Buf fillcount: %d bytes", m_buf.getUsedSize());
				setupRecv(); // This needs to be done AFTER reading our data from the buffer
				m_socket->asyncSend(msg.c_str(), static_cast<int>(msg.size()), [this](auto err, auto bytesTransfered)
				{
					CZ_ASSERT(err.isOk() && bytesTransfered!=0);
					CZ_LOG(logTests, Log, "EchoServerConnection: Sent %d bytes", bytesTransfered);
				});
			});
	}

	ZeroSemaphore m_pending;
	std::shared_ptr<TCPSocket> m_socket;
	RingBuffer m_buf;
	int m_waitMs;
};


class EchoServer
{
public:
	EchoServer(int listenPort)
	{
		m_ths.start(1);
		m_serverSocket = std::make_unique<TCPServerSocket>(m_ths.iocp, listenPort);
		prepareAccept();
	}

	~EchoServer()
	{
		m_serverSocket->shutdown();
		m_pending.wait();
		m_clients.clear();
		m_ths.stop();
	}

	void prepareAccept()
	{
		auto socket = std::make_shared<TCPSocket>(m_ths.iocp);
		m_pending.increment();
		m_serverSocket->asyncAccept(*socket,
			[this, socket=socket](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			SCOPE_EXIT{ m_pending.decrement(); };
			if (!err.isOk())
				return;
			m_inPrepare++;
			CZ_LOG(logTests, Log, "Client from %s connected", socket->getRemoteAddress().toString(true));
			auto con = std::make_unique<EchoServerConnection>(socket);
			m_clients.push_back(std::move(con));
			m_inPrepare--;
		});
	}

private:
	int m_inPrepare = 0;
	ZeroSemaphore m_pending;
	IOCPThreads m_ths;
	std::unique_ptr<TCPServerSocket> m_serverSocket;
	std::vector<std::unique_ptr<EchoServerConnection>> m_clients;
};


struct ClientConnection
{
	int num = 0;
	int sendCount = 0;
	int recvCount = 0;
	ZeroSemaphore pending;
	std::unique_ptr<TCPSocket> socket;
	RingBuffer recvBuf;
	ZeroSemaphore pendingEchos;

	ClientConnection(CompletionPort& iocp)
	{
		socket = std::make_unique<TCPSocket>(iocp);
	}

	~ClientConnection()
	{
		pendingEchos.wait();
		socket->shutdown();
		pending.wait();
	}

	void send()
	{
		pending.increment();
		pendingEchos.increment();
		auto str = formatString("Client:%d:%d\n", num, sendCount++);
		CZ_LOG(logTests, Log, formatString("Sending %s", str));
		socket->asyncSend(
			str, static_cast<int>(strlen(str)),
			[this](const SocketCompletionError& err, unsigned bytesTransfered)

		{
			if (!err.isOk() || bytesTransfered==0)
			{
				CZ_LOG(logTests, Warning, "ClientConnection: Failed to send.");
			}
			else
			{
				CZ_LOG(logTests, Log, "ClientConnection: sent %d bytes", bytesTransfered)
			}
			pending.decrement();
		});
	}

	void prepareRecv()
	{
		pending.increment();
		socket->asyncReceiveUntil(recvBuf, '\n',
			[this](const SocketCompletionError& err, unsigned bytesTranfered)
		{
			SCOPE_EXIT{ pending.decrement(); };
			if (bytesTranfered == 0)
			{
				CZ_LOG(logTests, Log, "ClientConnection: Disconnected");
				return;
			}
			std::string msg(recvBuf.begin(), recvBuf.begin() + bytesTranfered-1);
			recvBuf.skip(bytesTranfered);
			prepareRecv(); // This needs to be done AFTER reading our data from the buffer
			CZ_LOG(logTestsVerbose, Log, "Client:Received: %s", msg.c_str());
			CHECK_EQUAL(formatString("Client:%d:%d", num, recvCount++), msg.c_str());
			pendingEchos.decrement();
		});
	}

};

TEST(Echo)
{
	IOCPThreads ths;
	ths.start(1);
	auto server = std::make_unique<EchoServer>(SERVER_PORT);

	std::vector<std::unique_ptr<ClientConnection>> clients;
	for (int i = 0; i < 1; i++)
	{
		clients.push_back(std::make_unique<ClientConnection>(ths.iocp));
		auto c = clients.back().get();
		CHECK(c->socket->connect("127.0.0.1", SERVER_PORT).get() == true);
		c->num = i;
		c->prepareRecv();
		c->send();
		c->send();
	}

	clients.clear();
	server = nullptr;
	ths.stop();
}


// This is what the handler expects. Full tokens (excluding the delimiter)
std::vector<std::string> multipleUntilExpected = { "This", "is", "a", "test", "!" };
// The client sends incomplete tokens. This tests the code that deals with holding on the data and only call the handler
// when there is a complete token
std::vector<std::string> multipleUntilSend = { "T", "his\nis", "\na\ntest\n!\n"};
// Whenever the server receives a token, there might be data left in the buffer, which will be eventually be handling
// by other calls to asyncReadUntil. This tests those cases.
std::vector<std::string> multipleUntilLeftovers = { "is", "a\ntest\n!\n", "test\n!\n", "!\n", "" };
struct MultipleUntilServer
{
	MultipleUntilServer()
	{
		ths.start(1);
		serverSocket = std::make_unique<TCPServerSocket>(ths.iocp, SERVER_PORT);
		clientSocket = std::make_unique<TCPSocket>(ths.iocp);
		expected = multipleUntilExpected;
		expectedRemaining = multipleUntilLeftovers;
		serverSocket->asyncAccept(*clientSocket, [this](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			CHECK(err.isOk());
			prepareRecv();
		});
	}

	~MultipleUntilServer()
	{
		pending.wait();
		serverSocket->shutdown();
		clientSocket->shutdown();
		ths.stop();
	}

	void prepareRecv()
	{
		pending.increment();
		clientSocket->asyncReceiveUntil(
			buf, '\n', [this](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			SCOPE_EXIT{ pending.decrement(); };
			CHECK(err.isOk());
			std::string msg(buf.begin(), buf.begin() + bytesTransfered-1);
			buf.skip(bytesTransfered);
			CZ_LOG(logTestsVerbose, Log, "MultipleUntilServer:Recv: %d, %s", bytesTransfered, msg.c_str());
			CHECK_EQUAL(expected.front(),msg);
			expected.erase(expected.begin());

			// Check if the buffer has whatever remaining data should be there
			std::string remaining(buf.begin(), buf.end());
			CHECK_EQUAL(expectedRemaining.front(), remaining);
			expectedRemaining.erase(expectedRemaining.begin());

			if (expected.empty())
				clientSocket->shutdown();
			else
				prepareRecv();
		}, 1024);
	}

	std::vector<std::string> expected;
	std::vector<std::string> expectedRemaining;
	ZeroSemaphore pending;
	std::unique_ptr<TCPServerSocket> serverSocket;
	std::unique_ptr<TCPSocket> clientSocket;
	RingBuffer buf;
	IOCPThreads ths;
};

TEST(MultipleUntil)
{
	auto server = std::make_unique<MultipleUntilServer>();
	IOCPThreads ths;
	ths.start(1);

	TCPSocket s(ths.iocp);
	CHECK(s.connect("127.0.0.1", SERVER_PORT).get() == true);

	ZeroSemaphore pending;
	auto sendString = [&](TCPSocket& socket, std::string str)
	{
		pending.increment();
		socket.asyncSend(str.c_str(), (int)str.size(), [&, len=(unsigned)str.size()](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			CHECK_EQUAL(len, bytesTransfered);
			pending.decrement();
		});
	};

	for(auto&& str : multipleUntilSend)
	{
		sendString(s, str);
		// Sleep a little bit, so the server has time to receive just the data we sent, therefore allowing us
		// to test incomplete tokens
		Sleep(1);
	}

	pending.wait();
	server = nullptr;
	s.shutdown();
	ths.stop();
}

}