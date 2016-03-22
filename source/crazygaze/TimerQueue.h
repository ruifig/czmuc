#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/ThreadingUtils.h"
#include "crazygaze/SharedQueue.h"

namespace cz
{

/*!
* Allows executions of handlers at a specified time in the future.
* NOTES:
*	- All handlers are executed ONCE, even if canceled (aborted is set to true)
*	- Handlers are ALWAYS executed in the timer thread created for that purpose. An handler never executes in the caller
*	  thread.
*	- Handlers ARE NOT guaranteed to execute in the same order they were inserted. This is due to the timer accuracy,
*	  and the sorting algorithm used.
*/
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
	*
	* \param milliseconds
	*	Time from the point of call to wait before calling the handler
	* \param handler
	*	Handler to call. If the handler was canceled, the handler parameter is true
	* \return
	*	Id you can use to cancel the handler.
	*/
	uint64_t add(unsigned milliseconds, std::function<void(bool)> handler);

	/*! Cancel the specified handler
	* Canceling an handler causes it to be queued immediately for execution, as aborted.
	*
	* \return
	* 0 - The handler was not canceled. You were too late to cancel, and the handler was either executed, or is queued
	* for execution as successful.
	* 1 - The handler was canceled, and it was queued immediately for execution as aborted.
	*/
	size_t cancel(uint64_t id);

	/*! Cancels all handlers
	* All handlers will be queued for execution as aborted
	*
	* \return
	*	Number of handlers canceled.
	*/
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


