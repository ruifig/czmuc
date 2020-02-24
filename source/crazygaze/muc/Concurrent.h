/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Inspired by Herb Sutter's call at 
	https://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Herb-Sutter-Concurrency-and-Parallelism ,
	but with some changes.
	- Queued work can return values (you get a std::future<R>) when queuing work
	- A ConcurrentTicker<T> version allows for automatic calls to a tick function.
	- Allows T*, so we can have a base class
*********************************************************************/
#pragma once

#include "SharedQueue.h"
#include "Timer.h"
#include <future>

#ifdef max
	#undef max
#endif

namespace cz
{

template<typename T>
class ConcurrentBaseObjectWrapper
{
private:
protected:
	mutable T m_t;
	using Type = T;
	template<typename... Args>
	ConcurrentBaseObjectWrapper(Args&&... args) : m_t(std::forward<Args>(args)...) {}
	Type& obj() const { return m_t; }
};

template<typename T>
class ConcurrentBaseObjectWrapper<T*>
{
private:
	std::unique_ptr<mutable T> m_t;
protected:
	using Type = T;
	ConcurrentBaseObjectWrapper(std::unique_ptr<T> ptr) : m_t(std::move(ptr)) {}
	Type& obj() const { return *m_t; }
};

template<typename T>
class ConcurrentBase : protected ConcurrentBaseObjectWrapper<T>
{
protected:
	mutable WorkQueue m_q;
	std::thread m_th;
	bool m_done = false;

	// Inherit constructors
	using ConcurrentBaseObjectWrapper::ConcurrentBaseObjectWrapper;

	~ConcurrentBase()
	{
		m_q.push([this] { m_done = true;});
		m_th.join();
	}

public:
	WorkQueue& getQueue()
	{
		return m_q;
	}

	std::thread::id getThreadId() const
	{
		return m_th.get_id();
	}

#if 1
	template<typename F>
	auto operator()(F f) const -> std::future<decltype(f(obj()))>
	{
		auto pr = std::make_shared<std::promise<decltype(f(obj()))>>();
		auto ft = pr->get_future();
		m_q.push([pr = std::move(pr), f=std::move(f), this]() mutable
		{
			fulfillPromise(*pr, f, obj());
		});

		return ft;
	}
#else
	template<typename F>
	auto operator()(F f) const -> std::future<decltype(f(m_t))>
	{
		auto pr = std::make_shared<std::promise<decltype(f(m_t))>>();
		auto ft = pr->get_future();
		m_q.push([pr = std::move(pr), f=std::move(f), this]() mutable
		{
			fulfillPromise(*pr, f, obj());
		});

		return ft;
	}
#endif
private:

	// Allows using the same code to set a promise with a value or void
	template<typename R, typename F, typename... Params>
	static void fulfillPromise(std::promise<R>& pr, F& f, Params&&... params)
	{
		pr.set_value(f(std::forward<Params>(params)...));
	}
	template<typename F, typename... Params>
	static void fulfillPromise(std::promise<void>& pr, F& f, Params&&... params)
	{
		f(std::forward<Params>(params)...);
		pr.set_value();
	}
};

template<typename T, bool AutoStart=true>
class Concurrent : public ConcurrentBase<T>
{
public:
	template<typename... Args>
	Concurrent(Args&&... args) : ConcurrentBase(std::forward<Args>(args)...)
	{
		if (AutoStart)
			start();
	}

	void start()
	{
		assert(!m_th.joinable());
		m_th = std::thread([this]
		{
			while (!m_done)
			{
				std::function<void()> f;
				m_q.wait_and_pop(f);
				f();
			}
		});
	}
};

template<typename T, bool AutoStart=true>
class ConcurrentTicker : public ConcurrentBase<T>
{
protected:
	// How to get a return type of a member function without an object...
	// https://stackoverflow.com/questions/5580253/get-return-type-of-member-function-without-an-object
	using TickReturnType = decltype(((Type*)nullptr)->tick(0));
public:
	template<typename... Args>
	ConcurrentTicker(Args&&... args) : ConcurrentBase<T>(std::forward<Args>(args)...)
	{
		if (AutoStart)
			start();
	}

	void start()
	{
		assert(!m_th.joinable());
		m_th = std::thread([this]
		{
			HighResolutionTimer timer;
			const auto eps = std::numeric_limits<TickReturnType>::epsilon();
			double interval = 0;
			while (!m_done)
			{
				auto timeout = std::max(double(0), interval - timer.seconds());
				std::function<void()> f;

				auto timeoutMs = static_cast<int>(clip(timeout * 1000, double(0), double(std::numeric_limits<int>::max()-1)));
				if (m_q.wait_and_pop(f, timeoutMs))
					f();

				if (timeout<=0)
				{
					double delta = timer.seconds();
					timer.reset();
					interval = obj().tick(static_cast<TickReturnType>(std::max((double)eps,delta)));
					interval = clip(interval, double(0), double(std::numeric_limits<int>::max()));
				}
			}
		});
	}
};

}

