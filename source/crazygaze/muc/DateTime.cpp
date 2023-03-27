#include "czmucPCH.h"
#include "crazygaze/muc/DateTime.h"
#include "crazygaze/muc/Logging.h"

namespace cz
{


/**
 * Time span related constants.
 */
namespace detail
{
}

const int32_t DateTime::ms_daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
const int32_t DateTime::ms_daysToMonth[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

DateTime::DateTime(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second, int32_t millisecond)
{
	if (!validate(year, month, day, hour, minute, second, millisecond))
	{
		CZ_LOG(logDefault, Fatal, "Invalid date values. Y:%d, M:%d, D:%d, H:%d, M:%d, S:%d, Ms:%d",
			year, month, day, hour, minute, second, millisecond);
	}

	int32_t totalDays = 0;

	if ((month > 2) && isLeapYear(year))
	{
		++totalDays;
	}

	--year;											// the current year is not a full year yet
	--month;										// the current month is not a full month yet

	totalDays += year * 365;
	totalDays += year / 4;							// leap year day every four years...
	totalDays -= year / 100;						// ...except every 100 years...
	totalDays += year / 400;						// ...but also every 400 years
	totalDays += ms_daysToMonth[month];				// days in this year up to last month
	totalDays += day - 1;							// days in this month minus today

	m_ticks = totalDays * Timespan::TicksPerDay
		+ hour * Timespan::TicksPerHour
		+ minute * Timespan::TicksPerMinute
		+ second * Timespan::TicksPerSecond
		+ millisecond * Timespan::TicksPerMillisecond;
}

void DateTime::utcTime(int32_t& year, int32_t& month, int32_t& dayOfWeek, int32_t& day, int32_t& hour, int32_t& minute, int32_t& second, int32_t& millisecond)
{
#if CZ_PLATFORM_WIN32
	SYSTEMTIME st;
	::GetSystemTime(&st);

	year = st.wYear;
	month = st.wMonth;
	dayOfWeek = st.wDayOfWeek;
	day = st.wDay;
	hour = st.wHour;
	minute = st.wMinute;
	second = st.wSecond;
	millisecond = st.wMilliseconds;

#elif CZ_PLATFORM_LINUX

	// get calendar time
	struct timeval time;
	gettimeofday(&time, NULL);

	// convert to UTC
	struct tm localTime;
	gmtime_r(&time.tv_sec, &localTime);

	year = localTime.tm_year + 1900;
	month = localTime.tm_mon + 1;
	dayOfWeek = localTime.tm_wday;
	day = localTime.tm_mday;
	hour = localTime.tm_hour;
	minute = localTime.tm_min;
	second = localTime.tm_sec;
	millisecond = time.tv_usec / 1000;
#else
	static_assert(false);
#endif
}

DateTime DateTime::utcNow()
{
	int32_t year, month, day, dayOfWeek;
	int32_t hour, minute, second, millisecond;
	utcTime(year, month, dayOfWeek, day, hour, minute, second, millisecond);
	return DateTime(year, month, day, hour, minute, second, millisecond);
}

bool DateTime::validate(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second, int32_t millisecond)
{
	return (year >= 1) && (year <= 9999) &&
		(month >= 1) && (month <= 12) &&
		(day >= 1) && (day <= daysInMonth(year, month)) &&
		(hour >= 0) && (hour <= 23) &&
		(minute >= 0) && (minute <= 59) &&
		(second >= 0) && (second <= 59) &&
		(millisecond >= 0) && (millisecond <= 999);
}

int32_t DateTime::daysInYear(int32_t year)
{
	if (isLeapYear(year))
		return 366;
	else
		return 365;
}

bool DateTime::isLeapYear(int32_t year)
{
	if ((year % 4) == 0)
		return (((year % 100) != 0) || ((year % 400) == 0));
	else
		return false;
}

int32_t DateTime::daysInMonth(int32_t year, int32_t month)
{
	CZ_ASSERT((month >= 1) && (month <= 12));

	if ((month == 2) && isLeapYear(year))
	{
		return 29;
	}

	return ms_daysPerMonth[month];
}

void DateTime::getDate(int32_t& year, int32_t& month, int32_t& day)
{
	// Based on FORTRAN code in:
	// Fliegel, H. F. and van Flandern, T. C.,
	// Communications of the ACM, Vol. 11, No. 10 (October 1968).

	int32_t i, j, k, l, n;

	l = int32_t(floorf((float)(getJulianDay() + 0.5))) + 68569;
	n = 4 * l / 146097;
	l = l - (146097 * n + 3) / 4;
	i = 4000 * (l + 1) / 1461001;
	l = l - 1461 * i / 4 + 31;
	j = 80 * l / 2447;
	k = l - 2447 * j / 80;
	l = j / 11;
	j = j + 2 - 12 * l;
	i = 100 * (n - 49) + i + l;

	year = i;
	month = j;
	day = k;
}

}