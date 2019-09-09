#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

// A similar implementation at : https://github.com/rhysd/array_view/blob/master/include/array_view.hpp
// Intentionally simple for now.
template<typename T>
class ArrayView
{
public:
	using value_type = T;
	using iterator = value_type*;
	using pointer =  value_type*;
	using reference = value_type&;
	using size_type = size_t;

	ArrayView() noexcept
		: m_data(nullptr)
		, m_size(0)
	{}

	ArrayView(pointer data, size_type size) noexcept
		: m_data(data)
		, m_size(size)
	{
	}

	ArrayView(pointer from, pointer to) noexcept
		: m_data(from)
		, m_size(to - from)
	{
		CZ_ASSERT(to>=from);
	}

	template<size_type N>
	ArrayView(value_type(&data)[N]) noexcept
		: m_data(N>0 ? std::addressof(data[0]) : nullptr)
		, m_size(N)
	{
		static_assert(N>0, "Zero-length array not permitted in ISO C++");
	}

	// std::initializer provides access to an array of objects of type "const T", so don't enable this if
	// our T is not const. This keeps the compiler from spitting a long error
	ArrayView(std::initializer_list<value_type> const& data)
	{
		static_assert(std::is_const_v<value_type>, "Only a ArrayView<const T> can be initialized with an initializer list");
		if constexpr (std::is_const_v<value_type>)
		{
			m_data = std::begin(data);
			m_size = data.size();
		}
	}

	constexpr pointer data() const noexcept
	{
		return m_data;
	}

	constexpr iterator begin() const noexcept
	{
		return m_data;
	}

	constexpr iterator end() const noexcept
	{
		return m_data + m_size;
	}

	size_type size() const noexcept
	{
		return m_size;
	}

	constexpr reference operator[](const size_type idx) const noexcept
	{
		CZ_ASSERT(idx<m_size);
		return *(m_data + idx);
	}

	// The standard doesn't allow containers of const elements,
	// For example, VS 2019 throws: "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed."
	// So, we remove the const
	std::vector<std::remove_const_t<value_type>> copyToVector() const
	{
		return std::vector<std::remove_const_t<value_type>>(begin(), end());
	}

private:
	pointer m_data;
	size_type m_size;
};

template<typename T>
constexpr
auto make_view(T* data, size_t size)
{
	return ArrayView<T>(data, size);
}

template<typename T>
constexpr
auto make_view(T* from,  T* to)
{
	return ArrayView<T>(from, to);
}

template<class T, size_t N>
auto make_view(T (&data)[N])
{
	return ArrayView<T>(data);
}

template<class T>
constexpr
ArrayView<const T> make_view(std::initializer_list<T> const& data)
{
	printf("Initializer\n");
	return ArrayView<const T>(data.begin(), data.size());
}

} // namespace cz

