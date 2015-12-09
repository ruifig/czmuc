#include "czlibPCH.h"
#include "RPCBase.h"
#include "RPCProcessor.h"

namespace cz
{
namespace rpc
{

void ReplyStream::write() // For void RPCs
{
	inrpc.processReply(*this);
}

}
}

