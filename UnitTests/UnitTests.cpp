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

#define CONTINUOUS_TESTING 1

std::string getCurrentTestName()
{
	auto test = UnitTest::CurrentTest::Details();
	return test->testName;
}

int runAllTests()
{
	initializeSocketsLib();
	auto res = UnitTest::RunAllTests();
	shutdownSocketsLib();
	if (res != EXIT_SUCCESS)
		return res;
	debugData.checkAndReset();
	return res;
}


// #TODO remove this crap, or refactor it?
WorkQueue* gRcvQueue;
bool gRcvQueueThreadFinish = false;
std::thread* gRcvQueueThread;
void waitForQueueToFinish()
{
	if (gRcvQueue)
	{
		CZ_ASSERT(gRcvQueue->size() == 0);
		return;
		auto pr = std::make_shared<std::promise<void>>();
		gRcvQueue->push([&]() { pr->set_value();});
		pr->get_future().get();
	}
}

int runAllTestsQueued()
{
	WorkQueue q;
	gRcvQueue = &q;
	gRcvQueueThreadFinish = false;

	auto qThread = std::thread([&q]
	{
		while (!gRcvQueueThreadFinish)
		{
			std::function<void()> f;
			q.wait_and_pop(f);
			f();
		}
	});
	gRcvQueueThread = &qThread;
	int ret = runAllTests();

	gRcvQueue->push([&]() { gRcvQueueThreadFinish = true;});
	gRcvQueueThread->join();

	gRcvQueue = nullptr;
	gRcvQueueThread = nullptr;
	return ret;
}

#if CONTINUOUS_TESTING
int _tmain(int argc, _TCHAR* argv[])
{
	int c = 0;
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
		if (ret!= EXIT_SUCCESS)
			return ret;
	}
	return EXIT_SUCCESS;
}

#else
int _tmain(int argc, _TCHAR* argv[])
{
	initializeSocketsLib();
	auto res = UnitTest::RunAllTests();
	shutdownSocketsLib();
	return EXIT_SUCCESS;
}
#endif
