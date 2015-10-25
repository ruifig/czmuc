#include "UnitTestsPCH.h"

int _tmain(int argc, _TCHAR* argv[])
{
	cz::net::initializeSocketsLib();
	auto res = UnitTest::RunAllTests();
	cz::net::shutdownSocketsLib();
	return res;
}
