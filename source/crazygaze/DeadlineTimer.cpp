#include "czlibPCH.h"
#include "crazygaze/DeadlineTimer.h"
#include "crazygaze/Logging.h"
#include "crazygaze/ScopeGuard.h"
#include "crazygaze/TimerQueue.h"

/*
Implementation notes:

Due to consecutive calls to asyncWait,cancel, and expireFromNow, several sets of handlers can be in flight at the same time.
The way I deal with this is by giving ownership of the set of handlers to the TimerQueue handler, and keep only a weak_ptr.
This makes it easy to figure out if there is still an active set of handlers, or if the timer expired.
It also allows having several canceled sets in flight. For example:

t.expireFromNow(2);
t.asyncWait(a);
t.asyncWait(b);
t.cancel();  // we might be too late to cancel, and the set is already in flight for execution
t.asyncWait(c); // Because we canceled the previous set, here we create another set, and end up with two sets in flight as expected

*/
namespace cz
{

namespace details
{

struct DeadlineTimerOperation : public CompletionPortOperation
{
	DeadlineTimerOperation(DeadlineTimer* owner) : owner(owner)
	{
	}
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override
	{
		CZ_ASSERT(aborted == false && bytesTransfered == false);
		owner->execute(this, completionKey);
	}
	DeadlineTimer* owner;
	DeadlineTimerHandler handler;
};

} // namespace details


std::shared_ptr<TimerQueue> DeadlineTimer::getDefaultQueue()
{
	return getSharedData<TimerQueue>();
}

DeadlineTimer::DeadlineTimer(CompletionPort& iocp, unsigned milliseconds) : m_iocp(iocp)
{
	m_q = getDefaultQueue();
	expiresFromNow(milliseconds);
}

DeadlineTimer::~DeadlineTimer()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto h = m_handlers.lock();
	if (h)
	{
		// Canceling our TimerQueue::add. Two things can happen
		// - It cancels successfully, and the handlers are queued for execution as aborted
		// - It fails to cancel because we are too late, and the handlers already will be queued for execution
		// Either case, the handlers will be queued as fast as possible, so we don't need to do anything else other
		// than calling cancel.
		m_q->cancel(m_qid);
	}
	m_pending.wait();
	CZ_ASSERT(h == nullptr || (h->size() == 0 && h.unique()));
}

size_t DeadlineTimer::cancel()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return cancelImpl();
}

size_t DeadlineTimer::expiresFromNow(unsigned milliseconds)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto count = cancelImpl();

	// A call to expireFromNow means cancel the current set, and start a new one.
	// In code terms, it means:
	//		- Cancel the current set. Doesn't matter if its successful or not
	//		- Start a new set, since we are setting a new expiry time
	// So we just create a new shared_ptr to pass to the new TimerQueue::add call, and set our weak_ptr to that one
	auto handlers = std::make_shared<std::vector<DeadlineTimerHandler>>();
	m_handlers = handlers;

	m_pending.increment();
	m_qid = m_q->add(milliseconds, [this, handlers](bool aborted) mutable
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto&& handler : *handlers)
			queueHandler(std::move(handler), aborted);
		// Before we are done with the handler (and unlock the mutex), we need to check if there are any other copies.
		// We are done with this set of handlers.
		// Also, we need to set release the shared_ptr explicitly here, instead of letting it go out of scope, so everything
		// is still done while holding the lock
		CZ_ASSERT(handlers.unique());
		handlers.reset();
		m_pending.decrement(); // This NEEDS to be last, so ::wait can wait on everything to finish
	});

	return count;
}

void DeadlineTimer::asyncWait(DeadlineTimerHandler handler)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto h = m_handlers.lock();
	if (h)
	{
		// If we can lock the set, it means it's still active (not expired yet), so we can add more handlers to that set
		h->emplace_back(std::move(handler));
	}
	else
	{
		// The timer already expired, so there is no active set. In this case we add the handler for execution right away
		queueHandler(std::move(handler), false);
	}
}

void DeadlineTimer::wait()
{
	m_pending.wait();
}


CompletionPort& DeadlineTimer::getIOCP()
{
	return m_iocp;
}


//////////////////////////////////////////////////////////////////////////
// Private methods
//////////////////////////////////////////////////////////////////////////

/*!
* This has the same behaviour as boost::deadline_timer.
* - Forces any pending handles to be queued for execution as aborted right away.
* - It does not change the expiry time
*/
size_t DeadlineTimer::cancelImpl()
{
	size_t ret = 0;
	auto h = m_handlers.lock();
	if (h)
	{
		// If we can lock the set, we explicitly queue for execution and empty the set, without canceling the current
		// TimerQueue::add. This means when our TimerQueue handler gets executed it will simply do nothing, since the handler
		// set is empty
		ret = h->size();
		for (auto&& handler : *h)
			queueHandler(std::move(handler), true);
		h->clear();
	}

	// NOTE: We leave the weak_ptr still pointing to the set (if any), since we don't want to touch the current expiry time,
	// and therefore the current set. This means any following calls to asyncWait will add the handler to the currently
	// in-flight set, as expected.
	return ret;
}

void DeadlineTimer::execute(details::DeadlineTimerOperation* op, uint64_t completionKey)
{
	op->handler(completionKey ? true : false);
}

void DeadlineTimer::queueHandler(DeadlineTimerHandler handler, bool aborted)
{
	auto op = std::make_unique<details::DeadlineTimerOperation>(this);
	op->handler = std::move(handler);
	m_iocp.post(std::move(op), 0, aborted ? 1 : 0);
}

}
