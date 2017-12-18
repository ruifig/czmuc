#include "czmucPCH.h"
#include "crazygaze/muc/Json.h"
#include "crazygaze/muc/StringUtils.h"

namespace cz
{
	
std::string to_json(const char* val)
{
	return cz::formatString("\"%s\"", val);
}

std::string to_json(const std::string& val)
{
	return to_json(val.c_str());
}

std::string to_json(int val)
{
	return std::to_string(val);
}

std::string to_json(long val)
{
	return std::to_string(val);
}

std::string to_json(long long val)
{
	return std::to_string(val);
}

std::string to_json(unsigned val)
{
	return std::to_string(val);
}

std::string to_json(unsigned long val)
{
	return std::to_string(val);
}

std::string to_json(unsigned long long val)
{
	return std::to_string(val);
}

std::string to_json(float val)
{
	return std::to_string(val);
}

std::string to_json(double val)
{
	return std::to_string(val);
}

std::string to_json(long double val)
{
	return std::to_string(val);
}

} // namespace cz

