cmake_minimum_required(VERSION 3.5)
project(tests)

include(../cmake/utils.cmake)
include(../cmake/ucm.cmake)

ucm_add_files(
		"targetver.h"
		"TestBuffer.cpp"
		"TestQuickVector.cpp"
		"TestChunkBuffer.cpp"
		"TestRingBuffer.cpp"
		"TestSharedQueue.cpp"
		"TestThreadingUtils.cpp"
		"UnitTests.cpp"
		"UnitTestsPCH.cpp"
		"UnitTestsPCH.h"
		TO SOURCE_FILES
		)

add_executable(tests ${SOURCE_FILES})
cz_set_precompiled_header(tests "UnitTestsPCH.h" "UnitTestsPCH.cpp")
target_link_libraries(tests czmuc UnitTest++)
if (MSVC)
	target_link_libraries(tests winmm)
endif()
cz_set_postfix(tests)

