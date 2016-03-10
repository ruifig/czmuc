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
	Bar(std::string n = "") : name(n) {}
	std::string name;
};
struct Foo
{
	Foo(std::string n = "") : name(n) {}
	std::string name;
};

TEST(1)
{
	Bar b1;
	Bar b2;
	Foo f1;

	{
		CHECK(Callstack<Bar>::contains(&b1) == nullptr);
		CHECK(Callstack<Bar>::contains(&b2) == nullptr);
		CHECK(Callstack<Foo>::contains(&f1) == nullptr);
		{
			Callstack<Bar>::Context ctx(&b1);
			CHECK(Callstack<Bar>::contains(&b1) != nullptr);
			CHECK(Callstack<Bar>::contains(&b2) == nullptr);
			CHECK(Callstack<Foo>::contains(&f1) == nullptr);
			{
				Callstack<Bar>::Context ctx1(&b1);
				Callstack<Foo>::Context ctx2(&f1);
				Callstack<Bar>::Context ctx3(&b2);
				CHECK(Callstack<Bar>::contains(&b1) != nullptr);
				CHECK(Callstack<Bar>::contains(&b2) != nullptr);
				CHECK(Callstack<Foo>::contains(&f1) != nullptr);
			}
		}
		CHECK(Callstack<Bar>::contains(&b1) == nullptr);
		CHECK(Callstack<Bar>::contains(&b2) == nullptr);
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

TEST(Iterators)
{
	Bar b1("b1");
	Bar b2("b2");
	Bar b3("b3");

	using BarCallstack = Callstack<Bar, int>;

	{
		int v1 = 1;
		BarCallstack::Context ctx(&b1, v1);
		int v11 = 11;
		BarCallstack::Context ctx2(&b1, v11);
		{
			int v2 = 2;
			BarCallstack::Context ctx(&b2, v2);
			{
				int v3 = 3;
				BarCallstack::Context ctx(&b3, v3);

				std::vector<std::pair<Bar*, int>> v;
				for (auto i : BarCallstack())
					v.emplace_back(i->getKey(), *i->getValue());

				CHECK_EQUAL(4, v.size());
				CHECK(v[0].first == &b3 && v[0].second == 3);
				CHECK(v[1].first == &b2 && v[1].second == 2);
				CHECK(v[2].first == &b1 && v[2].second == 11);
				CHECK(v[3].first == &b1 && v[3].second == 1);
			}
		}
	}



}

}


