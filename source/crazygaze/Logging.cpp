/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "Logging.h"

namespace cz
{

CZ_DEFINE_LOG_CATEGORY(logDefault)

#if CZ_NO_LOGGING
LogCategoryLogNone logNone;
#endif

void LogCategoryBase::setVerbosity(LogVerbosity verbosity)
{
	// Take into considering the minimum compiled verbosity
	m_verbosity = LogVerbosity( std::min((int)m_compileTimeVerbosity, (int)verbosity) );
}

LogOutput::SharedData* LogOutput::getSharedData()
{
	// This is thread safe, according to C++11 (aka: Magic Statics)
	static SharedData data;
	return &data;
}

LogOutput::LogOutput()
{
	auto data = getSharedData();
	auto lk = std::unique_lock<std::mutex>(data->mtx);
	data->outputs.push_back(this);
}

LogOutput::~LogOutput()
{
	auto data = getSharedData();
	auto lk = std::unique_lock<std::mutex>(data->mtx);
	data->outputs.erase(std::find(data->outputs.begin(), data->outputs.end(), this));
}

void LogOutput::logToAll(const char* file, int line, const LogCategoryBase* category, LogVerbosity verbosity, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char buf[1024];
	auto count = _vsnprintf_s(buf, sizeof(buf), fmt, args);
	if (!(count>=0 && count<sizeof(buf)))
		buf[sizeof(buf) - 1] = 0;

	auto data = getSharedData();
	auto lk = std::unique_lock<std::mutex>(data->mtx);
	for (auto&& out : data->outputs)
	{
		out->log(file, line, category, verbosity, buf);
	}
}


}
