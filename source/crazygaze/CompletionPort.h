/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Windows I/O Completion ports
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/ThreadingUtils.h"
#include "crazygaze/Semaphore.h"

namespace cz
{

using CompletionHandler = std::function<void(unsigned)>;

struct CompletionPortOperation
{
	CompletionPortOperation();
	CompletionPortOperation(const CompletionPortOperation& other) = delete;
	CompletionPortOperation& operator=(const CompletionPortOperation& other) = delete;
	virtual ~CompletionPortOperation() {}
	virtual void execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey) = 0;
	WSAOVERLAPPED overlapped;
	Semaphore readyToExecute;
};

class CompletionPort
{
  public:
	explicit CompletionPort();
	~CompletionPort();
	CompletionPort(const CompletionPort&) = delete;
	CompletionPort& operator=(const CompletionPort) = delete;

	HANDLE getHandle();

	//! Run and executes all handles. It blocks until the Completion Port is stopped
	// \return
	//		Return the number of items handled
	size_t run();

	//! Execute any ready handles, without blocking
	// \return
	//		Return the number of items handled
	size_t poll();

	//! Stops the completion port
	// This will cause any threads currently calling #run to exit
	void stop();

	void add(std::unique_ptr<CompletionPortOperation> operation);

	void post(std::unique_ptr<CompletionPortOperation> op, unsigned bytesTransfered, uint64_t completionKey);
  protected:
	HANDLE m_port;
	struct Data
	{
		std::unordered_map<CompletionPortOperation*, std::unique_ptr<CompletionPortOperation>> items;
	};
	Monitor<Data> m_data;

	size_t runImpl(DWORD timeoutMs);

};

} // namespace cz
