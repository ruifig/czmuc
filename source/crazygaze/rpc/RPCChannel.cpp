/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/rpc/RPCChannel.h"
#include "crazygaze/rpc/RPCClient.h"

namespace cz
{
namespace rpc
{

Channel::Channel()
{
}

int Channel::hasFullRPC(const ChunkBuffer& in)
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

void Channel::onReceivedData(const ChunkBuffer& in)
{
	if (m_owner)
		m_owner->onReceivedData(in);
}

void Channel::onDisconnected()
{
	if (m_owner)
		m_owner->onDisconnected();
}

void Channel::setOwner(BaseClient* owner)
{
	m_owner = owner;
}

void ServerChannel::setOnNewClient(std::function<void(std::unique_ptr<Channel>)> fn)
{
	m_onNewClient = std::move(fn);
}

void ServerChannel::setOnClientDisconnect(std::function<void(Channel*)> fn)
{
	m_onClientDisconnect = std::move(fn);
}

} // namespace rpc
} // namespace cz
