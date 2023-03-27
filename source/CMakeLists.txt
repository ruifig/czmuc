cmake_minimum_required(VERSION 3.16)

set(CZMUC_SOURCE_FILES
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
	"crazygaze/muc/czmucPCH.h"
	"crazygaze/muc/DateTime.h"
	"crazygaze/muc/DateTime.cpp"
	"crazygaze/muc/Expected.h"
	"crazygaze/muc/File.cpp"
	"crazygaze/muc/File.h"
	"crazygaze/muc/FileLogOutput.cpp"
	"crazygaze/muc/FileLogOutput.h"
	"crazygaze/muc/Filename.cpp"
	"crazygaze/muc/Filename.h"
	"crazygaze/muc/Filesystem.cpp"
	"crazygaze/muc/Filesystem.h"
	"crazygaze/muc/Filetime.cpp"
	"crazygaze/muc/Filetime.h"
	"crazygaze/muc/FNVHash.h"
	"crazygaze/muc/Guid.cpp"
	"crazygaze/muc/Guid.h"
	"crazygaze/muc/IdAccessible.h"
	"crazygaze/muc/IniFile.cpp"
	"crazygaze/muc/IniFile.h"
	"crazygaze/muc/Json.cpp"
	"crazygaze/muc/Json.h"
	"crazygaze/muc/Logging.cpp"
	"crazygaze/muc/Logging.h"
	"crazygaze/muc/NetworkUtils.h"
	"crazygaze/muc/NetworkUtils.cpp"
	"crazygaze/muc/Parameters.cpp"
	"crazygaze/muc/Parameters.h"
	"crazygaze/muc/QuickVector.h"
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
	"crazygaze/muc/WindowsConsole.h"
	"crazygaze/muc/czMUCPCH.h"
	)

# Add SFMT sources
set(SFMT_SOURCE_FILES
	"crazygaze/muc/detail/SFMT/SFMT-alti.h"
	"crazygaze/muc/detail/SFMT/SFMT-params.h"
	"crazygaze/muc/detail/SFMT/SFMT-params11213.h"
	"crazygaze/muc/detail/SFMT/SFMT-params1279.h"
	"crazygaze/muc/detail/SFMT/SFMT-params132049.h"
	"crazygaze/muc/detail/SFMT/SFMT-params19937.h"
	"crazygaze/muc/detail/SFMT/SFMT-params216091.h"
	"crazygaze/muc/detail/SFMT/SFMT-params2281.h"
	"crazygaze/muc/detail/SFMT/SFMT-params4253.h"
	"crazygaze/muc/detail/SFMT/SFMT-params44497.h"
	"crazygaze/muc/detail/SFMT/SFMT-params607.h"
	"crazygaze/muc/detail/SFMT/SFMT-params86243.h"
	"crazygaze/muc/detail/SFMT/SFMT-sse2.h"
	"crazygaze/muc/detail/SFMT/SFMT.h"
   )

#Add utfcpp sources
set(UTFCPP_SOURCE_FILES
		"crazygaze/muc/detail/utfcpp/source/utf8.h"
		"crazygaze/muc/detail/utfcpp/source/utf8/checked.h"
		"crazygaze/muc/detail/utfcpp/source/utf8/core.h"
		"crazygaze/muc/detail/utfcpp/source/utf8/unchecked.h"
	)

source_group("crazygaze\\muc" FILES ${CZMUC_SOURCE_FILES})
source_group("crazygaze\\muc\\detail\\SFMT" FILES ${SFMT_SOURCE_FILES})
source_group("crazygaze\\muc\\detail\\utfcpp" FILES ${UTFCPP_SOURCE_FILES})

add_library(czmuc STATIC
		${CZMUC_SOURCE_FILES}
		${SFMT_SOURCE_FILES}
		${UTFCPP_SOURCE_FILES})
add_library(cz::muc ALIAS czmuc)
set_target_properties(czmuc PROPERTIES FOLDER "${CZ_IDE_FOLDER}${PROJECT_NAME}")

target_include_directories(czmuc PUBLIC "")
set_target_properties(czmuc
		PROPERTIES LINKER_LANGUAGE CXX)
#target_compile_features(czmuc PUBLIC cxx_std_17)
target_precompile_headers(czmuc PUBLIC "crazygaze/muc/czMUCPCH.h")

if (MSVC)
	target_sources(czmuc PUBLIC czmuc.natvis)
	target_link_libraries(czmuc PRIVATE Shlwapi Winmm Iphlpapi.lib)
endif()

