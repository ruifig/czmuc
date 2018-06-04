#include "UnitTestsPCH.h"

using namespace cz;

SUITE(ChunkBuffer)
{

TEST(Simple)
{
	ChunkBuffer buf(0,2);

	CHECK(buf.numBlocks() == 0);
	buf.write((uint8_t)0xA1);
	CHECK(buf.calcSize() == 1);
	buf.write((uint16_t)0xB1B2);
	CHECK(buf.calcSize() == 1+2);
	buf.write((uint32_t)0xC1C2C3C4);
	CHECK(buf.calcSize() == 1+2+4);

	CHECK(buf.numBlocks() == 4);

	uint8_t c;
	uint16_t s;
	uint32_t i;
	buf.read(c);
	CHECK(c == 0xA1);
	CHECK(buf.calcSize() == 2 + 4);
	buf.read(s);
	CHECK(s == 0xB1B2);
	CHECK(buf.calcSize() == 4);
	buf.read(i);
	CHECK(i == 0xC1C2C3C4);
	CHECK(buf.calcSize() == 0);
}

TEST(WriterAndReader)
{
	ChunkBuffer buf(2, 2);

	buf << char(0xA1) << char(0xA2) << short(0xB1B2);
	CHECK(buf.numBlocks() == 2);
	uint8_t c1,c2;
	buf >> c1 >> c2;
	CHECK(c1 == 0xA1 && c2==0xA2);
	CHECK(buf.numBlocks() == 1);

	buf << int(0x11223344);
	CHECK(buf.numBlocks() == 3);
	uint16_t s;
	buf >> s;
	CHECK(buf.numBlocks() == 2);
	CHECK(s == 0xB1B2);

	int i;
	buf >> i;
	CHECK(i == 0x11223344); 
	CHECK(buf.numBlocks() == 0);
}


TEST(Move)
{
	ChunkBuffer buf(0, 2);
	buf << int(0x11223344);

	ChunkBuffer other(std::move(buf));
	CHECK(buf.calcSize() == 0);
	CHECK(buf.numBlocks() == 0);

	CHECK(other.calcSize() == 4);
	CHECK(other.numBlocks() == 2);

	int i;
	other >> i;
	CHECK(i == 0x11223344);
}


TEST(IterateBlock)
{
	ChunkBuffer buf(0, 2);
	for (char i = 0; i < 5; i++)
		buf << i;
	CHECK(buf.numBlocks() == 3);
	CHECK(buf.calcSize() == 5);

	char expected = 0;
	buf.iterateBlocks([&](const char* data, unsigned size)
	{
		while (size--)
		{
			CHECK(*data == expected);
			expected++;
			data++;
		}
	});

	CHECK(expected == 5);
}

TEST(WriteBlock)
{
	ChunkBuffer buf(0, 16);

	buf << short(0x1122);
	int capacity = 4;
	//auto data = std::unique_ptr<char[]>(new char[capacity]);
	auto data = std::shared_ptr<char[]>(new char[capacity]);
	for (char i = 0; i < capacity-1; i++)
		data[i] = i;

	buf.writeBlock(std::move(data), capacity, capacity - 1);

	CHECK(buf.numBlocks() == 2);
	CHECK(buf.calcSize() == 2 + (capacity - 1));

	// This should write to the block we just appended
	buf << char(capacity-1);
	CHECK(buf.numBlocks() == 2);
	CHECK(buf.calcSize() == 2 + capacity);

	short s;
	buf >> s;
	CHECK(s == 0x1122);
	CHECK(buf.numBlocks() == 1);

	for (char i = 0; i < capacity; i++)
	{
		char c;
		buf >> c;
		CHECK_EQUAL((int)i, (int)c);
	}
	CHECK(buf.calcSize() == 0);
}


TEST(Chunk)
{
	ChunkBuffer buf(0, 2);
	const char* str = "Hello World!";
	buf.write(str, static_cast<int>(strlen(str)));
	char c[32];
	buf.read(&c, static_cast<int>(strlen(str)));
	CHECK(strncmp(c, str, strlen(str)) == 0);
	CHECK(buf.calcSize() == 0);
	CHECK(buf.numBlocks() == 0);
}

TEST(ReserveWrite)
{
	ChunkBuffer buf(0, 2);
	buf << char(0x11);

	uint32_t i = 0x22334455;
	auto pos = buf.writeReserve(sizeof(i));
	buf << char(0x66);
	CHECK_EQUAL(3, buf.numBlocks());
	CHECK_EQUAL(6, buf.calcSize());
	buf.writeAt(pos, i);
	CHECK_EQUAL(3, buf.numBlocks());
	CHECK_EQUAL(6, buf.calcSize());

	// Read
	char c;
	buf >> c;
	CHECK(c == 0x11);
	i = 0;
	buf >> i;
	CHECK(i == 0x22334455);
	buf >> c; 
	CHECK(c == 0x66);

	CHECK_EQUAL(0, buf.numBlocks());
	CHECK_EQUAL(0, buf.calcSize());
}

TEST(VectorSerialization)
{
	ChunkBuffer buf(6, 6);
	std::vector<int> v1{ 1,2,3,4,5,6 };
	std::vector<int> v2{ 7,8,9,10};
	buf << v1 << v2;
	std::vector<int> v3;
	std::vector<int> v4;
	buf >> v3 >> v4;
	CHECK_ARRAY_EQUAL(v1, v3, (int)v1.size());
	CHECK_ARRAY_EQUAL(v2, v4, (int)v2.size());
	CHECK(buf.numBlocks() == 0);
}

TEST(StringSerialization)
{
	ChunkBuffer buf(1, 1);
	buf << "Hello" << "World!";
	std::string s1, s2;
	buf >> s1 >> s2;
	CHECK_EQUAL("Hello", s1);
	CHECK_EQUAL("World!", s2);
}

TEST(TupleSerialization)
{
	ChunkBuffer buf(3, 3);
	auto t1 = std::make_tuple(char(0x11), short(0x2233), int(0x44556677));
	buf << t1;
	CHECK_EQUAL(1 + 2 + 4, buf.calcSize());
	decltype(t1) t2;
	buf >> t2;
	CHECK(t1 == t2);
}

TEST(ParameterPackSerialization)
{
	ChunkBuffer buf(6, 6);
	serializeParameterPack(buf, char(0x11), short(0x2233), int(0x44556677));
	char a;
	short b;
	int c;
	deserializeParameterPack(buf, a, b, c);
	CHECK(a == 0x11);
	CHECK(b == 0x2233);
	CHECK(c == 0x44556677);
}

// Reserve with no initial blocks, and not fitting in a single block
TEST(ReserveWithNoBlocks_1)
{
	ChunkBuffer buf(0, 3);
	auto pos = buf.writeReserve(sizeof(int));
	buf << char(0x55);
	buf.writeAt(pos, int(0x11223344));
	char c;
	int i;
	buf >> i;
	buf >> c;
	CHECK(i == 0x11223344);
	CHECK(c == 0x55);
}


// Reserve, with the write position at the end of a block (as in: last block with no free space)
TEST(Reserve_2)
{
	ChunkBuffer buf(0, 2);
	buf << short(0x1122);
	auto pos = buf.writeReserve(sizeof(int));
	buf.writeAt(pos, int(0x33445566));
	short s;
	int i;
	buf >> s;
	buf >> i;
	CHECK(s == 0x1122);
	CHECK(i == 0x33445566);
}

}
