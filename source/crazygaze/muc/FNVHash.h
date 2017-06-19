// Based on https://gist.github.com/filsinger/1255697 , with some changes to work with VS 2015 CTP 6
#pragma once

#include <cstdint>

// C++11 32bit FNV-1 and FNV-1a string hashing (Fowler–Noll–Vo hash)
//
// Requires a compiler with C++11 support
// See main(...) for examples
 
namespace hash
{
	template <typename S> struct fnv_internal;
	template <typename S> struct fnv1;
	template <typename S> struct fnv1a;
 
	template <> struct fnv_internal<uint32_t>
	{
		constexpr static uint32_t default_offset_basis = 0x811C9DC5;
		constexpr static uint32_t prime				   = 0x01000193;
	};
 
	template <> struct fnv1<uint32_t> : public fnv_internal<uint32_t>
	{
		constexpr static inline uint32_t hash(char const*const aString, const uint32_t val = default_offset_basis)
		{
			return (aString[0] == '\0') ? val : hash( aString + 1, ((uint64_t)val * prime ) ^ uint32_t(aString[0]) );
		}
 
		constexpr static inline uint32_t hash(char const*const aString, const size_t aStrlen, const uint32_t val)
		{
			return (aStrlen == 0) ? val : hash( aString + 1, aStrlen - 1, ( (uint64_t)val * prime ) ^ uint32_t(aString[0]) );
		}
	};
 
	template <> struct fnv1a<uint32_t> : public fnv_internal<uint32_t>
	{
		constexpr static inline uint32_t hash(char const*const aString, const uint32_t val = default_offset_basis)
		{
			return (aString[0] == '\0') ? val : hash( aString + 1, ( (uint64_t)val ^ uint32_t(aString[0]) ) * prime);
		}
 
		constexpr static inline uint32_t hash(char const*const aString, const size_t aStrlen, const uint32_t val)
		{
			return (aStrlen == 0) ? val : hash( aString + 1, aStrlen - 1, ( (uint64_t)val ^ uint32_t(aString[0]) ) * prime);
		}
	};
} // namespace hash
 
 
inline constexpr uint32_t operator "" _fnv1 (const char* aString, const size_t aStrlen)
{
	typedef hash::fnv1<uint32_t> hash_type;
	return hash_type::hash(aString, aStrlen, hash_type::default_offset_basis);
}
 
inline constexpr uint32_t operator "" _fnv1a (const char* aString, const size_t aStrlen)
{
	typedef hash::fnv1a<uint32_t> hash_type;
	return hash_type::hash(aString, aStrlen, hash_type::default_offset_basis);
}
 


/*
#include <iostream>
#include <cassert>

int main(int argc, char* argv[])
{
	using namespace hash;
	static_assert( fnv1<uint32_t>::hash("FNV Hash Test")  == 0xAE2253F1, "fnv1_32::hash failure" );
	static_assert( fnv1<uint32_t>::hash("FNV Hash Test") == "FNV Hash Test"_fnv1, "fnv1_32::hash failure" );
	static_assert( fnv1a<uint32_t>::hash("FNV Hash Test") == 0xF38B3DB9, "fnv1a_32::hash failure" );
	static_assert( fnv1a<uint32_t>::hash("FNV Hash Test") == "FNV Hash Test"_fnv1a, "fnv1a_32::hash failure" );
 
	assert( fnv1<uint32_t>::hash("FNV Hash Test")  == 0xAE2253F1 );
	assert( fnv1a<uint32_t>::hash("FNV Hash Test") == 0xF38B3DB9 );
	assert( fnv1<uint32_t>::hash("FNV Hash Test")  == "FNV Hash Test"_fnv1 );
	assert( fnv1a<uint32_t>::hash("FNV Hash Test") == "FNV Hash Test"_fnv1a );
 
 
	for(int ii=1;ii<argc;ii++)
	{
		const uint32_t inputNumber = atoi(argv[ii]);
		switch(inputNumber)
		{
		case fnv1<uint32_t>::hash("FNV Hash Test"):
			std::cout << "Hey, you input the FNV-1 hash of the static_assert test case!" << std::endl;
			break;
		case fnv1a<uint32_t>::hash("FNV Hash Test"):
			std::cout << "Hey, you input the FNV-1a hash of the static_assert test case!" << std::endl;
			break;
		}
 
		std::cout	<< " FVN-1	(\"" << argv[ii] << "\") = " << fnv1<uint32_t>::hash(argv[ii]) << std::endl
					<< " FVN-1a (\"" << argv[ii] << "\") = " << fnv1a<uint32_t>::hash(argv[ii]) << std::endl;
	}
 
	return EXIT_SUCCESS;
}

*/
