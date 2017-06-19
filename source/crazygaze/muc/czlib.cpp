/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czlibPCH.h"
#include "crazygaze/muc/StringUtils.h"
#include "crazygaze/muc/Logging.h"

#if defined(_MSC_VER)
extern "C" {
	_CRTIMP void __cdecl _wassert(_In_z_ const wchar_t * _Message, _In_z_ const wchar_t *_File, _In_ unsigned _Line);
}
#endif

int cz_vsnprintf(char* buffer, int bufSize, const char* format, va_list args)
{
#if CZ_PLATFORM == CZ_PLATFORM_WIN32
	return _vsnprintf(buffer, bufSize, format, args);
#elif CZ_PLATFORM == CZ_PLATFORM_LINUX
	return vsnprintf(buffer, bufSize, format, args);
#else
	#error Implementation missing
#endif
}

int cz_snprintf(char* buffer, int bufSize, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	return cz_vsnprintf(buffer, bufSize, format, args);
}

namespace cz
{

static_assert(sizeof(s8) ==1, "Size mismatch");
static_assert(sizeof(u8) ==1, "Size mismatch");
static_assert(sizeof(s16)==2, "Size mismatch");
static_assert(sizeof(u16)==2, "Size mismatch");
static_assert(sizeof(s32)==4, "Size mismatch");
static_assert(sizeof(u32)==4, "Size mismatch");
static_assert(sizeof(s64)==8, "Size mismatch");
static_assert(sizeof(u64)==8, "Size mismatch");

void _doAssert(const char* file, int line, const char* fmt, ...)
{
	static bool executing;

	// Detect reentrancy, since we call a couple of things from here, that might end up asserting
	if (executing)
		__debugbreak();
	executing = true;

	char buf[1024];
	va_list args;
	va_start(args, fmt);
	cz_vsnprintf(buf, 1024, fmt, args);
	va_end(args);

	// TODO : Add some kind of cross-platform logging

#if CZ_PLATFORM == CZ_PLATFORM_WIN32
    #if defined(_MSC_VER)
	    wchar_t wbuf[1024];
	    wchar_t wfile[1024];

	    mbstowcs(wbuf, buf, 1024);
	    mbstowcs(wfile, file, 1024);

		CZ_LOG(logDefault, Error, "ASSERT: %s,%d: %s\n", file, line, buf);
		if (::IsDebuggerPresent())
		{
			__debugbreak(); // This will break in all builds
		}
		else
		{
			//_wassert(wbuf, wfile, line);
			//DebugBreak();
			__debugbreak(); // This will break in all builds
		}
    #elif defined(__MINGW32__)
	    DebugBreak();
    #endif
#else
    #error debugbreak not implemented for current platform/compiler
#endif

}

#if CZ_PLATFORM==CZ_PLATFORM_WIN32
const char* getLastWin32ErrorMsg(int err)
{
	DWORD errCode = (err==0) ? GetLastError() : err;
	char* errString = cz::getTemporaryString();
	// http://msdn.microsoft.com/en-us/library/ms679351(VS.85).aspx
	int size = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM, // use windows internal message table
		0,       // 0 since source is internal message table
		errCode, // this is the error code returned by WSAGetLastError()
		// Could just as well have been an error code from generic
		// Windows errors from GetLastError()
		0,       // auto-determine language to use
		errString, // this is WHERE we want FormatMessage
		CZ_TEMPORARY_STRING_MAX_SIZE,
		0 );               // 0, since getting message from system tables


	// FormatMessage leaves a new line (\r\n) at the end, so remove if that's the case.
	size_t i = strlen(errString);
	while (errString[i] < ' ')
	{
		errString[i] = 0;
		i--;
	}

	return errString;
}
#endif

}


