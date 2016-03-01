/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Allows checking if a specified instance (key) is already being
	executed in the callstack
	
	Similar and inspired by boost::asio::detail::call_stack
*********************************************************************/

#pragma once

namespace cz
{

template<typename Key>
class Callstack
{
public:
	class Context
	{
	public:
		explicit Context(Key* k)
			: m_key(k)
			, m_next(Callstack<Key>::ms_top)
		{
			Callstack<Key>::ms_top = this;
		}

		~Context()
		{
			Callstack<Key>::ms_top = m_next;
		}
	private:
		friend class Callstack<Key>;
		Key* m_key;
		Context* m_next;
	};

	static bool contains(const Key* k)
	{
		Context* elem = ms_top;
		while(elem)
		{
			if (elem->m_key == k)
				return true;
			elem = elem->m_next;
		}
		return false;
	}

private:
	friend class Context;
	static thread_local Context* ms_top;
};

template<typename Key>
typename thread_local Callstack<Key>::Context* Callstack<Key>::ms_top = nullptr;

}

