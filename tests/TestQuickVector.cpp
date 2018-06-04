#include "UnitTestsPCH.h"
#include "crazygaze/muc/Guid.h"

using namespace cz;

namespace
{

	std::unordered_map<int, int> gFoos;
	int gFooCounter = 0;
	int gFooConstructedCount = 0;
	int gFooNonEmptyDestroyed = 0;

	void clearFooStats()
	{
		gFoos.clear();
	}

	struct Foo
	{
		Foo(int n) : n(n)
		{
			//printf("%s : %d\n", __FUNCTION__, n);
			id = gFooCounter++;
			gFoos[id] = n;
		}

		Foo(const Foo& other) = delete;

		Foo(Foo&& other)
		{
			n = other.n;
			id = other.id;
			other.n = -1;
			other.id = -1;
		}

		~Foo()
		{
			//printf("%s : %d\n", __FUNCTION__, n);
			if (n != -1)
			{
				auto it = gFoos.find(id);
				assert(it != gFoos.end());
				gFoos.erase(it);
			}
		}
	
		int n;
		int id;
	};

}

bool operator==(const Foo& a, const Foo& b)
{
	return a.n == b.n;
}

std::ostream& operator<<(std::ostream& s, const Foo& f)
{
	s << f.n;
	return s;
}

SUITE(QuickVector)
{

TEST(EmptyVector)
{
	QuickVector<int, 4> v;
	CHECK_EQUAL(0, v.size());
	CHECK_EQUAL(4, v.capacity());
	CHECK(v.begin() == v.end());
}

TEST(reserve)
{
	QuickVector<int, 4> v;

	CHECK_EQUAL(4, v.capacity());
	auto b = v.begin();
	v.reserve(5);
	CHECK_EQUAL(0, v.size());
	CHECK_EQUAL(5, v.capacity());
	CHECK(v.begin() != b);
	CHECK(v.begin() == v.end());
}

TEST(push_back_emplace_back)
{
	QuickVector<int, 4> v;
	std::vector<int> expected{10, 11, 12, 13, 14, 15};

	v.emplace_back(10);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 1);
	CHECK_EQUAL(1, v.size());
	CHECK_EQUAL(4, v.capacity());
	CHECK((v.begin()+1) == v.end());

	v.push_back(11);
	v.push_back(12);
	v.push_back(13);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 4);
	CHECK_EQUAL(4, v.size());
	CHECK_EQUAL(4, v.capacity());
	CHECK((v.begin()+4) == v.end());

	// Grow past the quick buffer
	v.push_back(14);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 5);
	v.push_back(15);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 6);
	CHECK_EQUAL(6, v.size());
	CHECK(v.capacity() > 4);
	CHECK((v.begin()+6) == v.end());
}

TEST(clear)
{
	QuickVector<int, 2> v;
	v.push_back(0);
	v.push_back(1);
	CHECK_EQUAL(2, v.size());
	CHECK_EQUAL(2, v.capacity());
	v.clear();
	CHECK_EQUAL(0, v.size());
	CHECK_EQUAL(2, v.capacity());

	v.push_back(0);
	v.push_back(1);
	v.push_back(2);
	CHECK_EQUAL(3, v.size());
	CHECK_EQUAL(4, v.capacity());
}

TEST(NonTrivial)
{
	static_assert(std::is_trivial_v<Foo> == false);
	static_assert(std::is_trivial_v<std::string> == false);

	QuickVector<Foo, 4> v;
	std::vector<int> expected{10, 11, 12, 13, 14, 15};

	v.push_back(10);
	v.push_back(11);
	v.push_back(12);
	v.push_back(13);
	v.push_back(14);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 5);
	CHECK_EQUAL(5, gFoos.size());

	v.clear();
	CHECK_EQUAL(0, v.size());
	CHECK_EQUAL(0, gFoos.size());
}

TEST(NonTrivialString)
{
	static_assert(std::is_trivial_v<std::string> == false);

	QuickVector<std::string, 2> v;
	std::vector<std::string> expected{ "0", "1", "2"};

	v.push_back("0");
	std::string s("1");
	v.push_back(s);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 2);
	s = "2";
	v.push_back(std::move(s));
	CHECK_EQUAL("", s);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 3);
}


TEST(indexing)
{
	QuickVector<int, 2> v;
	const QuickVector<int, 2>& vv = v;

	v.push_back(10);
	v.push_back(11);
	CHECK_EQUAL(10, v[0]);
	CHECK_EQUAL(10, vv[0]);
	CHECK_EQUAL(11, v[1]);
	CHECK_EQUAL(11, vv[1]);
	v.push_back(12);
	CHECK_EQUAL(12, v[2]);
	CHECK_EQUAL(12, vv[2]);
}

template<class QV>
int testForLoop(const std::vector<int>& v, QV& q)
{
	int count = 0;
	auto vi = v.begin();
	for (auto&& i : q)
	{
		CHECK_EQUAL(*vi, i);
		vi++;
		count++;
	}

	CHECK_EQUAL(q.size(), count);
	return count;
}

TEST(ForLoop)
{
	std::vector<int> expected{10, 11, 12, 13, 14, 15};
	QuickVector<int, 4> v;
	const QuickVector<int, 4>& vv = v;

	v.push_back(10);
	v.push_back(11);
	v.push_back(12);
	v.push_back(13);
	CHECK_EQUAL(4, testForLoop(expected, v));
	CHECK_EQUAL(4, testForLoop(expected, vv));
	v.push_back(14);
	CHECK_EQUAL(5, testForLoop(expected, v));
	CHECK_EQUAL(5, testForLoop(expected, vv));
}


}




