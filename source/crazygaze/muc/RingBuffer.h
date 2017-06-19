/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"

#if CZ_DEBUG
	#define CZ_RINGBUFFER_DEBUG 1
#else
	//#define CZ_RINGBUFFER_DEBUG 0
#endif

namespace cz
{
	class RingBuffer
	{
	public:
		RingBuffer();
		~RingBuffer();

		//! Manually grows to the specified max size, if the current max size is smaller
		void reserve(int size);
		int getUsedSize() const
		{
			return m_fillcount;
		}
		int getMaxSize() const
		{
			return m_maxsize;
		}
		int getFreeSize() const
		{
			return m_maxsize - m_fillcount;
		}


		//! Empties the buffer
		/*
		 * \param releaseMemory
		 *    If true, it will free any allocated resources, which means it will need to allocate
		 *    memory again when witting data to the buffer.
 		 */
		void clear(bool releaseMemory=false);

		//! Tells if it's empty
		bool empty() const
		{
			return m_fillcount == 0;
		}

		//! Writes data to the buffer
		/*!
		 * \param ptr Data to write
		 * \param size how many bytes to write
		 * \return number of bytes written
		 * \note If necessary, the buffer will expand to accept the new data
		 */
		int write(const void *ptr, int size);

		//! Simulates a write, and give you the pointers you can use to write the data yourself
		/*!
		 */
		int write(int writeSize, void** ptr1, int* ptr1size, void **ptr2, int* ptr2size);

		//! Reads data from the buffer
		/*
		 *
		 * \param ptr where you get the data
		 * \param size how many bytes to read
		 * \return the number of bytes actually read
		 */
		int read(void *ptr, int size);

		//! \brief Returns the internal buffer for read pointer
		/*!
		 * You can use this function to get a pointer to the internal buffer, without worrying about
		 * the wrap around, as the buffer returned is what you can use without wrapping around.
		 * \param ptr Where you get the pointer
		 * \return The number of bytes you can read from the pointer, or 0 if nothing to read.
		 */
		int getReadPointer(void **ptr);

		//!
		/*!
		 * \brief Returns the internal buffers for the specified read operation, without removing data from the buffer
		 *
		 * \param readsize How many bytes you want to read
		 * \param ptr1 Where you'll get the pointer to the first part
		 * \param ptr1size Here you'll get how many bytes you can read from ptr1
		 * \param ptr2 Where you'll get the pointer to the first part, or NULL if everything is available with ptr1
		 * \param ptr2size Here you'll get how many bytes you can read from ptr2
		 * \param size size you want to read
		 * \return
		 *   Number of bytes you can actually read from the returned pointers. If it's smaller than the readsize,
		 *   it means there wasn't enough bytes available in the buffer. 
		 *
		 * \note
		 *  This returns pointers to the internal memory, for read only, which is handy in some cases to avoid some unnecessary memory copying
		 *  Therefore you shouldn't keep those pointers. Those pointers should be considered valid only until another method is called on the buffer
		 */
		int getReadPointers(int readsize, void **ptr1, int *ptr1size, void **ptr2, int *ptr2size);

		//!
		/*!
		 * \brief Returns the internal buffers pointers for the specified read operation
		 *
		 * This function is useful if you want to avoid some memory copying when reading from the buffer.
		 *
		 * \param readsize How many bytes you want to read
		 * \param ptr1 Where you'll get the pointer to the first part
		 * \param ptr1size Here you'll get how many bytes you can read from ptr1
		 * \param ptr2 Where you'll get the pointer to the first part, or NULL if everything is available with ptr1
		 * \param ptr2size Here you'll get how many bytes you can read from ptr2
		 * \param size size you want to read
		 * \return
		 *   Number of bytes you can actually read from the returned pointers. If it's smaller than the readsize,
		 *   it means there wasn't enough bytes available in the buffer. 
		 *
		 * \note
		 *  This returns pointers to the internal memory, for read only, which is handy in some cases to avoid some unnecessary memory copying
		 *  Therefore you shouldn't keep those pointers. Those pointers should be considered valid only until another method is called on the buffer
		 */
		int read(int readsize, void **ptr1, int *ptr1size, void **ptr2, int *ptr2size);

		//! Skips the specified amount of bytes
		/*!
		 * \param size How many bytes to dump
		 * \return how many bytes were actually dumped
		 */
		int skip(int size);


		/*
		Templated write function, to make it easier to write primitive types
		*/
		template<typename T>
		int write(T v)
		{
			static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
			return write(&v, sizeof(v));
		}

		/*
		Templated read function, to make it easier to read primitive types
		*/
		template<typename T>
		int read(T* v)
		{
			static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
			return read(v, sizeof(*v));
		}


		/*
		Peek at data, without removing it from the buffer.
		Returns true if successful, false if there is enough data in the buffer
		*/
		template<typename T>
		bool peek(T* v)
		{
			return peek(v, sizeof(*v));
		}

		RingBuffer(const RingBuffer& other) = delete;
		void operator=(const RingBuffer& other) = delete;


		class Iterator : public std::iterator<std::forward_iterator_tag, char>
		{
		public:
			Iterator(const RingBuffer* owner, int idx)
				: m_owner(const_cast<RingBuffer*>(owner))
				, m_idx(idx)
#if CZ_RINGBUFFER_DEBUG
				, m_readcounter(owner->m_readcounter)
#endif
			{
			}
			
			// Iterator functionality is organized according to the table at:
			// http://www.cplusplus.com/reference/iterator/

			//
			// Functionality for all categories of iterators
			//
			Iterator(const Iterator& other)
				: m_owner(other.m_owner)
				, m_idx(other.m_idx)
#if CZ_RINGBUFFER_DEBUG
				, m_readcounter(other.m_readcounter)
#endif
			{
			}
			Iterator& operator=(const Iterator& other)
			{
				m_owner = other.m_owner;
				m_idx = other.m_idx;
#if CZ_RINGBUFFER_DEBUG
				m_readcounter = other.m_readcounter;
#endif
				return *this;
			}
			Iterator& operator ++()
			{
				m_idx++;
				check();
				return *this;
			}
			Iterator operator ++(int) // postfix
			{
				auto tmp = *this;
				m_idx++;
				check();
				return tmp;
			}

			//
			// Input iterators functionality
			//
			bool operator==(const Iterator& other) const
			{
				check();
#if CZ_RINGBUFFER_DEBUG
				CZ_ASSERT(m_owner == other.m_owner);
#endif
				return m_idx == other.m_idx;
			}
			bool operator!=(const Iterator& other) const
			{
				return !(operator==(other));
			}
			char operator*() const
			{
				check();
				return m_owner->getAtIndex(m_idx);
			}
			char& operator*()
			{
				check();
				return m_owner->getAtIndex(m_idx);
			}
			// char* operator->() { } // This is operator is not needed since Ringbuffer only takes char elements

			// default constructible
			Iterator()
				: m_owner(nullptr)
				, m_idx(0)
#if CZ_RINGBUFFER_DEBUG
				, m_readcounter(0)
#endif
			{
			}

			//
			// Bidirectional iterators functionality
			//
			Iterator& operator--()
			{
				m_idx--;
				check();
				return *this;
			}
			Iterator operator--(int) // postfix
			{
				auto tmp = *this;
				m_idx--;
				check();
				return tmp;
			}

			//
			// Random access
			//
			Iterator operator+(int n) const
			{
				check();
				return Iterator(m_owner, m_idx + n);
			}
			Iterator operator-(int n) const
			{
				check();
				return Iterator(m_owner, m_idx - n);
			}

			int operator- (const Iterator& other) const
			{
				check();
#if CZ_RINGBUFFER_DEBUG
				CZ_ASSERT(m_owner == other.m_owner && m_idx >= other.m_idx);
#endif
				return m_idx - other.m_idx;
			}

			void check() const
			{
#if CZ_RINGBUFFER_DEBUG
				CZ_ASSERT(
					m_owner &&
					m_owner->m_readcounter == m_readcounter &&
					m_idx <= m_owner->getUsedSize()
				);
#endif
			}


		private:
			RingBuffer* m_owner = nullptr;
			int m_idx = 0;

#if CZ_RINGBUFFER_DEBUG
			int m_readcounter;
#endif
		};

		const Iterator begin() const
		{
			return Iterator(this, 0);
		}
		Iterator begin()
		{
			return Iterator(this, 0);
		}
		const Iterator end() const
		{
			return Iterator(this, m_fillcount);
		}
		Iterator end()
		{
			return Iterator(this, m_fillcount);
		}

#if CZ_RINGBUFFER_DEBUG
		int getReadCounter() const
		{
			return m_readcounter;
		}
#endif

	private:

		friend Iterator;
		char& getAtIndex(int idx)
		{
#if CZ_RINGBUFFER_DEBUG
			CZ_ASSERT(idx < m_fillcount);
#endif
			int i = m_readpos + idx;
			if (i >= m_maxsize)
				i -= m_maxsize;
			return m_pBuf.get()[i];
		}

		bool peek(void* buf, int size);

		std::unique_ptr<char[]> m_pBuf;
		int m_maxsize;
		int m_fillcount;
		int m_readpos;
		int m_writepos;

#if CZ_RINGBUFFER_DEBUG
		int m_readcounter = 0; // how many reads
#endif
	};


	inline RingBuffer::Iterator operator+(int n, const RingBuffer::Iterator& it)
	{
		return it.operator+(n);
	}
	inline RingBuffer::Iterator operator-(int n, const RingBuffer::Iterator& it)
	{
		return it.operator-(n);
	}


} // namespace cz

