cmake_minimum_required(VERSION 3.16)

project(czmuc)

add_subdirectory(source)

if (TARGET UnitTest++)
	add_subdirectory(tests)
endif()

