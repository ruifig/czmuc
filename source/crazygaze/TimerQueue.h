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


	/*!
	* Adds a new handler to execute after specified amount of time.
	* Due to the way the queue is implemented, and the accuracy of the timer used, order of execution of the handlers
	* is not guaranteed. For example, consider adding these handlers in succession.
	* q.add(100, [](bool) {}); // Handler A
	* q.add(100, [](bool) {}); // Handler B: Same interval as the previous, but not guaranteed it will execute after A.
	* q.add(101, [](bool) {}); // Handler C: Even though the interval is 101, due to accuracy, you should not depend on it executing after B.
	*/

	uint64_t add(unsigned milliseconds, std::function<void(bool)> handler);
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


