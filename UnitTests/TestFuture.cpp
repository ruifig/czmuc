#include "UnitTestsPCH.h"

using namespace cz;

SUITE(Future)
{

TEST(Future_Simple)
{
	//
	// Simple sequential test
	Promise<std::string> pr;
	auto ft1 = pr.get_future();
	pr.set_value("Hello");
	CHECK_EQUAL(std::string("Hello"), ft1.get());
}

// Async waiting
// The main thread waits on a result, which should arrive in X milliseconds
// 
TEST(Future_Async)
{
	UnitTest::Timer timer;
	timer.Start();

	Promise<std::string> pr;
	auto ft1 = pr.get_future();
	Semaphore checkpoint1;
	auto t = std::thread([&checkpoint1, pr = std::move(pr)]() mutable
	{
		// Wait for the main thread be ready to wait on the future, so we can measure the time
		checkpoint1.wait();
		UnitTest::TimeHelpers::SleepMs(100);
		pr.set_value("Hello");
	});

	checkpoint1.notify();
	auto t1 = timer.GetTimeInMs();
	auto res = ft1.get();
	auto t2 = timer.GetTimeInMs();
	CHECK_EQUAL(std::string("Hello"), res);
	CHECK_CLOSE(100, t2 - t1, 10);
	t.join();
}

//
// Test multiples futures sharing the same state
TEST(Future_SharedState)
{
	Promise<std::string> pr;

	std::vector<std::thread> threads;
	for (int i = 0; i < 10; i++)
	{
		threads.push_back(std::thread([ft = pr.get_future()]
		{
			CHECK_EQUAL(std::string("Hello"), ft.get());
		}));
	}

	threads.push_back(std::thread([pr]() mutable
	{
		pr.set_value("Hello");
	}));

	for (auto&& t : threads)
		t.join();
}

template<typename F>
void checkFutureErrorCode(FutureError::Code code, F f)
{
	try
	{
		f();
	}
	catch (const FutureError& e)
	{
		CHECK_EQUAL((int)code, (int)e.code());
		return;
	}
	catch (...)
	{
		CHECK(0);
	}

	CHECK(0);
}


//
// Test exceptions (without continuations)
TEST(Future_Exceptions_AlreadySatisfied)
{
	Promise<std::string> pr1;
	auto pr2 = pr1;
	pr1.set_value("");
	checkFutureErrorCode(FutureError::Code::PromiseAlreadySatisfied, [&] {pr1.set_value("");});
	checkFutureErrorCode(FutureError::Code::PromiseAlreadySatisfied, [&] {pr2.set_value("");});
}

TEST(Future_Exceptions_BrokenPromise_NoState)
{
	auto pr = std::make_unique<Promise<std::string>>();

	std::vector<std::thread> threads;
	ZeroSemaphore checkpoint1;
	for (int i = 0; i < 2; i++)
	{
		checkpoint1.increment();
		threads.push_back(std::thread([&checkpoint1, ft = pr->get_future()]
		{
			checkpoint1.decrement();
			//CHECK_THROW(ft.get(), FutureError);
			checkFutureErrorCode(FutureError::Code::BrokenPromise, [&] {ft.get();});
		}));
	}

	// Wait for all the threads to be ready
	checkpoint1.wait();

	//
	// Delete the promise. It will cause the futures to fail 
	{
		// move to another promise, so we can test the NoState
		auto tmp = std::move(*pr.get());
		checkFutureErrorCode(FutureError::Code::NoState, [&] {pr->set_value("");});
	}

	for (auto&& t : threads)
		t.join();
}

//
// Test continuations, by setting the value at the end
TEST(Future_Continuations_1)
{
	Promise<std::string> pr;
	auto ft1 = pr.get_future();

	auto ft2 = ft1.then([&](Future<std::string>& ft)
	{
		CHECK_EQUAL("Hello", ft.get());
		return 1;
	});

	auto ft3 = ft1.then([&](Future<std::string>& ft)
	{
		CHECK_EQUAL("Hello", ft.get());
		return 2;
	});

	auto ft4 = ft2.then([&](Future<int>& ft)
	{
		return ft.get() + 10;
	});

	pr.set_value("Hello");
	CHECK_EQUAL(1, ft2.get());
	CHECK_EQUAL(2, ft3.get());
	CHECK_EQUAL(11, ft4.get());
}

//
// Test continuations, by setting the value at the beginning
TEST(Future_Continuations_2)
{
	Promise<std::string> pr;
	auto ft1 = pr.get_future();
	pr.set_value("Hello");

	auto ft2 = ft1.then([&](Future<std::string>& ft)
	{
		CHECK_EQUAL("Hello", ft.get());
		return 1;
	});

	auto ft3 = ft1.then([&](Future<std::string>& ft)
	{
		CHECK_EQUAL("Hello", ft.get());
		return 2;
	});

	auto ft4 = ft2.then([&](Future<int>& ft)
	{
		return ft.get() + 10;
	});

	CHECK_EQUAL(1, ft2.get());
	CHECK_EQUAL(2, ft3.get());
	CHECK_EQUAL(11, ft4.get());
}

}