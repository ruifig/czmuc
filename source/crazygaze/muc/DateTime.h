#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

	struct Timespan
	{

		// The maximum number of ticks that can be represented in Timespan
		static constexpr int64_t MaxTicks = 9223372036854775807;

		// The minimum number of ticks that can be represented in Timespan
		static constexpr int64_t MinTicks = -9223372036854775807 - 1;

		// The number of nanoseconds per tick
		static constexpr int64_t NanosecondsPerTick = 100;

		// The number of ticks per day
		static constexpr int64_t TicksPerDay = 864000000000;

		// The number ticks per hour
		static constexpr int64_t TicksPerHour = 36000000000;

		// The number of ticks per microsecond
		static constexpr int64_t TicksPerMicrosecond = 10;

		// The number of ticks per millisecond
		static constexpr int64_t TicksPerMillisecond = 10000;

		// The number of ticks per minute
		static constexpr int64_t TicksPerMinute = 600000000;

		// The number of ticks per second
		static constexpr int64_t TicksPerSecond = 10000000;

		// The number of ticks per week
		static constexpr int64_t TicksPerWeek = 6048000000000;

		// The number of ticks per year (365 days, not accounting for leap years)
		static constexpr int64_t TicksPerYear = 365 * TicksPerDay;

		Timespan(int64_t ticks) : m_ticks(ticks) {}

		int64_t getTicks() const
		{
			return m_ticks;
		}

	private:
		// Time span with a 100 nanoseconds resolution
		int64_t m_ticks;

	};

	/**
	* Implements a date and time
	* Internally, the time value is stored in ticks of 100 nanoseconds since January 1, 0001
	* Valid ranges are between 00:00:00, January 1, 001 and 23:59:59.999, December 31, 9999
	*/
	struct DateTime
	{
	public:

		DateTime(int64_t ticks) : m_ticks(ticks) {}
		DateTime(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second, int32_t millisecond);

		/**
		 * \param year
		 * \param month (1-12)
		 * \param day (1-31)
		 */
		void getDate(int32_t& year, int32_t& month, int32_t& day);

		/**
		 * \return 0-6, where 0 is Monday
		 */
		int32_t getDayOfWeek() const
		{
			// January 1, 0001 was a Monday
			return static_cast<int32_t>((m_ticks / Timespan::TicksPerDay) % 7);
		}

		/**
		 * \return 0-23
		 */
		int32_t getHour() const
		{
			return (int32_t)((m_ticks / Timespan::TicksPerHour) % 24);
		}

		/**
		 * \return 0-59
		 */
		int32_t getMinute() const
		{
			return (int32_t)((m_ticks / Timespan::TicksPerMinute) % 60);
		}

		/**
		 * \return 0-50
		 */
		int32_t getSecond() const
		{
			return (int32_t)((m_ticks / Timespan::TicksPerSecond) % 60);
		}

		/**
		 * \return 0-999
		 */
		int32_t getMillisecond() const
		{
			return (int32_t)((m_ticks / Timespan::TicksPerMillisecond) % 1000);
		}

		Timespan operator-(const DateTime& other) const
		{
			return Timespan(m_ticks - other.m_ticks);
		}

		DateTime operator-(const Timespan& other) const
		{
			return DateTime(m_ticks - other.getTicks());
		}

		DateTime operator+(const Timespan& other) const
		{
			return DateTime(m_ticks + other.getTicks());
		}

		DateTime& operator-=(const Timespan& other)
		{
			m_ticks -= other.getTicks();
			return *this;
		}

		DateTime& operator+=(const Timespan& other)
		{
			m_ticks += other.getTicks();
			return *this;
		}

	public:

		/**
		 * \param year
		 * \param month (1-12)
		 * \param dayOfWeek (0-6, where 0 is Sunday)
		 * \param day (1-31)
		 * \param hour (0-23)
		 * \param minute (0-59)
		 * \param second (0-59)
		 * \param millisecond (0-999)
		 *
		 * \warning
		 *	Please note that the dayOfWeek starts on Sunday(0) for this function, but day of week for DateTime starts on Monday
		 */
		static void utcTime(int32_t& year, int32_t& month, int32_t& dayOfWeek, int32_t& day, int32_t& hour, int32_t& minute, int32_t& second, int32_t& millisecond);

		static DateTime utcNow();
		static	bool validate(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second, int32_t millisecond);
		static int32_t daysInYear(int32_t year);
		static bool isLeapYear(int32_t year);
		static int32_t daysInMonth(int32_t year, int32_t month);

		double getJulianDay() const
		{
			return (double)(1721425.5 + m_ticks / Timespan::TicksPerDay);
		}

	private:
		// Days per month in a non-leap year
		static const int32_t ms_daysPerMonth[];

		// Cumulative days per month in a non-leap year
		static const int32_t ms_daysToMonth[];

		int64_t m_ticks;
	};


}