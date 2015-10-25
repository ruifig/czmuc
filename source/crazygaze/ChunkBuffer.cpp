/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/ChunkBuffer.h"

namespace cz
{

struct BlockReadInfo
{
	char* dst;
	unsigned size; // bytes left to read
};

struct BlockWriteInfo
{
	const char* src;
	unsigned size; // bytes left to write
};

struct BlockReserveWriteInfo
{
	unsigned size; // bytes left to write
};

//////////////////////////////////////////////////////////////////////////
//
//	ChunkBuffer::Block
//
//////////////////////////////////////////////////////////////////////////

ChunkBuffer::Block::Block(std::unique_ptr<char[]> ptr, unsigned capacity, unsigned usedSize)
	: m_ptr(std::move(ptr))
	, m_capacity(capacity)
	, m_readPos(0)
	, m_writePos(usedSize)
{
}

ChunkBuffer::Block::Block(Block&& other)
	: m_ptr(std::move(other.m_ptr))
	, m_capacity(other.m_capacity)
	, m_readPos(other.m_readPos)
	, m_writePos(other.m_writePos)
{
	other.m_capacity = 0;
	other.m_readPos = 0;
	other.m_writePos = 0;
}

unsigned ChunkBuffer::Block::size() const
{
	return m_writePos - m_readPos;
}

unsigned ChunkBuffer::Block::unused() const
{
	return m_capacity - m_writePos;
}

unsigned ChunkBuffer::Block::peek(BlockReadInfo& info) const
{
	auto portion = std::min(size(), info.size);
	memcpy(info.dst, m_ptr.get() + m_readPos, portion);
	info.dst += portion;
	info.size -= portion;
	return portion;
}

void ChunkBuffer::Block::read(BlockReadInfo& info) const
{
	m_readPos += peek(info);
}

void ChunkBuffer::Block::write(BlockWriteInfo& info)
{
	auto portion = std::min(unused(), info.size);
	memcpy(m_ptr.get() + m_writePos, info.src, portion);
	info.src += portion;
	info.size -= portion;
	m_writePos += portion;
}

void ChunkBuffer::Block::reserveWrite(BlockReserveWriteInfo& info)
{
	auto portion = std::min(unused(), info.size);
	info.size -= portion;
	m_writePos += portion;
}

void ChunkBuffer::Block::writeAt(unsigned pos, BlockWriteInfo& info)
{
	auto portion = std::min(m_writePos - pos, info.size);
	memcpy(m_ptr.get() + pos, info.src, portion);
	info.src += portion;
	info.size -= portion;
}

const char* ChunkBuffer::Block::getReadPtr() const
{
	return m_ptr.get() + m_readPos;
}

//////////////////////////////////////////////////////////////////////////
//
//	ChunkBuffer
//
//////////////////////////////////////////////////////////////////////////

ChunkBuffer::ChunkBuffer(unsigned initialCapacity, unsigned defaultBlockSize)
	: m_defaultBlockSize(defaultBlockSize)
{
	if (initialCapacity)
		m_blocks.emplace(std::unique_ptr<char[]>(new char[initialCapacity]), initialCapacity, 0);
}

unsigned ChunkBuffer::calcSize() const
{
	unsigned size = 0;
	for (auto&& i : m_blocks.container())
		size += i.size();
	return size;
}

unsigned ChunkBuffer::numBlocks() const
{
	return static_cast<unsigned>(m_blocks.size());
}

void ChunkBuffer::iterateBlocks(std::function<void(const char*, unsigned)> f)
{
	for (auto&& i : m_blocks.container())
		f(i.getReadPtr(), i.size());
}

void ChunkBuffer::writeBlock(std::unique_ptr<char[]> data, unsigned capacity, unsigned size)
{
	CZ_ASSERT(capacity && size <= capacity);
	m_blocks.emplace(std::move(data), capacity, size);
}

void ChunkBuffer::write(const void* data, unsigned size)
{
	BlockWriteInfo info;
	info.src = reinterpret_cast<const char*>(data);
	info.size = size;
	while (info.size)
	{
		// Add another block if necessary
		if (m_blocks.size()==0 || m_blocks.back().unused() == 0)
			writeBlock(std::unique_ptr<char[]>(new char[m_defaultBlockSize]), m_defaultBlockSize, 0);
		m_blocks.back().write(info);
	}
}

void ChunkBuffer::read(void* data, unsigned size) const
{
#if CZ_DEBUG
	m_dbgReadCounter++;
#endif
	BlockReadInfo info;
	info.dst = reinterpret_cast<char*>(data);
	info.size = size;
	while (info.size)
	{
		if (m_blocks.size() == 0) // no more blocks
			throw std::runtime_error("No more data left to read.");
		m_blocks.front().read(info);
		// Drop the block if no more data to read
		if (m_blocks.front().size() == 0)
			m_blocks.pop();
	}
}

void ChunkBuffer::writeAt(WritePos pos, const void* data, unsigned size)
{
#if CZ_DEBUG
	// Make sure no reads were made since this write position was created
	CZ_ASSERT(pos.dbgReadCounter == m_dbgReadCounter);
#endif

	BlockWriteInfo info;
	info.src = reinterpret_cast<const char*>(data);
	info.size = size;
	auto it = m_blocks.container().begin() + pos.block;
	auto writePos = pos.write;
	while (info.size)
	{
		CZ_ASSERT(it != m_blocks.container().end());
		it->writeAt(writePos, info);
		writePos = 0;
		it++;
	}
}

// #TODO This function crashes, if there are no blocks. Fix it, and write a unit test to make sure it doesn't happen again
cz::ChunkBuffer::WritePos ChunkBuffer::writeReserve(unsigned size)
{
	WritePos ret;
	if (m_blocks.container().size()==0)
		writeBlock(std::unique_ptr<char[]>(new char[m_defaultBlockSize]), m_defaultBlockSize, 0);
	ret.block = static_cast<unsigned>(m_blocks.container().size()) - 1;
	ret.write = m_blocks.back().size();
#if CZ_DEBUG
	ret.dbgReadCounter = m_dbgReadCounter;
#endif

	BlockReserveWriteInfo info;
	info.size = size;
	while (info.size)
	{
		// Add another block if necessary
		if (m_blocks.back().unused() == 0)
			writeBlock(std::unique_ptr<char[]>(new char[m_defaultBlockSize]), m_defaultBlockSize, 0);
		m_blocks.back().reserveWrite(info);
	}

	return ret;
}

void ChunkBuffer::peek(void* data, unsigned size) const
{
	BlockReadInfo info;
	info.dst = reinterpret_cast<char*>(data);
	info.size = size;
	auto it = m_blocks.container().begin();
	while (info.size)
	{
		if (it == m_blocks.container().end() || it->size() == 0)
			throw std::runtime_error("No more data left to read.");
		it->peek(info);
		it++;
	}
}

//////////////////////////////////////////////////////////////////////////
//
// Write operators
//
//////////////////////////////////////////////////////////////////////////
ChunkBuffer& operator<<(ChunkBuffer& stream, const std::string& v)
{
	stream.write(static_cast<unsigned>(v.size()));
	stream.write(v.data(), static_cast<unsigned>(v.size()));
	return stream;
}

ChunkBuffer& operator << (ChunkBuffer& stream, const char* v)
{
	unsigned len = static_cast<unsigned>(strlen(v));
	stream.write(len);
	stream.write(v, len);
	return stream;
}

ChunkBuffer& operator << (ChunkBuffer& stream, const cz::Any &v)
{
	return v.saveToStream(stream);
}

//////////////////////////////////////////////////////////////////////////
//
// Read operators
//
//////////////////////////////////////////////////////////////////////////

const ChunkBuffer& operator >> (const ChunkBuffer& stream, std::string& v)
{
	unsigned size;
	stream >> size;
	v.clear();
	v.reserve(size);
	v.append(size, 0);
	stream.read(&v[0], size);
	return stream;
}

const ChunkBuffer& operator >> (const ChunkBuffer& stream, cz::Any &v)
{
	return v.readFromStream(stream);
}

void details::ParameterPack::serialize(ChunkBuffer& stream)
{
}

void details::ParameterPack::deserialize(const ChunkBuffer& stream)
{
}

} // namespace cz




