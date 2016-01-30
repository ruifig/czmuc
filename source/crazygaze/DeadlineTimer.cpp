#include "czlibPCH.h"
#include "crazygaze/DeadlineTimer.h"
#include "crazygaze/Logging.h"
#include "crazygaze/ScopeGuard.h"

namespace cz
{

namespace details
{

struct DeadlineTimerSharedData
{
	DeadlineTimerSharedData(const DeadlineTimerSharedData&) = delete;
	DeadlineTimerSharedData& operator= (const DeadlineTimerSharedData&) = delete;

	DeadlineTimerSharedData()
	{
		handle = CreateTimerQueue();
		if (handle == NULL)
			CZ_LOG(logDefault, Fatal, "Error calling CreateTimerQueue: %s", getLastWin32ErrorMsg());

		cancelEvt = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (cancelEvt == NULL)
			CZ_LOG(logDefault, Fatal, "Error calling CreateEvent: %s", getLastWin32ErrorMsg());
	}

	~DeadlineTimerSharedData()
	{
		if (handle)
			shutdown();

		auto res = WaitForSingleObject(cancelEvt, INFINITE);
		if (res != WAIT_OBJECT_0)
			CZ_LOG(logDefault, Fatal, "Error waiting for timer queue shutdown event %s", getLastWin32ErrorMsg());

		CloseHandle(cancelEvt);
	}

	void shutdown()
	{
		if (handle==NULL)
			return;

		DeleteTimerQueueEx(
			handle,
			cancelEvt // This event is set when all callbacks have completed
			);
		handle = NULL; // Mark as NULL, so we know shutdown was called
	}

	HANDLE handle = NULL;
	HANDLE cancelEvt = NULL;

	static std::shared_ptr<DeadlineTimerSharedData> get()
	{
		static std::mutex mtx;
		static std::weak_ptr<DeadlineTimerSharedData> ptr;
		std::lock_guard<std::mutex> lk(mtx);
		auto p = ptr.lock();
		if (p)
			return std::move(p);

		p = std::make_shared<DeadlineTimerSharedData>();
		ptr = p;
		return std::move(p);
	}

};

struct DeadlineTimerOperation : public CompletionPortOperation
{
	DeadlineTimerOperation(DeadlineTimer* owner) : owner(owner)
	{
	}
	virtual void execute(unsigned bytesTransfered, uint64_t completionKey) override
	{
		owner->execute(this, completionKey);
	}
	DeadlineTimer* owner;
	DeadlineTimerHandler handler;
};

} // namespace details

static void callback_DeadlineTimer(void* context, BOOLEAN timerOrWaitFired)
{
	reinterpret_cast<DeadlineTimer*>(context)->_callback(timerOrWaitFired == TRUE);
}


void DeadlineTimer::init(unsigned ms)
{
	m_shared = details::DeadlineTimerSharedData::get();
	m_handle = NULL;
	auto res = CreateTimerQueueTimer(
		&m_handle,
		m_shared->handle,
		callback_DeadlineTimer, // Callback
		this, // Parameter to the callback
		ms, // DueTime. Is INFINITE supported here? The documentation doesn't say anything
		0, // Period. This needs to be zero, since we are using the WT_EXECUTEONLYONCE flag
		WT_EXECUTEDEFAULT|WT_EXECUTEONLYONCE // Flags
		);
	
	if (!res)
		CZ_LOG(logDefault, Fatal, "Error calling CreateTimerQueueTimer: %s", getLastWin32ErrorMsg());

	m_completionEvt = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_completionEvt==NULL)
		CZ_LOG(logDefault, Fatal, "Error calling CreateEvent: %s", getLastWin32ErrorMsg());

	m_callbackCheck.setupCount++;
}

DeadlineTimer::DeadlineTimer(CompletionPort& iocp)
	: m_iocp(iocp)
{
	init(INFINITE);
}

DeadlineTimer::DeadlineTimer(CompletionPort& iocp, unsigned milliseconds) : m_iocp(iocp)
{
	init(milliseconds);
}

void DeadlineTimer::shutdown()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	if (m_handle == NULL)
		return;

	auto res = DeleteTimerQueueTimer(
		m_shared->handle,
		m_handle,
		m_completionEvt
		);
	if (!res)
		CZ_LOG(logDefault, Fatal, "Error calling DeleteTimerQueueTimer: %s", getLastWin32ErrorMsg());
	m_handle = NULL;
}

DeadlineTimer::~DeadlineTimer()
{
	if (m_handle)
		shutdown();

	auto waitRes = WaitForSingleObject(m_completionEvt, INFINITE);
	if (waitRes != WAIT_OBJECT_0)
		CZ_LOG(logDefault, Fatal, "Error waiting for timer-queue timer shutdown event %s", getLastWin32ErrorMsg());

	CZ_CHECK(CloseHandle(m_completionEvt));
}

void DeadlineTimer::asyncWait(DeadlineTimerHandler handler)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	m_handlers.push_back(std::move(handler));
	if (isSignaled())
		queueHandlers(DeadlineTimerResult::Code::Ok);
}

size_t DeadlineTimer::expiresFromNow(unsigned milliseconds)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto count = m_handlers.size();
	queueHandlers(DeadlineTimerResult::Code::Aborted);

	auto res = ChangeTimerQueueTimer(
		m_shared->handle,
		m_handle,
		milliseconds,
		0 );
	if (!res)
		CZ_LOG(logDefault, Fatal, "Error calling ChangeTimerQueueTimer: %s", getLastWin32ErrorMsg());
	return count;
}

size_t DeadlineTimer::cancel()
{
	std::lock_guard<std::mutex> lk(m_mtx);
	auto ret = m_handlers.size();
	queueHandlers(DeadlineTimerResult::Code::Aborted);
	return ret;
}

CompletionPort& DeadlineTimer::getIOCP()
{
	return m_iocp;
}

void DeadlineTimer::_callback(bool timerOrWaitFired)
{
	//Sleep(1000);
	std::lock_guard<std::mutex> lk(m_mtx);
	CZ_ASSERT(!isSignaled());
	m_callbackCheck.callbackCount++;
	if (m_callbackCheck.callbackCount != m_callbackCheck.setupCount)
		return;

	queueHandlers(DeadlineTimerResult::Code::Ok);
}

void DeadlineTimer::execute(details::DeadlineTimerOperation* op, uint64_t completionKey)
{
	auto code = completionKey==0 ? DeadlineTimerResult::Code::Ok : DeadlineTimerResult::Code::Aborted;
	op->handler(DeadlineTimerResult(DeadlineTimerResult::Code(completionKey)));
}

void DeadlineTimer::queueHandlers(DeadlineTimerResult::Code code)
{
	for (auto&& handler : m_handlers)
	{
		auto op = std::make_unique<details::DeadlineTimerOperation>(this);
		op->handler = std::move(handler);
		m_iocp.post(std::move(op), 0, (uint64_t)code);
	}
	m_handlers.clear();
}

}
