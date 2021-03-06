/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czmucPCH.h"
#include "crazygaze/muc/Semaphore.h"

namespace cz
{

void cz::Semaphore::notify()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	m_count++;
	m_cv.notify_one();
}

void cz::Semaphore::wait()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	m_cv.wait(lock, [this]() {return m_count > 0; });
	m_count--;
}

bool cz::Semaphore::trywait()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	if (m_count)
	{
		m_count--;
		return true;
	}
	else
	{
		return false;
	}
}

void ZeroSemaphore::increment()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	m_count++;
}

void ZeroSemaphore::decrement()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	m_count--;
	m_cv.notify_all();
}

void ZeroSemaphore::wait()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	m_cv.wait(lock, [this]()
	{
		return m_count == 0;
	});
}

bool ZeroSemaphore::trywait()
{
	std::unique_lock<std::mutex> lock(m_mtx);
	if (m_count == 0)
		return true;
	else
		return false;
}

} // namespace cz
