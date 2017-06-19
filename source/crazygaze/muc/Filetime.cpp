/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Encapsulates a file time, and provides some basic operations
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/muc/Filetime.h"

namespace cz
{

Filetime::Filetime()
{
	memset(&m_time, 0, sizeof(m_time));
}

#if CZ_PLATFORM==CZ_PLATFORM_WIN32
Filetime::Filetime(FILETIME time) : m_time(time)
{
}
#endif

bool Filetime::isValid() const
{
	return !(m_time.dwHighDateTime==0 && m_time.dwLowDateTime==0);
}

bool Filetime::operator==(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)==0 ? true : false;
}

bool Filetime::operator<(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)<0 ? true : false;
}

bool Filetime::operator<=(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)<=0 ? true : false;
}

bool Filetime::operator>(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)>0 ? true : false;
}

bool Filetime::operator>=(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)>=0 ? true : false;
}

bool Filetime::operator!=(const Filetime& other) const
{
	return CompareFileTime(&m_time,&other.m_time)!=0 ? true : false;
}

bool Filetime::youngerThan(const Filetime& other) const
{
	if ( !(this->isValid() && other.isValid()) )
		return false;
	else
		return *this > other;
}

bool Filetime::olderThan(const Filetime& other) const
{
	if ( !(this->isValid() && other.isValid()) )
		return false;
	else
		return *this < other;
}

Filetime Filetime::get(const UTF8String& filename, Filetime::TimeType type)
{
	HANDLE hFile = CreateFileW(filename.widen().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	Filetime filetime;
	if (hFile == INVALID_HANDLE_VALUE)
		return Filetime();

	FILETIME ftCreate, ftAccess, ftWrite;

	// Retrieve the file times for the file.
	if (GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite))
	{
		if (type==kCreated)
			filetime = Filetime(ftCreate);
		else if (type==kModified)
			filetime = Filetime(ftWrite);
		else
		{
			CZ_UNEXPECTED();
		}
	}

	CloseHandle(hFile);
	return filetime;
}

} // namespace cz

