/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Lock based multiple producer / multiple consumer queue
*********************************************************************/

#pragma once

#include "crazygaze/muc/czmuc.h"
#include <queue>
#include <mutex>
#include <condition_variable>

namespace cz
{

//
// Multiple producer, multiple consumer thread safe queue
//
template<typename T>
class SharedQueue
{
private:
	std::queue<T> m_queue;
	mutable std::mutex m_mtx;
	std::condition_variable m_data_cond;

	SharedQueue& operator=(const SharedQueue&) = delete;
	SharedQueue(const SharedQueue& other) = delete;

public:
	SharedQueue(){}

	template<typename... Args>
	void emplace(Args&&... args)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.emplace(std::forward<Args>(args)...);
		m_data_cond.notify_one();
	}

	template<typename T>
	void push(T&& item){
		std::lock_guard<std::mutex> lock(m_mtx);
		m_queue.push(std::forward<T>(item));
		m_data_cond.notify_one();
	}

	//! Tries to pop an item from the queue. It does not block waiting for
	// items.
	// \return Returns true if an Items was retrieved
	bool try_and_pop(T& popped_item){
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_queue.empty()){
			return false;
		}
		popped_item = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

	//! Retrieves all items into the supplied queue.
	// This should be more efficient than retrieving one item at a time when a
	// thread wants to process as many items as there are currently in the
	// queue. Example:
	// std::queue<Foo> local;
	// if (q.try_and_popAll(local)) {
	//     ... process items in local ...
	// }
	//
	// \return
	//	True if any items were retrieved
	// \note
	//	Any elements in the destination queue will be lost.
	bool try_and_popAll(std::queue<T>& dest)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		dest = std::move(m_queue);
		return dest.size()!=0;
	}

	// Retrieves an item, blocking if necessary to wait for items.
	void wait_and_pop(T& popped_item){
		std::unique_lock<std::mutex> lock(m_mtx);
		m_data_cond.wait(lock, [this] { return !m_queue.empty();});
		popped_item = std::move(m_queue.front());
		m_queue.pop();
	}

	//! Retrieves an item, blocking if necessary for the specified duration
	// until items arrive.
	//
	// \return
	//	false : Timed out (There were no items)
	//	true  : Item retrieved
	bool wait_and_pop(T& popped_item, int64_t timeoutMs){
		std::unique_lock<std::mutex> lock(m_mtx);
		if (!m_data_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_queue.empty();}))
			return false;

		popped_item = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

	//! Checks if the queue is empty
	bool empty() const{
		std::lock_guard<std::mutex> lock(m_mtx);
		return m_queue.empty();
	}

	//! Returns how many items there are in the queue
	unsigned size() const{
		std::lock_guard<std::mutex> lock(m_mtx);
		return static_cast<unsigned>(m_queue.size());
	}
};

using WorkQueue = SharedQueue<std::function<void()>>;

} // namespace cz

