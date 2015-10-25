#pragma once
#include "RPCChatCommon.h"
#include <string>

class ChatServerInterface
{
public:

	//
	// \return
	// "OK" if login accepted, or failure reason
	virtual std::string login(const std::string& user, const std::string& pass) = 0;

	virtual void sendMsg(const std::string& msg) = 0;
	virtual void kick(const std::string& user) = 0;
};

#define RPCTABLE_CLASS ChatServerInterface
#define RPCTABLE_CONTENTS \
		REGISTERRPC(login) \
		REGISTERRPC(sendMsg) \
		REGISTERRPC(kick)
#include "crazygaze/rpc/RPCGenerate.h"
