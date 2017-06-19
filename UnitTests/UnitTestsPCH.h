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
// czmuc
//
#include "crazygaze/muc/czmucPCH.h"
#include "crazygaze/muc/WindowsConsole.h"
#include "crazygaze/muc/Future.h"
#include "crazygaze/muc/Semaphore.h"
#include "crazygaze/muc/ChunkBuffer.h"
#include "crazygaze/muc/ThreadingUtils.h"
#include "crazygaze/muc/Concurrent.h"
#include "crazygaze/muc/AsyncCommandQueue.h"
#include "crazygaze/muc/Buffer.h"
#include "crazygaze/muc/RingBuffer.h"
#include "crazygaze/muc/TimerQueue.h"
#include "crazygaze/muc/Callstack.h"

CZ_DECLARE_LOG_CATEGORY(logTests, Log, Log)

#if CZ_DEBUG
	CZ_DECLARE_LOG_CATEGORY(logTestsVerbose, Log, Log)
#else
	CZ_DECLARE_LOG_CATEGORY(logTestsVerbose, Log, Fatal)
#endif 

void spinMs(double ms);
