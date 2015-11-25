/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/Guid.h"

namespace cz
{

	Guid::Guid(const char* str)
	{
		a=0; b=0; c=0; d=0;
		fromString(str);
	}

	Guid Guid::create()
	{
		static_assert(sizeof(GUID)==sizeof(Guid), "Windows GUID and Guid sizes don't match");
		Guid guid(0,0,0,0);
		CZ_CHECK(CoCreateGuid((GUID*)&guid) == S_OK );
		return guid;
	}

	std::string Guid::toString() const
	{
		char buf[100];
		_snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", a,b,c,d);
		return std::string(buf);
	}

	bool Guid::fromString(const std::string& str)
	{
		int _a, _b, _c, _d;
		int ret = sscanf(str.c_str(), "%08X%08X%08X%08X", &_a, &_b, &_c, &_d);
		if (ret!=4)
			return false;
		a = _a;
		b = _b;
		c = _c;
		d = _d;
		return true;
	}

	bool Guid::operator<(const Guid& other) const
	{
		if (a<other.a)
			return true;
		if (other.a < a)
			return false;

		if (b<other.b)
			return true;
		if (other.b < b)
			return false;

		if (c<other.c)
			return true;
		if (other.c < c)
			return false;

		if (d<other.d)
			return true;

		return false;
	}

	const ChunkBuffer& operator >> (const ChunkBuffer& stream, cz::Guid& v)
	{
        stream >> v.a;
        stream >> v.b;
        stream >> v.c;
        stream >> v.d;
        return stream;
	}

	ChunkBuffer& operator << (ChunkBuffer& stream, const cz::Guid& v)
	{
        stream << v.a;
        stream << v.b;
        stream << v.c;
        stream << v.d;
        return stream;
	}

} // namespace cz
