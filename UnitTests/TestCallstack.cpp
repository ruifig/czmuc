#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::net;

SUITE(Callstack)
{

struct IOThread
{
public:
	IOThread(int count = 1)
	{
		while (count--)
		{
			th.emplace_back([this]
			{
				iocp.run();
			});
		}
	}

	~IOThread()
	{
		iocp.stop();
		for (auto&& t : th)
			t.join();
	}

	CompletionPort iocp;
	std::vector<std::thread> th;
};


struct Bar
{
	int a;
};
struct Foo
{
	int b;
};

TEST(1)
{
	IOThread ioth(3);
	Bar b1;
	Foo f1;

	{
		CHECK(Callstack<Bar>::contains(&b1) == nullptr);
		CHECK(Callstack<Foo>::contains(&f1) == nullptr);
		{
			Callstack<Bar>::Context ctx(&b1);
			CHECK(Callstack<Bar>::contains(&b1) != nullptr);
			CHECK(Callstack<Foo>::contains(&f1) == nullptr);
			{
				Callstack<Bar>::Context ctx1(&b1);
				Callstack<Foo>::Context ctx2(&f1);
				CHECK(Callstack<Bar>::contains(&b1) != nullptr);
				CHECK(Callstack<Foo>::contains(&f1) != nullptr);
			}
		}
		CHECK(Callstack<Bar>::contains(&b1) == nullptr);
		CHECK(Callstack<Foo>::contains(&f1) == nullptr);
	}

	{
		using BarT = Callstack<Bar, std::string>;
		using FooT = Callstack<Foo, std::string>;
		CHECK(BarT::contains(&b1) == nullptr);
		CHECK(FooT::contains(&f1) == nullptr);
		{
			std::string s1("Bar1");
			BarT::Context ctx(&b1, s1);
			CHECK_EQUAL(s1, *BarT::contains(&b1));
			CHECK(FooT::contains(&f1) == nullptr);
			{
				std::string s2("Bar2");
				std::string s3("Foo1");
				BarT::Context ctx1(&b1, s2);
				FooT::Context ctx2(&f1, s3);
				CHECK_EQUAL(s2, *BarT::contains(&b1));
				CHECK_EQUAL(s3, *FooT::contains(&f1));
			}

			CHECK_EQUAL(s1, *BarT::contains(&b1));
			CHECK(FooT::contains(&f1) == nullptr);
		}
		CHECK(BarT::contains(&b1) == nullptr);
		CHECK(FooT::contains(&f1) == nullptr);
	}
}

}


