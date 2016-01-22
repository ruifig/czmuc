/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Puts together a pointer, a capacity, and a used size.
	The underlying memory is not owned, and must be kept valid
	for the duration of the buffer
	
*********************************************************************/
#pragma once

namespace cz
{

struct Buffer
{
	Buffer()
	{
	}

	template<typename T>
	Buffer(std::vector<T>& buf)
	{
		_init(buf.data(), buf.size());
	}

	template<typename T>
	Buffer(T* ptr, size_t count)
	{
		_init(ptr, count);
	}

	template<typename T, size_t N>
	Buffer(T(&buf)[N])
	{
		_init(&buf[0], N);
	}
	
	Buffer(const Buffer& buf) : ptr(buf.ptr), size(buf.size)
	{
		printf("copied\n");
	}

	template<typename T>
	void _init(T* ptr_, size_t count_)
	{
		static_assert(std::is_arithmetic<T>::value, "T need to be an arithmetic type");
		if (ptr_ == nullptr)
			return;
		ptr = reinterpret_cast<char*>(ptr_);
		size = count_ * sizeof(T);
	}

	const char* begin() const { return ptr; }
	const char* end() const { return ptr + size; }
	char* begin() { return ptr; }
	char* end() { return ptr + size; }
	char* ptr = nullptr;
	size_t size = 0;
};

Buffer operator+(const Buffer& buf, size_t offset);

} // namespace cz



