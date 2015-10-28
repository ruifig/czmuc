/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once


#define CZ_PLATFORM_WIN32 1
#define CZ_PLATFORM_LINUX 2
#define CZ_ARCH_32 1
#define CZ_ARCH_64 2

//
// Find what platform we are compiling for
//
#if defined(_WIN32)
	#define CZ_PLATFORM CZ_PLATFORM_WIN32
	#if (_WIN32_WINNT < 0x0501)
		#error Only supported for Windows XP or above. Set _WIN32_WINNT to 0x0501 or above
	#endif
#elif __linux__
	#define CZ_PLATFORM CZ_PLATFORM_LINUX
#endif

//
// Find if we are building for 32 bits or 64 bits
//
#if defined(__x86_64__) || defined(_M_X64) || defined(__powerpc64__) || defined(__alpha__) || defined(__ia64__) || defined(__s390__) || defined(__s390x__)
	#define CZ_ARCH CZ_ARCH_64
#else
	#define CZ_ARCH CZ_ARCH_32
#endif

//
// Find if we are building DEBUG or RELEASE
//
#if defined(DEBUG) || defined(_DEBUG) || defined(_DOXYGEN)
	#define CZ_DEBUG 1
	#if CZ_USER_BUILD
		#error "CZ_USER_BUILD can't be set for a debug build"
	#endif
#else
	#if !defined(NDEBUG)
		#error No DEBUG/_DEBUG or NDEBUG defined
	#endif
	#define CZ_DEBUG 0
#endif

//
// 
#ifndef CZ_USER_BUILD
	#define CZ_USER_BUILD 0
#endif

#include "crazygaze/config.h"


//
// Disable some warnings
#if (CZ_PLATFORM==CZ_PLATFORM_WIN32) && defined(_MSC_VER)
#	pragma warning( disable: 4996) // Disable all the deprecated warning for C standard libraries
#endif


#if CZ_PLATFORM == CZ_PLATFORM_WIN32
	#include <WS2tcpip.h>
	#include <MSWSock.h>
	#include <winsock2.h>
	#include <windows.h>
#endif

#ifdef max
	#undef max
	#undef min
#endif

#include <assert.h>

//
// Assert
//
namespace cz
{
	// Assert types
	void _doAssert(const char* file, int line, const char* fmt, ...);
#if CZ_PLATFORM==CZ_PLATFORM_WIN32
	const char* getLastWin32ErrorMsg(int err=0);
#endif
} // namespace cz


#if CZ_USER_BUILD
	#define CZ_ASSERT(expression) ((void)0)
	#define CZ_ASSERT_F(expression, fmt, ...) ((void)0)
	#define CZ_CHECK(expression) expression
	#define CZ_UNEXPECTED() ::cz::_doAssert(__FILE__, __LINE__, "Unexpected code path")
#else

/*! Checks if the expression is true/false and causes an assert if it's false.
 @hideinitializer
 Depending on the build configuration, asserts might be enabled for release build too
 */
	#define CZ_ASSERT(expression) if (!(expression)) { ::cz::_doAssert(__FILE__, __LINE__, #expression); }
	//#define CZ_ASSERT(expression) (void(0))

/*! Checks if the expression is true/false and causes an assert if it's false.
@hideinitializer
The difference between this and \link CZ_ASSERT \endlink is that it's suitable to display meaningful messages.
\param expression Expression to check
\param fmt printf style format string
*/
	#define CZ_ASSERT_F(expression, fmt, ...) if (!(expression)) { ::cz::_doAssert(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } // By using ##__VA_ARGS__ , it will remove the last comma, if __VA_ARGS__ is empty

/*! Evaluates the expression, and asserts if asserts are enabled.
 @hideinitializer
 Note that even if asserts are disabled, it still evaluates the expression (it's not compiled-out like the standard 'assert' for release builds),
 so can be used to check if for example a function returned a specific value:
 \code
 CZ_CHECK( doSomethingCritical()==true );
 \endcode
 */
	#define CZ_CHECK(expression) if (!(expression)) { ::cz::_doAssert(__FILE__, __LINE__, #expression); }

	#define CZ_UNEXPECTED() ::cz::_doAssert(__FILE__, __LINE__, "Unexpected code path")
	#define CZ_UNEXPECTED_F(fmt, ...) ::cz::_doAssert(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#endif

struct ReentrantCheck
{
	ReentrantCheck(int* counter_)
	{
		counter = counter_;
		CZ_ASSERT(*counter==0);
		*counter++;
	}
	~ReentrantCheck()
	{
		counter--;
	}

	int *counter;
};

#define CHECK_REENTRANCE \
	static int reentrance_check=0; \
	ReentrantCheck reentrance_check_obj(&reentrance_check);

#define CZ_S8_MIN -128
#define CZ_S8_MAX 127
#define CZ_U8 MAX 255
#define CZ_S16_MIN -32768
#define CZ_S16_MAX 32767
#define CZ_U16 MAX 65535
#define CZ_S32_MIN (-2147483647-1)
#define CZ_S32_MAX 2147483647
#define CZ_U32_MAX 0xffffffff

namespace cz
{
	typedef unsigned char u8;
	typedef signed char s8;
	typedef unsigned short u16;
	typedef signed short s16;
	typedef unsigned int u32;
	typedef signed int s32;
	typedef signed long long s64;
	typedef unsigned long long u64;
}

/*!
	@}
*/