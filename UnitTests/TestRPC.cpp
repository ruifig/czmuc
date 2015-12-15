#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::rpc;
using namespace cz::net;

extern RPCWorkQueue* gRpcQueue;
extern std::thread* gRcvQueueThread;
extern void waitForQueueToFinish();

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
class Object
{
public:
	const char* getName()
	{
		return "Object";
	}
};
#define RPCTABLE_CONTENTS_Object \
	REGISTERRPC(getName)

#define RPCTABLE_CLASS Object
#define RPCTABLE_CONTENTS \
	RPCTABLE_CONTENTS_Object
#include "crazygaze/rpc/RPCGenerate.h"


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// For testing with interfaces and virtual functions
class CalculatorInterface : public Object
{
public:
	virtual int multiply(int a, int b) = 0;
	virtual int divide(int a, int b) = 0;
};

#define RPCTABLE_CONTENTS_CalculatorInterface \
	RPCTABLE_CONTENTS_Object \
	REGISTERRPC(multiply) \
	REGISTERRPC(divide)

#define RPCTABLE_CLASS CalculatorInterface
#define RPCTABLE_CONTENTS \
	RPCTABLE_CONTENTS_CalculatorInterface
#include "crazygaze/rpc/RPCGenerate.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
class Calculator  : public CalculatorInterface
{
public:
	int add(int a, int b)
	{
		return a + b;
	}

	int subtract(int a, int b)
	{
		return a - b;
	}

	virtual int multiply(int a, int b) override
	{
		return a*b;
	}
	virtual int divide(int a, int b) override
	{
		return a / b;
	}

	/*
	std::future<int> slowSum(int a, int b)
	{
		return std::async(std::launch::async, [=]
		{
			Sleep(10);
			return a + b;}
		);
	}
	*/
	Future<int> slowSum(int a, int b)
	{
		Promise<int> pr;
		pr.set_value(a + b);
		return pr.get_future();
	}

	std::vector<int> primeNumbers()
	{
		return{ 2,3,5 };
	}

	void doNothing(const std::string& err)
	{
		if (err != "")
			throw std::runtime_error(err);
	}
};

#define RPCTABLE_CLASS Calculator
#define RPCTABLE_CONTENTS \
	RPCTABLE_CONTENTS_CalculatorInterface \
	REGISTERRPC(add) \
	REGISTERRPC(subtract) \
	REGISTERRPC(slowSum) \
	REGISTERRPC(primeNumbers) \
	REGISTERRPC(doNothing)
#include "crazygaze/rpc/RPCGenerate.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
class RPCTest
{
public:

	const char* addStrings1(const char* a, const char* b)
	{
		static char res[128];
		auto tmp = std::string(a) + b;
		memcpy(res, tmp.c_str(), tmp.size()+1);
		return res;
	}

	// By value and const ref
	std::string addStrings2(const char* a, std::string b, const std::string& c)
	{
		return std::string(a) + b + c;
	}

	// Test rvalue parameter
	void setText(std::string&& txt)
	{
		m_text = std::move(txt);
	}

	std::string getText() const
	{
		return m_text;
	}

	void doSomething1()
	{
		m_text = "1";
	}
	void doSomething2() const
	{
		m_text = "2";

	}

	int broken(int a)
	{
		throw std::runtime_error("Failed rpc");
		return a;
	}

	void brokenVoid(int a)
	{
		throw std::runtime_error("void RPC failed");
	}

	// These will just call something back on the client, to test RPCs both ways
	void doFunc1(int v);
	void doFunc2(const std::string& v);
	void doFunc3(const std::string& v)
	{
		m_doFunc3.set_value(v);
	}

	mutable std::string m_text;
	Future<int> m_doFunc1Ret;
	Promise<std::string> m_doFunc3;
};

#define RPCTABLE_CLASS RPCTest
#define RPCTABLE_CONTENTS \
	REGISTERRPC(addStrings1) \
	REGISTERRPC(addStrings2) \
	REGISTERRPC(setText) \
	REGISTERRPC(getText) \
	REGISTERRPC(doSomething1) \
	REGISTERRPC(doSomething2) \
	REGISTERRPC(broken) \
	REGISTERRPC(brokenVoid) \
	REGISTERRPC(doFunc1) \
	REGISTERRPC(doFunc2) \
	REGISTERRPC(doFunc3)
#include "crazygaze/rpc/RPCGenerate.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//! To test Server->Client RPCs
struct RPCTestClient
{
	int func1(int v);
	void func2(const std::string& str);

	std::promise<int> p1;
	std::promise<std::string> p2;
	Connection<RPCTest, RPCTestClient>* connection;
};

#define RPCTABLE_CLASS RPCTestClient
#define RPCTABLE_CONTENTS \
	REGISTERRPC(func1) \
	REGISTERRPC(func2)
#include "crazygaze/rpc/RPCGenerate.h"

// NOTE: Can't put these in the class itself, because at that point, the RPC Tables are not defined yet
int RPCTestClient::func1(int v)
{
	p1.set_value(v);
	CALLRPC(*connection, doFunc3, "Back to server");
	return v + 1;
}
void RPCTestClient::func2(const std::string& str)
{
	p2.set_value(str);
}

// NOTE: Can't put these in the class itself, because at that point, the RPC Tables are not defined yet
void RPCTest::doFunc1(int v)
{
	auto client = Connection<RPCTestClient, RPCTest>::getCurrent();
	m_doFunc1Ret = CALLRPC(*client, func1, v);
}
void RPCTest::doFunc2(const std::string& v)
{
	auto client = Connection<RPCTestClient, RPCTest>::getCurrent();
	CALLRPC(*client, func2, v);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

SUITE(RPC)
{
TEST(Simple)
{
	Semaphore serverRunning, finish;
	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<Calculator> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// Wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<Calculator> calcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		auto res1 = CALLRPC(calcClient, add, 1, 2);
		auto res2 = CALLRPC(calcClient, subtract, 10, 1);
		auto res3 = CALLRPC(calcClient, multiply, 4, 2);
		auto res4 = CALLRPC(calcClient, add, 4, 2); // Call something but don't keep the future
		auto res5 = CALLRPC(calcClient, primeNumbers);

		std::vector<cz::Any> params;
		auto res6 = CALLGENERICRPC(calcClient, "primeNumbers", params);

		CHECK_EQUAL(3, res1.get());
		CHECK_EQUAL(9, res2.get());
		CHECK_EQUAL(8, res3.get());

		std::vector<int> v{ 2,3,5 };
		CHECK_ARRAY_EQUAL(v, res5.get(), (int)v.size());
		CHECK(res6.get() == to_json(v));
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

TEST(IgnoredFutured)
{
	Semaphore serverRunning, finish;
	bool serverFinished = false;
	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<Calculator> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// Wait for the client to finish
		finish.wait();
		serverFinished = true;
	});

	// wait for the server to be running
	serverRunning.wait();

	bool clientFinished = false;
	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<Calculator> calcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));
		auto res1 = CALLRPC(calcClient, add, 1, 2);
		CALLRPC(calcClient, add, 4, 2); // Call something but don't keep the future
		clientFinished = true;
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

TEST(VariousParameters)
{
	Semaphore serverRunning, finish;
	auto serverThread = std::thread([&]()
	{
		RPCTest obj;
		CompletionPort iocp(1);
		Server<RPCTest> rpcServer(obj, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// Wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(4);
		Connection<RPCTest> rpcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		// Test strings
		{
			auto res1 = CALLRPC(rpcClient, addStrings1, "Hi ", "There");
			auto res2 = CALLRPC(rpcClient, addStrings1, std::string("Hi "), "There");
			auto res3 = CALLRPC(rpcClient, addStrings1, "Hi ", std::string("There"));
			auto res4 = CALLRPC(rpcClient, addStrings1, std::string("Hi "), std::string("There"));

			// Check if a return type of const char* is treated as std::string
			static_assert(std::is_same<std::decay<decltype(res1.get())>::type, std::string>::value, "Wrong type");

			CHECK_EQUAL(std::string("Hi There"), res1.get());
			CHECK_EQUAL(std::string("Hi There"), res2.get());
			CHECK_EQUAL(std::string("Hi There"), res3.get());
			CHECK_EQUAL(std::string("Hi There"), res4.get());
		}

		// Test by value and const refs
		{
			auto res1 = CALLRPC(rpcClient, addStrings2, "Hi ", "Ther", "e");
			CHECK_EQUAL(std::string("Hi There"), res1.get());
		}

		// Test no parameters and no return value
		{
			auto res1 = CALLRPC(rpcClient, setText, "hello");
			res1.get();
			auto res2 = CALLRPC(rpcClient, getText);
			CHECK_EQUAL(std::string("hello"), res2.get());

			CALLRPC(rpcClient, doSomething1);
			res2 = CALLRPC(rpcClient, getText);
			CHECK_EQUAL(std::string("1"), res2.get());
			CALLRPC(rpcClient, doSomething2);
			res2 = CALLRPC(rpcClient, getText);
			CHECK_EQUAL(std::string("2"), res2.get());
		}

	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

//
// Test RPCs for a pure interface
TEST(Virtuals)
{
	Semaphore serverRunning, finish;

	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<CalculatorInterface> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<CalculatorInterface> calcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		auto res1 = CALLRPC(calcClient, multiply, 4, 2);
		auto res2 = CALLRPC(calcClient, divide, 6, 2);

		CHECK_EQUAL(8, res1.get());
		CHECK_EQUAL(3, res2.get());
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

//
// Test inheritance
TEST(Inheritance)
{
	Semaphore serverRunning, finish;

	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<Calculator> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		std::unique_ptr<BaseConnection> base  =
			std::unique_ptr<Connection<Calculator>>( new Connection<Calculator>(
			std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue)));


		{
			auto obj = static_cast<Connection<Object>*>(base.get());
			auto name = CALLRPC(*obj, getName);
			CHECK_EQUAL("Object", name.get());
		}

		{
			auto obj = static_cast<Connection<CalculatorInterface>*>(base.get());
			auto name = CALLRPC(*obj, getName);
			auto res1 = CALLRPC(*obj, multiply, 4, 2);
			auto res2 = CALLRPC(*obj, divide, 6, 2);
			CHECK_EQUAL("Object", name.get());
			CHECK_EQUAL(8, res1.get());
			CHECK_EQUAL(3, res2.get());
		}

		{
			auto obj = static_cast<Connection<Calculator>*>(base.get());
			auto name = CALLRPC(*obj, getName);
			auto res1 = CALLRPC(*obj, multiply, 4, 2);
			auto res2 = CALLRPC(*obj, divide, 6, 2);
			auto res3 = CALLRPC(*obj, add, 10, 1);
			CHECK_EQUAL("Object", name.get());
			CHECK_EQUAL(8, res1.get());
			CHECK_EQUAL(3, res2.get());
			CHECK_EQUAL(11, res3.get());
		}

	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}


TEST(ConnectFailure)
{
	CompletionPort iocp(1);
	CHECK_THROW(TCPTransport("127.0.0.1", 28000, iocp, gRpcQueue), std::runtime_error);
	waitForQueueToFinish();
}

template<typename R>
void checkRPCThrow(R&& res, const std::string& what)
{
	bool failed = false;
	try
	{
		res.get();
	}
	catch (std::exception& e)
	{
		CHECK_EQUAL(what, e.what());
		failed = true;
	}
	CHECK(failed == true);
}

//! Test an RPC throwing an exception, and passing it back to the client
TEST(RPCFailure)
{
	Semaphore serverRunning, finish;

	auto serverThread = std::thread([&]()
	{
		RPCTest calc;
		CompletionPort iocp(1);
		Server<RPCTest> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// wait for the client to finish
		finish.wait();
	});
	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<RPCTest> client(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		checkRPCThrow(CALLRPC(client, broken, 1), "Failed rpc");

		// Test a void RPC sending an exception, since the client does expect data from those
		{
			std::promise<std::string> pr;
			client.setExceptionCallback([&](RPCHeader hdr, const BaseRPCInfo& rpcInfo, const std::string& err)
			{
				pr.set_value(err);
			});
			CALLRPC(client, brokenVoid, 1);
			CHECK_EQUAL("void RPC failed", pr.get_future().get());
			// Remove callback
			client.setExceptionCallback([&](RPCHeader hdr, const BaseRPCInfo& rpcInfo, const std::string& err)
			{
			});
		}

		// Try a normal RPC, to check if the data flow is not broken
		CHECK_EQUAL("Hi There", CALLRPC(client, addStrings1, "Hi ", "There").get());
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

TEST(ConnectionDropped)
{
	Semaphore serverRunning, finish;

	auto serverThread = std::thread([&]()
	{
		RPCTest calc;
		CompletionPort iocp(1);
		Server<RPCTest> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// wait
		finish.wait();
	});
	// wait for the server to be running
	serverRunning.wait();
	Sleep(1);

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<RPCTest> client(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		// Signal the server to finish, so it will fail when we call an RPC
		finish.notify();
		serverThread.join();

		// Now, calling the RPC should fail
		auto res = CALLRPC(client, addStrings1, "Hi ", "There");
		CHECK_THROW(res.get(), FutureError);
	});

	clientThread.join();
	waitForQueueToFinish();
}

TEST(BothDirections)
{
	Semaphore serverRunning, finish;

	auto serverThread = std::thread([&]()
	{
		RPCTest calc;
		CompletionPort iocp(1);
		{
			Server<RPCTest, RPCTestClient> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
			serverRunning.notify();

			// wait for the client to finish
			finish.wait();
		}

		// This is outside the server scope, to make sure the future we are checking has time to be set
		CHECK_EQUAL(1234 + 1, calc.m_doFunc1Ret.get());

		CHECK_EQUAL("Back to server", calc.m_doFunc3.get_future().get());
	});
	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(2);
		RPCTestClient obj;
		Connection<RPCTest, RPCTestClient> client(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue), obj);
		obj.connection = &client;

		CALLRPC(client, doFunc1, 1234);
		CALLRPC(client, doFunc2, "Hi there");
		CHECK_EQUAL(1234, obj.p1.get_future().get());
		CHECK_EQUAL("Hi there", obj.p2.get_future().get());

	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}


TEST(FutureReturnValue)
{
	Semaphore serverRunning, finish;
	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<Calculator> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// Wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<Calculator> calcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));
		auto res1 = CALLRPC(calcClient, slowSum, 1, 2);
		CHECK_EQUAL(3, res1.get());
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}

template<typename T>
T convertAny(const cz::Any& a)
{
	T v;
	if (!a.getAs(v))
		throw std::runtime_error("Invalid type");
	return v;
}

TEST(GenericRPC)
{
	Semaphore serverRunning, finish;
	auto serverThread = std::thread([&]()
	{
		Calculator calc;
		CompletionPort iocp(1);
		Server<Calculator> calcServer(calc, std::make_unique<TCPServerTransport>(28000, iocp, gRpcQueue));
		serverRunning.notify();
		// Wait for the client to finish
		finish.wait();
	});

	// wait for the server to be running
	serverRunning.wait();

	auto clientThread = std::thread([&]()
	{
		CompletionPort iocp(1);
		Connection<GenericRPCClass> calcClient(std::make_unique<TCPTransport>("127.0.0.1", 28000, iocp, gRpcQueue));

		std::vector<Any> params;
		params.push_back(Any(1));
		params.push_back(Any(2));

		auto res1 = CALLGENERICRPC(calcClient, "add", params);
		CHECK_EQUAL("3", res1.get());

		checkRPCThrow(CALLGENERICRPC(calcClient, "addd", params), "Unknown RPC (addd)");

		// Try a void generic RPC sending an exception
		{
			std::vector<Any> params;
			params.push_back(Any("generic RPC failed"));
			checkRPCThrow(CALLGENERICRPC(calcClient, "doNothing", params), "generic RPC failed");
		}

		// Test generic RPC with future
		auto res2 = CALLGENERICRPC(calcClient, "slowSum", params);
		CHECK_EQUAL("3", res2.get());

		{
			std::vector<Any> params;
			params.push_back(Any(1));
			params.push_back(Any(2));
			auto res = CALLGENERICRPC(calcClient, "add", params);
			CHECK_EQUAL("3", res.get());
		}

		{
			std::vector<Any> params;
			params.push_back(Any(1));
			params.push_back(Any(2));
			checkRPCThrow(CALLGENERICRPC(calcClient, "addd", params), "Unknown RPC (addd)");
		}

		{
			std::vector<Any> params;
			params.push_back(Any(1));
			params.push_back(Any("Hello"));
			checkRPCThrow(CALLGENERICRPC(calcClient, "add", params), "Invalid parameter count or types");
		}
	});

	clientThread.join();
	finish.notify();
	serverThread.join();
	waitForQueueToFinish();
}


}

