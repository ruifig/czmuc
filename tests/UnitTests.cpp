#include "UnitTestsPCH.h"

/*
int _tmain(int argc, _TCHAR* argv[])
{
	cz::net::initializeSocketsLib();
	auto res = UnitTest::RunAllTests();
	cz::net::shutdownSocketsLib();
	return res;
}
*/

using namespace cz;

#define CONTINUOUS_TESTING 0

std::string getCurrentTestName()
{
	auto test = UnitTest::CurrentTest::Details();
	return test->testName;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int c = 0;
	WindowsConsole con(120, 40, 300, 1000);
	logTestsVerbose.setVerbosity(LogVerbosity::Warning);
	//logTests.setVerbosity(LogVerbosity::Warning);

	// So Sleep calls are as accurate as possible
	timeBeginPeriod(1);

	CZ_LOG(logDefault, Log, "Test %d", 1);

#if CONTINUOUS_TESTING
	while (true)
	{
		printf("*** Run %d\n***", c++);
		int ret;

		ret = UnitTest::RunAllTests();
		if (ret!= EXIT_SUCCESS)
			return ret;
	}
	return EXIT_SUCCESS;
#else
	auto res = UnitTest::RunAllTests();
	return EXIT_SUCCESS;
#endif
}
