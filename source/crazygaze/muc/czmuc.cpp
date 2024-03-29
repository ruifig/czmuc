/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#include "czmucPCH.h"
#include "crazygaze/muc/StringUtils.h"
#include "crazygaze/muc/Logging.h"
#include "crazygaze/muc/ScopeGuard.h"

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

	executing = false;
}

#if CZ_PLATFORM==CZ_PLATFORM_WIN32
// #CMAKE : Test this for when building with UNICODE
std::string getWin32Error(DWORD err, const char* funcname)
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	if (err == ERROR_SUCCESS)
		err = GetLastError();

	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				   NULL,
				   err,
				   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				   (LPWSTR)&lpMsgBuf,
				   0,
				   NULL);
	SCOPE_EXIT{ LocalFree(lpMsgBuf); };

	int funcnameLength = funcname ? lstrlen((LPCTSTR)funcname) : 0;

	lpDisplayBuf =
		(LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlenW((LPCWSTR)lpMsgBuf) + funcnameLength + 50) * sizeof(wchar_t));
	if (lpDisplayBuf == NULL)
		return "Win32ErrorMsg failed";
	SCOPE_EXIT{ LocalFree(lpDisplayBuf); };

	auto wfuncname = funcname ? widen(funcname) : L"";

	StringCchPrintfW((LPWSTR)lpDisplayBuf,
					 LocalSize(lpDisplayBuf) / sizeof(wchar_t),
					 L"%s failed with error %lu: %s",
					 wfuncname.c_str(),
					 err,
					 (LPWSTR)lpMsgBuf);

	std::wstring ret = (LPWSTR)lpDisplayBuf;

	// Remove the \r\n at the end
	while (ret.size() && ret.back() < ' ')
		ret.pop_back();

	return narrow(ret);
}
#endif

}


