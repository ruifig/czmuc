/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com

	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/muc/czlib.h"
#include "crazygaze/muc/Filename.h"
#include "crazygaze/muc/UTF8String.h"

namespace cz
{
	class File
	{
	public:
		
		enum Mode
		{
			FILEMODE_READ,
			FILEMODE_WRITE,
			FILEMODE_READWRITE_EXISTING,
			FILEMODE_APPEND,
			FILEMODE_APPENDNEW
		};
		enum SeekMode
		{
			FILESEEK_SET,
			FILESEEK_CURRENT,
			FILESEEK_END
		};

		File();
		File(const UTF8String& filename, Mode mode);
		File(const char* filename, Mode mode);
		~File();
		File(File&& other);
		File& operator=(File&& other);

		void open(const UTF8String& filename, Mode mode);
		void open(const char* filename, Mode mode);
		bool try_open(const UTF8String& filename, Mode mode);
		bool try_open(const char* filename, Mode mode);
		bool isOpen() const;
		size_t write(const void* buffer, int size, int count);
		size_t read(void* buffer, int size, int count);
		size_t write(const void* buffer, int bytes);
		size_t read(void* buffer, int bytes);
		bool eof() const;
		int tell() const;
		bool seek(int offset, SeekMode seekmode);

		int size();
		void close();
		const Filename& getFilename() const
		{
			return mName;
		}

	protected:
		bool _open(const UTF8String& filename, Mode mode, bool raiseError);

		Filename mName;
		FILE* mFile;
		Mode mMode;
	private:
		// Left unimplemented, so it doesn't compile (we can't use these functions)
		bool operator=(const File& other);
		File& operator==(const File& other);
		explicit File(const File& other);

	};
}


