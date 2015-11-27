/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCTransport.h"
#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/Semaphore.h"
#include "crazygaze/Any.h"
#include "crazygaze/Future.h"

namespace cz
{
namespace rpc
{

class BaseOutProcessor
{
public:
	BaseOutProcessor();
	virtual ~BaseOutProcessor();

	void processReceivedReply(RPCHeader hdr, const ChunkBuffer& in);
	void shutdown();

	void setExceptionCallback(ExceptionCallback func) {
		m_exceptionCallback = std::move(func);
	}

	virtual Future<cz::Any> _callgenericrpc(Transport& transport, const char* func, const std::vector<cz::Any>& params) = 0;

protected:
	virtual const BaseRPCInfo* getRPCInfo(RPCHeader hdr) = 0;

	// This is necessary just so that we have a function with this signature, to make it easier to code support
	// for console commands.
	static cz::Any _genericRPCDummy(const char* func, const std::vector<cz::Any>& params)
	{
		return cz::Any(false);
	}

	template<typename U>
	friend struct ReplyInfo;

	template<typename ResultType>
	struct ReplyInfo
	{
		BaseOutProcessor& outer;
		std::shared_ptr<Promise<ResultType>> pr;
		RPCHeader hdr;
		ReplyInfo(BaseOutProcessor& outer, RPCHeader hdr) : outer(outer), hdr(hdr)
		{
			pr = std::make_shared<Promise<ResultType>>();

			std::unique_lock<std::mutex> lk(outer.m_mtx);
			// NOTE: Capturing outer as a pointer, so it doesn't get copied into the lambda.
			outer.m_replies[hdr.key()] = [outer{&outer}, pr{ pr }](const ChunkBuffer& in, RPCHeader hdr)
			{
				if (hdr.bits.success)
				{
					ResultType ret;
					in >> ret;
					pr->set_value(std::move(ret));
				}
				else
				{
					std::string err;
					in >> err;
					outer->m_exceptionCallback(hdr, *outer->getRPCInfo(hdr), err);
					pr->set_exception(std::make_exception_ptr(std::runtime_error(err)));
				}
			};
		}
		Future<ResultType> error()
		{
			auto ft = pr->get_future();
			{
				std::unique_lock<std::mutex> lk(outer.m_mtx);
				outer.m_replies.erase(hdr.key());
			}
			return ft;
		}
		Future<ResultType> success()
		{
			return pr->get_future();
		}
	};

	template<>
	struct ReplyInfo<void>
	{
		BaseOutProcessor& outer;
		Promise<void> pr;
		RPCHeader hdr;
		ReplyInfo(BaseOutProcessor& outer, RPCHeader hdr)
			: outer(outer), hdr(hdr)
		{
		}
		Future<void> error()
		{
			std::string err("RPC Call failed");
			outer.m_exceptionCallback(hdr, *outer.getRPCInfo(hdr), err);
			pr.set_exception(std::make_exception_ptr(std::runtime_error(err)));
			return pr.get_future();
		}
		Future<void> success()
		{
			pr.set_value();
			return pr.get_future();
		}
	};

	template<typename T>
	struct RemoveFuture
	{
		typedef T type;
	};
	template<typename T>
	struct RemoveFuture<Future<T>>
	{
		typedef T type;
	};

#ifndef __clcpp_parse__
	template<class F, typename... Args>
	auto _callrpcImpl(Transport& transport, uint32_t rpcid, F f, Args&&... args)
	{
		typedef std::tuple<typename Decay<Args>::type...> ArgsTuple;
		typedef ParamTuple<decltype(f)>::type FuncParamsTuple;
		typedef RemoveFuture<Decay<ResultOf<F>::type>::type>::type ResultType;
		static_assert(
			std::is_same<FuncParamsTuple, ArgsTuple>::value,
			"RPC and specified parameters mismatch.");

		ChunkBuffer out = transport.prepareSend();

		RPCHeader hdr;
		hdr.bits.counter = ++m_replyIdCounter;
		hdr.bits.rpcid = rpcid;
		ReplyInfo<ResultType> replyInfo(*this, hdr);
		out << hdr.all;
		serializeParameterPack(out, std::forward<Args>(args)...);
		// If the send fails, we need to remove the reply from the reply map
		if (transport.send(std::move(out)))
			return replyInfo.success();
		else
			return replyInfo.error();
	}
#else
	template<class F, typename... Args>
	auto _callrpcImpl(Transport& transport, uint32_t rpcid, F f, Args&&... args) -> Future<decltype(f(std::forward<Args>(args)...))>;
#endif

	uint32_t m_replyIdCounter = 0;
	std::mutex m_mtx;
	std::unordered_map<uint32_t, std::function<void(const ChunkBuffer&, RPCHeader)>> m_replies;
	ExceptionCallback m_exceptionCallback = [](RPCHeader, const BaseRPCInfo&, const std::string&) {};
};

template<typename T>
class OutProcessor : public BaseOutProcessor
{
public:
	typedef T Type;
	OutProcessor()
	{
	}


#ifndef __clcpp_parse__
	template<class F, typename... Args>
	auto _callrpc(Transport& transport, uint32_t rpcid, F f, Args&&... args)
	{
		static_assert(
			std::is_member_function_pointer<decltype(f)>::value &&
			std::is_base_of<ClassOfMethod<F>::type, Type>::value,
			"Not a member function of the wrapped class");
		CZ_ASSERT(m_tbl.isValid(rpcid));
		return _callrpcImpl(transport, rpcid, std::forward<F>(f), std::forward<Args>(args)...);
	}
#else
	template<class F, typename... Args>
	auto _callrpc(Transport& transport, uint32_t rpcid, F f, Args&&... args) 
				-> Future<decltype(f(std::forward<Args>(args)...))>;
#endif

protected:

	virtual Future<cz::Any> _callgenericrpc(Transport& transport, const char* func, const std::vector<cz::Any>& params) override
	{
		return _callrpcImpl(transport, uint32_t(decltype(m_tbl)::RPCId::genericRPC), &_genericRPCDummy, func, params);
	}

	virtual const BaseRPCInfo* getRPCInfo(RPCHeader hdr) override
	{
		return m_tbl.get(hdr.bits.rpcid);
	}

	Table<Type> m_tbl;
};

// Template specialization for void, so we don't try to use it
template<>
class OutProcessor<void> : public BaseOutProcessor
{
public:
	void shutdown()
	{
	}
	void processReceivedReply(RPCHeader hdr, const ChunkBuffer& in)
	{
		CZ_UNEXPECTED_F("No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
	}

protected:
#pragma warning(push)
// warning C4702: unreachable code
#pragma warning(disable:4702)
	virtual Future<cz::Any> _callgenericrpc(Transport& transport, const char* func, const std::vector<cz::Any>& params) override
	{
		CZ_UNEXPECTED_F("No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
		return Future<cz::Any>();
	}
#pragma warning(pop)

#pragma warning(push)
// warning C4702: unreachable code
#pragma warning(disable:4702)
	virtual const BaseRPCInfo* getRPCInfo(RPCHeader hdr) override
	{
		CZ_UNEXPECTED_F("No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
		// warning C4702: unreachable code
		#pragma warning( suppress : 4702 )
		return nullptr;
	}
#pragma warning(pop)
};

class BaseInProcessor
{
public:
	~BaseInProcessor()
	{
		m_futureRepliesCount.wait();
	}


	template<typename T>
	void processReply(const ReplyStream& reply, T r)
	{
		ChunkBuffer out = reply.transport.prepareSend();
		RPCHeader hdr;
		hdr.bits.isReply = true;
		hdr.bits.success = true;
		hdr.setKey(reply.hdr.key());
		out << hdr.all;
		if (reply.hdr.isGenericRPC())
			out << cz::Any(std::forward<T>(r));
		else
			out << std::forward<T>(r);
		reply.transport.send(std::move(out));
	}

	template<typename T>
	void processReply(const ReplyStream& reply, Future<T> r)
	{
		m_futureRepliesCount.increment();

		//
		// Setup the continuation without holding the lock, since if it executes in situ, it will mess up.
		// So we setup the continuation, and add the resulting future to the map later
		auto ft = r.then([this, reply](const Future<T>& r)
		{
			processReply(reply, r.get());
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				// Delay the future removal
				m_futureRepliesDone.push(reply.hdr.key());
			}
		});

		std::unique_lock<std::mutex> lk(m_mtx);
		m_futureReplies[reply.hdr.key()] = std::move(ft);
		// Clear up any finished futures
		// This is necessary, because the continuations can execute in situ, and will mess up because the mutex
		// is not recursive. Therefore, we delay the deletion of completed futures 
		while(m_futureRepliesDone.size())
		{
			uint32_t key = m_futureRepliesDone.front();
			m_futureRepliesDone.pop();
			auto it = m_futureReplies.find(key);
			m_futureReplies.erase(it);
			m_futureRepliesCount.decrement();
		}
	}

	virtual void dispatchReceived(uint32_t rpcid, const ChunkBuffer& in, ReplyStream& replyStream) = 0;

#ifndef __clcpp_parse__
	void processReceivedRPC(Transport& transport, RPCHeader hdr, const ChunkBuffer& in)
	{
		try
		{
			ReplyStream replyStream(hdr, transport, *this);
			dispatchReceived(hdr.bits.rpcid, in, replyStream);
		}
		catch (std::exception& e)
		{
			ChunkBuffer out = transport.prepareSend();
			RPCHeader outhdr(0);
			outhdr.bits.isReply = true;
			outhdr.bits.success = false;
			outhdr.setKey(hdr.key());
			out << outhdr.all;
			out << e.what();
			auto res = transport.send(std::move(out));
			CZ_ASSERT(res);
		}
	}
#else
	void processReceivedRPC(Transport& transport, RPCHeader hdr, const ChunkBuffer& in);
#endif


protected:

	std::mutex m_mtx;
	std::unordered_map<uint32_t, Future<void>> m_futureReplies;

	//! This is used to put in future replies that completed.
	// It's necessary, since deleting the futures from inside the continuation itself will result in a deadlock because
	// a future's destructor will block until its value is set.
	std::queue<uint32_t> m_futureRepliesDone;

	cz::ZeroSemaphore m_futureRepliesCount;
};

template<typename T>
class InProcessor : public BaseInProcessor
{
public:
	typedef T Type;

	explicit InProcessor(Type& obj) : m_obj(obj) { }

	InProcessor(InProcessor&& other)
		: m_obj(std::move(other.m_obj))
		, m_tbl(std::move(other.m_tbl))
	{
	}

	~InProcessor()
	{
	}

protected:
	virtual void dispatchReceived(uint32_t rpcid, const ChunkBuffer& in, ReplyStream& replyStream) override
	{
		auto&& info = m_tbl.get(rpcid);
		info->dispatcher(m_obj, in, replyStream);
	}

	Table<Type> m_tbl;
	Type& m_obj;
};

// Specialization so we don't use a void type
template<>
class InProcessor<void> : public BaseInProcessor
{
public:
	void processReceivedRPC(Transport& transport, RPCHeader hdr, const ChunkBuffer& in)
	{
		CZ_UNEXPECTED_F("No local type specified to receive RPC calls");
		throw std::logic_error("No local type specified to receive RPC calls");
	}
private:
	virtual void dispatchReceived(uint32_t rpcid, const ChunkBuffer& in, ReplyStream& replyStream) override { }
};

} // namespace rpc
} // namespace cz

