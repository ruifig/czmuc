#pragma once
#include <type_traits>
#if defined(_MSC_VER)
#else
	#include <cstdlib>
#endif
#include <assert.h>

namespace cz
{

namespace detail
{
	// From https://stackoverflow.com/questions/10585450/how-do-i-check-if-a-template-parameter-is-a-power-of-two
	constexpr bool is_powerof2(int v)
	{
		return v && ((v & (v - 1)) == 0);
	}
	constexpr bool is_powerof2(size_t v)
	{
		return v && ((v & (v - 1)) == 0);
	}

	template<typename T>
	void* alignedAlloc(size_t n)
	{
#if defined(_MSC_VER)
		static_assert(detail::is_powerof2(alignof(T)));
		return _aligned_malloc(sizeof(T) * n, alignof(T));
#else
		return std::aligned_alloc(alignof(T), std::max(alignof(T), n * sizeof(T)));
#endif
	}

	inline void alignedFree(void* ptr)
	{
#if defined(_MSC_VER)
		_aligned_free(ptr);
#else
		std::free(ptr);
#endif
	}
}

/*
Minimalistic vector class, which allocates N items on the stack before growing
dynamically

The limited interface it provides is compatible with std::vector
*/
template<typename T, unsigned int N, typename SIZE_TYPE=size_t>
class alignas(alignof(T) > alignof(size_t) ? alignof(T) : alignof(size_t)) QuickVector
{
public:
	using value_type = T;
	using size_type = SIZE_TYPE;

	template<typename T, unsigned int N, typename SIZE_TYPE>
	friend class QuickVector;

	//////////////////////////////////////////////////////////////////////////
	// Member Functions
	//////////////////////////////////////////////////////////////////////////
	QuickVector()
	{
	}

	QuickVector(const QuickVector& other)
	{
		copyFrom(other);
	}

	template<unsigned int NN>
	QuickVector(const QuickVector<T, NN, size_type>& other)
	{
		copyFrom(other);
	}

	QuickVector(QuickVector&& other)
	{
		moveFrom(std::move(other));
	}

	template<unsigned int NN>
	QuickVector(QuickVector<T, NN, size_type>&& other)
	{
		moveFrom(std::move(other));
	}

	~QuickVector()
	{
		static_assert(std::is_unsigned_v<size_type>);
		static_assert(N >= 1);
		// Make sure we got the alignment right
		// Putting this here, since the destructor is surely compiled in
		static_assert(alignof(QuickVector) >= alignof(T));
		destroyRange(begin(), end());
		if (m_buf)
			detail::alignedFree(m_buf);
	}

	QuickVector& operator=(const QuickVector& other)
	{
		copyFrom(other);
		return *this;
	}

	template<unsigned int NN>
	QuickVector& operator=(const QuickVector<T, NN, size_type>& other)
	{
		copyFrom(other);
		return *this;
	}

	QuickVector& operator=(QuickVector&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	template<unsigned int NN>
	QuickVector& operator=(QuickVector<T, NN, size_type>&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	//////////////////////////////////////////////////////////////////////////
	// Element access
	//////////////////////////////////////////////////////////////////////////

	T& at(size_type pos)
	{
		if (pos >= m_size)
			throw std::out_of_range("Out of range at QuickVector::at");
		return getRef(pos);
	}

	const T& at(size_type pos) const
	{
		if (pos >= m_size)
			throw std::out_of_range("Out of range at QuickVector::at");
		return getRef(pos);
	}

	T& operator[](size_type pos)
	{
		return getRef(pos);
	}

	const T& operator[](size_type pos) const
	{
		return getRef(pos);
	}

	T& front()
	{
		return *getPtr();
	}

	const T& front() const
	{
		return *getPtr();
	}

	T& back()
	{
		return getRef(m_size - 1);
	}

	const T& back() const
	{
		return getRef(m_size - 1);
	}

	T* data() noexcept
	{
		return getPtr();
	}
	const T* data() const noexcept
	{
		return getPtr();
	}

	//////////////////////////////////////////////////////////////////////////
	// Iterators
	//////////////////////////////////////////////////////////////////////////

	T* begin()
	{
		return getPtr();
	}

	const T* begin() const
	{
		return getPtr();
	}

	T* end()
	{
		return getPtr() + m_size;
	}

	const T* end() const
	{
		return getPtr() + m_size;
	}

	//////////////////////////////////////////////////////////////////////////
	// Capacity
	//////////////////////////////////////////////////////////////////////////

	bool empty() const noexcept
	{
		return m_size == 0;
	}

	size_type size() const
	{
		return m_size;
	}

	size_type max_size() const noexcept
	{
		return size_type(~0) / sizeof(T);
	}

	void reserve(size_type capacity)
	{
		if (capacity <= m_capacity)
			return;

		m_capacity = capacity;
		if (m_capacity <= N)
			return;

		auto newbuf = detail::alignedAlloc<T>(m_capacity);
		if (!newbuf)
			throw std::bad_alloc();

		moveConstruct<true>(begin(), end(), reinterpret_cast<T*>(newbuf));
		if (m_buf)
			detail::alignedFree(m_buf);
		m_buf = reinterpret_cast<uint8_t*>(newbuf);
	}

	size_type capacity() const
	{
		return m_capacity;
	}

	//////////////////////////////////////////////////////////////////////////
	// Modifiers
	//////////////////////////////////////////////////////////////////////////

	void clear()
	{
		destroyRange(begin(), end());
		m_size = 0;
	}

	void push_back(const T& value)
	{
		if (m_size == m_capacity)
			reserve(m_capacity * 2);
		::new((void*)end()) T(value);
		m_size++;
	}

	void push_back(T&& value)
	{
		if (m_size == m_capacity)
			reserve(m_capacity * 2);
		::new((void*)end()) T(std::move(value));
		m_size++;
	}

	template<class... Args>
	void emplace_back(Args&&... args)
	{
		if (m_size == m_capacity)
			reserve(m_capacity * 2);
		::new((void*)end()) T(std::forward<Args>(args)...);
		m_size++;
	}

	//////////////////////////////////////////////////////////////////////////
	// Private bits
	//////////////////////////////////////////////////////////////////////////
private:

	template<unsigned int NN>
	void copyFrom(const QuickVector<T, NN, size_type>& other)
	{
		if (m_size)
			clear();
		reserve(other.m_size);
		m_size = other.m_size;
		copyConstruct(other.begin(), other.end(), getPtr());
	}

	template<unsigned int NN>
	void moveFrom(QuickVector<T, NN, size_type>&& other)
	{
		if (m_size)
			clear();

		auto freeBuffer = [this]()
		{
			if (m_buf)
			{
				detail::alignedFree(m_buf);
				m_buf = nullptr;
				m_capacity = N;
			}
		};

		// If it fits in our quick buffer, we give preference to that
		if (other.m_size <= N)
		{
			freeBuffer();

			m_size = other.m_size;
			moveConstruct<true>(other.begin(), other.end(), begin());
			other.m_size = 0;
		}
		else
		{
			if (other.m_buf)
			{
				freeBuffer();
				m_capacity = other.m_capacity;
				m_size = other.m_size;
				m_buf = other.m_buf;
				other.m_capacity = NN;
				other.m_size = 0;
				other.m_buf = nullptr;
			}
			else
			{
				reserve(other.m_size);
				m_size = other.m_size;
				moveConstruct<true>(other.begin(), other.end(), begin());
				other.m_size = 0;
			}
		}
	}

	/*
	Moves a range of T elements into a new section of memory, constructing using
	the new elements by moving if possible, and destroying the old elements
	*/
	template<bool destroy>
	static void moveConstruct(T* first, T* last, T* dst)
	{
		assert(dst < first || dst >= last);
		if constexpr(std::is_trivial_v<T>)
		{
			::memcpy(dst, first, (last - first) * sizeof(T));
		}
		else
		{
			while (first != last)
			{
				::new((void*)dst) T(std::move(*first));
				if constexpr(destroy)
					first->~T();
				first++;
				dst++;
			}
		}
	}

	static void copyConstruct(const T* first, const T* last, T* dst)
	{
		assert(dst < first || dst >= last);
		if constexpr(std::is_trivial_v<T>)
		{
			::memcpy(dst, first, (last - first) * sizeof(T));
		}
		else
		{
			while (first != last)
			{
				::new((void*)dst) T(*first);
				first++;
				dst++;
			}
		}
	}

	static void destroyRange(T* first, T* last)
	{
		if constexpr(std::is_trivial_v<T>)
		{
			// Nothing to do
		}
		else
		{
			while (first != last)
			{
				first->~T();
				first++;
			}
		}
	}

	T* getPtr()
	{
		return m_buf ? reinterpret_cast<T*>(m_buf) : reinterpret_cast<T*>(m_quickbuf);
	}

	const T* getPtr() const
	{
		return m_buf ? reinterpret_cast<const T*>(m_buf) : reinterpret_cast<const T*>(m_quickbuf);
	}

	T& getRef(size_type idx)
	{
		assert(idx < m_size);
		return getPtr()[idx];
	}

	const T& getRef(size_type idx) const
	{
		assert(idx < m_size);
		return getPtr()[idx];
	}

	// This needs to be the first member, because of the alignment
	uint8_t m_quickbuf[sizeof(T)* N];
	uint8_t* m_buf = nullptr;
	size_type m_capacity = N;
	size_type m_size = 0;
};

}

