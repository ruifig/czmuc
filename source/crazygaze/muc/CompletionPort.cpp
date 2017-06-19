/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/CompletionPort.h"
#include "crazygaze/Logging.h"
#include "crazygaze/ScopeGuard.h"

namespace cz
{

//////////////////////////////////////////////////////////////////////////
//
// CompletionPortOperation
//
//////////////////////////////////////////////////////////////////////////

CompletionPortOperation::CompletionPortOperation()
{
	memset(&overlapped, 0, sizeof(overlapped));
}

//////////////////////////////////////////////////////////////////////////
//
// CompletionPort
//
//////////////////////////////////////////////////////////////////////////

CompletionPort::CompletionPort()
{
	m_port = ::CreateIoCompletionPort(
		INVALID_HANDLE_VALUE, // FileHandle
		NULL, // ExistingCompletionPort
		0, // CompletionKey
		0 // NumberOfConcurrentThreads (0 : allow as many concurrently running threads as there are processors in the system)
		);

	if (m_port == NULL)
		CZ_LOG(logDefault, Fatal, "Error creating io completion port: %s", getLastWin32ErrorMsg());
}

CompletionPort::~CompletionPort()
{
	stop();
}

HANDLE CompletionPort::getHandle()
{
	return m_port;
}

void CompletionPort::stop()
{
	auto doStop = m_data([&](Data& data)
	{
		if (data.stopped)
			return false;
		else
		{
			data.stopped = true;
			return true;
		}
	});

	if (doStop)
		::CloseHandle(m_port);
}

void CompletionPort::add(std::unique_ptr<CompletionPortOperation> operation)
{
	m_data([&](auto&& data)
	{
		auto p = operation.get();
		data.items[operation.get()] = std::move(operation);
		p->readyToExecute.notify();
	});
}

void CompletionPort::post(std::unique_ptr<CompletionPortOperation> op, unsigned bytesTransfered, uint64_t completionKey)
{
	auto res = PostQueuedCompletionStatus(
		m_port,
		bytesTransfered,
		completionKey,
		&op->overlapped
		);
	if (res!=TRUE)
		CZ_LOG(logDefault, Fatal, "PostQueuedCompletionStatus failed with '%s'", getLastWin32ErrorMsg());
	add(std::move(op));
}



size_t CompletionPort::runImpl(DWORD timeoutMs)
{
	Callstack<CompletionPort>::Context ctx(this);

	size_t counter = 0;
	while (true)
	{
		DWORD bytesTransfered = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED* overlapped = NULL;
		BOOL res = ::GetQueuedCompletionStatus(m_port, &bytesTransfered, &completionKey, &overlapped, timeoutMs);
		int err = 0;
		if (res == FALSE)
			err = ::GetLastError();

		//
		// Note: If overlapped was NOT NULL, it means an operation was dequeued, successful or not.
		if (overlapped)
		{
			// If overlapped was set, but we have an error, it means the operation was dequeued, and
			// The error is treated in the handler itself.
			// It has nothing to do with the CompletionPort
			//

			CompletionPortOperation* operation = CONTAINING_RECORD(overlapped, CompletionPortOperation, overlapped);
			// This is needed, since we can get here after the WSASend/WSARecv but before the operation is put into
			// the map.
			// If it was not for this check, the send/recv could end up adding operations to the map, AFTER the handler
			// was executed. Those operations would then never been removed from the map
			operation->readyToExecute.wait();
			operation->execute(err==ERROR_OPERATION_ABORTED ? true : false, bytesTransfered, completionKey);
			counter++;

			m_data([operation](Data& data)
			{
				//
				// Instead of doing .erase(key), I'm first doing a find, so I can assert the item exists
				auto it = data.items.find(operation);
				CZ_ASSERT(it != data.items.end());
				data.items.erase(it);
			});
		}
		else if (
			res==FALSE || // "res==FALSE && overlapped==NULL" means a timeout
			err == ERROR_ABANDONED_WAIT_0 || // handle closed
			err ==ERROR_INVALID_HANDLE // m_port was set to INVALID_HANDLE, has part of #stop
			) 
		{
			return counter;
		}
		else
		{
			CZ_LOG(logDefault, Fatal, "Unexpected error: '%s'", getLastWin32ErrorMsg());
		}
	};
}
size_t CompletionPort::run()
{
	return runImpl(INFINITE);
}

size_t CompletionPort::poll()
{
	return runImpl(0);
}

} // namespace cz



