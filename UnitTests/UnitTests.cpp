#include "UnitTestsPCH.h"

/*
int _tmain(int argc, _TCHAR* argv[])
{
	cz::net::initializeSocketsLib();
	auto res = UnitTest::RunAllTests();
	cz::net::shutdownSocketsLib();
	return res;
}
*/

using namespace cz;
using namespace cz::net;
using namespace cz::rpc;

#define CONTINUOUS_TESTING 0

std::string getCurrentTestName()
{
	auto test = UnitTest::CurrentTest::Details();
	return test->testName;
}

int runAllTests()
{
	auto res = UnitTest::RunAllTests();
	if (res != EXIT_SUCCESS)
		return res;
	return res;
}


// #TODO remove this crap, or refactor it?
class RPCQueueImplementation : public rpc::RPCWorkQueue
{

public:
	AsyncCommandQueueAutomatic cmdQueue;
	virtual void push(std::function<void()> work) override
	{
		cmdQueue.send(std::move(work));
	}

	unsigned size()
	{
		return cmdQueue.getQueue().size();
	}
};


rpc::RPCWorkQueue* gRpcQueue;

void waitForQueueToFinish()
{
	if (gRpcQueue)
	{
		CZ_ASSERT(static_cast<RPCQueueImplementation*>(gRpcQueue)->size() == 0);
		return;
		auto pr = std::make_shared<std::promise<void>>();
		gRpcQueue->push([&]() { pr->set_value();});
		pr->get_future().get();
	}
}

int runAllTestsQueued()
{
	RPCQueueImplementation rpcQueue;
	gRpcQueue = &rpcQueue;
	bool rpcQueueThreadFinish = false;

	int ret = runAllTests();

	gRpcQueue = nullptr;
	return ret;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int c = 0;
	WindowsConsole con(120, 40, 300, 1000);
	logTestsVerbose.setVerbosity(LogVerbosity::Warning);
	//logTests.setVerbosity(LogVerbosity::Warning);
	logNet.setVerbosity(LogVerbosity::Error);

	// So Sleep calls are as accurate as possible
	timeBeginPeriod(1);

#if CONTINUOUS_TESTING
	while (true)
	{
		printf("*** Run %d\n***", c++);
		int ret;

		printf("Running with RPC processing in place\n");
		ret = runAllTests();
		if (ret!= EXIT_SUCCESS)
			return ret;

		printf("Running with RPC processing queued\n");
		ret = runAllTestsQueued();
		if (ret != EXIT_SUCCESS)
		{
			system("pause");
			return ret;
		}

	}
	return EXIT_SUCCESS;
#else
	auto res = UnitTest::RunAllTests();
	return EXIT_SUCCESS;
#endif
}
