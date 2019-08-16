/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	File logging output
	
*********************************************************************/

#include "czmucPCH.h"
#include "FileLogOutput.h"

namespace cz
{

FileLogOutput::FileLogOutput(const char* filename)
	: m_filename(filename)
{
	m_file.open(filename, std::ios::out | std::ios::trunc);
}

FileLogOutput::~FileLogOutput()
{
}

void FileLogOutput::log(const char* /*file*/, int /*line*/, const LogCategoryBase* /*category*/, LogVerbosity /*verbosity*/, const char* msg)
{
	m_q.send([this, msg = std::string(msg)]()
	{
		m_file << msg << std::endl;
	});
}

std::string FileLogOutput::getContents()
{
	std::promise<std::string> pr;
	m_q.send([&]()
	{
		// reopen for reading
		m_file.close();
		std::ifstream ifs(m_filename);
		std::string str;
		str.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
		pr.set_value(str);

		// Reopen for writing
		m_file.open(m_filename, std::ios::out | std::ios::app);
	});

	return pr.get_future().get();
}

}

