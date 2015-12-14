/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Inspired by Herb Sutter's call at 
	https://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Herb-Sutter-Concurrency-and-Parallelism ,
	but with some changes.
	- Queued work can return values (you get a Future<R>) when queuing work
	- A ConcurrentTicker<T> version allows for automatic calls to a tick function.

*********************************************************************/
#pragma once

#include "SharedQueue.h"
#include "Future.h"
#include "Timer.h"

#ifdef max
	#undef max
#endif

namespace cz
{

template<typename T>
class ConcurrentBase
{
protected:
	mutable WorkQueue m_q;
	mutable T m_t;
	std::thread m_th;
	bool m_done = false;

	template<typename... Args>
	ConcurrentBase(Args&&... args) : m_t(std::forward<Args>(args)...)
	{}

	~ConcurrentBase()
	{
		m_q.push([this] { m_done = true;});
		m_th.join();
	}

	template<typename R, typename F>
	void exec(Promise<R>& pr, F& f) const
	{
		pr.set_value(f(m_t));
	}
	template<typename F>
	void exec(Promise<void> pr, F& f) const
	{
		f(m_t);
		pr.set_value();
	}

public:
	WorkQueue& getQueue()
	{
		return m_q;
	}

	template<typename F>
	auto operator()(F f) const -> Future<decltype(f(m_t))>
	{
		Promise<decltype(f(m_t))> pr;
		auto ft = pr.get_future();
		m_q.push([pr = std::move(pr), f=std::move(f), this]() mutable
		{
			exec(pr, f);
		});

		return ft;
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
				m_q.wait_and_pop()();
		});
	}
};

template<typename T, bool AutoStart=true>
class ConcurrentTicker : public ConcurrentBase<T>
{
protected:
	using TickReturnType = decltype(m_t.tick(0));
public:
	template<typename... Args>
	ConcurrentTicker(Args&&... args) : ConcurrentBase(std::forward<Args>(args)...)
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
					interval = m_t.tick(static_cast<TickReturnType>(std::max((double)eps,delta)));
					interval = clip(interval, double(0), double(std::numeric_limits<int>::max()));
				}
			}
		});
	}
};

}
