#include "UnitTestsPCH.h"

using namespace cz;
using namespace cz::rpc;

SUITE(RPCTraits)
{

struct Foo
{
	void ok1() const
	{
	}
	void ok2(int a)
	{
	}
	int ok3()
	{
		return 0;
	}
	int ok4(int a) const
	{
		return 0;
	}
	const int& ok5(int a, const int& b)
	{
		static int v = 1;
		return v;
	}

	void err1(int& a)
	{
	}
	void err2(int* b)
	{
	}
	int* err3()
	{
		return nullptr;
	}
	int& err4(int a)
	{
		static int v = 1;
		return v;
	}
};

TEST(RPCTraits)
{
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::ok1)>::value, "Invalid RPC function signature");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::ok2)>::value, "Invalid RPC function signature");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::ok3)>::value, "Invalid RPC function signature");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::ok4)>::value, "Invalid RPC function signature");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::ok5)>::value, "Invalid RPC function signature");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::err1)>::value == false, "Should be invalid");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::err2)>::value == false, "Should be invalid");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::err3)>::value == false, "Should be invalid");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::err4)>::value == false, "Should be invalid");
	static_assert(ValidateRPCFuncSignature<decltype(&Foo::err4)>::value == false, "Should be invalid");
}

}

