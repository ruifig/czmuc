#include "czmucPCH.h"
#include "crazygaze/muc/Json.h"
#include "crazygaze/muc/StringUtils.h"

namespace cz
{
	
std::string to_json(const char* val)
{
	std::string res = "\"";
	while (*val)
	{
		switch (*val)
		{
		case '\b':
			res += "\\b";
			break;
		case '\f':
			res += "\\f";
			break;
		case '\n':
			res += "\\n";
			break;
		case '\r':
			res += "\\r";
			break;
		case '\t':
			res += "\\t";
			break;
		case '"':
			res += "\\\"";
			break;
		case '\\':
			res += "\\\\";
			break;
		default:
			res += *val;
		}
		val++;
	}
	return  res + "\"";
}

} // namespace cz

