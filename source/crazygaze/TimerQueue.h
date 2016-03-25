#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/Semaphore.h"
 
namespace cz
{
// Timer Queue
//
// Allows execution of handlers at a specified time in the future
// Guarantees:
//  - All handlers are executed ONCE, even if cancelled (aborted parameter will
// be set to true)
//      - If TimerQueue is destroyed, it will cancel all handlers.
//  - Handlers are ALWAYS executed in the Timer Queue worker thread.
//  - Handlers execution order is NOT guaranteed
//
class TimerQueue
{
  public:
	TimerQueue();
	~TimerQueue();

	//! Adds a new timer
	// \return
	//  Returns the ID of the new timer. You can use this ID to cancel the
	// timer
	uint64_t add(int64_t milliseconds, std::function<void(bool)> handler);

	//! Cancels the specified timer
	// \return
	//  1 if the timer was cancelled.
	//  0 if you were too late to cancel (or the timer ID was never valid to
	// start with)
	size_t cancel(uint64_t id);

	//! Cancels all timers
	// \return
	//  The number of timers cancelled
	size_t cancelAll();

  private:
	using Clock = std::chrono::steady_clock;
	TimerQueue(const TimerQueue&) = delete;
	TimerQueue& operator=(const TimerQueue&) = delete;

	void run();
	std::pair<bool, Clock::time_point> calcWaitTime();
	void checkWork();

	Semaphore m_checkWork;
	std::thread m_th;
	bool m_finish = false;
	uint64_t m_idcounter = 0;

	struct WorkItem
	{
		Clock::time_point end;
		uint64_t id;  // id==0 means it was cancelled
		std::function<void(bool)> handler;
		bool operator>(const WorkItem& other) const { return end > other.end; }
	};

	std::mutex m_mtx;
	// Inheriting from priority_queue, so we can access the internal container
	class Queue : public std::priority_queue<WorkItem, std::vector<WorkItem>, std::greater<WorkItem>>
	{
	  public:
		std::vector<WorkItem>& getContainer() { return this->c; }
	} m_items;
};

}  // namespace cz
