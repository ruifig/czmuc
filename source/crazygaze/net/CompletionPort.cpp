/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/net/CompletionPort.h"
#include "crazygaze/StringUtils.h"
#include "crazygaze/net/details/TCPSocketDebug.h"

namespace cz
{
namespace net
{

//////////////////////////////////////////////////////////////////////////
//
// CompletionPortOperation
//
//////////////////////////////////////////////////////////////////////////

CompletionPortOperation::CompletionPortOperation(std::shared_ptr<CompletionPortOperationBaseData> sharedData_)
	: sharedData(std::move(sharedData_))
{
	memset(&overlapped, 0, sizeof(overlapped));
	debugData.operationCreated(this);
	++sharedData->iocp.m_queuedCount;
}

CompletionPortOperation::~CompletionPortOperation()
{
	--sharedData->iocp.m_queuedCount;
	debugData.operationDestroyed(this);
}

void CompletionPortOperation::destroy()
{
	delete this;
}

//////////////////////////////////////////////////////////////////////////
//
// CompletionPort
//
//////////////////////////////////////////////////////////////////////////
CompletionPort::CompletionPort(int numThreads)
{
	CZ_ASSERT(numThreads>0);
	m_port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, numThreads);
	if (m_port == NULL)
		throw std::runtime_error(formatString("Error creation io completion port: %s", getLastWin32ErrorMsg()));
	while (numThreads--)
	{
		m_threads.push_back(std::thread([this]
		{
			try
			{
				run();
			}
			catch (std::exception& /*e*/)
			{
				CZ_UNEXPECTED();
			}

		}));
	}
}

CompletionPort::~CompletionPort()
{
	while (m_queuedCount)
		std::this_thread::yield();

	// Send one dummy command per thread, to unblock them
	for (auto&& t : m_threads)
		::PostQueuedCompletionStatus(m_port, 0, (DWORD)NULL, NULL);

	// Now wait for the threads to finish
	// This can't be in the same loop as the PostQueuedCompletionStatus, as we don't know what thread will be
	// dequeuing and finishing.
	for (auto&& t : m_threads)
	{
		t.join();
	}


	::CloseHandle(m_port);
}

HANDLE CompletionPort::getHandle()
{
	return m_port;
}

void CompletionPort::runImpl()
{
	static int val = 0;
	int ourid = val++;
	int counter = 0;
	while (true)
	{
		DWORD bytesTransfered = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED* overlapped = NULL;

		BOOL res = ::GetQueuedCompletionStatus(m_port, &bytesTransfered, &completionKey, &overlapped, INFINITE);
		int err = 0;
		if (res == FALSE)
			err = ::GetLastError();

		// This is a signal to finish
		if (completionKey == 0)
			return;
		CZ_ASSERT(overlapped);

		CompletionPortOperation* operation = CONTAINING_RECORD(overlapped, CompletionPortOperation, overlapped);

		{
			// Make a copy, so we can hold lock the mutex and not end up with the shared data being destroyed while we
			// hold the mutex
			auto data = operation->sharedData;
			auto lk = data->lock();

			if (err == ERROR_OPERATION_ABORTED)
				operation->onError();
			else
				operation->onSuccess(bytesTransfered);

			operation->destroy();
		}

	};
}

void CompletionPort::run()
{
	runImpl();
}

} // namespace net
} // namespace cz



