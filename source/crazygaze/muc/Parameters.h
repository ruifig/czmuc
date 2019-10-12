/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czmuc.h"
#include "crazygaze/muc/UTF8String.h"
#include <vector>

namespace cz
{

class Parameters
{
public:
	struct Param
	{
		template<class T1, class T2>
		Param(T1&& name_, T2&& value_) : name(std::forward<T1>(name_)), value(std::forward<T2>(value_)){}
		cz::UTF8String name;
		cz::UTF8String value;
	};
	Parameters();
	void set(int argc, char* argv[]);
	const Param* begin() const;
	const Param* end() const;
	bool has(const char* name, bool caseSensitive = false) const;
	bool has(const UTF8String& name, bool caseSensitive = false) const;
	const cz::UTF8String& get(const char* name, bool caseSensitive = false) const;
	void clear()
	{
		m_args.clear();
	}
private:
	static cz::UTF8String ms_empty;
	std::vector<Param> m_args;
};

} // namespace cz

