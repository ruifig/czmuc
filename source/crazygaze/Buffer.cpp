/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	Puts together a pointer, a capacity, and a used size.
	The underlying memory is not owned, and must be kept valid
	for the duration of the buffer
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/Buffer.h"

namespace cz
{

cz::Buffer operator+(const Buffer& buf, size_t offset)
{
	if (buf.ptr == nullptr || offset > buf.size)
	{
		return Buffer();
	}
	else
	{
		return Buffer(buf.ptr + offset, buf.size - offset);
	}
}

} // namespace cz


