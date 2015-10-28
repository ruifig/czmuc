/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Stream like buffer, from which we can read and write.
	It is implemented with chunks, so writing and reading will allocate or drop chunks,
	therefore avoiding the need to move/copy memory
*********************************************************************/

#pragma once

#include "crazygaze/Any.h"
#include <memory>
#include <queue>
#include <functional>

namespace cz
{

struct BlockReadInfo;
struct BlockWriteInfo;
struct BlockReserveWriteInfo;

class ChunkBuffer
{
private:

	class Block
	{
	public:
		Block(std::unique_ptr<char[]> ptr, unsigned capacity, unsigned usedSize);
		Block(Block&& other);
		Block(const Block&) = delete;
		Block& operator = (const Block&) = delete;
		//! Data available to read

		unsigned size() const;
		//! Unused capacity
		unsigned unused() const;
		unsigned peek(BlockReadInfo& info) const;
		void read(BlockReadInfo& info) const;
		void write(BlockWriteInfo& info);
		void reserveWrite(BlockReserveWriteInfo& info);
		void writeAt(unsigned pos, BlockWriteInfo& info);
		const char* getReadPtr() const;
	private:
		std::unique_ptr<char[]> m_ptr;
		unsigned m_capacity = 0;
		mutable unsigned m_readPos = 0;
		unsigned m_writePos = 0;
	};

	class Queue : public std::queue<Block, std::deque<Block>>
	{
	public:
		auto& container()
		{
			return this->c;
		}
		const auto& container() const
		{
			return this->c;
		}
	};

	mutable Queue m_blocks;
	unsigned m_defaultBlockSize=0;
#ifndef NDEBUG
	mutable unsigned m_dbgReadCounter = 0;
#endif

public:

	ChunkBuffer(unsigned initialCapacity=0, unsigned defaultBlockSize=4096);
	ChunkBuffer(ChunkBuffer&) = delete; // Implement this if required
	ChunkBuffer(ChunkBuffer&& other)
		: m_blocks(std::move(other.m_blocks))
		, m_defaultBlockSize(std::move(other.m_defaultBlockSize))
#ifndef NDEBUG
		, m_dbgReadCounter(std::move(other.m_dbgReadCounter))
#endif
	{
#ifndef NDEBUG
		other.m_dbgReadCounter = 0;
#endif
	};

	ChunkBuffer& operator=(ChunkBuffer&) = delete; // Implement this if required
	ChunkBuffer& operator=(ChunkBuffer&&) = delete; // Implement this if required

	//! Returns how many bytes are available to read
	unsigned calcSize() const;

	unsigned numBlocks() const;
	void iterateBlocks(std::function<void(const char*, unsigned)> f);

	void writeBlock(std::unique_ptr<char[]> data, unsigned capacity, unsigned size);
	void write(const void* data, unsigned size);

	struct WritePos
	{
		unsigned block = 0; // block index
		unsigned write = 0; // position within the block
#ifndef NDEBUG
		unsigned dbgReadCounter = 0;
#endif
	};

	//! Simulate a write and returns the write position
	/*! This is useful for when we want to reserve space for value we don't know yet, such as when writing an header,
	* and you only know the size when you finish serializing.
	*
	* \return
	*	Write information that can be used with writeAt. No reads are allowed between the call to writeReserve and writeAt
	*/
	WritePos writeReserve(unsigned size);
	void peek(void* data, unsigned size) const;
	void read(void* data, unsigned size) const;

	//! Write any arithmetic type to the buffer
	template<typename T>
	void write(const T& val)
	{
		static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
		write(&val, sizeof(val));
	}

	//! Read any arithmetic type
	template<typename T>
	void read(T& val) const
	{
		static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
		read(&val, sizeof(val));
	}

	void writeAt(WritePos pos, const void* data, unsigned size);

	template<typename T>
	void writeAt(WritePos pos, const T& val)
	{
		static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
		writeAt(pos, &val, sizeof(val));
	}

	template<typename T>
	bool peek(T& dst) const
	{
		static_assert(std::is_arithmetic<T>::value, "Type is not an arithmetic type");
		if (calcSize() < sizeof(dst))
			return false;
		peek(&dst, sizeof(dst));
		return true;
	}

	bool tryRead(std::string& dst) const
	{
		int bufSize = calcSize();
		int strSize;
		if (!peek(strSize))
			return false;
		if (bufSize < static_cast<int>(sizeof(strSize)) + strSize)
			return false;

		dst.clear();
		dst.reserve(strSize);
		dst.append(strSize, 0);
		read<int>(strSize);
		read(&dst[0], strSize);
		return true;
	}

};


namespace details
{
	//
	// std::vector serialization
	//

	// This non-specialized version will work for any types that have the requires << and >> operators defined
	template<typename T, class ENABLED = void>
	struct VectorSerialization
	{
		template<typename Container>
		static cz::ChunkBuffer& serialize(cz::ChunkBuffer& stream, const Container& v)
		{
			stream << static_cast<int>(v.size());
			for (auto&& i : v)
				stream << i;
			return stream;
		}

		template<typename Container>
		static const cz::ChunkBuffer& deserialize(const cz::ChunkBuffer& stream, Container& v)
		{
			int size;
			stream >> size;
			v.clear();
			v.reserve(size);
			int todo = size;
			while (todo--)
			{
				v.emplace_back();
				stream >> v.back();
			}
			return stream;
		}
	};

	// This is a specialized version for vector of arithmetic types, to use memory copying
	template<typename T>
	struct VectorSerialization<T, typename std::enable_if<std::is_arithmetic<T>::value>::type>
	{

		template<typename Container>
		static cz::ChunkBuffer& serialize(cz::ChunkBuffer& stream, const Container& v)
		{
			stream << static_cast<int>(v.size());
			stream.write(&v[0], sizeof(v[0])*static_cast<unsigned>(v.size()));
			return stream;
		}

		template<typename Container>
		static const cz::ChunkBuffer& deserialize(const cz::ChunkBuffer& stream, Container& v)
		{
			int size;
			stream >> size;
			v.clear();
			v.resize(size);
			stream.read(&v[0], sizeof(v[0])*size);
			return stream;
		}
	};

	//
	// Tuple serialization
	// 
	template<typename Tuple, bool Done, int N>
	struct TupleSerialization
	{
		static ChunkBuffer& serialize(ChunkBuffer& stream, const Tuple& v)
		{
			stream << std::get<N>(v);
			return TupleSerialization<Tuple, N == std::tuple_size<Tuple>::value - 1, N + 1>::serialize(stream, v);
		}

		static const ChunkBuffer& deserialize(const ChunkBuffer& stream, Tuple& v)
		{
			stream >> std::get<N>(v);
			return TupleSerialization<Tuple, N == std::tuple_size<Tuple>::value - 1, N + 1>::deserialize(stream, v);
		}
	};

	template<typename Tuple, int N>
	struct TupleSerialization<Tuple, true, N>
	{
		static ChunkBuffer& serialize(ChunkBuffer& stream, const Tuple& v)
		{
			return stream;
		}
		static const ChunkBuffer& deserialize(const ChunkBuffer& stream, Tuple& v)
		{
			return stream;
		}
	};

	namespace ParameterPack
	{
		void serialize(ChunkBuffer& stream);
		template<typename First, typename... Rest>
		void serialize(ChunkBuffer& stream, const First& first, const Rest&... rest)
		{
			stream << first;
			serialize(stream, rest...);
		}

		void deserialize(const ChunkBuffer& stream);
		template<typename First, typename... Rest>
		void deserialize(const ChunkBuffer& stream, First& first, Rest&... rest)
		{
			stream >> first;
			deserialize(stream, rest...);
		}
	}

}

template<typename... Args>
ChunkBuffer& serializeParameterPack(ChunkBuffer& stream, const Args&... args)
{
	details::ParameterPack::serialize(stream, args...);
	return stream;
}

template<typename... Args>
const ChunkBuffer& deserializeParameterPack(const ChunkBuffer& stream, Args&... args)
{
	details::ParameterPack::deserialize(stream, args...);
	return stream;
}

//
// Write operators
//
template<typename T>
inline ChunkBuffer& operator << (ChunkBuffer& stream, T v)
{
	stream.write(v); return stream;
}
ChunkBuffer& operator << (ChunkBuffer& stream, const std::string& v);
ChunkBuffer& operator << (ChunkBuffer& stream, const char* v);
ChunkBuffer& operator << (ChunkBuffer& stream, const cz::Any &v);
template<typename T>
ChunkBuffer& operator << (ChunkBuffer& stream, const std::vector<T>& v)
{
	return details::VectorSerialization<T>::serialize(stream, v);
}
template<typename A, typename B>
ChunkBuffer& operator << (ChunkBuffer& stream, const std::pair<A,B>& v)
{
	stream << v.first;
	stream << v.second;
	return stream;
}

template<typename... Elements>
ChunkBuffer& operator << (ChunkBuffer& stream, const std::tuple<Elements...>& v)
{
	typedef std::remove_reference<decltype(v)>::type TupleType;
	return details::TupleSerialization<std::decay<decltype(v)>::type, std::tuple_size<TupleType>::value==0, 0>::serialize(stream, v);
}

//
// Read operations
//

// 
template<typename T>
inline const ChunkBuffer& operator >> (const ChunkBuffer& stream, T& v) {
	stream.read(v); return stream;
}
const ChunkBuffer& operator >> (const ChunkBuffer& stream, std::string& v);
const ChunkBuffer& operator >> (const ChunkBuffer& stream, cz::Any &v);

template<typename T>
const ChunkBuffer& operator >> (const ChunkBuffer& stream, std::vector<T>& v)
{
	return details::VectorSerialization<T>::deserialize(stream, v);
}
template<typename A, typename B>
const ChunkBuffer& operator >> (const ChunkBuffer& stream, std::pair<A,B>& v)
{
	stream >> v.first;
	stream >> v.second;
	return stream;
}
template<typename... Elements>
const ChunkBuffer& operator >> (const ChunkBuffer& stream, std::tuple<Elements...>& v)
{
	typedef std::remove_reference<decltype(v)>::type TupleType;
	return details::TupleSerialization<std::decay<decltype(v)>::type, std::tuple_size<TupleType>::value==0 , 0>::deserialize(stream, v);
}

} // namespace cz


