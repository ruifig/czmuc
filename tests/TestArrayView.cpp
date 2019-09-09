#include "UnitTestsPCH.h"

using namespace cz;

static cz::ArrayView<const int> gV;
static cz::ArrayView<const int> gV2;
SUITE(ArrayView)
{
	static int gData[4] = {0,1,2,3};
	static const int gConstData[4] = {0,10,20,30};

	TEST(MakeView)
	{
		{
			auto v = make_view(&gData[0], 4);
			CHECK_ARRAY_EQUAL(gData, v.data(), 4);
			CHECK_EQUAL(4, v.size());
		}

		{
			auto v = make_view(&gData[0], &gData[4]);
			CHECK_ARRAY_EQUAL(gData, v.data(), 4);
			CHECK_EQUAL(4, v.size());
		}

		{
			auto v = make_view(gData);
			CHECK_ARRAY_EQUAL(gData, v.data(), 4);
			CHECK_EQUAL(4, v.size());
		}

		{
			// Initializer list
			auto i1 = {0,1,2,3};
			auto i2 = {0,10,20,30};
			ArrayView<const int> v1 = make_view(i1);
			ArrayView<const int> v2 = make_view(i2);
			CHECK_ARRAY_EQUAL(gData, v1.data(), 4);
			CHECK_EQUAL(4, v1.size());
			CHECK_ARRAY_EQUAL(gConstData, v2.data(), 4);
		}

		{
			auto v = make_view(gConstData);
			CHECK_ARRAY_EQUAL(gConstData, v.data(), 4);
			CHECK_EQUAL(4, v.size());
			static_assert(std::is_const_v<decltype(v)::value_type>, "array_view T should be const");
		}
	}

	TEST(Iteration)
	{

		// Const
		{
			ArrayView<const int> v(gConstData);
			CHECK_ARRAY_EQUAL(gConstData, v.data(), 4);
			CHECK_EQUAL(4, v.size());
			int tmp[4];
			int idx=0;
			for (auto&& i : v)
			{
				tmp[idx] = i;
				idx++;
			}

			CHECK_EQUAL(4, idx);
			CHECK_ARRAY_EQUAL(gConstData, tmp, idx);
		}

		// non-const, changing the elements
		{
			int data[4] = {0,1,2,3};
			ArrayView<int> v(data);
			CHECK_ARRAY_EQUAL(v.data(), data, 4);
			CHECK_EQUAL(4, v.size());
			int idx=0;
			for (auto&& i : v)
			{
				i = idx*10;
				idx++;
			}

			CHECK_EQUAL(4, idx);
			CHECK_ARRAY_EQUAL(gConstData, v, idx);
		}
	}

	TEST(Indexing)
	{
		static int data[4] = {0,1,2,3};
		ArrayView<int> v(data);

		CHECK_EQUAL(1, v[1]);

		// Try changing the value
		v[1] = v[1] * 10;
		CHECK_EQUAL(10, v[1]);
	}

	TEST(Converting)
	{
		std::vector<int> copy = make_view(gData).copyToVector();
		CHECK_ARRAY_EQUAL(gData, copy, 4);
	}

}





