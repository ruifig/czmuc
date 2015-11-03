// ChatClient.cpp : Defines the entry point for the console application.
//

#include "RPCChatClientPCH.h"

// Disable deprecation warnings
#pragma warning(disable : 4996)

using namespace cz;
using namespace cz::net;
using namespace cz::rpc;

class ChatClient : public Service<ChatServerInterface>::ClientInterface
{
public:
private:
	//
	// ClientInterface
	//
	virtual void onMsg(const std::string& user, const std::string& msg) override
	{
		printf("%s: %s\n", user.c_str(), msg.c_str());
	}
	
	virtual void onSystemMsg(const std::string& msg) override
	{
		printf("SYSTEM: %s\n", msg.c_str());
	}
};

static const char* helpStr =
"*** Chat Client sample ***\n"
""
"- Passwords are fixed to 'pass'\n"
"- 'Admin' user can kick people with '/kick <name>' command\n"
"\n"
"Commands:\n"
"	/exit - Terminates the chat\n"
"	/kick <name> - Kicks the specified user, if permissions allow it (Only 'Admin' can kick)\n"
"\n";

int runChat(const std::string& name, const std::string& pass, const std::string& ip, int port)
{
	ChatClient chat;
	CompletionPort iocp(1);
	Service<ChatServerInterface>::ClientConnection client(std::make_unique<TCPTransport>(ip, port, iocp), chat);
	client.setExceptionCallback([](RPCHeader hr, const BaseRPCInfo& info, const std::string& msg)
	{
		printf("%s\n", msg.c_str());
		exit(1);
	});

	printf("SYSTEM: Connected to server %s:%d\n", ip.c_str(), port);

	auto res = CALLRPC(client, login, name, pass);
	auto resStr = res.get();
	if (resStr != "OK")
	{
		printf("LOGIN ERROR: %s\n", resStr.c_str());
		return EXIT_FAILURE;
	}

	while (true)
	{
		std::string msg;
		std::getline(std::cin, msg);
		if (msg == "/exit")
		{
			printf("Exiting...\n");
			return EXIT_SUCCESS;
		}
		else if (strncmp(msg.c_str(), "/kick ", strlen("/kick ")) == 0)
		{
			CALLRPC(client, kick, std::string(msg.begin() + strlen("/kick "), msg.end()));
		}
		else
		{
			auto res = CALLRPC(client, sendMsg, msg);
			res.get();
		}
	}
}

int main()
{
	std::string name;
	std::string pass;
	std::string ip;
	int port;
	std::string tmp;

	printf(helpStr);

	std::string defaultName = formatString("User%d", GetCurrentProcessId());
	printf("Enter your name (empty will assume %s): ", defaultName.c_str());
	std::getline(std::cin, name);
	if (name == "")
		name = defaultName;
	printf("Enter your password: ");
	std::getline(std::cin, pass);

	printf("Enter the server IP (empty assumes 127.0.0.1): ");
	std::getline(std::cin, ip);
	if (ip == "")
		ip = "127.0.0.1";
	printf("Enter server port (empty assumes 28000): ");
	std::getline(std::cin, tmp);
	if (tmp == "")
		port = 28000;
	else
		port = std::stoi(tmp);

	initializeSocketsLib();
	int res = EXIT_FAILURE;
	try
	{
		res = runChat(name, pass, ip, port);
	}
	catch (std::exception& e)
	{
		printf("Exception: %s\n", e.what());
	}
	shutdownSocketsLib();
	printf("done...\n");
	getch();
    return res;
}

