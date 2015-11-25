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


//
// Forward declarations
//
template<typename T> class Future;
template<typename T> class Promise;
template<typename T>
Future<T> makeReadyFuture(T&& v);

namespace details
{

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

		void set_value(T v)
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			if (m_result.is_ready())
				throw FutureError(FutureError::Code::PromiseAlreadySatisfied);
			m_result.set_value(std::move(v));
			m_cond.notify_all();
			if (m_continuations.size())
			{
				// Unlock before calling the continuations, so we don't call unknown code while holding the lock
				// This means the the continuations vector cannot be changed. This is fine, since per design,
				// when calling "then", if the result if ready, it will call the continuation right away, and not
				// add anything to the continuations vector
				lk.unlock();
				for (auto&& c : m_continuations)
					c();
			}
		}

		void set_exception(std::exception_ptr p)
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			if (m_result.is_ready())
				throw FutureError(FutureError::Code::PromiseAlreadySatisfied);
			m_result.set_exception(std::move(p));
			m_cond.notify_all();
		}

		template<typename Cont>
		auto then(Cont w) -> Future<decltype(w(Future<T>()))>
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			using WT = decltype(w(Future<T>()));

			if (m_result.is_ready())
			{
				// Unlock before calling the continuation, since it's unknown code
				lk.unlock();
				return makeReadyFuture<WT>(w(Future<T>(this->shared_from_this())));
			}

			Promise<WT> pr;
			auto ft = pr.get_future();
			m_continuations.push_back(
				[this, pr=std::move(pr), w=std::move(w)]() mutable
			{
				pr.set_value(w(Future<T>(this->shared_from_this())));
			});
			return ft;
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

	private:
		mutable std::mutex m_mtx;
		mutable std::condition_variable m_cond;
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

		} m_result;
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

	template<typename Cont>
	auto then(Cont w)
	{
		if (!m_data)
			throw FutureError(FutureError::Code::NoState);
		return m_data->then(std::move(w));
	}

private:
	std::shared_ptr<details::FutureData<T>> m_data;
};

template<typename T>
Future<T> makeReadyFuture(T&& v)
{
	auto data = std::make_shared<details::FutureData<T>>(std::forward<T>(v));
	return Future<T>(std::move(data));
}

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
		if (m_data != &rhs.m_data)
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
private:
	std::shared_ptr<details::FutureData<T>> m_data;
};

}

#pragma warning(pop)
