#include "UnitTestsPCH.h"

using namespace cz;


SUITE(TimerQueue)
{

struct DebugInfo
{
	int resIdx;
	double elapsed;
	double error;
};

TEST(TestTimerQueue)
{
	return;
	TimerQueue q;

	UnitTest::Timer timer;
	timer.Start();

	std::vector<DebugInfo> res;
	std::map<int, int> resIdx;
	ZeroSemaphore pending;
	auto enqueue = [&q, &pending, &res, &resIdx, &timer](int expectedResIdx, int interval, int spin, bool expectedAborted) -> int64_t
	{
		pending.increment();
		auto id = q.add(
			interval,
			[&pending, &res, &resIdx, &timer, expectedAborted, spin, expectedResIdx, interval, start = timer.GetTimeInMs()](bool aborted)
		{
			CHECK(aborted==expectedAborted);
			auto elapsed = timer.GetTimeInMs() - start;
			res.push_back({ expectedResIdx, elapsed, elapsed - interval });
			resIdx[expectedResIdx]++;
			if (spin)
				spinMs(spin);
			pending.decrement();
		});
		return id;
	};


	const double toleranceMs = 2;
#define checkReceivedAll(expectedIntervals_) \
	{ \
		std::vector<double> expectedIntervals = expectedIntervals_; \
		CHECK_EQUAL(res.size(), resIdx.size()); \
		CHECK_EQUAL(res.size(), expectedIntervals.size()); \
		for (auto&& i : resIdx) \
			CHECK_EQUAL(1, i.second); \
		for (size_t i = 0; i < res.size(); i++) \
		{ \
			CHECK_CLOSE(expectedIntervals[i] , res[i].elapsed, toleranceMs); \
			CZ_LOG(logTestsVerbose, Log, "Handler %d: Elapsed: %5.3fms. Error: %2.3fms", i, res[i].elapsed, res[i].error); \
		} \
		res.clear(); \
		resIdx.clear(); \
	}

	//
	// Test with fast handlers that don't spin to waste cpu.
	{
		CZ_LOG(logTests, Log, "Handlers with no spin");
		res.clear();
		int interval = 100;
		for (int i = 0; i < 5; i++)
		{
			enqueue(i, interval, 0, false);
			spinMs(1);
		}
		pending.wait();
		checkReceivedAll(std::vector<double>(res.size(), interval))
	}

	//
	// Test with handlers that spin for a bit, to waste cpu.
	// This tests if the scheduler deals with ever late handlers
	{
		CZ_LOG(logTests, Log, "Handlers with spin");
		res.clear();
		int interval = 100;
		std::vector<double> expected;
		for (int i = 0; i < 5; i++)
		{
			expected.push_back(interval + i * 10);
			enqueue(i, interval, 10, false);
		}
		pending.wait();
		checkReceivedAll(expected);
	}

	//
	// Test canceling all
	{
		CZ_LOG(logTests, Log, "Cancelling all");
		res.clear();
		{
			int interval = 100;
			int64_t id;
			for (int i = 0; i < 10; i++)
				id = enqueue(i, interval, 0, true);
			CHECK_EQUAL(10, q.cancelAll());
		}

		pending.wait();
		checkReceivedAll(std::vector<double>(res.size(), 0));
	}


	//
	// Test canceling 1
	//
	{
		CZ_LOG(logTests, Log, "Canceling one");
		res.clear();
		enqueue(3, 50, 0, false);
		enqueue(1, 20, 0, false);
		enqueue(2, 30, 0, false);
		int64_t id = enqueue(0, 25, 0, true);
		CHECK_EQUAL(1, q.cancel(id));
		pending.wait();
		checkReceivedAll(std::vector<double>({0, 20, 30, 50}));
	}
}


}

SUITE(DeadlinerTimer)
{

TEST(Test1)
{
	CompletionPort iocp;
	auto th = std::thread([&iocp]()
	{
		iocp.run();
	});

	UnitTest::Timer timer;
	DeadlineTimer t(iocp, 50);
	ZeroSemaphore pending;
	timer.Start();
	t.expiresFromNow(50);

	std::vector<std::function<void()>> res;
	auto addRes = [&timer, &res](int num, bool aborted, bool abortedExpected, int expected)
	{
		res.emplace_back([elapsed=timer.GetTimeInMs(), num, expected, aborted, abortedExpected]()
		{
			CHECK_EQUAL(aborted, abortedExpected);
			CHECK_CLOSE(expected, elapsed, 4);
			CZ_LOG(logTestsVerbose, Log, "%d: Elapsed: %4.3f. aborted=%d", num, elapsed, (int)aborted);
		});
	};
	auto checkRes = [&]
	{
		pending.wait();
		for (auto&& f : res)
			f();
		res.clear();
	};

	// This one is canceled right away
	pending.increment();
	t.asyncWait([&](bool aborted)
	{
		addRes(1, aborted, true, 0);
		pending.decrement();
	});
	t.cancel();

	pending.increment();
	t.asyncWait([&](bool aborted)
	{
		addRes(2, aborted, false, 50);
		pending.decrement();
	});
	//checkRes();

	iocp.stop();
	th.join();
}


}
