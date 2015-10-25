/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/Any.h"
#include "crazygaze/StringUtils.h"

namespace cz
{

Any::Any()
{
	static_assert( sizeof(m_integer)==sizeof(m_float), "Check the union" );
	m_type = kNone;
}

Any::Any(Any&& other) :
	m_type( std::move(other.m_type) ),
	m_integer( std::move( other.m_integer) ),
	m_string( std::move(other.m_string) ),
	m_blob( std::move(other.m_blob) )
{
}

Any::Any(bool v) : m_type(kBool), m_integer(v ? 1 : 0)
{
}
Any::Any(int v) : m_type(kInteger), m_integer(v)
{
}
Any::Any(unsigned int v) : m_type(kUnsignedInteger), m_uinteger(v)
{
}
Any::Any(float v) : m_type(kFloat), m_float(v)
{
}
Any::Any(const char* v) : m_type(kString), m_string(v)
{
}
Any::Any(std::string v) : m_type(kString), m_string(std::move(v))
{
}

Any::Any(std::vector<uint8_t> blob)
	: m_type(kBlob), m_blob(std::move(blob))
{
}


bool Any::asBool(bool & v) const
{
	if (m_type==kBool || m_type==kInteger || m_type==kUnsignedInteger)
	{
		v = m_integer!=0;
		return true;
	}
	else
	{
		return false;
	}
}

bool Any::asInteger(int& v) const
{
	if (m_type==kInteger)
	{
		v = m_integer;
		return true;
	}
	else if (m_type==kFloat)
	{
		v = static_cast<int>(m_float);
		return true;
	}
	else
	{
		return false;
	}
}

bool Any::asUnsignedInteger(unsigned int& v) const
{
	if (m_type==kUnsignedInteger)
	{
		v = m_integer;
		return true;
	}
	else if (m_type==kFloat)
	{
		v = static_cast<unsigned int>(m_float);
		return true;
	}
	else
	{
		return false;
	}
}

bool Any::asString(std::string& v) const
{
	if (m_type==kString)
	{
		v = m_string;
		return true;
	}
	else
	{
		return false;
	}
}
bool Any::asFloat(float& v) const
{
	if (m_type==kFloat)
	{
		v = m_float;
		return true;
	}
	else if (m_type==kInteger)
	{
		v = static_cast<float>(m_integer);
		return true;
	}
	else if (m_type==kUnsignedInteger)
	{
		v = static_cast<float>(m_uinteger);
		return true;
	}
	else
	{
		return false;
	}
}

bool Any::asBlob(std::vector<uint8_t>& v) const
{
	if (m_type==kBlob)
	{
		v = m_blob;
		return true;
	}
	else
	{
		return false;
	}
}


const char* Any::convertToString() const
{
	switch(m_type)
	{
		case kNone:
			return "";
		case kBool:
			return m_integer ? "true" : "false";
		case kInteger:
			return cz::formatString("%d", m_integer);
		case kUnsignedInteger:
			return cz::formatString("%u", m_integer);
		case kFloat:
			return cz::formatString("%.4f", m_float);
		case kString:
			return m_string.c_str();
		case kBlob:
			return "BINARY BLOB";
		default:
			CZ_UNEXPECTED();
			return "";
	}
}

void Any::set(const Any& other)
{
	m_type = other.m_type;
	m_integer = other.m_integer;
	m_string = other.m_string;
	m_blob = other.m_blob;
}
void Any::set(Any&& other)
{
	m_type = std::move(other.m_type);
	m_integer = std::move(other.m_integer);
	m_string = std::move(other.m_string);
	m_blob = std::move(other.m_blob);
}

Any& Any::operator=(const Any& other)
{
	set(other);
	return *this;
}

Any& Any::operator=(Any&& other)
{
	set(std::move(other));
	return *this;
}


} // namespace cz



