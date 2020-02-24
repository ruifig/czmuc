cmake_minimum_required(VERSION 3.16)

project(czmuc)

add_subdirectory(source)

# If we have a unittest-cpp folder but the parent project didn't add UnitTest++, then automatically
# define the UnitTest++ target
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/unittest-cpp" AND NOT TARGET UnitTest++)
	message("unittest-cpp folder found in ${PROJECT_NAME}'s root. Adding UnitTest++ target")
	add_subdirectory(unittest-cpp)
endif()

if (TARGET UnitTest++)
	message("Enabling ${PROJECT_NAME} tests")
	add_subdirectory(tests)
endif()

