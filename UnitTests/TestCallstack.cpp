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

struct Foo
{
};

TEST(1)
{
	IOThread ioth(3);

}

}


