/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com

	purpose:

*********************************************************************/

#pragma once

#include "crazygaze/muc/czmuc.h"
#include <string>
#include <bitset>
#include "crazygaze/muc/ChunkBuffer.h"

namespace cz
{

	/*! \addtogroup General
		@{
	*/

	/*
		Creates UUID/GUID (Universally unique identifier)
	*/
	class Guid
	{
	public:
		Guid() : a(0), b(0), c(0), d(0)
		{
		}
		Guid(uint32_t _a, uint32_t _b, uint32_t _c, uint32_t _d) : a(_a), b(_b), c(_c), d(_d)
		{
		}

		explicit Guid(const char* str);

		static Guid create();

		bool operator==(const Guid& other) const
		{
			return (a == other.a && b == other.b && c == other.c && d == other.d);
		}

		bool operator!=(const Guid& other) const
		{
			return !(operator==(other));
		}

		bool isValid() const
		{
			return (a | b | c | d) != 0;
		}

		void invalidate()
		{
			a = b = c = d = 0;
		}

		std::string toString() const;
		bool fromString(const std::string& str);

		// So it can be used as a key in maps
		bool operator<(const Guid& other) const;

		uint32_t a, b, c, d;
	};

	const ChunkBuffer& operator >> (const ChunkBuffer& stream, cz::Guid& v);
	ChunkBuffer& operator << (ChunkBuffer& stream, const cz::Guid& v);

	std::string to_json(const Guid& v);

	/*!
		@}
	*/

} // namespace cz


// Define std::hash<Guid>, so it can be used as a key for std::unordered_map
namespace std
{
	template<>
	struct hash<cz::Guid>
	{
		std::size_t operator()(const cz::Guid& guid) const
		{
			using guidbitset = std::bitset<128>;
			static_assert(sizeof(guidbitset) == sizeof(cz::Guid), "bitset doesn't match Guid size");
			const guidbitset& bits = *reinterpret_cast<const guidbitset*>(&guid);
			return std::hash<guidbitset>()(bits);
		}
	};
}
