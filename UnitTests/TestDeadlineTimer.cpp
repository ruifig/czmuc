#include "UnitTestsPCH.h"

using namespace cz;

SUITE(DeadlinerTimer)
{

TEST(Test1)
{
	return;

	CompletionPort iocp;
	auto th = std::thread([&iocp]()
	{
		iocp.run();
	});

	UnitTest::Timer timer;
	timer.Start();
	DeadlineTimer t(iocp, 2000);
	//Sleep(1050);
	auto isset = t._isSet();
	ZeroSemaphore pending;
	pending.increment();
	t.expiresFromNow(2000);
	t.asyncWait([&](DeadlineTimerResult err)
	{
		CZ_LOG(logTests, Log, "1: Elapsed: %4.3f. Res=%d\n", timer.GetTimeInMs(), (int)err.code);
		pending.decrement();
	});
	t.cancel();
	Sleep(500);
	pending.increment();
	t.asyncWait([&](DeadlineTimerResult err)
	{
		CZ_LOG(logTests, Log, "2: Elapsed: %4.3f. Res=%d\n", timer.GetTimeInMs(), (int)err.code);
		pending.decrement();
	});

	pending.wait();
	iocp.stop();
	th.join();
}

TEST(TestTimerQueue)
{
	TimerQueue q;

	UnitTest::Timer timer;
	timer.Start();

	std::vector<std::pair<double, double>> res;
	ZeroSemaphore pending;
	auto enqueue = [&q, &pending, &res, &timer](int i, int interval, int spin, bool expectedAborted) -> int64_t
	{
		pending.increment();
		auto id = q.add([&pending, &res, &timer, expectedAborted, spin, i, interval, start = timer.GetTimeInMs()](bool aborted)
		{
			CHECK(aborted==expectedAborted);
			auto elapsed = timer.GetTimeInMs() - start;
			res.emplace_back(elapsed, elapsed - interval);
			//CZ_LOG(logTests, Log, "Handler %d: Interval: %5.3fms. Error: %2.3fms", i, elapsed, elapsed - interval);
			if (spin)
				spinMs(spin);
			pending.decrement();
		}, interval);
		return id;
	};


	//
	// Test with fast handlers that don't spin to waste cpu.
	const double toleranceMs = 2;
	{
		CZ_LOG(logTests, Log, "Handlers with no spin");
		res.clear();
		int interval = 100;
		for (int i = 0; i < 5; i++)
			enqueue(i, interval, 0, false);
		pending.wait();
		for (int i = 0; i < (int)res.size(); i++)
		{
			CHECK_CLOSE(0, res[i].second, toleranceMs);
			CZ_LOG(logTests, Log, "Handler %d: Interval: %5.3fms. Error: %2.3fms", i, res[i].first, res[i].second);
		}
	}

	//
	// Test with handlers that spin for a bit, to waste cpu.
	// This tests if the scheduler deals with ever late handlers
	{
		CZ_LOG(logTests, Log, "Handlers with spin");
		res.clear();
		int interval = 100;
		for (int i = 0; i < 5; i++)
			enqueue(i, interval, 10, false);
		pending.wait();
		for (int i = 0; i < (int)res.size(); i++)
		{
			CHECK_CLOSE(interval+i*10, res[i].first, toleranceMs);
			CZ_LOG(logTests, Log, "Handler %d: Interval: %5.3fms. Error: %2.3fms", i, res[i].first, res[i].second);
		}
	}

	//
	// Test canceling all
	//while (true)
	{
		CZ_LOG(logTests, Log, "Cancelling all");
		res.clear();
		{
			int interval = 100;
			int64_t id;
			for (int i = 0; i < 10; i++)
				id = enqueue(i, interval, 0, true);
			CHECK_EQUAL(10, q.cancelAll());
			pending.wait();
		}

		for (int i = 0; i < (int)res.size(); i++)
		{
			CHECK_CLOSE(0, res[i].first, toleranceMs);
			CZ_LOG(logTests, Log, "Handler %d: Interval: %5.3fms. Error: %2.3fms", i, res[i].first, res[i].second);
		}
	}

}

}
