/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/rpc/RPCConnection.h"

namespace cz
{
namespace rpc
{

Transport::Transport()
{
}

int Transport::hasFullRPC(const ChunkBuffer& in)
{
	//
	// Check if we have enough data in the buffer for a complete RPC message
	int bufSize = in.calcSize();
	if (bufSize < sizeof(int))
		return 0;
	int rpcSize = 0;
	in.peek(&rpcSize, sizeof(rpcSize));
	if (bufSize - (int)sizeof(int) < rpcSize)
		return 0;
	// Consume the RPC size header
	in >> rpcSize;
	CZ_ASSERT(rpcSize > 0);
	return rpcSize;
}

void Transport::onReceivedData(const ChunkBuffer& in)
{
	if (m_owner)
		m_owner->onReceivedData(in);
}

void Transport::onDisconnected()
{
	if (m_owner)
		m_owner->onDisconnected();
}

void Transport::setOwner(BaseConnection* owner)
{
	m_owner = owner;
}

void ServerTransport::setOnNewClient(std::function<void(std::unique_ptr<Transport>)> fn)
{
	m_onNewClient = std::move(fn);
}

void ServerTransport::setOnClientDisconnect(std::function<void(Transport*)> fn)
{
	m_onClientDisconnect = std::move(fn);
}

} // namespace rpc
} // namespace cz
