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
		UnitTest::TimeHelpers::SleepMs(50);
		pr.set_value("Hello");
	});

	checkpoint1.notify();
	auto t1 = timer.GetTimeInMs();
	auto res = ft1.get();
	auto t2 = timer.GetTimeInMs();
	CHECK_EQUAL(std::string("Hello"), res);
	CHECK_CLOSE(50, t2 - t1, 5);
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

	auto ft2 = ft1.then([&](Future<std::string>& ft) -> int
	{
		CHECK_EQUAL("Hello", ft.get());
		return 1;
	});

	auto ft3 = ft1.then([&](auto& ft) // Trying with generic lambda (so I don't need to type the parameter type)
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


TEST(Future_thenQueue)
{
	struct WorkThread
	{
		WorkThread()
		{
			workQueue = std::make_shared<WorkQueue>();
			th = std::thread([this]
			{
				while (!finish)
				{
					std::function<void()> f;
					workQueue->wait_and_pop(f);
					f();
				}
			});

		}

		~WorkThread()
		{
			workQueue->push([this] { finish = true;});
			th.join();
		}
		std::shared_ptr<WorkQueue> workQueue;
		std::thread th;
		bool finish=false;
	};

	auto th = std::make_unique<WorkThread>();

	// Just a quick way to execute lambdas on another thread
	Concurrent<int> conc;


	{
		Semaphore checkpoint1;
		Semaphore checkpoint2;
		auto readyFt = conc([&](auto& i)
		{
			return 3;
		});
		auto nonReadyFt = conc([&](auto& i)
		{
			checkpoint1.notify(); // When this is signaled, we know that readyFt is ready
			checkpoint2.wait();
			return 5;
		});


		checkpoint1.wait();
		CHECK(readyFt.is_ready());
		{
			auto ft = readyFt.thenQueue(
				th->workQueue,
				[&](Future<int>& resFt)
			{
				// Even though the future was already ready when we called thenQueue, we still want the work to be
				// queued to the other thread as we requested.
				CHECK(std::this_thread::get_id() == th->th.get_id());
				return resFt.get() * 0.5f;
			});
			CHECK_CLOSE(1.5f, ft.get(), std::numeric_limits<float>::epsilon());
		}

		{
			auto ft = nonReadyFt.thenQueue(
				th->workQueue,
				[&](Future<int>& resFt)
			{
				CHECK(std::this_thread::get_id() == th->th.get_id());
				return resFt.get() * 0.5f;
			});
			CHECK(!nonReadyFt.is_ready());
			// calling this here after the thenQueue, makes sure that we can test the nonReadyFt as not ready
			checkpoint2.notify(); 
			// This future will not be ready, and it will block
			CHECK_CLOSE(2.5f, ft.get(), std::numeric_limits<float>::epsilon());
		}
	}

	// Test with the queue deleted
	{
		Semaphore checkpoint1;
		auto ft1 = conc([&](auto& i)
		{
			checkpoint1.wait();
			return 1;
		});

		auto resFt1 = ft1.thenQueue(
			th->workQueue,
			[](Future<int>& resFt)
		{
			return resFt.get() + 1;
		});
		th = nullptr;
		checkpoint1.notify();
		checkFutureErrorCode(FutureError::Code::BrokenPromise, [&] {resFt1.get();});
	}

}

TEST(Future_void)
{
	// Simple
	{
		Promise<void> pr;
		auto ft = pr.get_future();
		pr.set_value();
		ft.get();
	}

	// Check async
	{
		UnitTest::Timer timer;
		timer.Start();
		Promise<void> pr;
		auto ft1 = pr.get_future();
		Semaphore checkpoint1;
		auto t = std::thread([&checkpoint1, pr = std::move(pr)]() mutable
		{
			// Wait for the main thread be ready to wait on the future, so we can measure the time
			checkpoint1.wait();
			UnitTest::TimeHelpers::SleepMs(50);
			pr.set_value();
		});

		checkFutureErrorCode(FutureError::Code::NoState, [&] {pr.set_value();});

		checkpoint1.notify();
		auto t1 = timer.GetTimeInMs();
		ft1.get();
		auto t2 = timer.GetTimeInMs();
		CHECK_CLOSE(50, t2 - t1, 5);
		t.join();
	}
}

TEST(Future_void_Continuations)
{
	Promise<void> pr;
	auto ft1 = pr.get_future();
	bool ft1Done = false, ft2Done = false;
	auto ft2 = ft1.then([&ft1Done](Future<void>& ft)
	{
		ft1Done = true;
	});

	auto ft3 = ft2.then([&ft2Done](Future<void>& ft)
	{
		ft2Done = true;
		return 1;
	});

	pr.set_value();

	CHECK(ft1Done);
	CHECK(ft2Done);
	CHECK_EQUAL(1, ft3.get());
}

TEST(Future_void_exceptions)
{
	{
		Promise<void> pr;
		pr.set_value();
		checkFutureErrorCode(FutureError::Code::PromiseAlreadySatisfied, [&] {pr.set_value();});
	}

	{
		Promise<void> pr;
		pr.set_exception(std::make_exception_ptr(std::logic_error("Error")));
		checkFutureErrorCode(FutureError::Code::PromiseAlreadySatisfied, [&] {pr.set_value();});
		CHECK_THROW(pr.get_future().get(), std::logic_error);
	}

	{
		Promise<void> pr;
		auto ft = pr.get_future();
		auto t = std::thread([pr=std::move(pr)]
		{
		});
		checkFutureErrorCode(FutureError::Code::NoState, [&] {pr.set_value();});
		checkFutureErrorCode(FutureError::Code::NoState, [&] {pr.get_future();});
		t.join();
		checkFutureErrorCode(FutureError::Code::BrokenPromise, [&] {ft.get();});
	}
}

TEST(Future_getMove)
{
	Promise<std::string> pr;
	auto ft1 = pr.get_future();
	auto ft2 = pr.get_future();
	pr.set_value("Hello");

	auto s = ft1.getMove();
	CHECK_EQUAL("Hello", s);
	// The string was moved out, so should be empty now
	CHECK_EQUAL("", ft1.get());
	CHECK_EQUAL("", ft2.get());
}


}