#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::net;

#define SERVER_PORT 28000

SUITE(TCPSocket2)
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
				pendingSend.decrement();
				s[i]->shutdown();
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

TEST(TestThroughput)
{
	CompletionPort iocp;
	std::vector<std::thread> ths;
	for (int i = 0; i < 1; i++)
	{
		ths.push_back(std::thread([&iocp]
		{
			CZ_LOG(logTestsVerbose, Log, "Thread started");
			iocp.run();
			CZ_LOG(logTestsVerbose, Log, "Thread finished");
		}));
	}

	auto server = std::make_unique<BigDataServer>();

	TCPSocket client(iocp);

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
		int const MBCOUNT = 20;
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
	iocp.stop();
	for (auto&& t : ths)
		t.join();
}

class EchoConnection
{
public:
	EchoConnection(std::unique_ptr<TCPSocket> socket, int waitMs=0)
		: m_socket(std::move(socket))
		, m_waitMs(0)
	{
	}

private:
	void prepareRecv()
	{
		auto buf = make_shared_array<char>(128);
		m_socket->asyncReceive(Buffer(buf.get(), 128), [this, buf](const SocketCompletionError& err, unsigned bytesTransfered)
		{
			if (bytesTransfered==0)
			{
				m_socket->shutdown();
				return;
			}

			if (m_waitMs)
				spinMs(m_waitMs);

			//m_socket->asyncSend(Buffer(buf.get(), ))
		});
	}
	std::unique_ptr<TCPSocket> m_socket;
	ChunkBuffer m_buf;
	int m_waitMs;
};

}