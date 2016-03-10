#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::rpc;


SUITE(Buffer)
{

TEST(TestEmpty)
{
	Buffer b;
	int count = 0;
	for (auto&& ch : b)
	{
		count++;
	}
	CHECK_EQUAL(0, count);
	CHECK_EQUAL(0, b.end() - b.begin());

	Buffer b2(b);
	CHECK(b2.size == 0);
}

TEST(TestLocalBuf)
{
	char buf[3] = { 1,2,3 };
	Buffer b(buf);
	CHECK(b.ptr == &buf[0]);

	int count = 0;
	for(auto&& ch : b)
	{
		count++;
		ch = -ch;
	}

	CHECK_EQUAL(3, count);
	CHECK_EQUAL(3, b.end() - b.begin());
	char newbuf[3] = { -1,-2,-3 };
	CHECK_ARRAY_EQUAL(newbuf, b.ptr, 3);
}

TEST(TestSub)
{
	char buf[3] = { 1,2,3 };
	Buffer b1(buf);

	{
		Buffer b2 = b1 + 0;
		CHECK(b2.ptr == b1.ptr);
		CHECK(b2.size == b1.size);
	}

	{
		Buffer b2 = b1 + 2;
		CHECK(b2.ptr == b1.ptr+2);
		CHECK(b2.size == b1.size-2);
		CHECK(b2.size == 1);
		char newbuf[1] = { 3 };
		CHECK_ARRAY_EQUAL(newbuf, b2.ptr, static_cast<int>(b2.size));
	}

	{
		Buffer b2 = b1 + 3;
		CHECK_EQUAL(0, b2.size);
		CHECK_EQUAL(0, b2.end() - b2.begin());
	}
}

}


