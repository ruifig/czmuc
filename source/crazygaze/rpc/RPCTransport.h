/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCBase.h"
#include "crazygaze/SharedQueue.h"
#include "crazygaze/ChunkBuffer.h"

namespace cz
{
namespace rpc
{

typedef cz::SharedQueue<std::function<void()>> WorkQueue;

class BaseConnection;

class Transport
{
public:
	explicit Transport();
	virtual ~Transport() {}
	virtual ChunkBuffer prepareSend() = 0;
	virtual bool send(ChunkBuffer&& data) = 0;
	virtual const std::string& getCustomID() const = 0;
	static int hasFullRPC(const ChunkBuffer& in);
	void onReceivedData(const ChunkBuffer& in);
	void onDisconnected();
protected:
	friend class BaseConnection;
	void setOwner(BaseConnection* owner);
private:
	BaseConnection* m_owner = nullptr;
};


class ServerTransport
{
public:
	ServerTransport() {}
	virtual ~ServerTransport() {}
	virtual int getNumClients() = 0;
	void setOnNewClient(std::function<void(std::unique_ptr<Transport>)> fn);
	void setOnClientDisconnect(std::function<void(Transport*)> fn);
protected:
	std::function<void(std::unique_ptr<Transport>)> m_onNewClient;
	std::function<void(Transport*)> m_onClientDisconnect;
};

} // namespace rpc
} // namespace cz


