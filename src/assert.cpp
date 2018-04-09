/*

Copyright (c) 2007-2018, Arvid Norberg
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
#include "libtorrent/assert.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_PRODUCTION_ASSERTS
#include <boost/atomic.hpp>
#endif

#if TORRENT_USE_ASSERTS \
	|| defined TORRENT_ASIO_DEBUGGING \
	|| defined TORRENT_PROFILE_CALLS \
	|| defined TORRENT_DEBUG_BUFFERS

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdio> // for snprintf
#include <boost/array.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

// uClibc++ doesn't have cxxabi.h
#if defined __GNUC__ && __GNUC__ >= 3 \
	&& !defined __UCLIBCXX_MAJOR__

#include <cxxabi.h>

std::string demangle(char const* name)
{
// in case this string comes
	// this is needed on linux
	char const* start = strchr(name, '(');
	if (start != NULL)
	{
		++start;
	}
	else
	{
		// this is needed on macos x
		start = strstr(name, "0x");
		if (start != NULL)
		{
			start = strchr(start, ' ');
			if (start != NULL) ++start;
			else start = name;
		}
		else start = name;
	}

	char const* end = strchr(start, '+');
	if (end) while (*(end-1) == ' ') --end;

	std::string in;
	if (end == NULL) in.assign(start);
	else in.assign(start, end);

	size_t len;
	int status;
	char* unmangled = ::abi::__cxa_demangle(in.c_str(), NULL, &len, &status);
	if (unmangled == NULL) return in;
	std::string ret(unmangled);
	free(unmangled);
	return ret;
}
#elif defined _WIN32

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

#include <cstdlib>
#include <cstdio>
#include <csignal>
#include "libtorrent/version.hpp"

#if TORRENT_USE_EXECINFO
#include <execinfo.h>

TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth, void*)
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

#elif defined _WIN32

#include "windows.h"
#include "libtorrent/utf8.hpp"
#include "libtorrent/thread.hpp"

#include "winbase.h"
#include "dbghelp.h"

TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth
	, void* ctx)
{
	// all calls to DbgHlp.dll are thread-unsafe. i.e. they all need to be
	// synchronized and not called concurrently. This mutex serializes access
	static libtorrent::mutex dbghlp_mutex;
	libtorrent::mutex::scoped_lock l(dbghlp_mutex);

	CONTEXT context_record;
	if (ctx)
	{
		context_record = *static_cast<CONTEXT*>(ctx);
	}
	else
	{
		// use the current thread's context
		RtlCaptureContext(&context_record);
	}

	int size = 0;
	boost::array<void*, 50> stack;

	STACKFRAME64 stack_frame;
	memset(&stack_frame, 0, sizeof(stack_frame));
#if defined(_WIN64)
	int const machine_type = IMAGE_FILE_MACHINE_AMD64;
	stack_frame.AddrPC.Offset = context_record.Rip;
	stack_frame.AddrFrame.Offset = context_record.Rbp;
	stack_frame.AddrStack.Offset = context_record.Rsp;
#else
	int const machine_type = IMAGE_FILE_MACHINE_I386;
	stack_frame.AddrPC.Offset = context_record.Eip;
	stack_frame.AddrFrame.Offset = context_record.Ebp;
	stack_frame.AddrStack.Offset = context_record.Esp;
#endif
	stack_frame.AddrPC.Mode = AddrModeFlat;
	stack_frame.AddrFrame.Mode = AddrModeFlat;
	stack_frame.AddrStack.Mode = AddrModeFlat;
	while (StackWalk64(machine_type,
		GetCurrentProcess(),
		GetCurrentThread(),
		&stack_frame,
		&context_record,
		NULL,
		&SymFunctionTableAccess64,
		&SymGetModuleBase64,
		NULL) && size < stack.size())
	{
		stack[size++] = reinterpret_cast<void*>(stack_frame.AddrPC.Offset);
	}

	struct symbol_bundle : SYMBOL_INFO
	{
		wchar_t name[MAX_SYM_NAME];
	};

	HANDLE p = GetCurrentProcess();
	static bool sym_initialized = false;
	if (!sym_initialized)
	{
		sym_initialized = true;
		SymInitialize(p, NULL, true);
	}
	SymRefreshModuleList(p);
	for (int i = 0; i < size && len > 0; ++i)
	{
		DWORD_PTR frame_ptr = reinterpret_cast<DWORD_PTR>(stack[i]);

		DWORD64 displacement = 0;
		symbol_bundle symbol;
		symbol.MaxNameLen = MAX_SYM_NAME;
		symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
		BOOL const has_symbol = SymFromAddr(p, frame_ptr, &displacement, &symbol);

		DWORD line_displacement = 0;
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		BOOL const has_line = SymGetLineFromAddr64(GetCurrentProcess(), frame_ptr,
			&line_displacement, &line);

		int ret = snprintf(out, len, "%2d: %p", i, stack[i]);
		out += ret; len -= ret; if (len <= 0) break;

		if (has_symbol)
		{
			ret = snprintf(out, len, " %s +%-4" PRId64
				, demangle(symbol.Name).c_str(), displacement);
			out += ret; len -= ret; if (len <= 0) break;
		}

		if (has_line)
		{
			ret = snprintf(out, len, " %s:%d"
				, line.FileName, line.LineNumber);
			out += ret; len -= ret; if (len <= 0) break;
		}


		ret = snprintf(out, len, "\n");
		out += ret;
		len -= ret;

		if (i == max_depth && max_depth > 0) break;
	}
}

#else

TORRENT_EXPORT void print_backtrace(char* out, int len, int /*max_depth*/, void* /* ctx */)
{
	out[0] = 0;
	strncat(out, "<not supported>", len);
}

#endif

#endif

#if TORRENT_USE_ASSERTS || defined TORRENT_ASIO_DEBUGGING

#ifdef TORRENT_PRODUCTION_ASSERTS
char const* libtorrent_assert_log = "asserts.log";
namespace {
// the number of asserts we've printed to the log
boost::atomic<int> assert_counter(0);
}
#endif

TORRENT_FORMAT(1,2)
TORRENT_EXPORT void assert_print(char const* fmt, ...)
{
#ifdef TORRENT_PRODUCTION_ASSERTS
	if (assert_counter > 500) return;

	FILE* out = fopen(libtorrent_assert_log, "a+");
	if (out == 0) out = stderr;
#else
	FILE* out = stderr;
#endif
	va_list va;
	va_start(va, fmt);
	vfprintf(out, fmt, va);
	va_end(va);

#ifdef TORRENT_PRODUCTION_ASSERTS
	if (out != stderr) fclose(out);
#endif
}

// we deliberately don't want asserts to be marked as no-return, since that
// would trigger warnings in debug builds of any code coming after the assert
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#endif

TORRENT_EXPORT void assert_fail(char const* expr, int line
	, char const* file, char const* function, char const* value, int kind)
{
#ifdef TORRENT_PRODUCTION_ASSERTS
	// no need to flood the assert log with infinite number of asserts
	if (assert_counter.fetch_add(1) + 1 > 500) return;
#endif

	char stack[8192];
	stack[0] = '\0';
	print_backtrace(stack, sizeof(stack), 0);

	char const* message = "assertion failed. Please file a bugreport at "
		"https://github.com/arvidn/libtorrent/issues\n"
		"Please include the following information:\n\n"
		"version: " LIBTORRENT_VERSION "-" LIBTORRENT_REVISION "\n";

	switch (kind)
	{
		case 1:
			message = "A precondition of a libtorrent function has been violated.\n"
				"This indicates a bug in the client application using libtorrent\n";
	}

	assert_print("%s\n"
#ifdef TORRENT_PRODUCTION_ASSERTS
		"#: %d\n"
#endif
		"file: '%s'\n"
		"line: %d\n"
		"function: %s\n"
		"expression: %s\n"
		"%s%s\n"
		"stack:\n"
		"%s\n"
		, message
#ifdef TORRENT_PRODUCTION_ASSERTS
		, assert_counter.load()
#endif
		, file, line, function, expr
		, value ? value : "", value ? "\n" : ""
		, stack);

	// if production asserts are defined, don't abort, just print the error
#ifndef TORRENT_PRODUCTION_ASSERTS
	// send SIGINT to the current process
	// to break into the debugger
	raise(SIGABRT);
	abort();
#endif
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#elif !TORRENT_USE_ASSERTS

// these are just here to make it possible for a client that built with debug
// enable to be able to link against a release build (just possible, not
// necessarily supported)
TORRENT_FORMAT(1,2)
TORRENT_EXPORT void assert_print(char const*, ...) {}
TORRENT_EXPORT void assert_fail(char const*, int, char const*
	, char const*, char const*, int) {}

#endif

