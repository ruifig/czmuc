/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
*********************************************************************/
#pragma once

#include "crazygaze/czlib.h"

namespace cz
{

std::string to_json(const char* val);
std::string to_json(std::string val);
std::string to_json(int val);
std::string to_json(long val);
std::string to_json(long long val);
std::string to_json(unsigned val);
std::string to_json(unsigned long val);
std::string to_json(unsigned long long val);
std::string to_json(float val);
std::string to_json(double val);
std::string to_json(long double val);

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

template<typename FIRST, typename SECOND>
std::string to_json(const std::pair<FIRST,SECOND>& val)
{
	std::string res = "{\"first\":" + to_json(val.first);
	res += ", \"second\":" + to_json(val.second) + "}";
	return res;
}

}



