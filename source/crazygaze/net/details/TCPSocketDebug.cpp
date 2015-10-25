/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/net/details/TCPSocketDebug.h"

namespace cz
{
namespace net
{

//////////////////////////////////////////////////////////////////////////
//
//	DebugData
//
//////////////////////////////////////////////////////////////////////////

DebugData debugData;

#if !defined(NDEBUG) && TCPSOCKET_DEBUG

DebugData::~DebugData()
{
	check();
	reset();
}

void DebugData::operationCreated(CompletionPortOperation* op)
{
	++activeOperations;
	operations.add(op);
}

void DebugData::operationDestroyed(CompletionPortOperation* op)
{
	--activeOperations;
	operations.remove(op);
}

void DebugData::serverSocketCreated(TCPServerSocket* s)
{
	++activeServerSockets;
	serverSockets.add(s);
}

void DebugData::serverSocketDestroyed(TCPServerSocket* s)
{
	--activeServerSockets;
	serverSockets.remove(s);
}

void DebugData::socketCreated(TCPSocket* s)
{
	++activeSockets;
	sockets.add(s);
}

void DebugData::socketDestroyed(TCPSocket* s)
{
	--activeSockets;
	sockets.remove(s);
}

void DebugData::receivedData(TCPSocketData* owner, const char* data, int size)
{
	socketReceivedData.change(owner, [&](ReceivedData& d)
	{
		d.data.insert(d.data.end(), data, data + size);
	});
}

void DebugData::check()
{
	CZ_ASSERT(activeOperations == 0);
	CZ_ASSERT(activeServerSockets == 0);
	CZ_ASSERT(activeSockets == 0);
}

void DebugData::reset()
{
	activeOperations = 0;
	activeServerSockets = 0;
	activeSockets = 0;

	operations.clear();
	serverSockets.clear();
	sockets.clear();
	socketReceivedData.clear();
}

void DebugData::checkAndReset()
{
	debugData.check();
	debugData.reset();
}

#endif

} // namespace net
} // namespace cz



