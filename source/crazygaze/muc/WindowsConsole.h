/********************************************************************
	CrazyGaze (http://www.crazygaze.com)
	Author : Rui Figueira
	Email  : rui@crazygaze.com
	
	purpose:
	
*********************************************************************/

#pragma once

#include "crazygaze/czlib.h"
#include "crazygaze/Logging.h"

namespace cz
{

class WindowsConsole : public LogOutput
{
  public:
	enum Colour
	{
		CONSOLE_COLOUR_BLACK = 0x0,
		CONSOLE_COLOUR_BLUE = 0x01,
		CONSOLE_COLOUR_GREEN = 0x02,
		CONSOLE_COLOUR_RED = 0x04,
		CONSOLE_COLOUR_CYAN = CONSOLE_COLOUR_BLUE + CONSOLE_COLOUR_GREEN,
		CONSOLE_COLOUR_PINK = CONSOLE_COLOUR_BLUE + CONSOLE_COLOUR_RED,
		CONSOLE_COLOUR_YELLOW = CONSOLE_COLOUR_GREEN + CONSOLE_COLOUR_RED,
		CONSOLE_COLOUR_WHITE = CONSOLE_COLOUR_BLUE + CONSOLE_COLOUR_GREEN + CONSOLE_COLOUR_RED,
		CONSOLE_COLOUR_BRIGHT_BLUE = CONSOLE_COLOUR_BLUE + 0x08,
		CONSOLE_COLOUR_BRIGHT_GREEN = CONSOLE_COLOUR_GREEN + 0x08,
		CONSOLE_COLOUR_BRIGHT_RED = CONSOLE_COLOUR_RED + 0x08,
		CONSOLE_COLOUR_BRIGHT_CYAN = CONSOLE_COLOUR_CYAN + 0x08,
		CONSOLE_COLOUR_BRIGHT_PINK = CONSOLE_COLOUR_PINK + 0x08,
		CONSOLE_COLOUR_BRIGHT_YELLOW = CONSOLE_COLOUR_YELLOW + 0x08,
		CONSOLE_COLOUR_BRIGHT_WHITE = CONSOLE_COLOUR_WHITE + 0x08
	};

	WindowsConsole();
	WindowsConsole(int width, int height, int bufferWidth, int bufferHeight);
	virtual ~WindowsConsole();
	void init(int width = 100, int height = 30, int bufferWidth = 110, int bufferHeight = 1000);
	void redirectStd(bool redirectStdOut = true, bool redirectStdIn = true, bool redirectStdErr = true,
	                 bool redirectCPP = true);
	void center();
	void minimize();
	void setTextColour(Colour colour);
	void print(const char* str);
	void printf(const char* format, ...);

	//! Enables UTF8 support for the console
	/*!
	 * \note For this to work you need to change the console font to other than a "Raster font"
	 */
	void enableUTF8Support();

  private:
	virtual void log(const char* file, int line, const LogCategoryBase* category, LogVerbosity verbosity,
	                 const char* msg) override;
	Colour mCurrColour;
	HANDLE mConsoleHandle;

	// Each process can only have one console, so this variable tells if the console was created
	// by this object, or if there was a console available already.
	// When there was a console available, it will not be destroyed.
	bool mOwnsConsole;
};

} // namespace cz
