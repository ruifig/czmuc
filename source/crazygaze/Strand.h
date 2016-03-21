/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	This is inspired and very similar to boost::asio::io_service::strand

	It guarantees the following:
	- Handlers are never executed concurrently
	- Handlers are only ever executed as a result of calls to CompletionPort::run

*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/ThreadingUtils.h"
#include "crazygaze/CompletionPort.h"

namespace cz
{

class Strand
{
	
public:
	Strand(CompletionPort& iocp);

	//! Executes the handler immediately if all the strand guarantees are met, or posts the handler for execution
	// if the guarantees are not met from inside this call
	template<typename F>
	void dispatch(F handler)
	{
		// If we are already in the strand, then we can execute immediately without any other checks
		if (Callstack<Strand>::contains(this))
		{
			handler();
			return;
		}

		// If we are not in our CompletionPort, then we cannot possibly execute the handler here, so
		// enqueue it
		if (!Callstack<CompletionPort>::contains(&m_iocp))
		{
			post(std::move(handker));
			return;
		}

		auto trigger = m_data([&this](Data &data)
		{
			if (data.running)
			{
				// The strand is already owned by someone else, so enqueue the handler for execution.
				// Whoever owns the strand will keep executing handlers until there are no more.
				data.q.push(std::move(handler));
				return false;
			}
			else
			{
				data.running = true;
				return true;
			}
		});

		if (trigger)
		{
			// Mark the strand as running in this thread
			Callstack<Strand>::Context ctx(this);
			handler();
			schedule();
		}
	}

	//! Post an handler for execution, and returns immediately
	// The handler is never executed as part of this call.
	template<typename F>
	void post(F handler)
	{
		bool trigger = m_data([&](Data& data)
		{
			data.q.push(std::move(handler));
			if (data.running)
			{
				return false;
			}
			else
			{
				// If the strand if not running, we take hold of it and schedule the processing
				data.running = true;
				return true;
			}
		});

		if (trigger)
			schedule();
	} 

	//! Tells if the current thread is executing a handler on this strand
	bool runningInThisThread() const;

private:

	struct StrandOperation : CompletionPortOperation
	{
		virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) override;
		Strand* owner;
	};
	friend struct StrandOperation;

	//! Schedule execution of any enqueued handlers
	void schedule();

	//! Processes any enqueued handlers.
	// This assumes the strand is running and locked
	// When there are no more handlers, it releases the strand
	void process();

	CompletionPort& m_iocp;
	struct Data
	{
		bool running = false;
		std::queue<std::function<void()>> q;
	};
	Monitor<Data> m_data;
};

}

