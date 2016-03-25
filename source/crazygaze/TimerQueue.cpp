#include "czlibPCH.h"
#include "crazygaze/TimerQueue.h"
#include "crazygaze/Logging.h"

namespace cz
{

TimerQueue::TimerQueue()
{
	m_th = std::thread([this] { run(); });
}


TimerQueue::~TimerQueue()
{
	cancelAll();
	// Abusing the timer queue to trigger the shutdown.
	add(0, [this](bool) { m_finish = true; });
	m_th.join();
}

uint64_t TimerQueue::add(int64_t milliseconds, std::function<void(bool)> handler)
{
	WorkItem item;
	item.end = Clock::now() + std::chrono::milliseconds(milliseconds);
	item.handler = std::move(handler);

	std::unique_lock<std::mutex> lk(m_mtx);
	uint64_t id = ++m_idcounter;
	item.id = id;
	m_items.push(std::move(item));
	lk.unlock();

	// Something changed, so wake up timer thread
	m_checkWork.notify();
	return id;
}

size_t TimerQueue::cancel(uint64_t id)
{
	// Instead of removing the item from the container (thus breaking the
	// heap integrity), we set the item as having no handler, and put
	// that handler on a new item at the top for immediate execution
	// The timer thread will then ignore the original item, since it has no
	// handler.
	std::unique_lock<std::mutex> lk(m_mtx);
	for (auto&& item : m_items.getContainer())
	{
		if (item.id == id && item.handler)
		{
			WorkItem newItem;
			// Zero time, so it stays at the top for immediate execution
			newItem.end = Clock::time_point();
			newItem.id = 0;  // Means it is a canceled item
			// Move the handler from item to newitem (thus clearing item)
			newItem.handler = std::move(item.handler);
			m_items.push(std::move(newItem));

			lk.unlock();
			// Something changed, so wake up timer thread
			m_checkWork.notify();
			return 1;
		}
	}
	return 0;
}

size_t TimerQueue::cancelAll()
{
	// Setting all "end" to 0 (for immediate execution) is ok,
	// since it maintains the heap integrity
	std::unique_lock<std::mutex> lk(m_mtx);
	for (auto&& item : m_items.getContainer())
	{
		if (item.id && item.handler)
		{
			item.end = Clock::time_point();
			item.id = 0;
		}
	}
	auto ret = m_items.size();

	lk.unlock();
	m_checkWork.notify();
	return ret;
}

void TimerQueue::run()
{
	while (!m_finish)
	{
		auto end = calcWaitTime();
		if (end.first)
		{
			// Timers found, so wait until it expires (or something else
			// changes)
			m_checkWork.waitUntil(end.second);
		}
		else
		{
			// No timers exist, so wait forever until something changes
			m_checkWork.wait();
		}

		// Check and execute as much work as possible, such as, all expired
		// timers
		checkWork();
	}

	// If we are shutting down, we should not have any items left,
	// since the shutdown cancels all items
	assert(m_items.size() == 0);
}

std::pair<bool, cz::TimerQueue::Clock::time_point> TimerQueue::calcWaitTime()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	while (m_items.size())
	{
		if (m_items.top().handler)
		{
			// Item present, so return the new wait time
			return std::make_pair(true, m_items.top().end);
		}
		else
		{
			// Discard empty handlers (they were cancelled)
			m_items.pop();
		}
	}

	// No items found, so return no wait time (causes the thread to wait
	// indefinitely)
	return std::make_pair(false, Clock::time_point());
}

void TimerQueue::checkWork()
{
	std::unique_lock<std::mutex> lk(m_mtx);
	while (m_items.size() && m_items.top().end <= Clock::now())
	{
		WorkItem item(std::move(m_items.top()));
		m_items.pop();

		lk.unlock();
		if (item.handler)
			item.handler(item.id == 0);
		lk.lock();
	}
}

}

