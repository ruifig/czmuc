/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Inspired by boost::deadline_timer

	Thread Safety:
	Distinct objects: Safe
	Shared objects: Unsafe

	Notes:
	- Handler execution ALWAYS happens in the thread serving the completion port
	- Handlers ARE NOT guaranteed to execute in the same order they were inserted.
	- Not thread safe. If you wish to call the timer from the handler itself, be sure you have the appropriate
	  synchronization in place.
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/CompletionPort.h"

namespace cz
{

namespace details
{
	// Forward declaration
	struct DeadlineTimerSharedData;
	struct DeadlineTimerOperation;
};

using DeadlineTimerHandler = std::function<void(bool)>;
class TimerQueue;

class DeadlineTimer
{
public:
	DeadlineTimer(CompletionPort& iocp, unsigned milliseconds=0);
	~DeadlineTimer();

	DeadlineTimer(const DeadlineTimer&) = delete;
	DeadlineTimer& operator=(const DeadlineTimer&) = delete;

	//! Adds a new handler
	void asyncWait(DeadlineTimerHandler handler);

	//! Synchronously waits for the timer to expire
	// Note that this returns when the timer expires and the handlers are queued for execution.
	// It does not MEAN the handlers were executed yet.
	void wait();

	/*! Aborts any pending handlers, and sets a new expiry time
	* \return 
	*	The number of handlers aborted. Note that if an handler wasn't executed yet, doesn't mean it will abort, since
	*	it might be already queued up for successful execution.
	*/
	size_t expiresFromNow(unsigned milliseconds);
	size_t cancel();
	CompletionPort& getIOCP();

	static std::shared_ptr<TimerQueue> getDefaultQueue();
private:

	friend struct details::DeadlineTimerOperation;
	size_t cancelImpl();
	void execute(details::DeadlineTimerOperation* op, uint64_t completionKey);
	void queueHandler(DeadlineTimerHandler handler, bool aborted);

	std::mutex m_mtx;
	CompletionPort& m_iocp;
	ZeroSemaphore m_pending;
	uint64_t m_qid;
	std::weak_ptr<std::vector<DeadlineTimerHandler>> m_handlers;
	std::shared_ptr<TimerQueue> m_q;

};

}


