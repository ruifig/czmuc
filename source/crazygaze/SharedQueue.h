/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Lock based multiple producer / multiple consumer queue
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include <queue>
#include <mutex>
#include <condition_variable>

namespace cz
{

/** Multiple producer, multiple consumer thread safe queue
* Since 'return by reference' is used this queue won't throw */
template<typename T>
class SharedQueue
{
	std::queue<T> m_queue;
	mutable std::mutex m_mtx;
	std::condition_variable m_data_cond;

	// These are not allowed
	SharedQueue& operator=(const SharedQueue&) = delete;
	SharedQueue(const SharedQueue& other) = delete;

public:
	SharedQueue(){}

	/* Emplace functions */
	template<typename A1>
	void emplace(A1 &&a1)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.emplace(std::forward<A1>(a1));
		m_data_cond.notify_one();
	}
	template<typename A1, typename A2>
	void emplace(A1 &&a1, A2 &&a2)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.emplace(std::forward<A1>(a1), std::forward<A2>(a2));
		m_data_cond.notify_one();
	}
	template<typename A1, typename A2, typename A3>
	void emplace(A1 &&a1, A2 &&a2, A3 &&a3)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.emplace(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3));
		m_data_cond.notify_one();
	}

	template<typename T>
	void push(T&& item){
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.push(std::forward<T>(item));
		m_data_cond.notify_one();
	}

	/// \return immediately, with true if successful retrieval
	bool try_and_pop(T& popped_item){
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_queue.empty()){
			return false;
		}
		popped_item = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

	/**
	 * Gets all the items from the queue, into another queue
	 * This should be more efficient than retrieving one item at a time, when a thread wants to process as many items
	 * as there are currently in the queue. Example:
	 * std::queue<Foo> local;
	 * if (q.try_and_popAll(local)) {
	 *     ... process items in local ...
	 * }
	 *
	 * \NOTE
	 *	Any elements in the destination queue will be lost.
	 */
	bool try_and_popAll(std::queue<T>& dest)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		dest = std::move(m_queue);
		return dest.size()!=0;
	}

	// Try to retrieve, if no items, wait till an item is available and try again
	void wait_and_pop(T& popped_item){
		std::unique_lock<std::mutex> lock(m_mtx);
		m_data_cond.wait(lock, [this] { return !m_queue.empty();});
		popped_item = std::move(m_queue.front());
		m_queue.pop();
	}

	// Try to retrieve, if no items, wait till an item is available and try again
	bool wait_and_pop(T& popped_item, int timeoutMs){
		std::unique_lock<std::mutex> lock(m_mtx);
		if (!m_data_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_queue.empty();}))
			return false;

		popped_item = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

	bool empty() const{
		std::lock_guard<std::mutex> lock(m_mtx);
		return m_queue.empty();
	}

	unsigned size() const{
		std::lock_guard<std::mutex> lock(m_mtx);
		return static_cast<unsigned>(m_queue.size());
	}
};


using WorkQueue = SharedQueue<std::function<void()>>;

} // namespace cz

