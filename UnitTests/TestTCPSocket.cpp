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

	TCPAcceptor acceptor(iocp);
	acceptor.listen(SERVER_PORT);
	std::vector<std::unique_ptr<TCPSocket>> s;
	std::vector<std::unique_ptr<TCPSocket>> c;
	ZeroSemaphore connected;
	ZeroSemaphore pendingSend;
	for (int i = 0; i < count; i++)
	{
		s.push_back(std::make_unique<TCPSocket>(iocp));
		c.push_back(std::make_unique<TCPSocket>(iocp));
		connected.increment();

		acceptor.asyncAccept(*s.back().get(), [&connected, &pendingSend, &s, i](const Error& err)
		{
			auto buf = make_shared_array<int>(1);
			*buf.get() = i;
			pendingSend.increment();
			s[i]->asyncSend(Buffer(buf.get(), 1), [buf, i, &s, &pendingSend](const Error& err, unsigned bytesTransfered)
			{
				CHECK_EQUAL(sizeof(int), bytesTransfered);
				s[i]->shutdown();
				pendingSend.decrement();
			});

			connected.decrement();
		});

	}

	ZeroSemaphore pendingConnects;
	for (auto&& s : c)
	{
		pendingConnects.increment();
		s->asyncConnect("127.0.0.1", SERVER_PORT, [&](const Error& ec)
		{
			CHECK(!ec);
			pendingConnects.decrement();
		});
	}

	pendingConnects.wait();

	std::unordered_map<int, bool> msgs;
	ZeroSemaphore activeClientSockets;
	for (size_t i = 0; i < c.size(); i++)
	{
		auto&& s = c[i];
		auto buf = make_shared_array<int>(1);
		activeClientSockets.increment();
		// #TODO : Use asyncReceive, to read the entire request
		s->asyncReceive(Buffer(buf.get(), 1), [buf, &s, &activeClientSockets, &msgs](const Error& err, unsigned bytesTransfered)
		{
			CHECK_EQUAL(sizeof(int), bytesTransfered);
			msgs[*buf.get()] = true;
			// Queue another receive, so we can detect socket shutdown
			s->asyncReceive(Buffer(buf.get(), 1), [buf, &activeClientSockets](const Error& err, unsigned bytesTransfered)
			{
				CHECK_EQUAL(0, bytesTransfered);
				activeClientSockets.decrement();
			});
		});
	}
	connected.wait(); // make sure all the handlers on the acceptor were called

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

TEST(VariousConnectMethods)
{
	CompletionPort iocp;
	auto th = std::thread([&]
	{
		iocp.run();
	});

	TCPAcceptor acceptor(iocp);
	acceptor.listen(SERVER_PORT);

	ZeroSemaphore pending;

	// Test synchronous connect
	{
		pending.increment();
		TCPSocket serverSide(iocp);
		acceptor.asyncAccept(serverSide, [&](const Error& ec)
		{
			CHECK(!ec);
			pending.decrement();
		});

		TCPSocket s(iocp);
		auto ec = s.connect("127.0.0.1", SERVER_PORT);
		CHECK(!ec);
		pending.wait();
	}
 
	// Test synchronous connect failure (on the wrong port)
	{
		TCPSocket s(iocp);
		// Test on the wrong port
		auto ec = s.connect("127.0.0.1", SERVER_PORT + 1);
		CHECK(ec);
	}

	// Test synchronous connect without a pending accept
	// This should actually succeed, since the connection stays established, waiting for the acceptor to accept it
	{
		TCPSocket s(iocp);
		auto ec = s.connect("127.0.0.1", SERVER_PORT);
		CHECK(!ec); // Actual connect succeeds
		pending.increment();
		s.asyncSend("A", 1, [&](auto ec, unsigned bytesTransfered)
		{
			CHECK(!ec); // Send still succeeds, even if the acceptor hasn't accepted the connection yet
			pending.decrement();
		});
		pending.wait();
	}

	// Test asynchronous connect
	{
		pending.increment();
		TCPSocket serverSide(iocp);
		acceptor.asyncAccept(serverSide, [&](const Error& ec)
		{
			CHECK(!ec);
			pending.decrement();
		});

		TCPSocket s(iocp);
		pending.increment();
		s.asyncConnect("127.0.0.1", SERVER_PORT, [&](auto ec)
		{
			CHECK(!ec);
			pending.decrement();
		});
		pending.wait();
	}

	// Test asynchronous connect failure (on the wrong port)
	{
		TCPSocket s(iocp);
		pending.increment();
		s.asyncConnect("127.0.0.1", SERVER_PORT+1, [&](auto ec)
		{
			CHECK(ec);
			pending.decrement();
		});
		pending.wait();
	}

	// Test asynchronous connect without a pending accept
	// This should succeed
	{
		TCPSocket s(iocp);
		pending.increment();
		s.asyncConnect("127.0.0.1", SERVER_PORT, [&](auto ec)
		{
			CHECK(!ec); // Connection should still succeed
			pending.decrement();
		});

		pending.increment();
		s.asyncSend("A", 1, [&](auto ec, unsigned bytesTransfered)
		{
			CHECK(!ec); // Send still succeeds, even if the server hasn't accepted the connection yet
			pending.decrement();
		});

		pending.wait();
	}
	
	iocp.stop();
	th.join();
}


TEST(SynchronousSendReceive)
{
	CompletionPort iocp;
	auto th = std::thread([&]
	{
		iocp.run();
	});

	TCPAcceptor acceptor(iocp);
	acceptor.listen(SERVER_PORT);
	ZeroSemaphore pending;

	// Synchronous send and receive
	{
		TCPSocket serverSide(iocp);
		acceptor.asyncAccept(serverSide, [&serverSide](const Error& ec)
		{
			CHECK(!ec);
			Error e;
			auto b = std::make_shared<char>('A'); // shared_ptr, since we need to keep it alive for the asyncSend
			CHECK_EQUAL(1, serverSide.send(b.get(), 1, 0xFFFFFFFF, e));
			CHECK(!e);
			(*b)++;
			CHECK_EQUAL(1, serverSide.send(b.get(), 1, 0xFFFFFFFF, e));
			CHECK(!e);

			// Mix with an asyncSend, to check if everything still works
			(*b)++;
			serverSide.asyncSend(b.get(), 1, [&serverSide, b](auto ec, auto bytesTransfered)
			{
				CHECK(!ec);
				CHECK_EQUAL(1, bytesTransfered);
				// another synchronous send to mix
				(*b)++;
				Error e;
				CHECK_EQUAL(1, serverSide.send(b.get(), 1, 0xFFFFFFFF, e));
				CHECK(!e);
			});
		});

		TCPSocket s(iocp);
		auto ec = s.connect("127.0.0.1", SERVER_PORT);
		CHECK(!ec);

		char b[4];
		pending.increment();
		// Receive with a mix of async and synchronous
		s.asyncReceive(Buffer(b, 1), [&](auto ec, auto bytesTransfered)
		{
			CHECK(!ec);
			CHECK_EQUAL(1, bytesTransfered);
			pending.decrement();
		});

		// Receive the rest of the data synchronously. This can't be inside the asyncReceive, since we are exchange
		// data with another socket being served by the same CompletionPort, and it would block forever, since handlers
		// won't be executed for the other socket
		pending.wait();
		Error e;
		CHECK_EQUAL(3, s.receive(&b[1], 3, 0xFFFFFFFF, e));
		CHECK(!e);
		CHECK_EQUAL(int('A'), (int)b[0]);
		CHECK_EQUAL(int('B'), (int)b[1]);
		CHECK_EQUAL(int('C'), (int)b[2]);
		CHECK_EQUAL(int('D'), (int)b[3]);

		// Send until it fails (because the server is not receiving it)
		const int size = 1024 * 1024*100;
		auto big = make_shared_array<char>(size);
		int sentBlocks = 0;
		while (true)
		{
			int sent = s.send(big.get(), size, 1002, ec);
			if (sent == size)
				sentBlocks++;
			else
				break;
		}
		CHECK(sentBlocks > 0); // It should succeed in sending a few blocks, even if the server is not reading it

		pending.wait();
	}

	iocp.stop();
	th.join();
}

TEST(SynchronousAcceptAndConnect)
{
	CompletionPort iocp;
	auto ioth = std::thread([&]
	{
		iocp.run();
	});

	timeBeginPeriod(1);
	
	Semaphore checkpoint;
	const int acceptTimeout = 200;
	auto serverTh = std::thread([&iocp, acceptTimeout, &checkpoint]()
	{
		TCPAcceptor acceptor(iocp);
		acceptor.listen(SERVER_PORT);
		TCPSocket s(iocp);

		UnitTest::Timer timer;
		timer.Start();

		auto t = timer.GetTimeInMs();
		auto ec = acceptor.accept(s, 0); // no blocking
		CHECK_CLOSE(0, timer.GetTimeInMs() - t, 5);
		CHECK(ec.code==Error::Code::Timeout);

		t = timer.GetTimeInMs();
		ec = acceptor.accept(s, acceptTimeout); // with timeout
		CHECK_CLOSE(acceptTimeout, timer.GetTimeInMs() - t, 5);
		CHECK(ec.code==Error::Code::Timeout);

		checkpoint.notify();
		// And try one with success
		ec = acceptor.accept(s, 0xFFFFFFFF);

		CZ_LOG(logDefault, Log, "Server: local: %s, remote: %s", s.getLocalAddress().toString(true), s.getRemoteAddress().toString(true));
		CHECK(!ec);

		char buf[5];
		CHECK_EQUAL(sizeof(buf), s.receive(buf, sizeof(buf), 0xFFFFFFFF, ec));
		CHECK(!ec);
		CHECK_EQUAL("ABCD", buf);

		CHECK_EQUAL(sizeof(buf), s.send(buf, sizeof(buf), 0xFFFFFFFF, ec));
		CHECK(!ec);
	});

	//
	// Client
	TCPSocket s(iocp);

	// Try to connect to the wrong port
	CHECK(s.connect("127.0.0.1", SERVER_PORT + 1));

	// Try a successful accept
	checkpoint.wait();
	CHECK(!s.connect("127.0.0.1", SERVER_PORT));
	CZ_LOG(logDefault, Log, "Client: local: %s, remote: %s", s.getLocalAddress().toString(true), s.getRemoteAddress().toString(true));

	Error ec;
	CHECK_EQUAL(5, s.send("ABCD", 5, 0xFFFFFFFF, ec));
	CHECK(!ec);
	char buf[5];
	CHECK_EQUAL(5, s.receive(buf, 5, 0xFFFFFFFF, ec));
	CHECK_EQUAL("ABCD", buf);
	CHECK(!ec);
	s.shutdown();

	iocp.stop();
	ioth.join();
	serverTh.join();
}

void setupSendTimer(int counter, DeadlineTimer& timer, TCPSocket& socket)
{
	if (counter == 0)
	{
		socket.shutdown();
		return;
	}
	counter--;
	timer.expiresFromNow(10);
	timer.asyncWait([counter, &timer, &socket](bool aborted)
	{
		int size = 2 ;
		auto data = make_shared_array<char>(size);
		data.get()[0] = counter;
		data.get()[1] = -counter;
		socket.asyncSend(data.get(), size, [data, counter, &timer, &socket](auto err, auto bytesTransfered)
		{
			setupSendTimer(counter, timer, socket);
		});
	});
}

// Test the asyncReceive, which uses multiple calls to asyncReceiveSome to receive all the bytes requested
// How it works:
//	The sender makes a small pause between each send, so that the receiver receives small portions
//	In order to work correctly, the receiver needs to wait for the entire data set, and then call the handler
TEST(CompoundReceive)
{
	CompletionPort iocp;
	DeadlineTimer sendTimer(iocp);
	TCPAcceptor acceptor(iocp);
	acceptor.listen(SERVER_PORT);

	std::vector<std::thread> ths;
	ths.emplace_back([&]()
	{
		iocp.run();
	});

	TCPSocket serverSide(iocp);
	const int numSends = 5;
	acceptor.asyncAccept(serverSide, [&](auto ec)
	{
		CHECK(!ec);
		setupSendTimer(numSends, sendTimer, serverSide);
	});


	TCPSocket clientSide(iocp);
	CHECK(!clientSide.connect("127.0.0.1", SERVER_PORT));

	Semaphore done;
	char buf[numSends * 2];
	clientSide.asyncReceive(Buffer(buf), [&](auto ec, auto bytesTransfered)
	{
		CHECK_EQUAL(sizeof(buf), bytesTransfered);
		done.notify();
	});

	done.wait();
	char c = numSends;
	for (int i = 0; i < numSends*2; i+=2)
	{
		c--;
		CHECK_EQUAL(c, buf[i]);
		CHECK_EQUAL(-c, buf[i+1]);
	}

	iocp.stop();
	for (auto&& th : ths)
		th.join();
}

class BigDataServer
{
public:
	BigDataServer()
	{
		m_s = std::make_unique<TCPSocket>(m_iocp);
		m_acceptor = std::make_unique<TCPAcceptor>(m_iocp);
		m_acceptor->listen(SERVER_PORT);
		m_acceptor->asyncAccept(*m_s, [this](const Error& ec)
		{
			CZ_ASSERT(!ec);
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
		m_s->asyncReceiveSome(Buffer(buf.get(), size), [this,size, buf](const Error& ec, unsigned bytesTransfered) mutable
		{
			CZ_LOG(logTestsVerbose, Log, "Server receive: %s, %d", ec.msg(), bytesTransfered);
			if (!ec)
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
				CZ_LOG(logTestsVerbose, Warning, "Receive failed with '%s'", ec.msg());
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
	std::unique_ptr<TCPAcceptor> m_acceptor;
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

	auto ft = client.connect("127.0.0.1", SERVER_PORT);
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
		client.asyncSend(buf, [&totalSent, &totalSentQueued, &sendDone](const Error& ec, unsigned bytesTransfered)
		{
			CZ_LOG(logTestsVerbose, Log, "Client send: %s, %d", ec.msg(), bytesTransfered);
			if (!ec)
			{
				totalSent += bytesTransfered;
				totalSentQueued -= bytesTransfered;
				sendDone.notify();
			}
			else
			{
				CZ_LOG(logTestsVerbose, Log, "Send failed with '%s'", ec.msg());
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
		client.asyncSend(buf, [&totalSent, &totalSentQueued, &sendDone](const Error& ec, unsigned bytesTransfered)
		{
			CZ_LOG(logTestsVerbose, Log, "Client send: %s, %d", ec.msg(), bytesTransfered);
			if (!ec)
			{
				totalSent += bytesTransfered;
				totalSentQueued -= bytesTransfered;
				sendDone.notify();
			}
			else
			{
				CZ_LOG(logTestsVerbose, Log, "Send failed with '%s'", ec.msg());
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
			[this](const Error& err, unsigned bytesTransfered)
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
				m_socket->asyncSend(msg.c_str(), static_cast<int>(msg.size()), [this](auto ec, auto bytesTransfered)
				{
					CZ_ASSERT(!ec && bytesTransfered!=0);
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
		m_acceptor = std::make_unique<TCPAcceptor>(m_ths.iocp);
		m_acceptor->listen(listenPort);
		prepareAccept();
	}

	~EchoServer()
	{
		m_acceptor->shutdown();
		m_pending.wait();
		m_clients.clear();
		m_ths.stop();
	}

	void prepareAccept()
	{
		auto socket = std::make_shared<TCPSocket>(m_ths.iocp);
		m_pending.increment();
		m_acceptor->asyncAccept(*socket, [this, socket=socket](const Error& ec)
		{
			SCOPE_EXIT{ m_pending.decrement(); };
			if (ec)
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
	std::unique_ptr<TCPAcceptor> m_acceptor;
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
			[this](const Error& ec, unsigned bytesTransfered)

		{
			if (ec || bytesTransfered==0)
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
			[this](const Error& ec, unsigned bytesTranfered)
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
		CHECK(!c->socket->connect("127.0.0.1", SERVER_PORT));
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
		acceptor = std::make_unique<TCPAcceptor>(ths.iocp);
		acceptor->listen(SERVER_PORT);
		clientSocket = std::make_unique<TCPSocket>(ths.iocp);
		expected = multipleUntilExpected;
		expectedRemaining = multipleUntilLeftovers;
		acceptor->asyncAccept(*clientSocket, [this](const Error& ec)
		{
			CHECK(!ec);
			prepareRecv();
		});
	}

	~MultipleUntilServer()
	{
		pending.wait();
		acceptor->shutdown();
		clientSocket->shutdown();
		ths.stop();
	}

	void prepareRecv()
	{
		pending.increment();
		clientSocket->asyncReceiveUntil(
			buf, '\n', [this](const Error& ec, unsigned bytesTransfered)
		{
			SCOPE_EXIT{ pending.decrement(); };
			CHECK(!ec);
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
	std::unique_ptr<TCPAcceptor> acceptor;
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
	CHECK(!s.connect("127.0.0.1", SERVER_PORT));

	ZeroSemaphore pending;
	auto sendString = [&](TCPSocket& socket, std::string str)
	{
		pending.increment();
		socket.asyncSend(str.c_str(), (int)str.size(), [&, len=(unsigned)str.size()](const Error& ec, unsigned bytesTransfered)
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