#include "UnitTestsPCH.h"

using namespace cz;

SUITE(RingBuffer)
{

void testIterators(RingBuffer& buf)
{
	CHECK(buf.empty());
	// The standard requires that for empty containers, begin()==end()
	CHECK(buf.begin() == buf.end());
	CHECK(buf.end() == buf.begin());

	buf.write<char>(0);
	CHECK(buf.begin() != buf.end());
	CHECK(buf.end() != buf.begin());
	CHECK(buf.begin() == buf.end()-1);
	CHECK(buf.begin() == 1 - buf.end());
	CHECK(buf.begin() == --buf.end());
	CHECK(++buf.begin() == buf.end());

	CHECK_EQUAL(0, *buf.begin());
	CHECK_EQUAL(0, *(buf.end()-1));
	CHECK_EQUAL(0, *(--buf.end()));
	// This is not valid, since is the postfix operator, and it is in practice the same as *(buf.end())
	// CHECK_EQUAL(0, *(buf.end()--));
	buf.write<char>(1);
	buf.write<char>(2);


	int idx=0;
	for(auto&& i : buf)
	{
		CHECK_EQUAL(idx++, (int)i);
	}

	*buf.begin() = 1;
	CHECK_EQUAL(1, *buf.begin());
	*buf.begin() = 0;
}

TEST(Iterators)
{
	{
		RingBuffer buf;
		testIterators(buf);
	}

	// Test, with readpos not starting at 0, since we added and removed 1 element
	{
		char ch;
		RingBuffer buf;
		buf.reserve(3);
		buf.write<char>(0);
		buf.read<char>(&ch);
		testIterators(buf);
		buf.clear();
		testIterators(buf);
	}
}


}


