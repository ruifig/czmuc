/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czlib.h"

namespace cz
{

class Semaphore
{
public:
    Semaphore (unsigned int count = 0) : m_count(count) {}
    void notify();
    void wait();
	bool trywait();

    template <class Clock, class Duration>
    bool waitUntil(const std::chrono::time_point<Clock, Duration>& point) {
        std::unique_lock<std::mutex> lock(m_mtx);
        if (!m_cv.wait_until(lock, point, [this]() { return m_count > 0; }))
            return false;
        m_count--;
        return true;
    }
private:
    std::mutex m_mtx;
    std::condition_variable m_cv;
    unsigned int m_count;
};

//!
// Blocks until the counter reaches zero
class ZeroSemaphore
{
  public:
	ZeroSemaphore() {}
	void increment();
	void decrement();
	void wait();
	bool trywait();

  private:
	std::mutex m_mtx;
	std::condition_variable m_cv;
	int m_count = 0;
};

}
