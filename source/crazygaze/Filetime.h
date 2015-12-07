/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Encapsulates a file time, and provides some basic operations
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/UTF8String.h"

#if CZ_PLATFORM==CZ_PLATFORM_WIN32
	#include <windows.h>
#endif

namespace cz
{

class Filetime
{
public:
	Filetime();
#if CZ_PLATFORM==CZ_PLATFORM_WIN32
	Filetime(FILETIME time);
#endif

	bool isValid() const;

	bool operator==(const Filetime& other) const;
	bool operator<(const Filetime& other) const;
	bool operator<=(const Filetime& other) const;
	bool operator>(const Filetime& other) const;
	bool operator>=(const Filetime& other) const;
	bool operator!=(const Filetime& other) const;

	bool youngerThan(const Filetime& other) const;
	bool olderThan(const Filetime& other) const;


	enum TimeType
	{
		kCreated,
		kModified
	};

	static Filetime get(const UTF8String& filename, TimeType type);

public:
#if CZ_PLATFORM==CZ_PLATFORM_WIN32
	FILETIME m_time;
#endif
};


} // namespace cz

