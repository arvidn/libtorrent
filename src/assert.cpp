/*

Copyright (c) 2007-2014, Arvid Norberg
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

#include "libtorrent/config.hpp"

#if TORRENT_PRODUCTION_ASSERTS
#include <boost/detail/atomic_count.hpp>
#endif

#if defined TORRENT_DEBUG || defined TORRENT_ASIO_DEBUGGING \
	|| TORRENT_RELEASE_ASSERTS || defined TORRENT_DEBUG_BUFFERS

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#include <string>
#include <cstring>
#include <stdlib.h>

// uClibc++ doesn't have cxxabi.h
#if defined __GNUC__ && __GNUC__ >= 3 \
	&& !defined __UCLIBCXX_MAJOR__

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
#elif defined WIN32

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // XP

#include "windows.h"
#include "dbghelp.h"

std::string demangle(char const* name) 
{ 
	char demangled_name[256];
	if (UnDecorateSymbolName(name, demangled_name, sizeof(demangled_name), UNDNAME_NO_THROW_SIGNATURES) == 0)
		demangled_name[0] = 0;
	return demangled_name;
}

#else
std::string demangle(char const* name) { return name; }
#endif

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "libtorrent/version.hpp"

#if TORRENT_USE_EXECINFO
#include <execinfo.h>

TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth)
{
	void* stack[50];
	int size = backtrace(stack, 50);
	char** symbols = backtrace_symbols(stack, size);

	for (int i = 1; i < size && len > 0; ++i)
	{
		int ret = snprintf(out, len, "%d: %s\n", i, demangle(symbols[i]).c_str());
		out += ret;
		len -= ret;
		if (i - 1 == max_depth && max_depth > 0) break;
	}

	free(symbols);
}

// visual studio 9 and up appears to support this
#elif defined WIN32 && _MSC_VER >= 1500

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // XP

#include "windows.h"
#include "libtorrent/utf8.hpp"

#include "winbase.h"
#include "dbghelp.h"

TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth)
{
	typedef USHORT (WINAPI *RtlCaptureStackBackTrace_t)(
		__in ULONG FramesToSkip,
		__in ULONG FramesToCapture,
		__out PVOID *BackTrace,
		__out_opt PULONG BackTraceHash);

	static RtlCaptureStackBackTrace_t RtlCaptureStackBackTrace = 0;

	if (RtlCaptureStackBackTrace == 0)
	{
		// we don't actually have to free this library, everyone has it loaded
		HMODULE lib = LoadLibrary(TEXT("kernel32.dll"));
		RtlCaptureStackBackTrace = (RtlCaptureStackBackTrace_t)GetProcAddress(lib, "RtlCaptureStackBackTrace");
		if (RtlCaptureStackBackTrace == 0)
		{
			out[0] = 0;
			return;
		}
	}

	int i;
	void* stack[50];
	int size = CaptureStackBackTrace(0, 50, stack, 0);

	SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR), 1);
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	HANDLE p = GetCurrentProcess();
	static bool sym_initialized = false;
	if (!sym_initialized)
	{
		sym_initialized = true;
		SymInitialize(p, NULL, true);
	}
	for (i = 0; i < size && len > 0; ++i)
	{
		int ret;
		if (SymFromAddr(p, uintptr_t(stack[i]), 0, symbol))
			ret = snprintf(out, len, "%d: %s\n", i, symbol->Name);
		else
			ret = snprintf(out, len, "%d: <unknown>\n", i);

		out += ret;
		len -= ret;
		if (i == max_depth && max_depth > 0) break;
	}
	free(symbol);
}

#else

TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth) {}

#endif

#endif

#if TORRENT_USE_ASSERTS || defined TORRENT_ASIO_DEBUGGING

#if TORRENT_PRODUCTION_ASSERTS
char const* libtorrent_assert_log = "asserts.log";
// the number of asserts we've printed to the log
boost::detail::atomic_count assert_counter(0);
#endif

TORRENT_EXPORT void assert_fail(char const* expr, int line, char const* file
	, char const* function, char const* value, int kind)
{
#if TORRENT_PRODUCTION_ASSERTS
	// no need to flood the assert log with infinite number of asserts
	if (++assert_counter > 500) return;

	FILE* out = fopen(libtorrent_assert_log, "a+");
	if (out == 0) out = stderr;
#else
	FILE* out = stderr;
#endif

	char stack[8192];
	stack[0] = '\0';
	print_backtrace(stack, sizeof(stack), 0);

	char const* message = "assertion failed. Please file a bugreport at "
		"https://github.com/arvidn/libtorrent/issues\n"
		"Please include the following information:\n\n"
		"version: " LIBTORRENT_VERSION "\n"
		LIBTORRENT_REVISION "\n";

	switch (kind)
	{
		case 1:
			message = "A precondition of a libtorrent function has been violated.\n"
				"This indicates a bug in the client application using libtorrent\n";
	}

	fprintf(out, "%s\n"
		"file: '%s'\n"
		"line: %d\n"
		"function: %s\n"
		"expression: %s\n"
		"%s%s\n"
		"stack:\n"
		"%s\n"
		, message, file, line, function, expr
		, value ? value : "", value ? "\n" : ""
		, stack);

	// if production asserts are defined, don't abort, just print the error
#if TORRENT_PRODUCTION_ASSERTS
	if (out != stderr) fclose(out);
#else
	// send SIGINT to the current process
	// to break into the debugger
	raise(SIGINT);
	abort();
#endif
}

#else

TORRENT_EXPORT void assert_fail(char const* expr, int line, char const* file
	, char const* function, char const* value, int kind) {}

#endif

