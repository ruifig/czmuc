/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com

	purpose:
	Allows a given class/struct to be accessible by an ID.
	From that ID, we can get the object back.

*********************************************************************/
#pragma once

#include "crazygaze/muc/czmuc.h"

namespace cz
{

template<typename T>
class ObjectId
{
public:
	using NoCvT = typename std::remove_cv<T>::type;
	using CounterType = typename T::CounterType;

	// ObjectId<const Foo> needs to be able to access ObjectId<Foo> internals
	friend class ObjectId<const NoCvT>;
	friend class ObjectId<NoCvT>;

	ObjectId() : m_id(0)
	{
	}

	//
	// If we are a ObjectIf<const Foo>, we still want to allow constructing from a ObjectId<Foo>
	//
	template<
		typename U = T,
		typename = std::enable_if_t<std::is_const_v<T>> >
	ObjectId(const NoCvT& obj)
		: m_id(obj.m_objectId)
	{
	}

	//
	// If we e.g a ObjectId<Foo>, we don't want to allow constructing from a ObjectId<const Foo>, which would remove constness.
	// Therefore if we are an ObjectId<Foo>, we only allow constructing from another ObjectId<Foo>
	//
	template<
		typename U = T,
		typename = std::enable_if_t<!std::is_const_v<T>> >
	ObjectId(NoCvT& obj)
		: m_id(obj.m_objectId)
	{
	}

	explicit ObjectId(CounterType id)
		: m_id(id)
	{
	}

	ObjectId(const ObjectId& rhs)
		: m_id(rhs.m_id)
	{
	}

	template<
		typename U = T,
		typename = std::enable_if_t<std::is_const_v<T>> >
	ObjectId(const ObjectId<NoCvT>& rhs)
		: m_id(rhs.m_id)
	{
	}

	//
	// We return T& and T* directly, so it matches what we need if we use e.g ObjectId<Foo> or ObjectId<const Foo>
	// This means that for ObjectId<Foo>, it returns a Foo& and Foo*, which can be modified by the caller, and for ObjectId<const Foo>
	// it returns a const Foo& and const Foo*, which therefore keeps the intended constness.
	T& getObject() const
	{
		return NoCvT::getObject(m_id);
	}

	T* tryGetObject() const
	{
		return NoCvT::tryGetObject(m_id);
	}

	template<typename U>
	U& getObjectAs() const
	{
		return static_cast<U&>(NoCvT::getObject(m_id));
	}

	template<typename U>
	U* tryGetObjectAs() const
	{
		return static_cast<U*>(NoCvT::tryGetObject(m_id));
	}

	bool operator==(const ObjectId<NoCvT>& rhs) const
	{
		return m_id==rhs.m_id;
	}

	bool operator==(const ObjectId<const NoCvT>& rhs) const
	{
		return m_id==rhs.m_id;
	}

	// 
	// Not sure if it's a good idea to allow operator bool(), because that might lead the user to believe
	// it points to a valid object, when in truth it just says the Id itself is valid, and not if the object
	// it is supposed to point is still valid
#if 0
	explicit operator bool() const
	{
		return m_id!=0;
	}
#endif

	CounterType getValue() const
	{
		return m_id;
	}

private:
	CounterType m_id = 0;
};

template <typename T, typename CT>
class IdAccessible
{
  public:
	using CounterType = CT;

	IdAccessible()
	{
		static CounterType counter = 0;
		m_objectId = ++counter;
		m_ids[m_objectId] = static_cast<T*>(this);
	}

	~IdAccessible()
	{
		m_ids.erase(m_objectId);
	}

	// Mark as noncopyable
	IdAccessible(const IdAccessible&) = delete;
	IdAccessible& operator=(const IdAccessible&) = delete;

	ObjectId<const T> getId() const
	{
		return ObjectId<const T>(m_objectId);
	}

	ObjectId<T> getId()
	{
		return ObjectId<T>(m_objectId);
	}

  protected:
	// ObjectId<T> needs to be able to access these
	friend class  ObjectId<T>;
	friend class ObjectId<const T>;
	static T& getObject(CounterType id)
	{
		auto it = m_ids.find(id);
		CZ_ASSERT(it != m_ids.end());
		return *it->second;
	}

	static T* tryGetObject(CounterType id)
	{
		auto it = m_ids.find(id);
		if (it != m_ids.end())
			return it->second;
		else
			return nullptr;
	}

  private:
	friend ObjectId<T>;
	CounterType m_objectId;

	// Instead of using std::unordered_map directly, we inherit from it,
	// so we can put breakpoints in the destructor
	struct MyMap : public std::unordered_map<CounterType, T*>
	{
		~MyMap()
		{
		}
	};

#if defined(_MSC_VER) && (_MSC_VER < 1920)
	// See https://developercommunity.visualstudio.com/content/problem/185559/debugger-called-twice.html
	#error "inline static have a bug in VS 2017. Use VS 2019"
#endif
	inline static MyMap m_ids;
};

} // namespace cz

