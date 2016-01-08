#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::rpc;

void testTuple0()
{
}
static void testTuple1(bool a, int b, unsigned c, const std::string& d, const char* e)
{
}
static void testTuple2(bool a, int b, unsigned c, const std::string& d, const char* e, std::vector<uint8_t>&& f)
{
}

template<typename F>
bool call(F f, const std::vector<Any> params)
{
	ParamTuple<F>::type t;
	if (!Any::toTuple(params, t))
		return false;
	call(f, t);
	return true;
}

SUITE(Any)
{

TEST(EmptyTuple)
{
	std::vector<Any> v;
	CHECK(call(&testTuple0, v));

	v.push_back(Any(1));
	CHECK(!call(testTuple0, v));
}

TEST(Params)
{
	std::vector<Any> v;
	v.push_back(Any(true));
	v.push_back(Any(int(1)));
	v.push_back(Any(unsigned(2)));
	v.push_back(Any("Hello"));
	v.push_back(Any("World"));

	ParamTuple<decltype(testTuple1)>::type t1;
	CHECK(Any::toTuple(v, t1));

	CHECK_EQUAL(true, std::get<0>(t1));
	CHECK_EQUAL(1, std::get<1>(t1));
	CHECK_EQUAL(2, std::get<2>(t1));
	CHECK_EQUAL("Hello", std::get<3>(t1));
	CHECK_EQUAL("World", std::get<4>(t1));

	uint8_t a[4] = { 1,2,3,4 };
	v.push_back(Any(std::vector<uint8_t>(&a[0], &a[4])));
	CHECK(!Any::toTuple(v, t1));

	ParamTuple<decltype(testTuple2)>::type t2;
	CHECK(Any::toTuple(v, t2));
	CHECK_EQUAL(true, std::get<0>(t2));
	CHECK_EQUAL(1, std::get<1>(t2));
	CHECK_EQUAL(2, std::get<2>(t2));
	CHECK_EQUAL("Hello", std::get<3>(t2));
	CHECK_EQUAL("World", std::get<4>(t2));
	CHECK_ARRAY_EQUAL(a, std::get<5>(t2), 4);

	std::to_string(10);
}

}