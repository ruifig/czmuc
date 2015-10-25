/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

namespace cz
{
namespace rpc
{

//////////////////////////////////////////////////////////////////////////
//
// Decay types to be usable as RPC parameters
//
//////////////////////////////////////////////////////////////////////////
template<typename T>
struct Decay
{
	typedef typename std::decay<T>::type type;
};
template<typename T>
struct Decay<const T&>
{
	typedef typename std::decay<T>::type type;
};
template<>
struct Decay<const char*>
{
	typedef std::string type;
};
template<>
struct Decay<const char*&>
{
	typedef std::string type;
};
template<int N>
struct Decay<const char (&)[N]>
{
	typedef std::string type;
};
template<typename T>
struct Decay<T*>
{
	static_assert(sizeof(T) == 0, "RPC parameters can't be pointers");
	typedef typename std::decay<T>::type type;
};
template<typename T>
struct Decay<T&>
{
	//static_assert(sizeof(T) == 0, "RPC parameters can't be non-const refs");
	typedef typename std::decay<T>::type type;
};


//////////////////////////////////////////////////////////////////////////
//
//  Gives a tuple with the parameter types of the specified function
//
//////////////////////////////////////////////////////////////////////////
template<typename T>
struct ParamTuple
{
	static_assert(sizeof(T) == 0, "Not a function");
	typedef void type;
};

template<typename R, typename... Args>
struct ParamTuple<R(Args...)>
{
	typedef std::tuple<typename Decay<Args>::type...> type;
};

template<typename R, typename... Args>
struct ParamTuple<R(*)(Args...)>
{
	typedef std::tuple<typename Decay<Args>::type...> type;
};

template<typename C, typename R, typename... Args>
struct ParamTuple<R(C::*)(Args...)>
{
	typedef std::tuple<typename Decay<Args>::type...> type;
};

template<typename C, typename R, typename... Args>
struct ParamTuple<R(C::*)(Args...) const>
{
	typedef std::tuple<typename Decay<Args>::type...> type;
};


//////////////////////////////////////////////////////////////////////////
//
//	Validates an RPC function signature
//
//////////////////////////////////////////////////////////////////////////

namespace details
{
	template<typename T>
	struct ValidRPCParam : std::true_type
	{
	};
	template<typename T>
	struct ValidRPCParam<const T&> : std::true_type
	{
	};
	template<>
	struct ValidRPCParam<const char*> : std::true_type
	{
	};
	template<int N>
	struct ValidRPCParam<const char(&)[N]> : std::true_type
	{
	};
	template<typename T>
	struct ValidRPCParam<T*> : std::false_type
	{
		//static_assert(sizeof(T) == 0, "RPC parameters or return type can't be pointers");
	};
	template<typename T>
	struct ValidRPCParam<T&> : std::false_type
	{
		// non-const refs are not allowed, since they normally mean the function will change those parameters
		//static_assert(sizeof(T) == 0, "RPC parameters or return type can't be non-const refs");
	};

	template<typename First, typename... Rest>
	struct ValidateRPCFuncParams
	{
		static constexpr bool value = ValidRPCParam<First>::value && ValidateRPCFuncParams<Rest...>::value;
	};

	template<typename First>
	struct ValidateRPCFuncParams<First>
	{
		static constexpr bool value = ValidRPCParam<First>::value;
	};

	template<typename T>
	struct ValidateRPCFuncSignatureImpl
	{
		static constexpr bool value = false;
	};

	template<typename C, typename R>
	struct ValidateRPCFuncSignatureImpl<R (C::*)()>
	{
		static constexpr bool value = ValidRPCParam<R>::value && true;
	};
	template<typename C, typename R>
	struct ValidateRPCFuncSignatureImpl<R (C::*)() const>
	{
		static constexpr bool value = ValidRPCParam<R>::value && true;
	};
	template<typename C, typename R, typename... Args>
	struct ValidateRPCFuncSignatureImpl<R (C::*)(Args...)>
	{
		static constexpr bool value = ValidRPCParam<R>::value && ValidateRPCFuncParams<Args...>::value;
	};

	template<typename C, typename R, typename... Args>
	struct ValidateRPCFuncSignatureImpl<R (C::*)(Args...) const>
	{
		static constexpr bool value = ValidRPCParam<R>::value && ValidateRPCFuncParams<Args...>::value;
	};
}

template<typename F>
struct ValidateRPCFuncSignature
{
	static constexpr bool value = details::ValidateRPCFuncSignatureImpl<F>::value;
};

} // namespace rpc
} // namespace cz
