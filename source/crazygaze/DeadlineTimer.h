/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Inspired by boost::deadline_timer
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

struct DeadlineTimerResult
{
	enum class Code
	{
		Ok,
		Aborted
	};

	Code code;
	explicit DeadlineTimerResult(Code code) : code(code) {}
	DeadlineTimerResult(const DeadlineTimerResult& other) : code(other.code) {}
	bool isAborted() const { return code == Code::Aborted; }
	bool isOk() const { return code == Code::Ok; }
};

using DeadlineTimerHandler = std::function<void(DeadlineTimerResult err)>;
class DeadlineTimer
{
public:
	DeadlineTimer(CompletionPort& iocp);
	DeadlineTimer(CompletionPort& iocp, unsigned milliseconds);
	~DeadlineTimer();

	DeadlineTimer(const DeadlineTimer&) = delete;
	DeadlineTimer& operator=(const DeadlineTimer&) = delete;

	void asyncWait(DeadlineTimerHandler handler);
	size_t expiresFromNow(unsigned milliseconds);
	size_t cancel();
	CompletionPort& getIOCP();

	// This is used internally. Do not call it on your own
	void _callback(bool timerOrWaitFired);

	// #TODO : Remove this
	bool _isSet() const
	{
		auto res = WaitForSingleObject(m_handle, 0);
		return res == WAIT_OBJECT_0;
	}
private:
	friend struct details::DeadlineTimerOperation;
	void init(unsigned ms);
	void shutdown();
	void execute(details::DeadlineTimerOperation* op, uint64_t completionKey);
	void queueHandlers(DeadlineTimerResult::Code code);
	std::mutex m_mtx;
	HANDLE m_handle;
	HANDLE m_completionEvt;
	CompletionPort& m_iocp;
	std::vector<DeadlineTimerHandler> m_handlers;
	std::shared_ptr<details::DeadlineTimerSharedData> m_shared;

	// Implementation notes:
	// Instead of have a simple bool m_signaled, I have two integers that are incremented when the timer is set,
	// and when the timer callback is called.
	// This solves the race condition for the following case:
	//	- We call expiresFromNow
	//		- It acquires the lock
	//		- Meanwhile _callback is called before we have the time to cancel. It will block waiting for expiresFromNow to release the lock
	//		- expiresFromNow dues the rest of the work (queue remaining handlers as aborted, and set a new expiry time)
	//	- We call asyncWait to add more handlers, before _callback gets the CPU back (and acquires the lock)
	//	- _callback resumes
	//		- It has handlers to dispatch, which should NOT be dispatched, since they are new and should be executed when
	//		  the timer expires due to the expiresFromNow called above
	struct  
	{
		unsigned setupCount = 0;
		unsigned callbackCount = 0;
	} m_callbackCheck;
	bool isSignaled() const
	{
		return m_callbackCheck.setupCount == m_callbackCheck.callbackCount;
	}

};

}


