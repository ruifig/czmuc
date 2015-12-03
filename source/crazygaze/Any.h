/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	A somewhat simplistic "Any" implementation
*********************************************************************/

#pragma once

#include <vector>

namespace cz
{

class Any
{
  public:
	Any();
	Any(const Any&) = default;
	Any(Any&& other);
	explicit Any(bool v);
	explicit Any(int v);
	explicit Any(unsigned int v);
	explicit Any(float v);
	explicit Any(const char* v);
	explicit Any(std::string v);
	explicit Any(std::vector<uint8_t> blob);
	template<typename T>
	explicit Any(const T& v) : m_type(kNone)
	{
	}

	enum Type
	{
		kNone,
		kBool,  // bool is encoded as an integer 0 or 1
		kInteger,
		kUnsignedInteger,
		kFloat,
		kString,
		kBlob
	};

	template<typename Stream>
	const Stream& readFromStream(const Stream& stream)
	{
		uint8_t t = kNone;
		stream >> t;
		m_type = static_cast<Type>(t);
		switch (m_type)
		{
		case kNone:
			break;
		case kString:
			stream >> m_string;
			break;
		case kBool:
		{
			uint8_t v;
			stream >> v;
			m_integer = v ? 1 : 0;
		}
		break;
		case kInteger:
		case kUnsignedInteger:
			stream >> m_integer;
			break;
		case kFloat:
			stream >> m_float;
			break;
		case kBlob:
			stream >> m_blob;
			break;
		default:
			CZ_ASSERT_F(false, "Invalid Generic parameter type (%d)", m_type);
		}
		return stream;
	}

	template<typename Stream>
	Stream& saveToStream(Stream& stream) const
	{
		stream << uint8_t(m_type);
		switch (m_type)
		{
		case kNone:
			break;
		case kString:
			stream << m_string;
			break;
		case kBool:
		{
			uint8_t v = m_integer ? 1 : 0;
			stream << v;
		}
		break;
		case kInteger:
		case kUnsignedInteger:
			stream << m_integer;
			break;
		case kFloat:
			stream << m_float;
			break;
		case kBlob:
			stream << m_blob;
			break;
		default:
			CZ_UNEXPECTED();
		}
		return stream;
	}

	Type getType() const { return m_type; }
	bool asBool(bool& v) const;
	bool asInteger(int& v) const;
	bool asUnsignedInteger(unsigned int& v) const;
	bool asString(std::string& v) const;
	bool asFloat(float& v) const;
	bool asBlob(std::vector<uint8_t>& v) const;

	template<typename T>
	bool getAs(T& dst) const
	{
		//static_assert(sizeof(T) == 0, "Unsupported type");
		return false;
	}
	bool getAs(bool& dst) const
	{
		return asBool(dst);
	}
	bool getAs(int& dst) const
	{
		return asInteger(dst);
	}
	bool getAs(unsigned int& dst) const
	{
		return asUnsignedInteger(dst);
	}
	bool getAs(float& dst) const
	{
		return asFloat(dst);
	}
	bool getAs(std::string& dst) const
	{
		return asString(dst);
	}
	bool getAs(std::vector<uint8_t>& dst) const
	{
		return asBlob(dst);
	}

	const char* convertToString() const;

	Any& operator=(const Any& other);
	Any& operator=(Any&& other);

	template <class T>
	Any& operator=(T&& v)
	{
		set(Any(std::forward<T>(v)));
		return *this;
	}

	template<typename Tuple>
	static bool toTuple(const std::vector<Any>& v, Tuple& dst)
	{
		if (v.size() != std::tuple_size<Tuple>::value)
			return false;

		return convert_any<Tuple, std::tuple_size<Tuple>::value==0, 0>::convert(v, dst);
	}

  private:
	void set(const Any& other);
	void set(Any&& other);


	template<typename Tuple, bool Done, int N>
	struct convert_any
	{
		static bool convert(const std::vector<Any>& v, Tuple& dst)
		{
			if (!v[N].getAs(std::get<N>(dst)))
				return false;
			return convert_any<Tuple, std::tuple_size<Tuple>::value == N + 1, N + 1>::convert(v, dst);
		}
	};

	template<typename Tuple, int N>
	struct convert_any<Tuple, true, N>
	{
		static bool convert(const std::vector<Any>& v, Tuple& dst)
		{
			return true;
		}
	};

	Type m_type;
	union
	{
		int m_integer;
		unsigned int m_uinteger;
		float m_float;
	};
	std::string m_string;
	std::vector<uint8_t> m_blob;
};

struct AnyTree
{
	AnyTree() {}
	AnyTree(std::string name_, Any data_) : name(name_), data(data_) {}
	AnyTree(AnyTree && other) : name(std::move(other.name)), data(std::move(other.data)), children(std::move(other.children))
	{
	}

	AnyTree& operator=(AnyTree && other)
	{
		name = std::move(other.name);
		data = std::move(other.data);
		children = std::move(other.children);
		return *this;
	}

	AnyTree(const AnyTree&) = default;
	AnyTree& operator=(const AnyTree&) = default;

	std::string name;
	Any data;
	std::vector<AnyTree> children;

};

}  // namespace cz
