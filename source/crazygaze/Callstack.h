/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Allows adding key:value pair markers to the current callstack.
	This allows for example to check if an instance/function is already being executed
	Very similar and inspired by boost::asio::detail::call_stack
*********************************************************************/

#pragma once

namespace cz
{

template<typename Key, typename Value = unsigned char>
class Callstack
{
public:
	class Context
	{
	public:
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;
		explicit Context(Key* k)
			: m_key(k)
			, m_next(Callstack<Key,Value>::ms_top)
		{
			m_val = reinterpret_cast<unsigned char*>(this);
			Callstack<Key, Value>::ms_top = this;
		}

		Context(Key* k, Value& v)
			: m_key(k)
			, m_val(&v)
			, m_next(Callstack<Key, Value>::ms_top)
		{
			Callstack<Key, Value>::ms_top = this;
		}

		~Context()
		{
			Callstack<Key,Value>::ms_top = m_next;
		}
	private:
		friend class Callstack<Key, Value>;
		Key* m_key;
		Value* m_val;
		Context* m_next;
	};

	// Determine if the specified owner is on the stack
	// \return
	//	The address of the value if present, nullptr if not present
	static Value* contains(const Key* k)
	{
		Context* elem = ms_top;
		while(elem)
		{
			if (elem->m_key == k)
				return elem->m_val;
			elem = elem->m_next;
		}
		return nullptr;
	}

private:
	friend class Context;
	static thread_local Context* ms_top;
};

template<typename Key, typename Value>
typename thread_local Callstack<Key, Value>::Context* Callstack<Key, Value>::ms_top = nullptr;

}

