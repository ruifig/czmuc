/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/ScopeGuard.h"
#include "crazygaze/rpc/RPCBase.h"
#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/rpc/RPCProcessor.h"

namespace cz
{
namespace rpc
{

class BaseConnection;
struct ConnectionUserData
{
	virtual ~ConnectionUserData() { }
	BaseConnection* con = nullptr;
};

class BaseConnection
{
public:
	explicit BaseConnection(std::unique_ptr<Transport> transport)
		: m_transport(std::move(transport))
	{
		m_transport->setOwner(this);
		m_connected = true;
	}
	virtual ~BaseConnection() {}
	virtual void onReceivedData(const ChunkBuffer& in) = 0;
	virtual void onDisconnected() = 0;

	const Transport* getTransport() const
	{
		return m_transport.get();
	}

	bool isConnected() const
	{
		return m_connected;
	}

	void setUserData(std::shared_ptr<ConnectionUserData> userData)
	{
		m_userData = std::move(userData);
		m_userData->con = this;
	}

	const std::shared_ptr<ConnectionUserData>& getUserData()
	{
		return m_userData;
	}

	void setExceptionCallback(ExceptionCallback func)
	{
		getOutProcessor()->setExceptionCallback(std::move(func));
	}

	void setOnDisconnected(std::function<void()> func)
	{
		m_onDisconnected = std::move(func);
	}

#ifndef __clcpp_parse__
	auto _callgenericrpc(const char* func, const std::vector<cz::Any>& params)
	{
		return getOutProcessor()->_callgenericrpc(*m_transport.get(), func, params);
	}
#else
	std::future<cz::Any> _callgenericrpc(const char* func, const std::vector<cz::Any>& params);
#endif

protected:

	virtual BaseOutProcessor* getOutProcessor() = 0;
	virtual BaseInProcessor* getInProcessor() = 0;

	std::unique_ptr<Transport> m_transport;
	std::shared_ptr<ConnectionUserData> m_userData;
	std::function<void()> m_onDisconnected;
	bool m_connected = false;
};

template<typename RemoteType_, typename LocalType_=void>
class Connection : public BaseConnection
{
public:
	typedef RemoteType_ RemoteType;
	typedef LocalType_ LocalType;
	explicit Connection(std::unique_ptr<Transport> transport)
	    : BaseConnection(std::move(transport))
	{
		static_assert(std::is_void<LocalType>::value,
		              "Need to specify a local object for this type of Connection.Use the other constructor");
	}

	template<typename U=LocalType_>
	Connection(std::unique_ptr<Transport> transport, typename std::enable_if<!std::is_void<U>::value,U>::type & obj)
		: BaseConnection(std::move(transport))
		, m_inProcessor(obj)
	{
	}

	Connection(Connection&& other)
		: BaseConnection(std::move(transport))
		, m_inProcessor(std::move(other.m_inProcessor))
		, m_outProcessor(std::move(other.m_outProcessor))
	{
	}

	virtual ~Connection()
	{
		// Release the transport first, before destroying the PRC Processors, otherwise will can still get data after
		// all this is destroyed
		m_transport.reset();
	}


#ifndef __clcpp_parse__
	template<class F, typename... Args>
	auto _callrpc(uint32_t rpcid, F f, Args&&... args)
	{
		return m_outProcessor._callrpc(*m_transport.get(), rpcid, std::forward<F>(f), std::forward<Args>(args)...);
	}
#else
	template<class F, typename... Args>
	auto _callrpc(uint32_t rpcid, F f, Args&&... args) -> std::future<decltype(f(std::forward<Args>(args)...))>;
#endif

	static Connection* getCurrent()
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
			m_inProcessor.processReceivedRPC(*m_transport.get(), hdr, in);
	}

	virtual void onDisconnected() override
	{
		m_connected = false;
		if (m_onDisconnected)
			m_onDisconnected();
		m_outProcessor.shutdown();
	}

	virtual BaseOutProcessor* getOutProcessor() override
	{
		return &m_outProcessor;
	}
	virtual BaseInProcessor* getInProcessor() override
	{
		return &m_inProcessor;
	}

	OutProcessor<RemoteType> m_outProcessor;
	InProcessor<LocalType> m_inProcessor;
	static thread_local Connection* ms_current;
};

template<typename RemoteType_, typename LocalType_>
thread_local Connection<RemoteType_, LocalType_>* Connection<RemoteType_, LocalType_>::ms_current;

#define CALLRPC(client, func, ...)                                                                           \
	(client)._callrpc((uint32_t)cz::rpc::Table<std::decay<decltype(client)>::type::RemoteType>::RPCId::func, \
	                  &std::decay<decltype(client)>::type::RemoteType::func, __VA_ARGS__)

#define CALLGENERICRPC(client, func, params) \
	(client)._callgenericrpc(func, params)

} // namespace rpc
} // namespace cz


