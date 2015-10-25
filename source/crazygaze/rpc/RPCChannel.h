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

class BaseClient;

class Channel
{
public:
	explicit Channel();
	virtual ~Channel() {}
	virtual ChunkBuffer prepareSend() = 0;
	virtual bool send(ChunkBuffer&& data) = 0;
	virtual const std::string& getCustomID() const = 0;
	static int hasFullRPC(const ChunkBuffer& in);
	void onReceivedData(const ChunkBuffer& in);
	void onDisconnected();
protected:
	friend class BaseClient;
	void setOwner(BaseClient* owner);
private:
	BaseClient* m_owner = nullptr;
};


class ServerChannel
{
public:
	ServerChannel() {}
	virtual ~ServerChannel() {}
	virtual int getNumClients() = 0;
	void setOnNewClient(std::function<void(std::unique_ptr<Channel>)> fn);
	void setOnClientDisconnect(std::function<void(Channel*)> fn);
protected:
	std::function<void(std::unique_ptr<Channel>)> m_onNewClient;
	std::function<void(Channel*)> m_onClientDisconnect;
};

} // namespace rpc
} // namespace cz


