// Based on https://gist.github.com/filsinger/1255697 , with some changes to work with VS 2015 CTP 6
#pragma once

#include <cstdint>

//
// Compile time hashing
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

//
// Runtime only hashing
// From http://www.isthe.com/chongo/src/fnv/hash_64a.c and http://www.isthe.com/chongo/src/fnv/hash_32a.c
struct Hash
{
	static constexpr uint32_t FNV1_32A_INIT = 0x811c9dc5;
	static constexpr uint32_t FNV_32_PRIME = 0x01000193;

	/*
	 * fnv_32a_buf - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a buffer
	 *
	 * input:
	 *	buf	- start of buffer to hash
	 *	len	- length of buffer in octets
	 *	hval	- previous hash value or 0 if first call
	 *
	 * returns:
	 *	32 bit hash as a static hash type
	 *
	 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
	 * 	 hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
	 */

	static uint32_t fnv_32a_buf(void* buf, size_t len, uint32_t hval = FNV1_32A_INIT)
	{
		const unsigned char *bp = (const unsigned char *)buf; /* start of buffer */
		const unsigned char *be = bp + len;             /* beyond end of buffer */

		/*
		 * FNV-1a hash each octet in the buffer
		 */
		while (bp < be)
		{
			/* xor the bottom with the current octet */
			hval ^= (uint32_t)*bp++;

			/* multiply by the 32 bit FNV magic prime mod 2^32 */
			hval *= FNV_32_PRIME;
		}

		/* return our new hash value */
		return hval;
	}

	/*
	 * fnv_32a_str - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a string
	 *
	 * input:
	 *	str	- string to hash
	 *	hval	- previous hash value or 0 if first call
	 *
	 * returns:
	 *	32 bit hash as a static hash type
	 *
	 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
	 *  	 hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
	 */
	static uint32_t fnv_32a_str(const char *str, uint32_t hval = FNV1_32A_INIT)
	{
		const unsigned char *s = (const unsigned char *)str;	/* unsigned string */

		/*
		 * FNV-1a hash each octet in the buffer
		 */
		while (*s)
		{
			/* xor the bottom with the current octet */
			hval ^= (uint32_t)*s++;

			/* multiply by the 32 bit FNV magic prime mod 2^32 */
			hval *= FNV_32_PRIME;
		}

		/* return our new hash value */
		return hval;
	}

	static constexpr uint64_t FNV1A_64_INIT = (uint64_t)0xcbf29ce484222325ULL;
	static constexpr uint64_t FNV_64_PRIME = (uint64_t)0x100000001b3ULL;

	/*
	 * fnv_64a_buf - perform a 64 bit Fowler/Noll/Vo FNV-1a hash on a buffer
	 *
	 * input:
	 *	buf	- start of buffer to hash
	 *	len	- length of buffer in octets
	 *	hval	- previous hash value or 0 if first call
	 *
	 * returns:
	 *	64 bit hash as a static hash type
	 *
	 * NOTE: To use the recommended 64 bit FNV-1a hash, use FNV1A_64_INIT as the
	 * 	 hval arg on the first call to either fnv_64a_buf() or fnv_64a_str().
	 */
	static uint64_t fnv_64a_buf(void *buf, size_t len, uint64_t hval = FNV1A_64_INIT)
	{
		const unsigned char *bp = (const unsigned char *)buf; /* start of buffer */
		const unsigned char *be = bp + len;             /* beyond end of buffer */

		/*
		 * FNV-1a hash each octet of the buffer
		 */
		while (bp < be)
		{
			/* xor the bottom with the current octet */
			hval ^= (uint64_t)*bp++;

			/* multiply by the 64 bit FNV magic prime mod 2^64 */
			hval *= FNV_64_PRIME;
		}

		/* return our new hash value */
		return hval;
	}

	/*
	 * fnv_64a_str - perform a 64 bit Fowler/Noll/Vo FNV-1a hash on a buffer
	 *
	 * input:
	 *	buf	- start of buffer to hash
	 *	hval	- previous hash value or 0 if first call
	 *
	 * returns:
	 *	64 bit hash as a static hash type
	 *
	 * NOTE: To use the recommended 64 bit FNV-1a hash, use FNV1A_64_INIT as the
	 * 	 hval arg on the first call to either fnv_64a_buf() or fnv_64a_str().
	 */
	static uint64_t fnv_64a_str(const char *str, uint64_t hval = FNV1A_64_INIT)
	{
		const unsigned char *s = (const unsigned char *)str; /* unsigned string */

		/*
		 * FNV-1a hash each octet of the string
		 */
		while (*s)
		{
			/* xor the bottom with the current octet */
			hval ^= (uint64_t)*s++;

			/* multiply by the 64 bit FNV magic prime mod 2^64 */
			hval *= FNV_64_PRIME;
		}

		/* return our new hash value */
		return hval;
	}

};

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
