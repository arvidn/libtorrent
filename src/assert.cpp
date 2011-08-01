/*

Copyright (c) 2007, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifdef TORRENT_DEBUG

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#include <string>
#include <cstring>
#include <stdlib.h>

// uClibc++ doesn't have cxxabi.h
#if defined __GNUC__ && !defined __UCLIBCXX_MAJOR__

#include <cxxabi.h>

std::string demangle(char const* name)
{
// in case this string comes
	// this is needed on linux
	char const* start = strchr(name, '(');
	if (start != 0)
	{
		++start;
	}
	else
	{
		// this is needed on macos x
		start = strstr(name, "0x");
		if (start != 0)
		{
			start = strchr(start, ' ');
			if (start != 0) ++start;
			else start = name;
		}
		else start = name;
	}

	char const* end = strchr(start, '+');
	if (end) while (*(end-1) == ' ') --end;

	std::string in;
	if (end == 0) in.assign(start);
	else in.assign(start, end);

	size_t len;
	int status;
	char* unmangled = ::abi::__cxa_demangle(in.c_str(), 0, &len, &status);
	if (unmangled == 0) return in;
	std::string ret(unmangled);
	free(unmangled);
	return ret;
}

#else
std::string demangle(char const* name) { return name; }
#endif

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "libtorrent/version.hpp"

// execinfo.h is available in the MacOS X 10.5 SDK.
#if (defined __linux__ || (defined __APPLE__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050))
#include <execinfo.h>

void print_backtrace(char const* label)
{
	void* stack[50];
	int size = backtrace(stack, 50);
	char** symbols = backtrace_symbols(stack, size);

	fprintf(stderr, "%s\n", label);
	for (int i = 1; i < size; ++i)
	{
		fprintf(stderr, "%d: %s\n", i, demangle(symbols[i]).c_str());
	}

	free(symbols);
}
#else

void print_backtrace(char const* label) {}

#endif

void assert_fail(char const* expr, int line, char const* file, char const* function)
{

	fprintf(stderr, "assertion failed. Please file a bugreport at "
		"http://code.rasterbar.com/libtorrent/newticket\n"
		"Please include the following information:\n\n"
		"version: " LIBTORRENT_VERSION "\n"
		"%s\n"
		"file: '%s'\n"
		"line: %d\n"
		"function: %s\n"
		"expression: %s\n", LIBTORRENT_REVISION, file, line, function, expr);

	print_backtrace("stack:");

 	// send SIGINT to the current process
 	// to break into the debugger
 	raise(SIGINT);
 	abort();
}

#else

void assert_fail(char const* expr, int line, char const* file, char const* function) {}

#endif

