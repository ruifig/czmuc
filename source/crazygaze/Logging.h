/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Logging functions/classes, somewhat inspired by how UE4 does it
	
*********************************************************************/

#pragma once


namespace cz
{

enum class LogVerbosity : uint8_t
{
	None, // Used internally only
	Fatal,
	Error,
	Warning,
	Log
};

#define CZ_LOG_MINIMUM_VERBOSITY Log

class LogCategoryBase
{
public:
	LogCategoryBase(const char* name, LogVerbosity verbosity, LogVerbosity compileTimeVerbosity)
		: m_name(name)
		, m_verbosity(verbosity)
		, m_compileTimeVerbosity(compileTimeVerbosity)
	{
	}

	__forceinline const std::string& getName() const
	{
		return m_name;
	}

	__forceinline bool isSuppressed(LogVerbosity verbosity) const
	{
		return verbosity > m_verbosity;
	}

	void setVerbosity(LogVerbosity verbosity);

protected:
	LogVerbosity m_verbosity;
	LogVerbosity m_compileTimeVerbosity;
	std::string m_name;
};

template<LogVerbosity DEFAULT, LogVerbosity COMPILETIME>
class LogCategory : public LogCategoryBase
{
public:
	LogCategory(const char* name) : LogCategoryBase(name, DEFAULT, COMPILETIME)
	{
	}

	// Compile time verbosity
	enum
	{
		CompileTimeVerbosity  = COMPILETIME
	};

private:
};

class LogOutput
{
public:
	LogOutput();
	virtual ~LogOutput();
	static void logToAll(const char* file, int line, const LogCategoryBase* category, LogVerbosity verbosity, const char* fmt, ...);
private:
	virtual void log(const char* file, int line, const LogCategoryBase* category, LogVerbosity LogVerbosity, const char* msg) = 0;

	struct SharedData
	{
		std::mutex mtx;
		std::vector<LogOutput*> outputs;
	};
	static SharedData* getSharedData();

};


#if CZ_NO_LOGGING

struct LogCategoryLogNone : public LogCategory<LogVerbosity::None, LogVerbosity::None>
{
	LogCategoryLogNone() : LogCategory("LogNone") {};
	void setVerbosity(LogVerbosity verbosity) {}
};
extern LogCategoryLogNone logNone;

#define CZ_DECLARE_LOG_CATEGORY(NAME, DEFAULT_VERBOSITY, COMPILETIME_VERBOSITY) extern cz::LogCategoryLogNone& NAME;
#define CZ_DEFINE_LOG_CATEGORY(NAME) cz::LogCategoryLogNone& NAME = cz::logNone;
#define CZ_LOG(...)

#else

#define CZ_DECLARE_LOG_CATEGORY(NAME, DEFAULT_VERBOSITY, COMPILETIME_VERBOSITY) \
	class LogCategory##NAME : public cz::LogCategory<cz::LogVerbosity::DEFAULT_VERBOSITY, cz::LogVerbosity::COMPILETIME_VERBOSITY> \
	{ \
		public: \
		LogCategory##NAME() : LogCategory(#NAME) {} \
	};

#define CZ_DEFINE_LOG_CATEGORY(NAME) LogCategory##NAME NAME;

#define CZ_LOG_CHECK_COMPILETIME_VERBOSITY(NAME, VERBOSITY) \
	(((int)cz::LogVerbosity::VERBOSITY <= LogCategory##NAME::CompileTimeVerbosity) && \
	 ((int)cz::LogVerbosity::VERBOSITY <= (int)cz::LogVerbosity::CZ_LOG_MINIMUM_VERBOSITY))


#define CZ_LOG(NAME,VERBOSITY, fmt, ...) \
	if (CZ_LOG_CHECK_COMPILETIME_VERBOSITY(NAME, VERBOSITY)) \
	{ \
		if (!NAME.isSuppressed(cz::LogVerbosity::VERBOSITY)) \
		{ \
			cz::LogOutput::logToAll(__FILE__, __LINE__, &NAME, cz::LogVerbosity::VERBOSITY, fmt, ##__VA_ARGS__); \
		} \
	}

#endif

} // namespace cz

