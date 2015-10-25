/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCBase.h"
#include "crazygaze/rpc/RPCChannel.h"
#include "crazygaze/rpc/RPCProcessor.h"

namespace cz
{
namespace rpc
{

struct ClientUserData
{
	virtual ~ClientUserData() { }
};

class BaseClient
{
public:
	explicit BaseClient(std::unique_ptr<Channel> channel)
		: m_channel(std::move(channel))
	{
		m_channel->setOwner(this);
	}
	virtual ~BaseClient() {}
	virtual void onReceivedData(const ChunkBuffer& in) = 0;
	virtual void onDisconnected() = 0;

	const Channel* getChannel() const
	{
		return m_channel.get();
	}

	void setUserData(std::shared_ptr<ClientUserData> userData)
	{
		m_userData = std::move(userData);
	}

	const std::shared_ptr<ClientUserData>& getUserData()
	{
		return m_userData;
	}

	void setExceptionCallback(ExceptionCallback func)
	{
		getOutProcessor()->setExceptionCallback(std::move(func));
	}

	auto _callgenericrpc(const char* func, const std::vector<cz::Any>& params)
	{
		return getOutProcessor()->_callgenericrpc(*m_channel.get(), func, params);
	}

protected:

	virtual BaseOutRPCProcessor* getOutProcessor() = 0;
	virtual BaseInRPCProcessor* getInProcessor() = 0;

	std::unique_ptr<Channel> m_channel;
	std::shared_ptr<ClientUserData> m_userData;
};

template<typename RemoteType_, typename LocalType_=void>
class Client : public BaseClient
{
public:
	typedef RemoteType_ RemoteType;
	typedef LocalType_ LocalType;
	explicit Client(std::unique_ptr<Channel> channel)
		: BaseClient(std::move(channel))
	{
	}

	template<typename U=LocalType_>
	Client(std::unique_ptr<Channel> channel, typename std::enable_if<!std::is_void<U>::value,U>::type & obj)
		: BaseClient(std::move(channel))
		, m_inProcessor(obj)
	{
	}

	Client(Client&& other)
		: BaseClient(std::move(channel))
		, m_inProcessor(std::move(other.m_inProcessor))
		, m_outProcessor(std::move(other.m_outProcessor))
	{
	}

	virtual ~Client()
	{
		// Release the channel first, before destroying the PRC Processors, otherwise will can still get data after
		// all this is destroyed
		m_channel.reset();
	}

	template<class F, typename... Args>
	auto _callrpc(uint32_t rpcid, F f, Args&&... args)
	{
		return m_outProcessor._callrpc(*m_channel.get(), rpcid, std::forward<F>(f), std::forward<Args>(args)...);
	}

	static Client* getCurrent()
	{
		return ms_current;
	}

protected:

	virtual void onReceivedData(const ChunkBuffer& in) override
	{
		RPCHeader hdr;
		in >> hdr.all;

		ms_current = this;
		auto guard = cz::scopeGuard([&] { ms_current = nullptr;});

		if (hdr.bits.isReply)
			m_outProcessor.processReceivedReply(hdr, in);
		else
			m_inProcessor.processReceivedRPC(*m_channel.get(), hdr, in);
	}

	virtual void onDisconnected() override
	{
		m_outProcessor.shutdown();
	}

	virtual BaseOutRPCProcessor* getOutProcessor() override
	{
		return &m_outProcessor;
	}
	virtual BaseInRPCProcessor* getInProcessor() override
	{
		return &m_inProcessor;
	}

	OutRPCProcessor<RemoteType> m_outProcessor;
	InRPCProcessor<LocalType> m_inProcessor;
	static thread_local Client* ms_current;
};

template<typename RemoteType_, typename LocalType_=void>
thread_local Client<RemoteType_, LocalType_>* Client<RemoteType_, LocalType_>::ms_current;

#define CALLRPC(client, func, ...) \
	(client)._callrpc((uint32_t)Table<std::decay<decltype(client)>::type::RemoteType>::RPCId::func, &std::decay<decltype(client)>::type::RemoteType::func, __VA_ARGS__)

#define CALLGENERICRPC(client, func, params) \
	(client)._callgenericrpc(func, params)

} // namespace rpc
} // namespace cz


