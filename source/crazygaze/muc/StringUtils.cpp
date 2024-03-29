#include "czmucPCH.h"
#include "crazygaze/muc/StringUtils.h"

namespace cz
{

bool notSpace(int a)
{
    return !(a==' ' || a=='\t' || a==0xA || a==0xD);
}

std::vector<std::string> splitString(const char* str, char delimiter)
{
	std::vector<std::string> tokens;

	while(*str!=0)
	{
		// find a delimiter
		const char* ptrToChar = str;
		while(!(*str==0 || *str==delimiter))
			str++;

		size_t numchars = str-ptrToChar;
		tokens.emplace_back(ptrToChar, numchars);

		// skip delimiters
		while(*str!=0 && *str==delimiter)
			str++;
	}

	return tokens;
}

std::vector<std::string> stringSplitIntoLines(const char* textbuffer, int buffersize)
{
	std::vector<std::string> lines;
    if (*textbuffer ==0)
        return lines;;

    const char *s = textbuffer;
    while(*s!=0 && s<textbuffer+buffersize)
    {
        const char* ptrToChar = s;
        while(!(*s==0 || *s==0xA || *s==0xD))
            s++;

        size_t numchars = s-ptrToChar;
		lines.emplace_back(ptrToChar, ptrToChar + numchars);

        // New lines format are:
        // Unix		: 0xA
        // Mac		: 0xD
        // Windows	: 0xD 0xA
        // If windows format a new line has 0xD 0xA, so we need to skip one extra character
        if (*s==0xD && *(s+1)==0xA)
            s++;

        if (*s==0)
            break;

        s++; // skip the newline character
    }

	return lines;
}

char* getTemporaryString()
{
	// Use several static strings, and keep picking the next one, so that callers can hold the string for a while without risk of it
	// being changed by another call.
	__declspec( thread ) static char bufs[CZ_TEMPORARY_STRING_MAX_NESTING][CZ_TEMPORARY_STRING_MAX_SIZE];
	__declspec( thread ) static int nBufIndex=0;

	char* buf = bufs[nBufIndex];
	nBufIndex++;
	if (nBufIndex==CZ_TEMPORARY_STRING_MAX_NESTING)
		nBufIndex = 0;

	return buf;
}

const char* formatString(_Printf_format_string_ const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const char *str= formatStringVA(format, args);
	va_end(args);
	return str;
}

char* formatStringVA(const char* format, va_list argptr)
{
	char* buf = getTemporaryString();
	if (_vsnprintf(buf, CZ_TEMPORARY_STRING_MAX_SIZE, format, argptr) == CZ_TEMPORARY_STRING_MAX_SIZE)
		buf[CZ_TEMPORARY_STRING_MAX_SIZE-1] = 0;
	return buf;
}

bool hasEnding(const std::string& str, const std::string& ending)
{
    if (str.length() >= ending.length()) {
        return (0 == str.compare (str.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool hasEnding(const std::string& str, const char* ending)
{
	const size_t endingLength = strlen(ending);
    if (str.length() >= endingLength) {
        return (0 == str.compare (str.length() - endingLength, endingLength, ending));
    } else {
        return false;
    }
}

#if CZ_PLATFORM_WIN32
std::wstring widen(const std::string& utf8)
{
	if (utf8.empty())
		return std::wstring();

	// Get length (in wchar_t's), so we can reserve the size we need before the
	// actual conversion
	const int length = ::MultiByteToWideChar(CP_UTF8,             // convert from UTF-8
		0,                   // default flags
		utf8.data(),         // source UTF-8 string
		(int)utf8.length(),  // length (in chars) of source UTF-8 string
		NULL,                // unused - no conversion done in this step
		0                    // request size of destination buffer, in wchar_t's
	);
	if (length == 0)
		throw std::exception("Can't get length of UTF-16 string");

	std::wstring utf16;
	utf16.resize(length);

	// Do the actual conversion
	if (!::MultiByteToWideChar(CP_UTF8,             // convert from UTF-8
		0,                   // default flags
		utf8.data(),         // source UTF-8 string
		(int)utf8.length(),  // length (in chars) of source UTF-8 string
		&utf16[0],           // destination buffer
		(int)utf16.length()  // size of destination buffer, in wchar_t's
	))
	{
		throw std::exception("Can't convert string from UTF-8 to UTF-16");
	}

	return utf16;
}


std::string narrow(const std::wstring& str)
{
	if (str.empty())
		return std::string();

	// Get length (in wchar_t's), so we can reserve the size we need before the
	// actual conversion
	const int utf8_length = ::WideCharToMultiByte(CP_UTF8,              // convert to UTF-8
		0,                    // default flags
		str.data(),           // source UTF-16 string
		(int)str.length(),  // source string length, in wchar_t's,
		NULL,                 // unused - no conversion required in this step
		0,                    // request buffer size
		NULL,
		NULL  // unused
	);

	if (utf8_length == 0)
		throw "Can't get length of UTF-8 string";

	std::string utf8;
	utf8.resize(utf8_length);

	// Do the actual conversion
	if (!::WideCharToMultiByte(CP_UTF8,              // convert to UTF-8
		0,                    // default flags
		str.data(),           // source UTF-16 string
		(int)str.length(),    // source string length, in wchar_t's,
		&utf8[0],             // destination buffer
		(int)utf8.length(),   // destination buffer size, in chars
		NULL,
		NULL  // unused
	))
	{
		throw "Can't convert from UTF-16 to UTF-8";
	}

	return utf8;
}
#endif

} // namespace cz


