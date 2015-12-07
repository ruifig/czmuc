/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Windows I/O Completion ports
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"

namespace cz
{
namespace net
{

// Instead of having a function to initialize the library, I call WSAStartup and WSACleanup as
// required, since they can be called several times
struct WSAInstance
{
	WSAInstance();
	~WSAInstance();
};

class CompletionPort;

struct CompletionPortOperationBaseData : public std::enable_shared_from_this<CompletionPortOperationBaseData>
{
	CompletionPortOperationBaseData(CompletionPort& iocp) : iocp(iocp) {}
	virtual ~CompletionPortOperationBaseData() {}

	std::recursive_mutex mtx;
	std::unique_lock<std::recursive_mutex> lock()
	{
		return std::unique_lock<std::recursive_mutex>(mtx);
	}
	CompletionPort& iocp;
};

struct CompletionPortOperation
{
	explicit CompletionPortOperation(std::shared_ptr<CompletionPortOperationBaseData> sharedData_);
	virtual ~CompletionPortOperation();
	CompletionPortOperation(const CompletionPortOperation& other) = delete;
	CompletionPortOperation& operator=(const CompletionPortOperation& other) = delete;
	virtual void onSuccess(unsigned bytesTransfered) = 0;
	virtual void onError() = 0;
	virtual void destroy();
	WSAOVERLAPPED overlapped;
	std::shared_ptr<CompletionPortOperationBaseData> sharedData;
};
class CompletionPort
{
  public:
	explicit CompletionPort(int numThreads);
	~CompletionPort();

	HANDLE getHandle();

  protected:
	void run();
	void runImpl();
	std::vector<std::thread> m_threads;
	HANDLE m_port;
	friend struct CompletionPortOperation;

	WSAInstance wsa;

	// #TODO Replace this with a ZeroSemaphore?
	std::atomic_int m_queuedCount = 0;
};

} // namespace net
} // namespace cz





