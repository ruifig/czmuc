/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:

	Allows a client to connect to a server without knowing anything at all about the type.
	This makes it possible for a client to connect and just call generic rpcs, since it doesn't know
	anything about the types.
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "RPCBase.h"

namespace cz
{
namespace rpc
{


class GenericRPCClass
{
};

} // namespace rpc
} // namespace cz


#define RPCTABLE_CLASS cz::rpc::GenericRPCClass
#define RPCTABLE_CONTENTS
#include "crazygaze/rpc/RPCGenerate.h"

