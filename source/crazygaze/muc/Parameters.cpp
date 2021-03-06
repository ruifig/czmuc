/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czmucPCH.h"
#include "crazygaze/muc/Parameters.h"
#include "crazygaze/muc/StringUtils.h"

namespace cz
{

namespace
{
	bool isEqual(const UTF8String& a, const UTF8String& b, bool caseSensitive)
	{
		if (caseSensitive)
			return a==b ? true : false;
		else
			return ci_equals(a.toUtf32(),b.toUtf32());
	}
}

cz::UTF8String Parameters::ms_empty;
Parameters::Parameters()
{
}

void Parameters::set(int argc, char* argv[])
{
	if (argc<=1)
		return;
	for (int i=1; i<argc; i++)
	{
		const char *arg = argv[i];
		if (*arg == '-')
			arg++;

		const char *seperator = strchr(arg, '=');
		if (seperator==nullptr)
		{
			m_args.emplace_back(arg, "");
		}
		else
		{
			cz::UTF8String name(arg, seperator);
			cz::UTF8String value(++seperator);
			m_args.emplace_back(std::move(name), std::move(value));
		}
	}
}

const Parameters::Param* Parameters::begin() const
{
	if (m_args.size())
		return &m_args[0];
	else
		return nullptr;
}

const Parameters::Param* Parameters::end() const
{
	return begin() + m_args.size();
}

bool Parameters::has( const char* name, bool caseSensitive) const
{
	for(auto &i: m_args)
	{
		if (isEqual(i.name, name, caseSensitive))
		{
			return true;
		}
	}
	return false;
}

bool Parameters::has( const cz::UTF8String& name, bool caseSensitive ) const
{
	return has(name.c_str(), caseSensitive);
}

const cz::UTF8String& Parameters::get( const char *name, bool caseSensitive) const
{
	for (auto &i: m_args)
	{
		if (isEqual(i.name, name, caseSensitive))
			return i.value;
	}
	return ms_empty;
}

const std::vector<UTF8String> Parameters::getMultiple(const char* name, bool caseSensitive) const
{
	std::vector<UTF8String> res;
	for (auto &i: m_args)
	{
		if (isEqual(i.name, name, caseSensitive))
			res.push_back(i.value);
	}
	return res;
}

} // namespace cz



