/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Timer services	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

	/*! High resolution timer
	To simply time a section of code, do this:
	\code
	HighResolutionTimer timer;
	... some code ...
	double secondsElapsed = timer.seconds();
	.... more code ...
	// Get the total of seconds since the timer creation
	double totalSeconds = timer.seconds();

	// Reset the base time for counting
	timer.reset()
	... some code ....
	// Get the number of seconds since the last reset()
	secondsElapsed = timer.seconds();
	*/
	class HighResolutionTimer {
	public:

		HighResolutionTimer();

		void reset();
		double seconds();
		double milliseconds()
		{
			return seconds() * 1000.0;
		}

		double lapSeconds()
		{
			double t = seconds();
			double ret = t - m_lapSeconds;
			m_lapSeconds = t;
			return ret;
		}

	private:
		double m_secondsPerCycle;
		double m_lapSeconds;
		LARGE_INTEGER  m_base;
	};

} // namespace cz
