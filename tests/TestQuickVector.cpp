#include "UnitTestsPCH.h"
#include "crazygaze/muc/Guid.h"

using namespace cz;

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
	// gFoosCreated = 0;
	// gFoosDestroyed = 0;
}

struct Foo
{
	Foo(int n)
		: n(n)
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

	void decreaseFoo()
	{
		if (n != -1)
		{
			assert(gFoos[n] > 0);
			gFoos[n]--;
			if (gFoos[n] == 0)
				gFoos.erase(n);
		}
	}

	~Foo()
	{
		destroyed = true;
		gFoosDestroyed++;
		decreaseFoo();
	}

	Foo& operator=(const Foo& other)
	{
		copyFrom(other);
		return *this;
	}

	Foo& operator=(Foo&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	void moveFrom(Foo&& other)
	{
		if (this == &other)
			return;
		decreaseFoo();
		n = other.n;

		if (other.n != -1)
		{
			other.n = -1;
			other.movedCount++;
		}
	}

	void copyFrom(const Foo& other)
	{
		if (this == &other)
			return;
		decreaseFoo();
		n = other.n;
		if (n != -1)
			gFoos[n]++;
	}

	int n = -1;
	int movedCount = 0;
	bool destroyed = false;
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
    {
      std::vector<Foo> v;
      v.insert(v.end(), 1, 1);
      v.insert(v.end(), 2, 2);
      v.insert(v.end(), 100, 3);
    }

    {
      std::vector<Foo> v;
      v.push_back(1);
      v.push_back(2);
      v.push_back(3);
    }
  }
#endif

	template <typename T>
	void testAlign()
	{
		QuickVector<T, 1> v;
		v.push_back(0);
		void* p1 = &v[0];
		v.push_back(1);

		void* p2 = &v[0];
		CHECK(p1 == &v);  // making sure it's the quickbuffer
		CHECK(p2 != p1);

		size_t alignment = alignof(T);
		CHECK((((size_t)p1) % alignment) == 0);
		CHECK((((size_t)p2) % alignment) == 0);
	}

	TEST(Alignment)
	{
		static_assert(alignof(Foo) == 16);
		static_assert(alignof(QuickVector<Foo, 2>) == alignof(Foo));

		static_assert(alignof(char) == 1);
		static_assert(alignof(uint32_t) == 4);
		static_assert(alignof(uint64_t) == 8);
		static_assert(alignof(QuickVector<char, 2>) == alignof(size_t));

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
		CHECK((v.begin() + 1) == v.end());

		v.push_back(11);
		v.push_back(12);
		v.push_back(13);
		CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 4);
		CHECK_EQUAL(4, v.size());
		CHECK_EQUAL(4, v.capacity());
		CHECK((v.begin() + 4) == v.end());

		// Grow past the quick buffer
		v.push_back(14);
		CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 5);
		v.push_back(15);
		CHECK_ARRAY_EQUAL(expected.begin(), v.begin(), 6);
		CHECK_EQUAL(6, v.size());
		CHECK(v.capacity() > 4);
		CHECK((v.begin() + 6) == v.end());
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
		std::vector<std::string> expected{"0", "1", "2"};

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

	template <class QV>
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

	template <size_t N1, size_t N2, bool assign>
	void testCopyImpl()
	{
		clearFooStats();
		std::vector<int> expected{10, 11, 12, 13, 14, 15};
		QuickVector<Foo, N1> v;
		v.push_back(10);
		v.push_back(11);
		v.push_back(12);
		if constexpr (assign)
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

	template <bool assign>
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

	template <size_t N1, size_t N2, bool assign>
	void testMoveImpl()
	{
		clearFooStats();
		std::vector<int> expected{10, 11, 12, 13, 14, 15};
		QuickVector<Foo, N1> v;
		v.push_back(10);
		v.push_back(11);
		v.push_back(12);

		if constexpr (assign)
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

	template <bool assign>
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

	template <size_t N>
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

	template <size_t N>
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

	template <typename T, size_t N>
	struct StdVector : public std::vector<T>
	{
	};

	template <template <typename, size_t> class VT, typename T, size_t N>
	void testInsertSingle()
	{
		clearFooStats();

		VT<T, N> v;
		std::vector<int> expected{0, 1, 2, 3, 4, 5};

		T a = 1;
		auto it = v.insert(v.end(), a);  // Test using "const T&"
		CHECK(it == v.end() - 1);
		CHECK_EQUAL(1, v.size());
		CHECK_ARRAY_EQUAL(&expected[1], v, 1);

		it = v.insert(v.end(), 2);  // "Test using T&&"
		CHECK(it == v.end() - 1);
		CHECK_EQUAL(2, v.size());
		CHECK_ARRAY_EQUAL(&expected[1], v, 2);

		v.insert(v.begin(), 0);
		CHECK_EQUAL(3, v.size());
		CHECK_ARRAY_EQUAL(&expected[0], v, 3);

		a = 10;
		v.insert(v.begin() + 1, a);  // Test using "const T&"
		CHECK_EQUAL(4, v.size());
		std::vector<int> exp2{0, 10, 1, 2, 3, 4, 5};
		CHECK_ARRAY_EQUAL(exp2, v, 4);

		v.insert(v.begin() + 2, -1);
		CHECK_EQUAL(5, v.size());
		std::vector<int> exp3{0, 10, -1, 1, 2, 3, 4, 5};
		CHECK_ARRAY_EQUAL(exp3, v, 5);
	}

	template <template <typename, size_t> class VT>
	void testInsertSingleAll()
	{
		testInsertSingle<VT, Foo, 4>();
		testInsertSingle<VT, Foo, 1>();
		testInsertSingle<VT, Foo, 10>();
	}

	TEST(insertSingle)
	{
		testInsertSingleAll<QuickVector>();
		testInsertSingleAll<StdVector>();
	}

	template <template <typename, size_t> class VT, typename T, size_t N>
	void testInsertMultiple()
	{
		clearFooStats();
		{
			VT<T, N> v;
			auto it = v.insert(v.end(), 0, 0);
			CHECK_EQUAL(0, v.size());
			CHECK(it == v.end());

			std::vector<int> exp{0};
			it = v.insert(v.end(), 1, 0);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			exp = {0, 1, 1};
			it = v.insert(v.end(), 2, 1);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin() + 1);
			CHECK_ARRAY_EQUAL(exp, v, 3);

			exp = {-1, -1, 0, 1, 1};
			it = v.insert(v.begin(), 2, -1);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin());
			CHECK_ARRAY_EQUAL(exp, v, 5);

			exp = {-1, -1, 0, 1, 1, 2, 2};
			it = v.insert(v.end(), 2, 2);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin() + 5);
			CHECK_ARRAY_EQUAL(exp, v, 7);

			exp = {-1, -1, 0, 1, 1, -2, -2, -2, 2, 2};
			it = v.insert(v.begin() + 5, 3, -2);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin() + 5);
			CHECK_ARRAY_EQUAL(exp, v, 7);
		}
		clearFooStats();
	}

	template <template <typename, size_t> class VT>
	void testInsertMultipleAll()
	{
		testInsertMultiple<VT, Foo, 1>();
		testInsertMultiple<VT, Foo, 2>();
		testInsertMultiple<VT, Foo, 3>();
		testInsertMultiple<VT, Foo, 4>();
		testInsertMultiple<VT, Foo, 5>();
		testInsertMultiple<VT, Foo, 6>();
		testInsertMultiple<VT, Foo, 7>();
		testInsertMultiple<VT, Foo, 8>();
		testInsertMultiple<VT, Foo, 9>();
		testInsertMultiple<VT, Foo, 10>();
	}

	TEST(insertMultiple)
	{
		testInsertMultipleAll<QuickVector>();
		testInsertMultipleAll<StdVector>();
	}

	template <template <typename, size_t> class VT, typename T, size_t N>
	void testInsertRange()
	{
		clearFooStats();
		{
			VT<T, N> v;
			auto it = v.insert(v.end(), 0, 0);
			CHECK_EQUAL(0, v.size());
			CHECK(it == v.end());

			std::vector<T> exp{0};
			it = v.insert(v.end(), exp.begin(), exp.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			exp = {0, 1, 2};
			std::vector<T> in{1, 2};
			it = v.insert(v.end(), in.begin(), in.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin() + 1);
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			exp = {0, -1, -2, -3, 1, 2};
			in = {-1, -2, -3};
			it = v.insert(v.begin() + 1, in.begin(), in.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin() + 1);
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			exp = {-4, -5, 0, -1, -2, -3, 1, 2};
			in = {-4, -5};
			it = v.insert(v.begin(), in.begin(), in.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK(it == v.begin());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// No-op (end,end)
			it = v.insert(v.begin(), in.end(), in.end());
			CHECK(it == v.begin());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
			// No-op (begin,begin)
			it = v.insert(v.begin() + 1, in.begin(), in.begin());
			CHECK(it == v.begin() + 1);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
			// No-op (begin+1,begin+1)
			it = v.insert(v.begin(), in.begin() + 1, in.begin() + 1);
			CHECK(it == v.begin());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
		}

		clearFooStats();
	}
	template <template <typename, size_t> class VT>
	void testInsertRangeAll()
	{
		testInsertRange<VT, Foo, 1>();
		testInsertRange<VT, Foo, 2>();
		testInsertRange<VT, Foo, 3>();
		testInsertRange<VT, Foo, 4>();
		testInsertRange<VT, Foo, 5>();
		testInsertRange<VT, Foo, 6>();
		testInsertRange<VT, Foo, 7>();
		testInsertRange<VT, Foo, 8>();
		testInsertRange<VT, Foo, 9>();
		testInsertRange<VT, Foo, 10>();
	}
	TEST(insertRange)
	{
		testInsertRangeAll<StdVector>();
		testInsertRangeAll<QuickVector>();
	}

	template <template <typename, size_t> class VT, typename T, size_t N>
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
		v.emplace(v.begin() + 1, a);
		CHECK_EQUAL(4, v.size());
		std::vector<int> exp2{0, 10, 1, 2, 3, 4};
		CHECK_ARRAY_EQUAL(exp2, v, 4);
	}

	template <template <typename, size_t> class VT>
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

	template <template <typename, size_t> class VT, typename T, size_t N>
	void testErase()
	{
		clearFooStats();
		{
			VT<T, N> v;

			std::vector<int> exp{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
			v.insert(v.end(), exp.begin(), exp.end());

			auto it = v.erase(v.end() - 1);
			exp.erase(exp.end() - 1);
			CHECK(it == v.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			it = v.erase(v.begin());
			exp.erase(exp.begin());
			CHECK(it == (v.begin()));
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			it = v.erase(v.begin() + 1);
			exp.erase(exp.begin() + 1);
			CHECK(it == (v.begin() + 1));
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			//
			// Erase range
			//

			// No-op (end,end)
			it = v.erase(v.end(), v.end());
			exp.erase(exp.end(), exp.end());
			CHECK(it == v.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
			// No-op (begin,begin)
			it = v.erase(v.begin(), v.begin());
			exp.erase(exp.begin(), exp.begin());
			CHECK(it == v.begin());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
			// No-op (begin+1,begin+1)
			it = v.erase(v.begin() + 1, v.begin() + 1);
			exp.erase(exp.begin() + 1, exp.begin() + 1);
			CHECK(it == v.begin() + 1);
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// erase at the beginning (1 element)
			it = v.erase(v.begin(), v.begin() + 1);
			exp.erase(exp.begin(), exp.begin() + 1);
			CHECK(it == v.begin());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// erase at the beginning (2 element)
			it = v.erase(v.begin(), v.begin() + 2);
			exp.erase(exp.begin(), exp.begin() + 2);
			CHECK(it == v.begin());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// erase at the end (1 element)
			it = v.erase(v.end() - 1, v.end());
			exp.erase(exp.end() - 1, exp.end());
			CHECK(it == v.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// erase at the end (2 element)
			it = v.erase(v.end() - 2, v.end());
			exp.erase(exp.end() - 2, exp.end());
			CHECK(it == v.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());

			// Erase all
			it = v.erase(v.begin(), v.end());
			exp.erase(exp.begin(), exp.end());
			CHECK(it == v.end());
			CHECK_EQUAL(exp.size(), v.size());
			CHECK_EQUAL(0, v.size());
			CHECK_ARRAY_EQUAL(exp, v, exp.size());
		}
		clearFooStats();
	}

	template <template <typename, size_t> class VT>
	void testEraseAll()
	{
		testErase<VT, Foo, 1>();
		testErase<VT, Foo, 2>();
		testErase<VT, Foo, 3>();
		testErase<VT, Foo, 4>();
		testErase<VT, Foo, 5>();
		testErase<VT, Foo, 6>();
		testErase<VT, Foo, 7>();
		testErase<VT, Foo, 8>();
		testErase<VT, Foo, 9>();
		testErase<VT, Foo, 10>();
		testErase<VT, Foo, 11>();
	}

	TEST(erase)
	{
		testEraseAll<StdVector>();
		testEraseAll<QuickVector>();
	}

	TEST(dummyend)
	{
		CHECK(gFoos.size() == 0);
	}
}
