/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/net/CompletionPort.h"
#include "crazygaze/Logging.h"

namespace cz
{
namespace net
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
	if (m_port != INVALID_HANDLE_VALUE)
		::CloseHandle(m_port);
	m_port = INVALID_HANDLE_VALUE;
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

size_t CompletionPort::run()
{
	size_t counter = 0;
	while (true)
	{
		DWORD bytesTransfered = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED* overlapped = NULL;
		BOOL res = ::GetQueuedCompletionStatus(m_port, &bytesTransfered, &completionKey, &overlapped, INFINITE);
		int err = 0;
		if (res == FALSE)
			err = ::GetLastError();

		//
		// Note: If overlapped was NOT NULL, it means an operation was dequeued, successful or not.
		if (overlapped)
		{
			CompletionPortOperation* operation = CONTAINING_RECORD(overlapped, CompletionPortOperation, overlapped);
			operation->execute(bytesTransfered, completionKey);

			// This is needed, since we can get after the WSASend/WSARecv, but before the operation is put into the map
			// If it was not for this check, the send/recv could end up adding operations to the map, AFTER the handler
			// was executed. Those operations would then never been removed from the map
			operation->readyToExecute.wait();
			m_data([operation](Data& data)
			{
				//
				// Instead of doing .erase(key), I'm first doing a find, so I can assert the item exists
				auto it = data.items.find(operation);
				CZ_ASSERT(it != data.items.end());
				data.items.erase(it);
			});
		}

		if (err==0)
		{
			// Operation successful
		}
		else if (
			err == ERROR_ABANDONED_WAIT_0 || // handle closed||
			err ==ERROR_INVALID_HANDLE // m_port was set to INVALID_HANDLE, has part of #stop
			)
		{
			return counter;
		}
		else if (overlapped)
		{
			// If overlapped was set, but we have an error, it means the operation was dequeued, and
			// The error is treated in the handler itself.
			// It has nothing to do with the CompletionPort
			//
		}
		else
		{
			CZ_LOG(logDefault, Fatal, "Unexpected error: '%s'", getLastWin32ErrorMsg());
		}
	};
}

size_t CompletionPort::poll()
{
	// #TODO : Implement this
	CZ_UNEXPECTED();
	return 0;
}

} // namespace net
} // namespace cz



