#include "UnitTestsPCH.h"
#include "crazygaze/muc/Guid.h"

using namespace cz;

//namespace {

class DerpDerp
{
	int a;
};

	std::unordered_map<int, int> gFoos;
	int gFoosCreated = 0;
	int gFoosDestroyed = 0;

	void clearFooStats()
	{
		CHECK(gFoosCreated == gFoosDestroyed);
		gFoos.clear();
		//gFoosCreated = 0;
		//gFoosDestroyed = 0;
	}

	struct Foo
	{
		Foo(int n) : n(n)
		{
			gFoosCreated++;
			gFoos[n]++;
		}

		Foo(const Foo& other)
		{
			gFoosCreated++;
			copyFrom(other);
		}

		Foo(Foo&& other)
		{
			gFoosCreated++;
			moveFrom(std::move(other));
		}

		~Foo()
		{
			gFoosDestroyed++;
			if (n != -1)
			{
				assert(gFoos[n] > 0);
				gFoos[n]--;
				if (gFoos[n] == 0)
					gFoos.erase(n);
			}
		}


		Foo& operator= (const Foo& other)
		{
			copyFrom(other);
			return *this;
		}

		Foo& operator= (Foo&& other)
		{
			moveFrom(std::move(other));
			return *this;
		}

		void moveFrom(Foo&& other)
		{
			if (n!=-1)
				gFoos[n]--;
			n = other.n;

			if (other.n != -1)
			{
				other.n = -1;
				other.movedCount++;
			}
		}

		void copyFrom(const Foo& other)
		{
			if (n!=-1)
				gFoos[n]--;
			n = other.n;
			if (n!=-1)
				gFoos[n]++;
		}

		int n = -1;
		int movedCount = 0;
		// Make Foo big, to make it easier to spot leaks
		alignas(16) char padding[1024];
	};

//}

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

#if 0
TEST(Derp)
{
	std::vector<Foo> v;
	v.reserve(10);
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.push_back(4);
	v.push_back(5);

	Foo f(0);
	v.insert(v.begin(), std::move(f));
}
#endif

template<typename T>
void testAlign()
{
	QuickVector<T, 1> v;
	v.push_back(0);
	void* p1 = &v[0];
	v.push_back(1);

	void* p2 = &v[0];
	CHECK(p1 == &v); // making sure it's the quickbuffer
	CHECK(p2 != p1);

	size_t alignment = alignof(T);
	CHECK((((size_t)p1) % alignment) == 0);
	CHECK((((size_t)p2) % alignment) == 0);
}

TEST(Alignment)
{
	static_assert(alignof(Foo)==16);
	static_assert(alignof(QuickVector<Foo, 2>)==alignof(Foo));

	static_assert(alignof(char)==1);
	static_assert(alignof(uint32_t)==4);
	static_assert(alignof(uint64_t)==8);
	static_assert(alignof(QuickVector<char, 2, uint32_t>)==alignof(size_t));
	static_assert(alignof(QuickVector<char, 2, size_t>)==alignof(size_t));

	testAlign<uint8_t>();
	testAlign<uint16_t>();
	testAlign<uint32_t>();
	testAlign<uint64_t>();
	testAlign<Foo>();
}

TEST(EmptyVector)
{
	clearFooStats();
	QuickVector<int, 4> v;
	CHECK_EQUAL(0, v.size());
	CHECK(v.empty());
	CHECK_EQUAL(4, v.capacity());
	CHECK(v.begin() == v.end());
	CHECK(v.data() == v.begin());

	CHECK_EQUAL(decltype(v)::size_type(~0) / sizeof(int), v.max_size());
}

TEST(reserve)
{
	clearFooStats();
	QuickVector<int, 4> v;

	CHECK_EQUAL(4, v.capacity());
	auto b = v.begin();
	v.reserve(5);
	CHECK_EQUAL(0, v.size());
	CHECK(v.empty());
	CHECK_EQUAL(5, v.capacity());
	CHECK(v.begin() != b);
	CHECK(v.begin() == v.end());
}

TEST(push_back_emplace_back)
{
	clearFooStats();
	QuickVector<int, 4> v;
	std::vector<int> expected{10, 11, 12, 13, 14, 15};

	v.emplace_back(10);
	CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 1);
	CHECK_EQUAL(1, v.size());
	CHECK(!v.empty());
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
	clearFooStats();
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
	CHECK_EQUAL(0, gFoos.size());
}

TEST(NonTrivial)
{
	clearFooStats();
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
	clearFooStats();
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
	clearFooStats();
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
	clearFooStats();
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

template<size_t N1, size_t N2, bool assign>
void testCopyImpl()
{
	clearFooStats();
	std::vector<int> expected{10, 11, 12, 13, 14, 15};
	QuickVector<Foo, N1> v;
	v.push_back(10);
	v.push_back(11);
	v.push_back(12);
	if constexpr(assign)
	{
		QuickVector<Foo, N2> vv;
		vv.push_back(0);
		vv = v;
		CHECK_EQUAL(3, vv.size());
		CHECK_ARRAY_EQUAL(expected, vv, 3);

		CHECK_EQUAL(2, gFoos[10]);
		CHECK_EQUAL(2, gFoos[11]);
		CHECK_EQUAL(2, gFoos[12]);
	}
	else
	{
		QuickVector<Foo, N2> vv(v);
		CHECK_EQUAL(3, vv.size());
		CHECK_ARRAY_EQUAL(expected, vv, 3);

		CHECK_EQUAL(2, gFoos[10]);
		CHECK_EQUAL(2, gFoos[11]);
		CHECK_EQUAL(2, gFoos[12]);
	}

	CHECK_EQUAL(1, gFoos[10]);
	CHECK_EQUAL(1, gFoos[11]);
	CHECK_EQUAL(1, gFoos[12]);

	CHECK_EQUAL(3, v.size());
	CHECK_ARRAY_EQUAL(expected, v, 3);
}

template<bool assign>
void testCopy()
{
	clearFooStats();

	testCopyImpl<4, 2, assign>();
	testCopyImpl<4, 3, assign>();
	testCopyImpl<4, 4, assign>();
	testCopyImpl<4, 5, assign>();
	CHECK_EQUAL(0, gFoos.size());

	testCopyImpl<2, 1, assign>();
	testCopyImpl<2, 2, assign>();
	testCopyImpl<2, 3, assign>();
	testCopyImpl<2, 4, assign>();
	CHECK_EQUAL(0, gFoos.size());
}

TEST(Copy)
{
	testCopy<false>();
	testCopy<true>();
}

template<size_t N1, size_t N2, bool assign>
void testMoveImpl()
{
	clearFooStats();
	std::vector<int> expected{10, 11, 12, 13, 14, 15};
	QuickVector<Foo, N1> v;
	v.push_back(10);
	v.push_back(11);
	v.push_back(12);

	if constexpr(assign)
	{
		QuickVector<Foo, N2> vv;
		vv.push_back(0);
		vv = (std::move(v));

		CHECK_EQUAL(3, vv.size());
		CHECK_ARRAY_EQUAL(expected, vv, 3);

		CHECK_EQUAL(1, gFoos[10]);
		CHECK_EQUAL(1, gFoos[11]);
		CHECK_EQUAL(1, gFoos[12]);
	}
	else
	{
		QuickVector<Foo, N2> vv(std::move(v));

		CHECK_EQUAL(3, vv.size());
		CHECK_ARRAY_EQUAL(expected, vv, 3);

		CHECK_EQUAL(1, gFoos[10]);
		CHECK_EQUAL(1, gFoos[11]);
		CHECK_EQUAL(1, gFoos[12]);
	}

	CHECK_EQUAL(0, gFoos.size());
	CHECK_EQUAL(0, v.size());
}

template<bool assign>
void testMove()
{
	clearFooStats();

	testMoveImpl<4, 2, assign>();
	testMoveImpl<4, 3, assign>();
	testMoveImpl<4, 4, assign>();
	testMoveImpl<4, 5, assign>();
	CHECK_EQUAL(0, gFoos.size());

	testMoveImpl<2, 1, assign>();
	testMoveImpl<2, 2, assign>();
	testMoveImpl<2, 3, assign>();
	testMoveImpl<2, 4, assign>();
	CHECK_EQUAL(0, gFoos.size());
}

TEST(Move)
{
	testMove<false>();
	testMove<true>();
}

template<size_t N>
void testFront_Back()
{
	QuickVector<Foo, N> v;
	const QuickVector<Foo, N>& vv = v;
	v.push_back(10);
	v.push_back(11);
	v.push_back(12);

	CHECK_EQUAL(10, v.front());
	CHECK_EQUAL(12, v.back());
	CHECK_EQUAL(10, vv.front());
	CHECK_EQUAL(12, vv.back());
}

TEST(Front_Back)
{
	testFront_Back<4>();
	testFront_Back<2>();
}

template<size_t N>
void test_at()
{
	QuickVector<Foo, N> v;
	const QuickVector<Foo, N>& vv = v;
	v.push_back(10);
	v.push_back(11);
	v.push_back(12);

	CHECK_EQUAL(10, v.at(0));
	CHECK_EQUAL(12, v.at(2));
	CHECK_THROW(v.at(~(size_t)0), std::out_of_range);
	CHECK_THROW(v.at(3), std::out_of_range);
	CHECK_EQUAL(10, vv.at(0));
	CHECK_EQUAL(12, vv.at(2));
	CHECK_THROW(vv.at(~(size_t)0), std::out_of_range);
	CHECK_THROW(vv.at(3), std::out_of_range);
}

TEST(at)
{
	test_at<4>();
	test_at<2>();
}


template<typename T, size_t N>
struct StdVector : public std::vector<T>
{
};

template <template<typename, size_t> class VT, typename T, size_t N>
void testInsert()
{
	clearFooStats();

	VT<T, N> v;
	std::vector<int> expected{0, 1, 2, 3, 4, 5};

	T a = 1;
	auto it = v.insert(v.end(), a); // Test using "const T&"
	CHECK(it == v.end() - 1);
	CHECK_EQUAL(1, v.size());
	CHECK_ARRAY_EQUAL(&expected[1], v, 1);

	it = v.insert(v.end(), 2); // "Test using T&&"
	CHECK(it == v.end() - 1);
	CHECK_EQUAL(2, v.size());
	CHECK_ARRAY_EQUAL(&expected[1], v, 2);

	v.insert(v.begin(), 0);
	CHECK_EQUAL(3, v.size());
	CHECK_ARRAY_EQUAL(&expected[0], v, 3);

	a = 10;
	v.insert(v.begin()+1, a); // Test using "const T&"
	CHECK_EQUAL(4, v.size());
	std::vector<int> exp2{0, 10, 1, 2, 3, 4};
	CHECK_ARRAY_EQUAL(exp2, v, 4);
}


template <template<typename, size_t> class VT>
void testInsertAll()
{
	testInsert<VT, Foo, 4>();
	testInsert<VT, Foo, 1>();
}

TEST(insert)
{
	testInsertAll<QuickVector>();
	testInsertAll<StdVector>();
}


template <template<typename, size_t> class VT, typename T, size_t N>
void testEmplace()
{
	clearFooStats();

	VT<T, N> v;
	std::vector<int> expected{0, 1, 2, 3, 4, 5};


	auto it = v.emplace(v.end(), 1);
	CHECK(it == v.end() - 1);
	CHECK_EQUAL(1, v.size());
	CHECK_ARRAY_EQUAL(&expected[1], v, 1);

	v.emplace_back(2);
	CHECK_EQUAL(2, v.size());
	CHECK_ARRAY_EQUAL(&expected[1], v, 2);

	v.emplace(v.begin(), 0);
	CHECK_EQUAL(3, v.size());
	CHECK_ARRAY_EQUAL(&expected[0], v, 3);

	T a = 10;
	v.emplace(v.begin()+1, a);
	CHECK_EQUAL(4, v.size());
	std::vector<int> exp2{0, 10, 1, 2, 3, 4};
	CHECK_ARRAY_EQUAL(exp2, v, 4);
}

template <template<typename, size_t> class VT>
void testEmplaceAll()
{
	testEmplace<VT, Foo, 4>();
	testEmplace<VT, Foo, 1>();
}

TEST(emplace)
{
	testEmplaceAll<QuickVector>();
	testEmplaceAll<StdVector>();
}

TEST(dummyend)
{
	CHECK(gFoos.size() == 0);
}

}




