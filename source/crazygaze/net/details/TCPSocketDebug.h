/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:

	Utility code for helping to debug the sockets
*********************************************************************/

#pragma once

//! If this is set to 1, it will enable some heavy debug code (If building a Debug build)
#define TCPSOCKET_DEBUG 0

namespace cz
{

struct CompletionPortOperation;

namespace net
{

class TCPSocket;
class TCPServerSocket;
struct TCPSocketData;

//
// Tracks some events used for debugging.
//
class DebugData
{
#if !defined(NDEBUG) && TCPSOCKET_DEBUG
  private:
	std::atomic_int activeOperations;
	std::atomic_int activeServerSockets;
	std::atomic_int activeSockets;

	template<typename T>
	struct Set
	{
		std::mutex mtx;
		std::set<T> c;
		void add(const T& obj)
		{
			std::lock_guard<std::mutex> lk(mtx);
			c.insert(obj);
		}
		void remove(const T& obj)
		{
			std::lock_guard<std::mutex> lk(mtx);
			c.erase(obj);
		}
		void clear()
		{
			std::lock_guard<std::mutex> lk(mtx);
			c.clear();
		}
	};

	template<typename K, typename T>
	struct Map
	{
		std::mutex mtx;
		std::unordered_map<K, T> c;
		template<typename F>
		void change(const K& key, F f)
		{
			std::lock_guard<std::mutex> lk(mtx);
			f(c[key]);
		}
		void remove(const K& key)
		{
			std::lock_guard<std::mutex> lk(mtx);
			c.erase(key);
		}
		void clear()
		{
			std::lock_guard<std::mutex> lk(mtx);
			c.clear();
		}
	};

	Set<CompletionPortOperation*> operations;
	Set<TCPServerSocket*> serverSockets;
	Set<TCPSocket*> sockets;

	struct ReceivedData
	{
		std::vector<char> data;
	};
	Map<TCPSocketData*, ReceivedData> socketReceivedData;

  public:
	~DebugData();
	void operationCreated(CompletionPortOperation* op);
	void operationDestroyed(CompletionPortOperation* op);
	void serverSocketCreated(TCPServerSocket* s);
	void serverSocketDestroyed(TCPServerSocket* s);
	void socketCreated(TCPSocket* s);
	void socketDestroyed(TCPSocket* s);
	void receivedData(TCPSocketData* owner, const char* data, int size);
	void check();
	void reset();
	void checkAndReset();
#else
  public:
	void operationCreated(CompletionPortOperation* op) {}
	void operationDestroyed(CompletionPortOperation* op) {}
	void serverSocketCreated(TCPServerSocket* s) {}
	void serverSocketDestroyed(TCPServerSocket* s) {}
	void socketCreated(TCPSocket* s) {}
	void socketDestroyed(TCPSocket* s) {}
	void receivedData(TCPSocketData* owner, const char* data, int size) {}
	void check() {}
	void reset() {};
	void checkAndReset() {}
#endif
};

extern DebugData debugData;
} // namespace net
} // namespace cz

