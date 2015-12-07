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
	std::queue<T> queue_;
	mutable std::mutex m_;
	std::condition_variable data_cond_;

	// These are not allowed
	SharedQueue& operator=(const SharedQueue&) = delete;
	SharedQueue(const SharedQueue& other) = delete;

public:
	SharedQueue(){}

	/* Emplace functions */
	template<typename A1>
	void emplace(A1 &&a1)
	{
		std::lock_guard<std::mutex> lock(m_);
		queue_.emplace(std::forward<A1>(a1));
		data_cond_.notify_one();
	}
	template<typename A1, typename A2>
	void emplace(A1 &&a1, A2 &&a2)
	{
		std::lock_guard<std::mutex> lock(m_);
		queue_.emplace(std::forward<A1>(a1), std::forward<A2>(a2));
		data_cond_.notify_one();
	}
	template<typename A1, typename A2, typename A3>
	void emplace(A1 &&a1, A2 &&a2, A3 &&a3)
	{
		std::lock_guard<std::mutex> lock(m_);
		queue_.emplace(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3));
		data_cond_.notify_one();
	}

	template<typename T>
	void push(T&& item){
		std::lock_guard<std::mutex> lock(m_);
		queue_.push(std::forward<T>(item));
		data_cond_.notify_one();
	}

	/// \return immediately, with true if successful retrieval
	bool try_and_pop(T& popped_item){
		std::lock_guard<std::mutex> lock(m_);
		if (queue_.empty()){
			return false;
		}
		popped_item = std::move(queue_.front());
		queue_.pop();
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
		std::lock_guard<std::mutex> lock(m_);
		dest = std::move(queue_);
		return dest.size()!=0;
	}

	// Try to retrieve, if no items, wait till an item is available and try again
	void wait_and_pop(T& popped_item){
		std::unique_lock<std::mutex> lock(m_); // note: unique_lock is needed for std::condition_variable::wait
		while (queue_.empty())
		{ //                       The 'while' loop below is equal to
			data_cond_.wait(lock);  //data_cond_.wait(lock, [](bool result){return !queue_.empty();});
		}
		popped_item = std::move(queue_.front());
		queue_.pop();
	}

	bool empty() const{
		std::lock_guard<std::mutex> lock(m_);
		return queue_.empty();
	}

	unsigned size() const{
		std::lock_guard<std::mutex> lock(m_);
		return static_cast<unsigned>(queue_.size());
	}
};

} // namespace cz

