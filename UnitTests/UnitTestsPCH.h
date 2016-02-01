/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "targetver.h"
#include <stdio.h>
#include <tchar.h>
#include <random>
#include <chrono>
#include <inttypes.h>
#include <map>

//
// UnitTest++
//
#include "UnitTest++\UnitTest++.h"
#include "UnitTest++\CurrentTest.h"

//
// czlib
//
#include "crazygaze/czlibPCH.h"
#include "crazygaze/TypeTraits.h"
#include "crazygaze/WindowsConsole.h"
#include "crazygaze/Any.h"
#include "crazygaze/Future.h"
#include "crazygaze/Semaphore.h"
#include "crazygaze/ChunkBuffer.h"
#include "crazygaze/ThreadingUtils.h"
#include "crazygaze/net/TCPSocket.h"
#include "crazygaze/net/TCPServer.h"
#include "crazygaze/rpc/RPCUtils.h"
#include "crazygaze/rpc/RPCConnection.h"
#include "crazygaze/rpc/TCPTransport.h"
#include "crazygaze/rpc/GenericRPC.h"
#include "crazygaze/Concurrent.h"
#include "crazygaze/AsyncCommandQueue.h"
#include "crazygaze/Buffer.h"
#include "crazygaze/RingBuffer.h"
#include "crazygaze/DeadlineTimer.h"
#include "crazygaze/TimerQueue.h"

CZ_DECLARE_LOG_CATEGORY(logTests, Log, Log)

#if CZ_DEBUG
	CZ_DECLARE_LOG_CATEGORY(logTestsVerbose, Log, Log)
#else
	CZ_DECLARE_LOG_CATEGORY(logTestsVerbose, Log, Fatal)
#endif 

void spinMs(double ms);

