/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czlib.h"

namespace cz
{

// Taken from http://en.cppreference.com/w/cpp/types/decay
template <typename T, typename U>
struct decay_equiv :
	std::is_same<typename std::decay<T>::type, U>::type
{};



//////////////////////////////////////////////////////////////////////////
//
// Gives the return type of a function
//
// #TODO : See if I can replace the use of this by std::result_of
//////////////////////////////////////////////////////////////////////////
template<typename T>
struct ResultOf
{
	static_assert(sizeof(T) == 0, "Not a function");
};
template<typename R, typename... Args>
struct ResultOf<R (Args...)>
{
	typedef R type;
};
template<typename R, typename... Args>
struct ResultOf<R (*)(Args...)>
{
	typedef R type;
};
template<typename R, typename C, typename... Args>
struct ResultOf<R (C::*)(Args...)>
{
	typedef R type;
};
template<typename R, typename C, typename... Args>
struct ResultOf<R (C::*)(Args...) const>
{
	typedef R type;
};

//////////////////////////////////////////////////////////////////////////
//
// Gives the class of a method
//
//////////////////////////////////////////////////////////////////////////
template<typename T>
struct ClassOfMethod
{
	static_assert(sizeof(T) == 0, "Not a member function");
};

template<class R, class C, class... Args>
struct ClassOfMethod<R (C::*) (Args...)>
{
	typedef C type;
};

template<class R, class C, class... Args>
struct ClassOfMethod<R (C::*) (Args...) const>
{
	typedef C type;
};

//////////////////////////////////////////////////////////////////////////
//
// Calls a function, unpacking the parameters from a tuple
//
//////////////////////////////////////////////////////////////////////////
// Based on: http://stackoverflow.com/questions/10766112/c11-i-can-go-from-multiple-args-to-tuple-but-can-i-go-from-tuple-to-multiple
#ifndef __clcpp_parse__
namespace detail
{
	template <typename F, typename Tuple, bool Done, int Total, int... N>
	struct call_impl
	{
		static decltype(auto) call(F f, Tuple && t)
		{
			return call_impl<F, Tuple, Total == 1 + sizeof...(N), Total, N..., sizeof...(N)>::call(f, std::forward<Tuple>(t));
		}
	};

	template <typename F, typename Tuple, int Total, int... N>
	struct call_impl<F, Tuple, true, Total, N...>
	{
		//
		// This converts std::string to const char*, so tuple elements of std::string can be used where the
		// required parameters are of type "const char*"
		template<typename T>
		static const T& convParam(const T& p)
		{
			return p;
		}
		static const char* convParam(const std::string& p)
		{
			return p.c_str();
		}

		static decltype(auto) call(F f, Tuple && t)
		{
			return f(convParam(std::get<N>(std::forward<Tuple>(t)))...);
		}
	};
}

// user invokes this
template <typename F, typename Tuple>
decltype(auto) call(F f, Tuple && t)
{
	typedef typename std::decay<Tuple>::type ttype;
	return detail::call_impl<F, Tuple, 0 == std::tuple_size<ttype>::value, std::tuple_size<ttype>::value>::call(f, std::forward<Tuple>(t));
}
#else
template <typename F, typename Tuple>
int call(F f, Tuple && t);
#endif

//////////////////////////////////////////////////////////////////////////
//
// Calls a method on the specified object, unpacking the parameters from a tuple
//
//////////////////////////////////////////////////////////////////////////
// Based on: http://stackoverflow.com/questions/10766112/c11-i-can-go-from-multiple-args-to-tuple-but-can-i-go-from-tuple-to-multiple
#ifndef __clcpp_parse__
namespace detail
{
	template <typename F, typename Tuple, bool Done, int Total, int... N>
	struct callmethod_impl
	{
		static decltype(auto) call(typename ClassOfMethod<F>::type& obj, F f, Tuple && t)
		{
			return callmethod_impl<F, Tuple, Total == 1 + sizeof...(N), Total, N..., sizeof...(N)>::call(obj, f, std::forward<Tuple>(t));
		}
	};

	template <typename F, typename Tuple, int Total, int... N>
	struct callmethod_impl<F, Tuple, true, Total, N...>
	{

		//
		// This converts std::string to const char*, so tuple elements of std::string can be used where the
		// required parameters are of type "const char*"
		template<typename T>
		static const T& convParam(const T& p)
		{
			return p;
		}
		static const char* convParam(const std::string& p)
		{
			return p.c_str();
		}

		static decltype(auto) call(typename ClassOfMethod<F>::type& obj, F f, Tuple && t)
		{
			return (obj.*f)(convParam(std::get<N>(std::forward<Tuple>(t)))...);
		}
	};
}

// user invokes this
template <typename F, typename Tuple>
decltype(auto) callmethod(typename ClassOfMethod<F>::type& obj, F f, Tuple && t)
{
	typedef typename std::decay<Tuple>::type ttype;
	return detail::callmethod_impl<F, Tuple, 0 == std::tuple_size<ttype>::value, std::tuple_size<ttype>::value>::call(obj, f, std::forward<Tuple>(t));
}

#else
template <typename F, typename Tuple>
int callmethod(typename ClassOfMethod<F>::type& obj, F f, Tuple && t);
#endif

//////////////////////////////////////////////////////////////////////////
//
// Custom std::future::then implementation
//
//////////////////////////////////////////////////////////////////////////
// From http://stackoverflow.com/questions/16296284/workaround-for-blocking-async
// futures returned by std::async will block in the destructor.
// This means that even for an async that doesn't return anything (e.g: std::future<void>), it will block
// This version of async creates a non-blocking future.
template< class Function, class... Args>
std::future<typename std::result_of<Function(Args...)>::type> async_nonblocking(Function&& f, Args&&... args)
{
	typedef typename std::result_of<Function(Args...)>::type R;
	auto bound_task = std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
	std::packaged_task<R()> task(std::move(bound_task));
	auto ret = task.get_future();
	std::thread t(std::move(task));
	t.detach();
	return ret;
}

#ifndef __clcpp_parse__
template <typename Fut, typename Work>
decltype(auto) then(Fut f, Work w)
{
	auto fptr = std::make_shared<std::decay<Fut>::type>(std::move(f));
	return std::async([fptr = std::move(fptr), w = std::move(w)]{ return w(fptr->get()); });
}

template <typename Fut, typename Work>
decltype(auto) then_nonblocking(Fut f, Work w)
{
	auto fptr = std::make_shared<std::decay<Fut>::type>(std::move(f));
	return async_nonblocking([fptr = std::move(fptr), w = std::move(w)]{ return w(fptr->get()); });
}
#else
template <typename Fut, typename Work>
std::future<void> then(Fut f, Work w);

template <typename Fut, typename Work>
std::future<void> then_nonblocking(Fut f, Work w);
#endif

} // namespace cz
