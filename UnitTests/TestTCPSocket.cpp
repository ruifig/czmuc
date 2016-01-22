#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::net;

const int serverPort = 28000;

// From https://en.wikipedia.org/wiki/NetBIOS
const int someReservedPort = 139; // NetBIOS NetBIOS Session Service

class EchoClient
{
public:
	EchoClient(CompletionPort& iocp, const char* ip, int port, const char* msg)
		: m_socket(iocp), m_msg(msg)
	{
		m_socket.setOnReceive([this](const ChunkBuffer& buf)
		{
			onSocketRecv(buf);
		});

		CHECK(m_socket.connect(ip, port).get());
	}

	~EchoClient()
	{
		m_socket.shutdown();
	}

	void send()
	{
		ChunkBuffer buf;
		buf << m_msg;
		CHECK(m_socket.send(std::move(buf)));
	}

	void wait()
	{
		CHECK_EQUAL(m_msg, m_resPr.get_future().get());
	}

private:

	void onSocketRecv(const ChunkBuffer& buf)
	{
		std::string str;
		if (!buf.tryRead(str))
			return;
		m_resPr.set_value(str);
	}

	std::string m_msg;
	std::promise<std::string> m_resPr;
	TCPSocket m_socket;
};

struct EchoServerConnection : public TCPServerClientInfo
{
	explicit EchoServerConnection(TCPServer* owner, std::unique_ptr<TCPSocket> socket, int waitMs=0)
		: TCPServerClientInfo(owner, std::move(socket))
		, waitMs(waitMs)
	{
	}

	virtual void onSocketReceive(const ChunkBuffer& buf) override
	{
		// Call base class
		TCPServerClientInfo::onSocketReceive(buf);

		std::string str;
		if (!buf.tryRead(str))
			return;

		// If required, simulate an expensive call
		if (waitMs)
		{
			auto start = std::chrono::high_resolution_clock::now();
			while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() <
				   (double)waitMs)
			{
			}
		}

		// We send back the echo, and disconnect the client
		ChunkBuffer out;
		out << str;
		m_socket->send(std::move(out));
		// Disconnect
		removeClient();
	}

	int waitMs;
};

SUITE(TCPSocket)
{

TEST(SimpleListen)
{
	CompletionPort iocp(1);
	TCPServerSocket serverSocket(iocp, serverPort, [](std::unique_ptr<TCPSocket> client)
	{
	});
}

TEST(InvalidPort)
{
	CompletionPort iocp(1);
	CHECK_THROW(
		TCPServerSocket serverSocket(iocp, someReservedPort, [](std::unique_ptr<TCPSocket> client) {}),
		std::runtime_error);
}


// Test connecting one client, send one string to the server, and wait the reply
TEST(EchoSingle)
{
	TCPServer echoServer(serverPort, 1, [](TCPServer* owner, std::unique_ptr<TCPSocket> socket)
						 {
							 return std::make_unique<EchoServerConnection>(owner, std::move(socket));
						 });

	CompletionPort iocp(1);
	EchoClient echoClient(iocp, "127.0.0.1", serverPort, "Hello World!");
	echoClient.send();
	echoClient.wait();
}

double testMultipleThreads(int serverThreads, int clientThreads, int blockMs)
{
	TCPServer echoServer(serverPort, serverThreads, [=](TCPServer* owner, std::unique_ptr<TCPSocket> socket)
						 {
							 return std::make_unique<EchoServerConnection>(owner, std::move(socket), blockMs);
						 });

	CompletionPort iocp(clientThreads);

	const int numClients = 128;
	std::vector<std::unique_ptr<EchoClient>> clients;
	for (int i = 0; i < numClients; i++)
	{
		auto str = std::to_string(i) + " Hello World!";
		auto c = std::make_unique<EchoClient>(iocp, "127.0.0.1", serverPort, str.c_str());
		clients.push_back(std::move(c));
	}

	// Wait a bit, so the server has time to accept all clients
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		CHECK_EQUAL(numClients, echoServer.getNumClients());
	}

	UnitTest::Timer timer;
	timer.Start();
	for (auto&& c : clients)
		c->send();
	for (auto&& c : clients)
		c->wait();
	return timer.GetTimeInMs();
}


//
// Check for scalability problems, by testing the server with several threads
//
// This test can easly fail if the machine doesn't have enough cores for this test.
// Use this just as a guideline
#if 1
TEST(TestScalability)
{
	printf("*** %s ***\n", this->m_details.testName);
	std::vector<double> times;
	times.push_back(0);
	for (int i = 1; i <= 4; i++)
		times.push_back(testMultipleThreads(i, 1, 5 ));

	const auto errorThreshold = 10.0; // #TODO Change this back to 10
	printf("1 thread(s): time %.4fms\n", times[1]);
	for (int i = 2; i < times.size(); i++)
	{
		auto t = times[i];
		auto expected = times[1] / i;
		auto error = (std::abs(times[i] - expected) / expected) * 100;
		printf("%d thread(s): time %.4fms, expected %.4fms, error %.4f%%\n", i, times[i], expected, error);
		CHECK(error < errorThreshold);
	}
}
#endif

#define BIGDATATEST_FILL 0
#define BIGDATATEST_PENDINGREADS_COUNT 10
#define BIGDATATEST_PENDINGREADS_SIZE (4096*20)
TEST(BigData)
{
	printf("*** %s ***\n", this->m_details.testName);
	class BigDataServerConnection : public TCPServerClientInfo
	{
	public:
		explicit BigDataServerConnection(TCPServer* owner, std::unique_ptr<TCPSocket> socket)
			: TCPServerClientInfo(owner, std::move(socket))
		{
			m_timer.Start();
		}
		virtual ~BigDataServerConnection()
		{
			auto ms = m_timer.GetTimeInMs();
			printf("(%lld bytes) %.2fMB received in %.2f ms. %fmbps/sec\n", m_received, m_received / (1000.0 * 1000.0), ms,
				   ((m_received*8) / (1000.0 * 1000.0)) / (ms / 1000.0));
		}
		virtual void onSocketReceive(const ChunkBuffer& buf) override

		{
			TCPServerClientInfo::onSocketReceive(buf); // Call base class

			unsigned char tmp[4096*10];
			auto size = buf.calcSize();
			bool ok = true;
			while (size)
			{
				auto len = std::min(size, static_cast<unsigned int>(sizeof(tmp)));
				buf.read(&tmp[0], len);

				// Check if we got what we expected
				if (BIGDATATEST_FILL)
				{
					for (unsigned i = 0; i < len; i++)
					{
						ok = ok && ((m_received & 0xFF) == tmp[i]);
						m_received++;
					}
				}
				else
				{
					m_received += len;
				}
				size -= len;
			}
			CHECK(ok);
		}
		uint64_t m_received = 0;
		UnitTest::Timer m_timer;
	};

	class Connection
	{
	public:
		enum
		{
			kMinimumPendingSend = 1024 * 1014 * 5
		};

		Connection(CompletionPort& iocp, const char* ip, int port, uint64_t transferSize)
			: m_socket(iocp, BIGDATATEST_PENDINGREADS_COUNT, BIGDATATEST_PENDINGREADS_SIZE), m_transferSize(transferSize)
		{

			m_socket.setOnSendCompleted([this]()
			{
				onSocketSendCompleted();
			});

			CHECK(m_socket.connect(ip, port).get());

			m_sendThread = std::thread([&]
			{
				unsigned char tmp[1024*512];

				// To send a random amount of bytes
				std::random_device rd;
				std::mt19937 eng(rd());
				std::uniform_int_distribution<> distr(sizeof(tmp) / 2, sizeof(tmp));
				uint64_t sent = 0;
				while (sent != m_transferSize)
				{
					// Pick a random amount of bytes to transfer, and fill that size with
					// the data to send
					int todo = static_cast<int>(std::min((uint64_t)distr(eng), m_transferSize - sent));
					if (BIGDATATEST_FILL)
					{
						for (int i = 0; i < todo; i++)
						{
							tmp[i] = sent & 0xFF;
							sent++;
						}
					}
					else
					{
						sent += todo;
					}

					ChunkBuffer out;
					out.write(&tmp[0], todo);
					CHECK(m_socket.send(std::move(out)));

					while (m_socket.getPendingSendBytes() >= kMinimumPendingSend)
						m_sendMore.wait();
				}
			});

		}

		~Connection()
		{
			// Wait for all the data to be transfered (which finished the thread)
			m_sendThread.join();

			// First remove us as a socket listener, so we don't get any notification when our member variables are being
			// destroyed
			m_socket.resetCallbacks();

			m_socket.shutdown();
		}

		void onSocketSendCompleted()
		{
			m_sendMore.notify();
		}

		TCPSocket m_socket;
		uint64_t m_transferSize;
		uint64_t m_received = 0;
		std::thread m_sendThread;
		Semaphore m_sendMore;
	};

	{
		TCPServer server(serverPort, 1,
						 [](TCPServer* owner, std::unique_ptr<TCPSocket> socket)
						 {
							 return std::make_unique<BigDataServerConnection>(owner, std::move(socket));
						 },
						 BIGDATATEST_PENDINGREADS_COUNT, BIGDATATEST_PENDINGREADS_SIZE);
		UnitTest::Timer t;
		uint64_t size=0;
		t.Start();

		// Client
		{
			CompletionPort iocp(1);
			size = (uint64_t)1000 * 1000 * 200;
			Connection client(iocp, "127.0.0.1", serverPort, size);
		}
		auto ms = t.GetTimeInMs();
		printf("(%lld bytes) %.2fMB transfered in %.2f ms. %fmbps/sec\n", size, size / (1000.0 * 1000.0), ms, ((size*8) / (1000.0 * 1000.0)) / (ms / 1000.0));
	}
}


}