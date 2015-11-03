/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com

	purpose:

*********************************************************************/

#pragma once

#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/Any.h"
#include "crazygaze/rpc/RPCUtils.h"
#include "crazygaze/StringUtils.h"
#include "crazygaze/TypeTraits.h"

namespace cz
{
namespace rpc
{

//! Small utility struct to make it easier to work with the RPC headers
struct RPCHeader
{
	enum
	{
		kCounterBits = 22,
		kRPCIdBits = 8,
		kKeyMask = (1 << (kCounterBits + kRPCIdBits)) - 1
	};
	explicit RPCHeader(uint32_t v = 0)
	{
		static_assert(sizeof(*this) == sizeof(uint32_t), "Invalid size. Check the bitfields");
		all = v;
	}

	struct Bits
	{
		unsigned rpcid : kRPCIdBits;
		unsigned counter : kCounterBits;
		unsigned success : 1;
		unsigned isReply : 1;
	};

	uint32_t key() const { return all & kKeyMask; }
	bool isGenericRPC() const { return bits.rpcid == 0; }
	void setKey(uint32_t key) { all = (all & ~kKeyMask) | (key & kKeyMask); }
	union
	{
		Bits bits;
		uint32_t all;
	};
};

struct BaseRPCInfo
{
	BaseRPCInfo() : hasReturnValue(false) {}
	std::string name;
	int numParams = 0;  // Used for debugging only
	bool hasReturnValue : 1;
};

typedef std::function<void(RPCHeader, const BaseRPCInfo&, const std::string&)> ExceptionCallback;

struct ReplyStream
{
	ReplyStream(RPCHeader hdr, class Transport& transport, class BaseInProcessor& inrpc)
		: hdr(hdr), transport(transport), inrpc(inrpc)
	{
	}
	RPCHeader hdr;
	class Transport& transport;
	class BaseInProcessor& inrpc;
	template <typename T>
	void write(T r)
	{
		inrpc.processReply(*this, std::forward<T>(r));
	}
};

class BaseTable
{
  public:
	BaseTable() {}
	virtual ~BaseTable() {}
	bool isValid(uint32_t rpcid) const { return rpcid < m_rpcs.size(); }
  protected:
	std::vector<std::unique_ptr<BaseRPCInfo>> m_rpcs;
};

template <typename T>
class TableImpl : public BaseTable
{
  public:
	typedef T Type;

	struct Info : public BaseRPCInfo
	{
		// function called by the RPC server to execute the rpc
		std::function<void(Type&, const ChunkBuffer& in, ReplyStream& out)> dispatcher;
	};

	template <typename T>
	struct Dispatcher
	{
		template <typename OBJ, typename F, typename Tuple>
		static void dispatch(ReplyStream& out, OBJ& obj, F& f, const Tuple& params)
		{
			out.write(callmethod(obj, f, params));
		}
	};

	template <>
	struct Dispatcher<void>
	{
		template <typename OBJ, typename F, typename Tuple>
		static void dispatch(ReplyStream& out, OBJ& obj, F& f, const Tuple& params)
		{
			callmethod(obj, f, params);
		}
	};

	void registerGenericRPC(uint32_t rpcid, const char* name)
	{
		CZ_ASSERT(rpcid == m_rpcs.size());
		auto info = std::make_unique<Info>();
		info->name = name;
		info->hasReturnValue = true;
		info->dispatcher = [this](T& obj, const ChunkBuffer& in, ReplyStream& out)
		{
			std::string name;
			in >> name;
			for (auto&& info : m_rpcs)
			{
				Info* r = static_cast<Info*>(info.get());
				if (r->name == name)
				{
					r->dispatcher(obj, in, out);
					return;
				}
			}
			// Read and drop the parameters vector
			std::vector<cz::Any> a;
			in >> a;
			throw std::runtime_error(cz::formatString("Unknown RPC (%s)", name.c_str()));
		};
		m_rpcs.push_back(std::move(info));
	}

	template <class F>
	void registerRPC(uint32_t rpcid, const char* name, F f)
	{
		static_assert(ValidateRPCFuncSignature<F>::value,
					  "Invalid RPC function signature (Unsupported return type or parameter types)");

		CZ_ASSERT(rpcid == m_rpcs.size());
		auto info = std::make_unique<Info>();
		using Tuple = typename ParamTuple<decltype(f)>::type;
		info->name = name;
		info->numParams = std::tuple_size<Tuple>::value;
		info->hasReturnValue = !std::is_void<typename ResultOf<F>::type>::value;
		info->dispatcher = [f](T& obj, const ChunkBuffer& in, ReplyStream& out)
		{
			Tuple params;

			if (out.hdr.isGenericRPC())
			{
				std::vector<cz::Any> a;
				in >> a;
				if (!cz::Any::toTuple(a, params))
					throw std::runtime_error("Invalid parameter count or types");
			}
			else
			{
				in >> params;
			}

			typedef typename ResultOf<F>::type ReturnType;
			Dispatcher<ReturnType>::dispatch(out, obj, f, params);
		};

		m_rpcs.push_back(std::move(info));
	}

	const Info* get(uint32_t rpcid) const
	{
		CZ_ASSERT(isValid(rpcid));
		return static_cast<Info*>(m_rpcs[rpcid].get());
	}

  protected:
	/*
	void generateHash()
	{
		std::string str;
		for(auto&& r : m_rpcs)
		{
			str += r.name + "," + std::to_string(r.numParams) + "," + std::to_string(r.hasReturnValue) + " : ";
		}
	}
	*/
};

template <typename T>
class Table : public TableImpl<T>
{
	static_assert(sizeof(T) == 0, "You need to specialize for the type you need");
};

template <typename SERVER_INTERFACE>
struct Service
{
	static_assert(sizeof(SERVER_INTERFACE) == 0, "You need to specialize for the required Server interface");
};

#define DEFINE_RPC_SERVICE(SERVER_INTERFACE, CLIENT_INTERFACE)                 \
	namespace cz                                                               \
	{                                                                          \
	namespace rpc                                                              \
	{                                                                          \
	template <>                                                                \
	struct Service<SERVER_INTERFACE>                                           \
	{                                                                          \
		using ServerInterface = SERVER_INTERFACE;                              \
		using ClientInterface = CLIENT_INTERFACE;                              \
		using ClientConnection = Connection<ServerInterface, ClientInterface>; \
		using Server = Server<ServerInterface, ClientInterface>;               \
		using ServerConnection = Connection<ClientInterface, ServerInterface>; \
	};                                                                         \
	}                                                                          \
	}

}  // namespace rpc
}  // namespace cz
