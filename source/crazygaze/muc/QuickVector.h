#pragma once
#include <type_traits>
#include <assert.h>

/*
TODO:
Align m_quickbuf and m_buf as per T requirements
	- see https://en.cppreference.com/w/cpp/types/alignment_of

*/

namespace cz
{


/*
Minimalistic vector class, which allocates N items on the stack before growing
dynamically

The limited interface it provides is compatible with std::vector
*/
template<typename T, size_t QuickSize>
class QuickVector
{
public:
	using value_type = T;
	using size_type = size_t;

	template<typename T, size_t NN>
	friend class QuickVector;

	QuickVector()
	{
	}

	QuickVector(const QuickVector& other)
	{
		copyFrom(other);
	}

	template<size_t NN>
	QuickVector(const QuickVector<T, NN>& other)
	{
		copyFrom(other);
	}

	QuickVector(QuickVector&& other)
	{
		moveFrom(std::move(other));
	}

	template<size_t NN>
	QuickVector(QuickVector<T, NN>&& other)
	{
		moveFrom(std::move(other));
	}

	~QuickVector()
	{
		destroyRange(begin(), end());
		if (m_buf)
			::free(m_buf);
	}

	QuickVector& operator=(const QuickVector& other)
	{
		copyFrom(other);
		return *this;
	}

	template<size_t NN>
	QuickVector& operator=(const QuickVector<T, NN>& other)
	{
		copyFrom(other);
		return *this;
	}

	QuickVector& operator=(QuickVector&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	template<size_t NN>
	QuickVector& operator=(QuickVector<T, NN>&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	void reserve(size_t capacity)
	{
		if (capacity <= m_capacity)
			return;

		m_capacity = capacity;
		if (m_capacity <= QuickSize)
			return;

		auto newbuf = ::malloc(m_capacity * sizeof(T));
		if (!newbuf)
			throw std::bad_alloc();

		moveConstruct<true>(begin(), end(), reinterpret_cast<T*>(newbuf));
		if (m_buf)
			::free(m_buf);
		m_buf = reinterpret_cast<uint8_t*>(newbuf);
	}

	size_t size() const
	{
		return m_size;
	}

	size_t capacity() const
	{
		return m_capacity;
	}

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

	void clear()
	{
		destroyRange(begin(), end());
		m_size = 0;
	}

	T& operator[](size_t pos)
	{
		return getRef(pos);
	}

	const T& operator[](size_t pos) const
	{
		return getRef(pos);
	}

private:

	template<size_t NN>
	void copyFrom(const QuickVector<T, NN>& other)
	{
		if (m_size)
			clear();
		reserve(other.m_size);
		m_size = other.m_size;
		copyConstruct(other.begin(), other.end(), getPtr());
	}

	template<size_t NN>
	void moveFrom(QuickVector<T, NN>&& other)
	{
		if (m_size)
			clear();

		auto freeBuffer = [this]()
		{
			if (m_buf)
			{
				::free(m_buf);
				m_buf = nullptr;
				m_capacity = QuickSize;
			}
		};

		// If it fits in our quick buffer, we give preference to that
		if (other.m_size <= QuickSize)
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

	T& getRef(size_t idx)
	{
		assert(idx < m_size);
		return getPtr()[idx];
	}

	const T& getRef(size_t idx) const
	{
		assert(idx < m_size);
		return getPtr()[idx];
	}

	size_t m_capacity = QuickSize;
	size_t m_size = 0;
	uint8_t m_quickbuf[sizeof(T)* QuickSize];
	uint8_t* m_buf = nullptr;
};

}

