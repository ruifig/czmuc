/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	File logging output
	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czmuc.h"
#include "crazygaze/muc/Logging.h"
#include "crazygaze/muc/AsyncCommandQueue.h"
#include "crazygaze/muc/Semaphore.h"
#include <fstream>

namespace cz
{

class FileLogOutput : public LogOutput
{
public:
	FileLogOutput(const char* filename);
	~FileLogOutput();
	void log(const char* file, int line, const LogCategoryBase* category, LogVerbosity verbosity, const char* msg) override;

	/**
	* Gets the entire contents of the file.
	* The file is closed, it's contents read to the string, and then reopened.
	* This allows us to get the contents to say... upload somewhere, while keeping the file open.
	*/
	std::string getContents();

private:
	AsyncCommandQueueAutomatic m_q;
	ZeroSemaphore m_getContents;
	std::ofstream m_file;
	std::string m_filename;
};


}

