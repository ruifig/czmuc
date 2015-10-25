/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/rpc/RPCProcessor.h"

namespace cz
{
namespace rpc
{

BaseOutRPCProcessor::BaseOutRPCProcessor()
{
}

BaseOutRPCProcessor::~BaseOutRPCProcessor()
{
}

void BaseOutRPCProcessor::processReceivedReply(RPCHeader hdr, const ChunkBuffer& in)
{
	std::function<void(const ChunkBuffer&, RPCHeader)> f;

	auto rpcInfo = getRPCInfo(hdr);
	CZ_ASSERT(rpcInfo->hasReturnValue || hdr.bits.success==false);

	// Find the reply information
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		auto it = m_replies.find(hdr.key());

		// If we are not waiting a reply for this...
		if (it==m_replies.end() )
		{
			CZ_ASSERT_F(rpcInfo->hasReturnValue == false, "Reply information is missing (for a call to '%s')", rpcInfo->name.c_str());
			CZ_ASSERT(!hdr.bits.success);

			// Still need to finish reading the stream, by reading the exception message
			std::string err;
			in >> err;
			//CZ_ASSERT_F(0, "Received an exception for a void RPC: %s", errMsg.c_str());
			m_exceptionCallback(hdr, *rpcInfo, err);
			return;
		}

		// remove from the map before calling it
		f = std::move(it->second);
		m_replies.erase(it);
	}

	// Execute the received reply processing
	f(in, hdr);
}

void BaseOutRPCProcessor::shutdown()
{
	std::unique_lock<std::mutex> lk(m_mtx);
	// Clearing the replies, will cause any pending promises to be deleted, and thus cause "broken promises".
	// This will effectively unblock any threads waiting on the respective futures
	m_replies.clear();
}

} // namespace rpc
} // namespace cz



