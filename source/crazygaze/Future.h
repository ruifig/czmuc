/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/
#pragma once

#include "crazygaze/czlib.h"

#pragma warning(push)
// destructor was implicitly defined as deleted because a base class destructor is inaccessible or deleted
#pragma warning(disable : 4624)

namespace cz
{

class FutureError : public std::logic_error
{
public:
    enum class Code
	{
		BrokenPromise = 1,
		PromiseAlreadySatisfied,
		NoState
	};

	explicit FutureError(Code code)
		: std::logic_error(whatStr(code))
		, m_code(code)
	{
	}

	Code code() const
	{
		return m_code;
	}

private:

	static const char* whatStr(Code code)
	{
		switch (code)
		{
		case Code::BrokenPromise: return "Broken promise";
		case Code::PromiseAlreadySatisfied: return "Promise already satisfied";
		case Code::NoState: return "No State";
		default:
			return "";
		}
	}

	Code m_code;
};


#ifdef __clcpp_parse__

template<typename T>
class Future
{
public:

};

template<typename T>
class Promise
{
public:
	template<typename U>
	void set_value(U v);
	void set_value();
	void set_exception(std::exception_ptr p);
	Future<T> get_future();
};

#else

//
// Forward declarations
//
template<typename T> class Future;
template<typename T> class Promise;

namespace details
{


	template<typename T> class FutureData;

	template<typename T>
	struct Result
	{
		enum class Type
		{
			None,
			Value,
			Exception
		};
		union
		{
			T val;
			//Continuation m_continuation;
			std::exception_ptr exc;
		};
		mutable Type type = Type::None;

		Result() { }
		~Result()
		{
			switch (type)
			{
			case Type::None:
				break;
			case Type::Value:
				val.~T();
				break;
			case Type::Exception:
				exc.~exception_ptr();
				break;
			};
		}

		bool is_ready() const { return type != Type::None; }
		void set_value(T v)
		{
			assert(type == Type::None);
			new (&val) T(std::move(v));
			type = Type::Value;
		}
		void set_exception(std::exception_ptr p)
		{
			assert(type == Type::None);
			new (&exc) std::exception_ptr(std::move(p));
			type = Type::Exception;
		}

		const T& get() const
		{
			switch (type)
			{
			case Type::None:
				throw FutureError(FutureError::Code::NoState);
			case Type::Value:
				return val;
			case Type::Exception:
				std::rethrow_exception(exc);
			default:
				assert(0 && "Unexpected");
				throw std::exception("Unexpected");
			}
		}

		T getMove()
		{
			switch (type)
			{
			case Type::None:
				throw FutureError(FutureError::Code::NoState);
			case Type::Value:
				return std::move(val);
			case Type::Exception:
				std::rethrow_exception(exc);
			default:
				assert(0 && "Unexpected");
				throw std::exception("Unexpected");
			}
		}
	};

	template<typename T>
	class FutureData : public std::enable_shared_from_this<FutureData<T>>
	{
	public:
		FutureData(const FutureData&) = delete;
		FutureData& operator = (const FutureData&) = delete;
		FutureData() {}
		FutureData(T v)
		{
            m_result.set_value(std::move(v));
		}

		//! Tells if the future is ready (a value or exception is available) 
		bool is_ready() const
		{
			return m_result.is_ready();
		}

		void wait() const
		{
			std::unique_lock<std::mutex> lk(m_mtx);
		    m_cond.wait(lk, [&] { return m_result.is_ready(); });
		}

		const T& get() const
		{
			if (!m_result.is_ready())
				wait();
		    return m_result.get();
	    }

		T getMove()
		{
			if (!m_result.is_ready())
				wait();
		    return std::move(m_result.getMove());
		}

		void set_value(T v)
		{
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				if (m_result.is_ready())
					throw FutureError(FutureError::Code::PromiseAlreadySatisfied);
				m_result.set_value(std::move(v));
			}
			onReady(); // Call this when NOT holding the lock
		}

		void set_exception(std::exception_ptr p)
		{
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				if (m_result.is_ready())
					throw FutureError(FutureError::Code::PromiseAlreadySatisfied);
				m_result.set_exception(std::move(p));
				m_cond.notify_all();
			}
			onReady(); // Call this when NOT holding the lock
		}

		void prAcquire()
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			++m_prCount;
		}
		void prRelease()
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			--m_prCount;
			if (m_prCount == 0 && !m_result.is_ready())
			{
				m_result.set_exception(std::make_exception_ptr(FutureError(FutureError::Code::BrokenPromise)));
				m_cond.notify_all();
			}
		}

		template< typename Cont, typename OuterFt>
		auto thenImpl(Cont w, OuterFt outerFt)
			-> typename std::enable_if<!std::is_void<decltype(w(outerFt))>::value, Future<decltype(w(outerFt))>>::type
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			using WT = decltype(w(outerFt));

			if (m_result.is_ready())
			{
				// Unlock before calling the continuation, since it's unknown code
				lk.unlock();
				return Future<WT>::makeReady(w(std::forward<OuterFt>(outerFt)));
			}

			Promise<WT> pr;
			auto ft = pr.get_future();
			m_continuations.push_back(
				[outerFt = std::move(outerFt), pr = std::move(pr), w = std::move(w)]() mutable
			{
				pr.set_value(w(std::forward<OuterFt>(outerFt)));
			});
			return ft;
		}

		template< typename Cont, typename OuterFt>
		auto thenImpl(Cont w, OuterFt outerFt)
			-> typename std::enable_if<std::is_void<decltype(w(outerFt))>::value, Future<decltype(w(outerFt))>>::type
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			using WT = decltype(w(outerFt));

			if (m_result.is_ready())
			{
				// Unlock before calling the continuation, since it's unknown code
				lk.unlock();
				w(std::reference_wrapper<OuterFt>(outerFt).get());
				return Future<WT>::makeReady();
			}

			Promise<WT> pr;
			auto ft = pr.get_future();
			m_continuations.push_back(
				[outerFt = std::move(outerFt), pr = std::move(pr), w = std::move(w)]() mutable
			{
				w(std::reference_wrapper<OuterFt>(outerFt).get());
				pr.set_value();
			});
			return ft;
		}
		
	protected:

		// This is called after setting the result to ready.
		// Should NOT be called when holding the lock
		void onReady()
		{
			m_cond.notify_all();
			for (auto&& c : m_continuations)
				c();
		}

		mutable std::mutex m_mtx;
		mutable std::condition_variable m_cond;
		Result<T> m_result;
		unsigned m_prCount = 0;
		std::vector<std::function<void()>> m_continuations;
	};

}

template<typename T>
class Future
{
public:

	Future() {}
	Future(std::shared_ptr<details::FutureData<T>> data) : m_data(std::move(data)) { }
	Future(Future&& other) : m_data(std::move(other.m_data)) { };
	Future(const Future& rhs) : m_data(rhs.m_data) { }
	~Future() { }

	Future& operator=(const Future& rhs)
	{
		m_data = rhs.m_data;
		return *this;
	}

	Future& operator=(Future&& rhs)
	{
		m_data = std::move(rhs.m_data);
		return *this;
	}

	const T& get() const
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->get();
	}

	
	/*! Similar to get, but moves out the value.
	* This is NOT thread safe.
	* Being not thread safe should not be a problem, since in the cases you want to move out
	* the value, you don't want to be sharing the state with other futures anyway.
	*/
	T getMove()
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return std::move(m_data->getMove());
	}

	template<typename Cont>
	auto then(Cont w)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->thenImpl(std::forward<Cont>(w), *this);
	}

	bool is_ready() const
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->is_ready();
	}

	static Future makeReady(T v)
	{
		return Future(std::make_shared<details::FutureData<T>>(std::move(v)));
	}

private:
	std::shared_ptr<details::FutureData<T>> m_data;
};

template<>
class Future<void>
{
public:
	Future() {}
	Future(std::shared_ptr<details::FutureData<bool>> data) : m_data(std::move(data)) { }
	Future(Future&& other) : m_data(std::move(other.m_data)) { };
	Future(const Future& rhs) : m_data(rhs.m_data) { }
	~Future() { }

	Future& operator=(const Future& rhs)
	{
		m_data = rhs.m_data;
		return *this;
	}

	Future& operator=(Future&& rhs)
	{
		m_data = std::move(rhs.m_data);
		return *this;
	}

	void get() const
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		m_data->get();
		return;
	}

	template<typename Cont>
	auto then(Cont w)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->thenImpl(std::forward<Cont>(w), *this);
	}

	bool is_ready() const
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->is_ready();
	}

	static Future makeReady()
	{
		return Future(std::make_shared<details::FutureData<bool>>(true));
	}

private:
	std::shared_ptr<details::FutureData<bool>> m_data;
};

template<typename T>
class Promise
{
public:
	Promise()
	{
		m_data = std::make_shared<details::FutureData<T>>();
		m_data->prAcquire();
	}

	~Promise()
	{
		if (m_data)
			m_data->prRelease();
	}

	Promise(const Promise& rhs)
		: m_data(rhs.m_data)
	{
		m_data->prAcquire();
	}

	Promise(Promise&& rhs)
		: m_data(std::move(rhs.m_data))
	{
	}

	Promise& operator=(const Promise& rhs)
	{
		if (m_data != rhs.m_data)
		{
			m_data = rhs.m_data;
			if (m_data)
				m_data->prAcquire();
		}
		return *this;
	}

	Promise& operator=(Promise&& rhs)
	{
		m_data = std::move(rhs.m_data);
		return *this;
	}

	Future<T> get_future()
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return Future<T>(m_data);
	}

	void set_value(T val)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		m_data->set_value(std::move(val));
	}
	
	void set_exception(std::exception_ptr p)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		m_data->set_exception(std::move(p));
	}

private:
	std::shared_ptr<details::FutureData<T>> m_data;
};

template<>
class Promise<void>
{
public:
	Promise()
	{
		m_data = std::make_shared<details::FutureData<bool>>();
		m_data->prAcquire();
	}

	~Promise()
	{
		if (m_data)
			m_data->prRelease();
	}

	Promise(const Promise& rhs)
		: m_data(rhs.m_data)
	{
		m_data->prAcquire();
	}

	Promise(Promise&& rhs)
		: m_data(std::move(rhs.m_data))
	{
	}

	Promise& operator=(const Promise& rhs)
	{
		if (m_data != rhs.m_data)
		{
			m_data = rhs.m_data;
			if (m_data)
				m_data->prAcquire();
		}
		return *this;
	}

	Promise& operator=(Promise&& rhs)
	{
		m_data = std::move(rhs.m_data);
		return *this;
	}

	Future<void> get_future()
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return Future<void>(m_data);
	}

	void set_value()
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		m_data->set_value(true);
	}

	void set_exception(std::exception_ptr p)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		m_data->set_exception(std::move(p));
	}
private:
	std::shared_ptr<details::FutureData<bool>> m_data;
};


//! Given a promise, a work lambda and work parameters, it fulfill the promise with work result
// This makes it easier for code to fulfill Promise<T> and Promise<void> with the same code
template<typename R, typename F, typename... WorkParams>
void fulfillPromiseFromWork(Promise<R>& pr, F& f, WorkParams&&... workParams )
{
	pr.set_value(f(std::forward<WorkParams>(workParams)...));
}
template<typename F, typename... WorkParams>
void fulfillPromiseFromWork(Promise<void>& pr, F& f, WorkParams&&... workParams )
{
	f(std::forward<WorkParams>(workParams)...);
	pr.set_value();
}
template<typename R>
void fulfillPromiseWithEmpty(Promise<R>& pr)
{
	pr.set_value(R());
}
inline void fulfillPromiseWithEmpty(Promise<void>& pr)
{
	pr.set_value();
}


#endif

}

#pragma warning(pop)
