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

	template <class T, class = void>
	struct is_iterator : std::false_type
	{
	};

	template <class T>
	struct is_iterator<T, std::void_t<typename std::iterator_traits<T>::iterator_category>> : std::true_type
	{
	};
}

/*
Minimalistic vector class, which allocates N items on the stack before growing
dynamically

The limited interface it provides is compatible with std::vector
*/
template<typename T, unsigned int N>
//class alignas(alignof(T) > alignof(size_t) ? alignof(T) : alignof(size_t)) QuickVector
class QuickVector
{
public:
	using value_type = T;
	using size_type = size_t;
	using iterator = value_type * ;
	using const_iterator = const value_type*;
	using reference = value_type & ;
	using const_reference = const value_type&;

	template<typename T, unsigned int N>
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
	QuickVector(const QuickVector<value_type, NN>& other)
	{
		copyFrom(other);
	}

	QuickVector(QuickVector&& other)
	{
		moveFrom(std::move(other));
	}

	template<unsigned int NN>
	QuickVector(QuickVector<value_type, NN>&& other)
	{
		moveFrom(std::move(other));
	}

	~QuickVector()
	{
		static_assert(std::is_unsigned_v<size_type>);
		static_assert(N >= 1);
		// Make sure we got the alignment right
		// Putting this here, since the destructor is surely compiled in
		static_assert(alignof(QuickVector) >= alignof(value_type));
		destroyRange(begin(), end());
		releaseBuffer<false>();
	}

	QuickVector& operator=(const QuickVector& other)
	{
		copyFrom(other);
		return *this;
	}

	template<unsigned int NN>
	QuickVector& operator=(const QuickVector<value_type, NN>& other)
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
	QuickVector& operator=(QuickVector<value_type, NN>&& other)
	{
		moveFrom(std::move(other));
		return *this;
	}

	//////////////////////////////////////////////////////////////////////////
	// Element access
	//////////////////////////////////////////////////////////////////////////

	reference at(size_type pos)
	{
		if (pos >= m_size)
			throw std::out_of_range("Out of range at QuickVector::at");
		return getRef(pos);
	}

	const_reference at(size_type pos) const
	{
		if (pos >= m_size)
			throw std::out_of_range("Out of range at QuickVector::at");
		return getRef(pos);
	}

	reference operator[](size_type pos)
	{
		return getRef(pos);
	}

	const_reference operator[](size_type pos) const
	{
		return getRef(pos);
	}

	reference front()
	{
		return *getPtr();
	}

	const_reference front() const
	{
		return *getPtr();
	}

	reference back()
	{
		return getRef(m_size - 1);
	}

	const_reference back() const
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

	iterator begin() noexcept
	{
		return getPtr();
	}

	const_iterator begin() const noexcept
	{
		return getPtr();
	}

	iterator end() noexcept
	{
		return getPtr() + m_size;
	}

	const_iterator end() const noexcept
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
		return size_type(~0) / sizeof(value_type);
	}

	void reserve(size_type capacity)
	{
		if (capacity <= m_capacity)
			return;
		grow(capacity - m_capacity);
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

	iterator insert(const_iterator pos, const T& value)
	{
		return insertImpl(pos, value);
	}

	iterator insert(const_iterator pos, T&& value)
	{
		return insertImpl(pos, std::move(value));
	}

	iterator insert(const_iterator pos, size_type count, const T& value)
	{
		assert(pos >= begin() && pos <= end());

		// Do the same as std::vector, according to http://en.cppreference.com/w/cpp/container/vector/insert
		if (count == 0)
			return const_cast<T*>(pos);

		auto[ptr, assignCount, constructCount] = insertSlots(pos, count);
		T* ret = ptr;
		while (assignCount--)
		{
			*ptr = value;
			ptr++;
		}

		while (constructCount--)
		{
			::new((void*)ptr) T(value);
			ptr++;
		}

		return ret;
	}

	template<class Iter,
		class = std::enable_if_t<detail::is_iterator<Iter>::value>>
	iterator insert(const_iterator pos, Iter first, Iter last)
	{
		assert(pos >= begin() && pos <= end());
		assert(last >= first);

		// Do the same as std::vector, according to http://en.cppreference.com/w/cpp/container/vector/insert
		if (first==last)
			return const_cast<T*>(pos);

		auto[ptr, assignCount, constructCount] = insertSlots(pos, last - first);
		T* ret = ptr;
		while (assignCount--)
		{
			*ptr = *first;
			ptr++;
			first++;
		}

		while (constructCount--)
		{
			::new((void*)ptr) T(*first);
			ptr++;
			first++;
		}

		return ret;
	}

	template<class ... Args>
	iterator emplace(const_iterator pos, Args&&... args)
	{
		assert(pos >= begin() && pos <= end());
		auto [ptr, assignCount, constructCount] = insertSlots(pos, 1);
		if (assignCount)
		{
			assert(constructCount == 0);
			*ptr = T(std::forward<Args>(args)...);
		}
		else
		{
			::new((void*)ptr) T(std::forward<Args>(args)...);
		}
		return ptr;
	}

	iterator erase(const_iterator pos)
	{
		assert(pos >= begin() && pos < end());

		T* ptr = const_cast<iterator>(pos);
		T* last = &back();

		T* ret = ptr;
		while (ptr != last)
		{
			*ptr = std::move(*(ptr + 1));
			ptr++;
		}

		// For the last one we call the destructor
		ptr->~T();
		m_size--;
		return ret;
	}

	iterator erase(const_iterator first, const_iterator last)
	{
		assert(
			last <= end() &&
			(first == last || (first < last && first >= begin()))
		);

		if (first == last)
			return const_cast<iterator>(first);

		T* dst = const_cast<iterator>(first);
		T* src = const_cast<iterator>(last);
		T* res = const_cast<iterator>(first);

		T* endPtr = end();
		while (src != endPtr)
		{
			*dst = std::move(*src);
			dst++;
			src++;
		}

		while (dst != end())
		{
			dst->~T();
			dst++;
		}

		m_size -= last - first;
		return res;
	}

	void push_back(const T& value)
	{
		if (m_size == m_capacity)
			grow(m_capacity);
		::new((void*)end()) T(value);
		m_size++;
	}

	void push_back(T&& value)
	{
		if (m_size == m_capacity)
			grow(m_capacity);
		::new((void*)end()) T(std::move(value));
		m_size++;
	}

	template<class... Args>
	void emplace_back(Args&&... args)
	{
		if (m_size == m_capacity)
			grow(m_capacity);
		::new((void*)end()) T(std::forward<Args>(args)...);
		m_size++;
	}

	//////////////////////////////////////////////////////////////////////////
	// Private bits
	//////////////////////////////////////////////////////////////////////////
private:

	template<typename TT>
	iterator insertImpl(const_iterator pos, TT&& value)
	{
		assert(pos >= begin() && pos <= end());
		auto [ptr, assignCount, constructCount] = insertSlots(pos, 1);
		if (assignCount)
		{
			assert(constructCount == 0);
			*ptr = std::move(value);
		}
		else
		{
			::new((void*)ptr) T(std::forward<TT>(value));
		}
		return ptr;
	}

	struct InsertSlotsResult
	{
		T* ptr;
		size_type assignCount;
		size_type constructCount;
	};

	//! Creates empty or unallocated slots, so the caller can insert new items
	// \ returns a pointer to the slot
	InsertSlotsResult insertSlots(const_iterator pos, size_type count)
	{
		size_type idx = pos - begin();
		assert(idx <= m_size && count>=1);
		InsertSlotsResult res;

		const size_t newSize = m_size + count;
		if (newSize <= m_capacity)
		{
			size_type todo = m_size - idx;

			if (todo == 0) // Insert at the end
			{
				res.assignCount = 0;
				res.constructCount = count;
			}
			else
			{
				if constexpr(std::is_trivial_v<T>)
				{
					::memmove(const_cast<iterator>(pos + count), pos, todo * sizeof(T));
					res.assignCount = count;
					res.constructCount = 0;
				}
				else
				{
					T* dst = end() + (count -1);
					T* src = end() - 1;

					// Depending on how many items we are inserting, the empty slots can end up being a combination
					// of "Empty Slots + Unconstructed slots", where empty slots are slots that previously had elements,
					// and unconstructed slots are uninitialized memory that had no elements at all.
					size_t numElemsFromPos = end() - pos; // number of elements starting at position "pos"
					if (count <= numElemsFromPos)
					{
						res.assignCount = count;
						res.constructCount = 0;
					}
					else
					{
						res.assignCount = numElemsFromPos;
						res.constructCount = count - numElemsFromPos;
					}

					// Anything that ends up after the current last item is uninitialized memory, and
					// needs to be fully constructed (as-in: Call the the T constructor)
					while (todo && dst >= end())
					{
						::new((void*)dst) T(std::move(*src));
						todo--;
						dst--;
						src--;
					}

					// The rest falls in memory that already had elements, so we use assignment instead
					// of constructor
					while (todo)
					{
						*dst = std::move(*src);
						todo--;
						dst--;
						src--;
					}
				}
			}
		}
		else
		{
			// Increment capacity as a multiple of the current capacity
			size_type extra = newSize - m_capacity;
			size_type multiplier = extra / m_capacity;
			if (extra % m_capacity)
				multiplier++;
			grow(m_capacity*multiplier, begin() + idx, count);
			res.assignCount = 0;
			res.constructCount = count;
		}

		m_size += count;
		assert(m_size <= m_capacity);
		res.ptr = begin() + idx;
		return res;
	}

	// Increase the capacity
	void grow(size_type count, T *emptySlot = nullptr, size_type emptySlotCount = 0)
	{
		if (count == 0)
			return;

		size_type newcapacity = m_capacity + count;
		if (newcapacity <= N)
			return;
		m_capacity = newcapacity;

		auto newbuf = detail::alignedAlloc<value_type>(m_capacity);
		if (!newbuf)
			throw std::bad_alloc();

		if (emptySlot == nullptr)
		{
			moveConstruct<true>(begin(), end(), reinterpret_cast<value_type*>(newbuf));
		}
		else
		{
			value_type* ptr = reinterpret_cast<value_type*>(newbuf);
			moveConstruct<true>(begin(), emptySlot, ptr);
			ptr += (emptySlot - begin()) + emptySlotCount;
			moveConstruct<true>(emptySlot, end(), ptr);
		}

		releaseBuffer<false>();
		m_buf = reinterpret_cast<uint8_t*>(newbuf);
	}

	template<unsigned int NN>
	void copyFrom(const QuickVector<T, NN>& other)
	{
		if (m_size)
			clear();
		reserve(other.m_size);
		m_size = other.m_size;
		copyConstruct(other.begin(), other.end(), getPtr());
	}

	template<unsigned int NN>
	void moveFrom(QuickVector<T, NN>&& other)
	{
		if (m_size)
			clear();

		// If it fits in our quick buffer, we give preference to that
		if (other.m_size <= N)
		{
			releaseBuffer<true>();

			m_size = other.m_size;
			moveConstruct<true>(other.begin(), other.end(), begin());
			other.m_size = 0;
		}
		else
		{
			if (other.m_buf)
			{
				releaseBuffer<true>();
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
			first = last; // Just to force VS to consider the parameters used
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

	template<bool reset>
	void releaseBuffer()
	{
		assert(reset==false || m_size == 0);
		if (m_buf)
		{
			detail::alignedFree(m_buf);
			if constexpr(reset)
			{
				m_buf = nullptr;
				m_capacity = N;
			}
		}
	}

	// This needs to be the first member, because of the alignment
	alignas(alignof(T)) uint8_t m_quickbuf[sizeof(T)* N];
	uint8_t* m_buf = nullptr;
	size_type m_capacity = N;
	size_type m_size = 0;
};

}

