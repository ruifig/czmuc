#include "UnitTestsPCH.h"

using namespace cz;

SUITE(ThreadingUtils)
{

TEST(AsyncVar_1)
{
	Semaphore checkpoint1;
	Semaphore checkpoint2;
	AsyncVar<std::string> var("1");
	auto t = std::thread([&]
	{
		checkpoint1.wait();
		(*var.writer())[0]++;
		checkpoint2.notify();
		checkpoint1.wait();
		(*var.writer())[0]++;
		(*var.writer())[0]++;
		checkpoint2.notify();
	});

	CHECK_EQUAL("1", var.get());
	checkpoint1.notify();
	checkpoint2.wait();
	CHECK_EQUAL("2", var.get());
	checkpoint1.notify();
	checkpoint2.wait();
	CHECK_EQUAL("4", var.get());

	t.join();
}
}

