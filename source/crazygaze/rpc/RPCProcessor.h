/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/rpc/RPCChannel.h"
#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/Semaphore.h"

namespace cz
{
namespace rpc
{

class BaseOutRPCProcessor
{
public:
	BaseOutRPCProcessor();
	virtual ~BaseOutRPCProcessor();

	void processReceivedReply(RPCHeader hdr, const ChunkBuffer& in);
	void shutdown();

	void setExceptionCallback(ExceptionCallback func) {
		m_exceptionCallback = std::move(func);
	}

	virtual std::future<cz::Any> _callgenericrpc(Channel& channel, const char* func, const std::vector<cz::Any>& params) = 0;

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
		BaseOutRPCProcessor& outer;
		std::shared_ptr<std::promise<ResultType>> pr;
		RPCHeader hdr;
		ReplyInfo(BaseOutRPCProcessor& outer, RPCHeader hdr) : outer(outer), hdr(hdr)
		{
			pr = std::make_shared<std::promise<ResultType>>();

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
		std::future<ResultType> error()
		{
			auto ft = pr->get_future();
			{
				std::unique_lock<std::mutex> lk(outer.m_mtx);
				outer.m_replies.erase(hdr.key());
			}
			return ft;
		}
		std::future<ResultType> success()
		{
			return pr->get_future();
		}
	};

	template<>
	struct ReplyInfo<void>
	{
		BaseOutRPCProcessor& outer;
		std::promise<void> pr;
		RPCHeader hdr;
		ReplyInfo(BaseOutRPCProcessor& outer, RPCHeader hdr)
			: outer(outer), hdr(hdr)
		{
		}
		std::future<void> error()
		{
			std::string err("RPC Call failed");
			outer.m_exceptionCallback(hdr, *outer.getRPCInfo(hdr), err);
			pr.set_exception(std::make_exception_ptr(std::runtime_error(err)));
			return pr.get_future();
		}
		std::future<void> success()
		{
			pr.set_value();
			return pr.get_future();
		}
	};

	template<typename T>
	struct RemoveFuture
	{
		typedef typename T type;
	};
	template<typename T>
	struct RemoveFuture<std::future<T>>
	{
		typedef typename T type;
	};

	template<class F, typename... Args>
	auto _callrpcImpl(Channel& channel, uint32_t rpcid, F f, Args&&... args)
	{
		typedef std::tuple<typename Decay<Args>::type...> ArgsTuple;
		typedef ParamTuple<decltype(f)>::type FuncParamsTuple;
		typedef RemoveFuture<Decay<ResultOf<F>::type>::type>::type ResultType;
		static_assert(
			std::is_same<FuncParamsTuple, ArgsTuple>::value,
			"RPC and specified parameters mismatch.");

		ChunkBuffer out = channel.prepareSend();

		RPCHeader hdr;
		hdr.bits.counter = ++m_replyIdCounter;
		hdr.bits.rpcid = rpcid;
		ReplyInfo<ResultType> replyInfo(*this, hdr);
		out << hdr.all;
		serializeParameterPack(out, std::forward<Args>(args)...);
		// If the send fails, we need to remove the reply from the reply map
		if (channel.send(std::move(out)))
			return replyInfo.success();
		else
			return replyInfo.error();
	}

	uint32_t m_replyIdCounter = 0;
	std::mutex m_mtx;
	std::unordered_map<uint32_t, std::function<void(const ChunkBuffer&, RPCHeader)>> m_replies;
	ExceptionCallback m_exceptionCallback = [](RPCHeader, const BaseRPCInfo&, const std::string&) {};
};

template<typename T>
class OutRPCProcessor : public BaseOutRPCProcessor
{
public:
	typedef T Type;
	OutRPCProcessor()
	{
	}

	template<class F, typename... Args>
	auto _callrpc(Channel& channel, uint32_t rpcid, F f, Args&&... args)
	{
		static_assert(
			std::is_member_function_pointer<decltype(f)>::value &&
			std::is_base_of<ClassOfMethod<F>::type, Type>::value,
			"Not a member function of the wrapped class");
		CZ_ASSERT(m_tbl.isValid(rpcid));
		return _callrpcImpl(channel, rpcid, std::forward<F>(f), std::forward<Args>(args)...);
	}

protected:

	virtual std::future<cz::Any> _callgenericrpc(Channel& channel, const char* func, const std::vector<cz::Any>& params) override
	{
		return _callrpcImpl(channel, uint32_t(decltype(m_tbl)::RPCId::genericRPC), &_genericRPCDummy, func, params);
	}

	virtual const BaseRPCInfo* getRPCInfo(RPCHeader hdr) override
	{
		return m_tbl.get(hdr.bits.rpcid);
	}

	Table<Type> m_tbl;
};

// Template specialization for void, so we don't try to use it
template<>
class OutRPCProcessor<void> : public BaseOutRPCProcessor
{
public:
	void shutdown()
	{
	}
	void processReceivedReply(RPCHeader hdr, const ChunkBuffer& in)
	{
		CZ_ASSERT(0 && "No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
	}

protected:
	virtual std::future<cz::Any> _callgenericrpc(Channel& channel, const char* func, const std::vector<cz::Any>& params) override
	{
		CZ_ASSERT(0 && "No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
		return std::future<cz::Any>();
	}
	virtual const BaseRPCInfo* getRPCInfo(RPCHeader hdr) override
	{
		CZ_ASSERT(0 && "No type specified to receive RPC replies");
		throw std::logic_error("No type specified to receive RPC replies");
		return nullptr;
	}
};

class BaseInRPCProcessor
{
public:
	~BaseInRPCProcessor()
	{
		m_futureRepliesCount.wait();
	}


	template<typename T>
	void processReply(const ReplyStream& reply, T r)
	{
		ChunkBuffer out = reply.channel.prepareSend();
		RPCHeader hdr;
		hdr.bits.isReply = true;
		hdr.bits.success = true;
		hdr.setKey(reply.hdr.key());
		out << hdr.all;
		if (reply.hdr.isGenericRPC())
			out << cz::Any(std::forward<T>(r));
		else
			out << std::forward<T>(r);
		auto res = reply.channel.send(std::move(out));
	}

	template<typename T>
	void processReply(const ReplyStream& reply, std::future<T> r)
	{
		m_futureRepliesCount.increment();

		std::unique_lock<std::mutex> lk(m_mtx);
		// Clear up any finished futures
		m_futureRepliesDone.clear();

		m_futureReplies[reply.hdr.key()] = then(std::move(r), [this, reply](T v)
		{
			processReply(reply, std::forward<T>(v));
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				auto it = m_futureReplies.find(reply.hdr.key());
				m_futureRepliesDone[reply.hdr.key()] = std::move(it->second);
				m_futureReplies.erase(it);
			}
			m_futureRepliesCount.decrement();
		});
	}

	virtual void dispatchReceived(uint32_t rpcid, const ChunkBuffer& in, ReplyStream& replyStream) = 0;

	void processReceivedRPC(Channel& channel, RPCHeader hdr, const ChunkBuffer& in)
	{
		try
		{
			ReplyStream replyStream(hdr, channel, *this);
			dispatchReceived(hdr.bits.rpcid, in, replyStream);
		}
		catch (std::exception& e)
		{
			ChunkBuffer out = channel.prepareSend();
			RPCHeader outhdr(0);
			outhdr.bits.isReply = true;
			outhdr.bits.success = false;
			outhdr.setKey(hdr.key());
			out << outhdr.all;
			out << e.what();
			auto res = channel.send(std::move(out));
			CZ_ASSERT(res);
		}
	}


protected:

	std::mutex m_mtx;
	std::unordered_map<uint32_t, std::future<void>> m_futureReplies;
	//! This is used to put in future replies that completed.
	// It's necessary, since deleting the futures from inside the continuation itself will result in a deadlock because
	// a future's destructor will block until its value is set.
	std::unordered_map<uint32_t, std::future<void>> m_futureRepliesDone;

	cz::ZeroSemaphore m_futureRepliesCount;
};

template<typename T>
class InRPCProcessor : public BaseInRPCProcessor
{
public:
	typedef T Type;

	explicit InRPCProcessor(Type& obj) : m_obj(obj) { }

	InRPCProcessor(InRPCProcessor&& other)
		: m_obj(std::move(other.m_obj))
		, m_tbl(std::move(other.m_tbl))
	{
	}

	~InRPCProcessor()
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
class InRPCProcessor<void> : public BaseInRPCProcessor
{
public:
	void processReceivedRPC(Channel& channel, RPCHeader hdr, const ChunkBuffer& in)
	{
		CZ_ASSERT(0 && "No local type specified to receive RPC calls");
		throw std::logic_error("No local type specified to receive RPC calls");
	}
private:
	virtual void dispatchReceived(uint32_t rpcid, const ChunkBuffer& in, ReplyStream& replyStream) override { }
};

} // namespace rpc
} // namespace cz

