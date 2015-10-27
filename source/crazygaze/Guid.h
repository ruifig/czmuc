/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include <string>

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
			return (a==other.a && b==other.b && c==other.c && d==other.d);
		}

		bool operator!=(const Guid& other) const
		{
			return !(operator==(other));
		}

		bool isValid() const
		{
			return (a | b | c | d) !=0;
		}

		void invalidate()
		{
			a = b = c = d = 0;
		}

		std::string toString() const;		
		bool fromString(const std::string& str);

		// So it can be used as a key in maps
		bool operator<(const Guid& other) const;

		uint32_t a,b,c,d;
	};

	template<typename Stream>
	const Stream& operator >> (const Stream& stream, cz::Guid& v)
	{
        stream >> v.a;
        stream >> v.b;
        stream >> v.c;
        stream >> v.d;
        return stream;
	}
	template<typename Stream>
	Stream& operator << (Stream& stream, const cz::Guid& v)
	{
        stream << v.a;
        stream << v.b;
        stream << v.c;
        stream << v.d;
        return stream;
	}

	/*!
		@}
	*/

} // namespace cz
