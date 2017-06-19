#include "UnitTestsPCH.h"

using namespace cz;

SUITE(SharedQueue)
{

struct Foo
{
	Foo() {}
	Foo(const char* s, bool copyAllowed = false) : s(s), copyAllowed(copyAllowed) {}
	Foo(std::string s, bool copyAllowed = false) : s(s), copyAllowed(copyAllowed) {}
	Foo(const Foo& other)
	{
		CHECK(other.copyAllowed);
	}
	Foo(Foo&& other) : s(std::move(other.s)), copyAllowed(other.copyAllowed)
	{
	}

	Foo& operator=(const Foo& other)
	{
		CHECK(other.copyAllowed);
		return *this;
	}
	Foo& operator=(Foo&& other)
	{
		s = std::move(other.s);
		return *this;
	}

	std::string s;
	bool copyAllowed = false;
};

TEST(1)
{
	SharedQueue<Foo> q;
	q.push("1");
	q.push("2");

	Foo f;
	q.wait_and_pop(f);
	CHECK_EQUAL("1", f.s);
	q.wait_and_pop(f);
	CHECK_EQUAL("2", f.s);

	printf("Derp\n");
}

TEST(2)
{
	SharedQueue<std::pair<Foo,Foo>> q;
	q.emplace("1", "2");
	q.emplace(Foo("1"), Foo("2"));

	Foo f1("1", true);
	Foo f2("2", true);
	q.emplace(f1, f2);

	{
		std::pair<Foo, Foo> f;
		q.wait_and_pop(f);
		CHECK_EQUAL("1", f.first.s);
		CHECK_EQUAL("2", f.second.s);
	}
	{
		std::pair<Foo, Foo> f;
		q.wait_and_pop(f);
		CHECK_EQUAL("1", f.first.s);
		CHECK_EQUAL("2", f.second.s);
	}
}

TEST(3)
{
	SharedQueue<Foo> q;

	Foo f;
	CHECK(q.try_and_pop(f) == false);
	CHECK(f.s == "");

	CHECK(q.wait_and_pop(f, 10) == false);
	CHECK(f.s == "");

	CHECK(q.size() == 0);
	CHECK(q.empty() == true);

	q.push("1");
	q.push("2");
	CHECK_EQUAL(2, q.size());
	CHECK(q.empty() == false);

	CHECK(q.try_and_pop(f) == true);
	CHECK(f.s == "1");
	CHECK(q.wait_and_pop(f, 1000) == true);
	CHECK(f.s == "2");

	std::async(std::launch::async, [&]
	{
		UnitTest::TimeHelpers::SleepMs(10);
		q.push("3");
	});

	CHECK(q.wait_and_pop(f, 1000) == true);
	CHECK(f.s == "3");

}

}


