/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com

	purpose:

*********************************************************************/

#include "czmucPCH.h"
#include "crazygaze/muc/Filesystem.h"
#include "crazygaze/muc/Logging.h"
#include "Shlwapi.h"
#include "Shlobj.h"
#include "Psapi.h"
#include <strsafe.h>

namespace cz
{

CZ_DEFINE_SINGLETON(Filesystem)

// Per-platform helper code
namespace
{
#if CZ_PLATFORM == CZ_PLATFORM_WIN32
	void _getCurrentDirectory(Filename& dest)
	{
		const int bufferLength = MAX_PATH;
		wchar_t buf[bufferLength+1];
		buf[0] = 0;
		CZ_CHECK(GetCurrentDirectoryW(bufferLength, buf)!=0);
		dest = Filename(std::wstring(buf));
		dest += '\\';
	}

	void _getAndSetDefaultCWD(Filename& dest)
	{
		const int bufferLength = MAX_PATH;
		wchar_t buf[bufferLength+1];
		// Change Current directory to where the executable is
		CZ_CHECK( GetModuleFileNameExW(GetCurrentProcess(), NULL, buf, bufferLength) !=0 );
		Filename f = &buf[0];
		CZ_CHECK( SetCurrentDirectoryW(f.getDirectory().widen().c_str()) !=0 );
		CZ_CHECK( GetCurrentDirectoryW(bufferLength, buf)!=0 );
		dest = Filename(std::wstring(buf));
		dest += '\\';
	}

	bool _isExistingDirectory(const cz::UTF8String& path)
	{
		return PathIsDirectoryW(path.widen().c_str()) ? true : false;
	}

	bool _isExistingFile(const cz::UTF8String& filename)
	{
		DWORD dwAttrib = GetFileAttributesW(filename.widen().c_str());

		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
	}

	bool _createDirectory(const cz::UTF8String& path)
	{
		return SHCreateDirectoryExW(NULL, path.widen().c_str(), NULL)==ERROR_SUCCESS ? true : false;
	}

#else
	#error Not implemented
#endif

}

Filesystem::Filesystem()
{
	_getCurrentDirectory(mOriginalCWD);
	mCWD = mOriginalCWD;
}

Filesystem::~Filesystem()
{
}

const Filename& Filesystem::getCWD() const
{
	return mCWD;
}

const Filename& Filesystem::getOriginalCWD() const
{
	return mOriginalCWD;
}

void Filesystem::setCWD(const UTF8String& path)
{
	mCWD = path;
}

void Filesystem::setCWDToDefault()
{
	_getAndSetDefaultCWD(mCWD);
}

Filename Filesystem::getExePath()
{
	const int bufferLength = MAX_PATH;
	wchar_t buf[bufferLength + 1];
	CZ_CHECK(GetModuleFileNameExW(GetCurrentProcess(), NULL, buf, bufferLength) != 0);
	Filename f = &buf[0];
	return f;
}

bool Filesystem::isExistingDirectory(const UTF8String& path)
{
	return _isExistingDirectory(path);
}

bool Filesystem::createRelativePath(UTF8String& dst, const UTF8String& from, const UTF8String& to)
{
	wchar_t szOut[MAX_PATH+1] = L"";

	if (!PathRelativePathToW(
			szOut,
			from.widen().c_str(), FILE_ATTRIBUTE_DIRECTORY,
			to.widen().c_str(), FILE_ATTRIBUTE_NORMAL))
		return false;

	dst = szOut;
	return true;
}

bool Filesystem::isExistingFile(const UTF8String& filename)
{
	return _isExistingFile(filename);
}

bool Filesystem::createDirectory(const UTF8String& path)
{
	return _createDirectory(path);
}

bool Filesystem::fullPath(UTF8String& dst, const UTF8String& path, const UTF8String& root)
{
	wchar_t fullpathbuf[MAX_PATH+1];
	wchar_t srcfullpath[MAX_PATH+1];
	UTF8String tmp = isRelativePath(path) ? root+path : path;
	wcscpy(srcfullpath, tmp.widen().c_str());
	wchar_t* d = srcfullpath;
	wchar_t* s = srcfullpath;
	while(*s)
	{
		if (*s == '/')
			*s = '\\';
		*d++= *s;

		// Skip any repeated separator
		if (*s == '\\')
		{
			s++;
			while (*s && (*s == '\\' || *s == '/'))
				s++;
		}
		else
		{
			s++;
		}
	}
	*d = 0;

	bool res = PathCanonicalizeW(fullpathbuf, srcfullpath) ? true : false;
	if (res)
		dst = fullpathbuf;
	return res;
}

UTF8String Filesystem::pathStrip(const UTF8String& path)
{
	wchar_t srcfullpath[MAX_PATH+1];
	wcscpy(srcfullpath, path.widen().c_str());
	wchar_t* p = srcfullpath;
	PathStripPathW(p);
	return UTF8String(p);
}

bool Filesystem::isRelativePath(const UTF8String& path)
{
	return PathIsRelativeW(path.widen().c_str()) ? true : false;
}


// Copied from  http://msdn.microsoft.com/en-gb/library/windows/desktop/aa365200(v=vs.85).aspx
std::vector<Filesystem::FileInfo> Filesystem::getFilesInDirectory(const UTF8String& path, const UTF8String& wildcard, bool includeDirectories)
{
	std::vector<FileInfo> res;

	// Check that the input path plus 3 is not longer than MAX_PATH.
	// Three characters are for the "\*" plus NULL appended below.

	size_t length_of_arg;
	wchar_t szDir[MAX_PATH];
	WIN32_FIND_DATAW ffd;

	CZ_CHECK(StringCchLengthW(path.widen().c_str(), MAX_PATH, &length_of_arg)==S_OK);

	if (length_of_arg > (MAX_PATH - 3))
	{
		CZ_LOG(logDefault, Warning, "Directory path is too long");
		return res;
	}

	// Prepare string for use with FindFile functions.  First, copy the
	// string to a buffer, then append '\*' to the directory name.
	StringCchCopyW(szDir, MAX_PATH, path.widen().c_str());
	StringCchCatW(szDir, MAX_PATH, (UTF8String("\\")+wildcard).widen().c_str());

	// Find the first file in the directory.
	auto hFind = FindFirstFileW(szDir, &ffd);

	if (INVALID_HANDLE_VALUE == hFind)
	{ 
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return res;
		CZ_LOG(logDefault, Warning, "%s failed: %s", __FUNCTION__, getWin32Error().c_str());
		return res;
	}

	// List all the files in the directory with some info about them.

	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (includeDirectories &&
				(StrCmpW(ffd.cFileName, L".")!=0) &&
				(StrCmpW(ffd.cFileName, L"..")!=0))
			{
				res.push_back({ Type::Directory, Filename(ffd.cFileName), 0 });
			}
		}
		else
		{
			LARGE_INTEGER filesize;
			filesize.LowPart = ffd.nFileSizeLow;
			filesize.HighPart = ffd.nFileSizeHigh;
			res.push_back({ Type::File, Filename(ffd.cFileName), filesize.QuadPart });
		}
	} while (FindNextFileW(hFind, &ffd) != 0);


	return res;
}

} // namespace cz

