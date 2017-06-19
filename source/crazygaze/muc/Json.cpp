#include "czlibPCH.h"
#include "crazygaze/Json.h"
#include "crazygaze/StringUtils.h"

namespace cz
{
	
std::string to_json(const char* val)
{
	return formatString("\"%s\"", val);
}

std::string to_json(std::string val)
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
}

