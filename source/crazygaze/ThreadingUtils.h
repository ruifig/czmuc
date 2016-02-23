/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	

	NOTES:
	The writer only has read access. This is enforced by the API, which only
	returns a const T&
*********************************************************************/

#pragma once

namespace cz
{

	namespace details
	{
		template<typename T>
		struct AsyncVarData
		{
			bool dirty = false;
			std::mutex mtx;
			T update;
			T val;
		};
	}

	template < typename T, bool KEEP_UPDATE = true >
	class AsyncVar
	{
	private:
		mutable details::AsyncVarData<T> m_data;
		void sync(bool wait) const
		{
			if (m_data.dirty)
			{
				// Instead of locking right away, we try to lock and if we can't get the lock, we don't do anything.
				// This is so that the reader doesn't block if any writers are still holding the lock.
				// This can happen, since readers can do multiple writes before we do a read.
				std::unique_lock<std::mutex> lk(m_data.mtx, std::defer_lock);
				if (wait)
					lk.lock();
				else if (!lk.try_lock())
					return;

				if (KEEP_UPDATE)
					m_data.val = m_data.update;
				else
					m_data.val = std::move(m_data.update);
				m_data.dirty = false;
			}
		}
	public:
		class Writer
		{
		private:
			std::unique_lock<std::mutex> m_lk;
			details::AsyncVarData<T>* m_data;
			Writer(const Writer&) = delete;
			Writer& operator= (const Writer&) = delete;
		public:
			explicit Writer(details::AsyncVarData<T>& data) : m_lk(data.mtx), m_data(&data) {}
			Writer(Writer&& other) : m_data(other.m_data)
			{
				other.m_data = nullptr;
			}
			~Writer()
			{
				if (m_data) // This can be nullptr, if this object was moved to another
					m_data->dirty = true;
			}

			T* operator->() { return &m_data->update; }
			const T* operator->() const { return &m_data->update; };
			T& operator*() { return m_data->update; }
			const T& operator*() const { return m_data->update; }
			Writer& operator=(T v) { m_data->update = std::move(v); return *this; }
		};

		AsyncVar() {}
		explicit AsyncVar(T v)
		{
			if (KEEP_UPDATE)
				m_data.update = v;
			m_data.val = std::move(v);
		}

		//! Returns an object we can use to write to the variable
		Writer writer()
		{
			return Writer(m_data);
		}

		const T& get(bool wait=false) const
		{
			sync(wait);
			return m_data.val;
		}

	};

	template <class T>
	class Monitor
	{
	private:
		mutable T m_t;
		mutable std::mutex m_mtx;

	public:
		using Type = T;
		Monitor() {}
		Monitor(T t_) : m_t(std::move(t_)) {}
		template <typename F>
		auto operator()(F f) const -> decltype(f(m_t))
		{
			std::lock_guard<std::mutex> hold{ m_mtx };
			return f(m_t);
		}
	};


	/*!
	* Allows the easy creation of a shared instance that work similar to a singleton, but will get destroyed when
	* there are no more strong references to it.
	*/
	template<typename T>
	std::shared_ptr<T> getSharedData()
	{
		static std::mutex mtx;
		static std::weak_ptr<T> ptr;
		std::lock_guard<std::mutex> lk(mtx);
		auto p = ptr.lock();
		if (p)
			return p;
		p = std::make_shared<T>();
		ptr = p;
		return p;
	}

}  // namespace cz
