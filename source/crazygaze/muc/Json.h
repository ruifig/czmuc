/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
*********************************************************************/
#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

	template< typename T >
	std::enable_if_t<std::is_arithmetic_v<T>, std::string> to_json(T val)
	{
		return std::to_string(val);
	}

	inline std::string to_json(bool val)
	{
		return std::string(val ? "true" : "false");
	}

	std::string to_json(const char* val);

	inline std::string to_json(const std::string& val)
	{
		return to_json(val.c_str());
	}

	// std::vector of any T that can be converted to json
	template<typename T>
	std::string to_json(const std::vector<T>& val)
	{
		if (val.size() == 0)
			return "[]";

		std::string res;
		for (auto&& v : val)
			res += "," + to_json(v);
		res += "]";
		res[0] = '[';

		return res;
	}

	// std::pair of any FIRST,SECOND that can be converted to json
	// #RVF : Not sure this is appropriate.
	// The user might not want fields named 'first' and 'second'
	template<typename FIRST, typename SECOND>
	std::string to_json(const std::pair<FIRST,SECOND>& val)
	{
		std::string res = "{\"first\":" + to_json(val.first);
		res += ", \"second\":" + to_json(val.second) + "}";
		return res;
	}

} // namespace cz
