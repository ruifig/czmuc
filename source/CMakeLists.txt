cmake_minimum_required(VERSION 3.5)

include(../cmake/utils.cmake)
include(../cmake/ucm.cmake)

ucm_add_files(
	"crazygaze/muc/Algorithm.h"
	"crazygaze/muc/ArrayView.h"
	"crazygaze/muc/AsyncCommandQueue.cpp"
	"crazygaze/muc/AsyncCommandQueue.h"
	"crazygaze/muc/Buffer.cpp"
	"crazygaze/muc/Buffer.h"
	"crazygaze/muc/Callstack.h"
	"crazygaze/muc/ChildProcessLauncher.cpp"
	"crazygaze/muc/ChildProcessLauncher.h"
	"crazygaze/muc/ChunkBuffer.cpp"
	"crazygaze/muc/ChunkBuffer.h"
	"crazygaze/muc/Concurrent.h"
	"crazygaze/muc/config.h"
	"crazygaze/muc/czmuc.cpp"
	"crazygaze/muc/czmuc.h"
	"crazygaze/muc/czmucPCH.cpp"
	"crazygaze/muc/czmucPCH.h"
	"crazygaze/muc/Expected.h"
	"crazygaze/muc/File.cpp"
	"crazygaze/muc/File.h"
	"crazygaze/muc/Filename.cpp"
	"crazygaze/muc/Filename.h"
	"crazygaze/muc/Filesystem.cpp"
	"crazygaze/muc/Filesystem.h"
	"crazygaze/muc/Filetime.cpp"
	"crazygaze/muc/Filetime.h"
	"crazygaze/muc/FNVHash.h"
	"crazygaze/muc/Guid.cpp"
	"crazygaze/muc/Guid.h"
	"crazygaze/muc/IniFile.cpp"
	"crazygaze/muc/IniFile.h"
	"crazygaze/muc/Json.cpp"
	"crazygaze/muc/Json.h"
	"crazygaze/muc/Logging.cpp"
	"crazygaze/muc/Logging.h"
	"crazygaze/muc/Parameters.cpp"
	"crazygaze/muc/Parameters.h"
	"crazygaze/muc/Random.cpp"
	"crazygaze/muc/Random.h"
	"crazygaze/muc/RingBuffer.cpp"
	"crazygaze/muc/RingBuffer.h"
	"crazygaze/muc/ScopeGuard.h"
	"crazygaze/muc/Semaphore.cpp"
	"crazygaze/muc/Semaphore.h"
	"crazygaze/muc/SharedFlag.cpp"
	"crazygaze/muc/SharedFlag.h"
	"crazygaze/muc/SharedQueue.h"
	"crazygaze/muc/Singleton.cpp"
	"crazygaze/muc/Singleton.h"
	"crazygaze/muc/StringUtils.cpp"
	"crazygaze/muc/StringUtils.h"
	"crazygaze/muc/targetver.h"
	"crazygaze/muc/ThreadingUtils.h"
	"crazygaze/muc/Ticker.h"
	"crazygaze/muc/Timer.cpp"
	"crazygaze/muc/Timer.h"
	"crazygaze/muc/TimerQueue.cpp"
	"crazygaze/muc/TimerQueue.h"
	"crazygaze/muc/UTF8String.cpp"
	"crazygaze/muc/UTF8String.h"
	"crazygaze/muc/WindowsConsole.cpp"
	TO SOURCE_FILES
	)

ucm_add_dirs("crazygaze/muc/detail" TO SOURCE_FILES RECURSIVE)
ucm_remove_files(
	"crazygaze/muc/detail/SFMT/SFMT.c"
	"crazygaze/muc/detail/SFMT/test.c"
	FROM SOURCE_FILES)

add_library(czmuc STATIC ${SOURCE_FILES})
cz_set_precompiled_header(czmuc "czmucPCH.h" "crazygaze/muc/czmucPCH.cpp")
target_include_directories(czmuc PUBLIC "")
cz_set_postfix(czmuc)

