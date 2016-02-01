#include "czlibPCH.h"
#include "crazygaze/TimerQueue.h"
#include "crazygaze/Logging.h"


namespace cz
{


//////////////////////////////////////////////////////////////////////////
//	TimerQueue
//////////////////////////////////////////////////////////////////////////
TimerQueue::TimerQueue()
{
	m_start = std::chrono::steady_clock::now();
	m_th = std::thread([this] { run(); });
}

void TimerQueue::makeHeap()
{
	std::make_heap(m_items.begin(), m_items.end(),
		[](const ItemInfo& a, const ItemInfo& b)
	{
		return a.endTime > b.endTime;
	});
}

void TimerQueue::pushHeap(ItemInfo info)
{
	m_items.push_back(std::move(info));
	std::push_heap(m_items.begin(), m_items.end(),
		[](const ItemInfo& a, const ItemInfo& b)
	{
		return a.endTime > b.endTime;
	});
}

TimerQueue::ItemInfo TimerQueue::popHeap()
{
	std::pop_heap(m_items.begin(), m_items.end(),
		[](const ItemInfo& a, const ItemInfo& b)
	{
		return a.endTime > b.endTime;
	});

	ItemInfo ret = std::move(m_items.back());
	m_items.pop_back();
	return ret;
}

uint64_t TimerQueue::add(unsigned milliseconds, std::function<void(bool)> handler)
{
	ItemInfo info;
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
	info.endTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start).count();
	info.handler = std::move(handler);

	uint64_t id;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		id = ++m_idcounter;
		info.id = id;
		pushHeap(std::move(info));
		m_q.push([] {}); // Empty command, just to wake up the thread and do any recalculations if necessary
	}

	return id;
}

TimerQueue::~TimerQueue()
{
	cancelAll();
	m_q.push([this] { m_finish = true;});
	m_th.join();
	CZ_ASSERT(m_items.size() == 0);
	CZ_ASSERT(m_q.size() == 0);
}

size_t TimerQueue::cancel(uint64_t id)
{
	std::function<void(bool)> handler;

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto it = m_items.begin(); it != m_items.end(); ++it)
		{
			if (it->id == id)
			{
				// Instead of removing from the heap, we clear the item pointer, since it's faster,
				// and let the worker thread just ignore the item if it tries to execute it.
				// This way, we don't waste time removing items, and making calls to make_heap
				handler = std::move(it->handler);
				break;
			}
		}
	}
	
	if (!handler)
		return 0;

	m_q.push([this, handler=std::move(handler)]
	{
		handler(true);
	});
	return 1;
}

size_t TimerQueue::cancelAll()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto ret = m_items.size();
	m_q.push([this, items = std::move(m_items)]
	{
		for (auto&& i : items)
			i.handler(true);
	});

	return ret;
}

void TimerQueue::run()
{
	while(!m_finish)
	{
		int64_t ms = 0xFFFFFFFF;
		int64_t now;

		{
			std::lock_guard<std::mutex> lk(m_mtx);
			now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start).count();
			if (m_items.size())
				ms = m_items.front().endTime - now;
		}

		std::function<void()> cmd;
		if (ms>0 && m_q.wait_and_pop(cmd, ms))
		{
			// Command available
			cmd();
		}
		else
		{
			// No command available, so it means a timer expired, and we need to execute it
			ItemInfo item;
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				if (m_items.size())
					item = popHeap();
			}
			if (item.handler)
				item.handler(false);
		}
	}
}

}

