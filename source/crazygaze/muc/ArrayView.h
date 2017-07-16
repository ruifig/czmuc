#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

// A similar implementation at : https://github.com/rhysd/array_view/blob/master/include/array_view.hpp
template<typename T>
class ArrayView
{
public:
	using value_type = T;
	using iterator = value_type* ;
	using size_type = size_t;

	ArrayView()
		: m_size(0)
		, m_data(nullptr)
	{}

	ArrayView(T* data, size_t size)
		: m_size(size)
		, m_data(data)
	{
	}

	constexpr iterator begin() noexcept
	{
		return m_data;
	}

	constexpr iterator end() noexcept
	{
		return m_data + m_size;
	}

private:
	T* m_data;
	size_type m_size;
};

template<typename T>
auto make_view(T* data, size_t size)
{
	return ArrayView<T>(data, size);
}

template<typename T>
auto make_view(const T* data, size_t size)
{
	return ArrayView<const T>(data, size);
}

} // namespace cz
