/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "UnitTestsPCH.h"

CZ_DEFINE_LOG_CATEGORY(logTests)
CZ_DEFINE_LOG_CATEGORY(logTestsVerbose)

void spinMs(double ms)
{
	UnitTest::Timer timer;
	timer.Start();
	auto now = timer.GetTimeInMs();
	while (timer.GetTimeInMs() - now < ms)
	{
		// Spin
	}
}
