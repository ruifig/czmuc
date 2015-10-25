#pragma once

#include <string>
#include "crazygaze/rpc/RPCProcessor.h"

class ChatClientInterface
{
public:
	// The server calls this if the client was kicked
	virtual void onMsg(const std::string& user, const std::string& msg) = 0;
	virtual void onSystemMsg(const std::string& msg) = 0;
};

#define RPCTABLE_CLASS ChatClientInterface
#define RPCTABLE_CONTENTS \
	REGISTERRPC(onMsg) \
	REGISTERRPC(onSystemMsg)
#include "crazygaze/rpc/RPCGenerate.h"