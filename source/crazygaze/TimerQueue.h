#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/ThreadingUtils.h"
#include "crazygaze/SharedQueue.h"

namespace cz
{

class TimerQueue
{
public:
	TimerQueue();
	~TimerQueue();
	TimerQueue(const TimerQueue&) = delete;
	TimerQueue& operator= (const TimerQueue&) = delete;
	TimerQueue(TimerQueue&&) = delete;
	TimerQueue& operator= (TimerQueue&&) = delete;

	uint64_t add(std::function<void(bool)> handler, unsigned milliseconds);
	size_t cancel(uint64_t id);
	size_t cancelAll();
private:

	struct ItemInfo
	{
		int64_t endTime;
		uint64_t id;
		std::function<void(bool)> handler;
	};

	void run();
	void makeHeap();
	void pushHeap(ItemInfo info);
	ItemInfo popHeap();


	mutable std::mutex m_mtx;
	decltype(std::chrono::steady_clock::now()) m_start;
	std::vector<ItemInfo> m_items;
	std::thread m_th;
	bool m_finish = false;
	int64_t m_idcounter;
	SharedQueue<std::function<void()>> m_q;
};


}


