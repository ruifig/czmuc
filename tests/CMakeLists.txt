cmake_minimum_required(VERSION 3.16)

set(SOURCE_FILES
		"targetver.h"
		"TestBuffer.cpp"
		"TestQuickVector.cpp"
		"TestChunkBuffer.cpp"
		"TestRingBuffer.cpp"
		"TestSharedQueue.cpp"
		"TestThreadingUtils.cpp"
		"UnitTests.cpp"
		"UnitTestsPCH.h"
		)

add_executable(czmuc_tests ${SOURCE_FILES})
target_link_libraries(czmuc_tests cz::muc UnitTest++)
if (MSVC)
	target_link_libraries(czmuc_tests winmm)
endif()

target_precompile_headers(czmuc_tests PUBLIC "UnitTestsPCH.h")

set_property(TARGET czmuc_tests PROPERTY FOLDER "${CZ_IDE_FOLDER}${PROJECT_NAME}")

