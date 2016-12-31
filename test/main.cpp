/*

Copyright (c) 2008, Arvid Norberg
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

#include <iostream>
#include <boost/config.hpp>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib> // for exit()
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "setup_transfer.hpp" // for _g_test_failures
#include "test.hpp"
#include "dht_server.hpp" // for stop_dht
#include "peer_server.hpp" // for stop_peer
#include "udp_tracker.hpp" // for stop_udp_tracker
#include <boost/system/system_error.hpp>

#include "libtorrent/assert.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/random.hpp"
#include <csignal>

#ifdef _WIN32
#include <windows.h> // fot SetErrorMode
#include <io.h> // for _dup and _dup2
#include <process.h> // for _getpid
#include <crtdbg.h>

#define dup _dup
#define dup2 _dup2

#else

#include <unistd.h> // for getpid()

#endif

using namespace libtorrent;

// these are global so we can restore them on abnormal exits and print stuff
// out, such as the log
int old_stdout = -1;
int old_stderr = -1;
bool redirect_stdout = true;
bool redirect_stderr = false;
bool keep_files = false;

extern int _g_test_idx;

// the current tests file descriptor
unit_test_t* current_test = nullptr;

void output_test_log_to_terminal()
{
	if (current_test == nullptr
		|| (old_stdout == -1 && old_stderr == -1)
		|| (!redirect_stdout && !redirect_stderr)
		|| current_test->output == nullptr)
		return;

	fflush(stdout);
	fflush(stderr);
	dup2(old_stdout, fileno(stdout));
	dup2(old_stderr, fileno(stderr));

	fseek(current_test->output, 0, SEEK_SET);
	std::printf("\x1b[1m[%s]\x1b[0m\n\n", current_test->name);
	char buf[4096];
	int size = 0;
	do {
		size = int(fread(buf, 1, sizeof(buf), current_test->output));
		if (size > 0) fwrite(buf, 1, size, stdout);
	} while (size > 0);
}

#ifdef _WIN32
LONG WINAPI seh_exception_handler(LPEXCEPTION_POINTERS p)
{
	char stack_text[10000];

#if TORRENT_USE_ASSERTS \
	|| defined TORRENT_ASIO_DEBUGGING \
	|| defined TORRENT_PROFILE_CALLS \
	|| defined TORRENT_DEBUG_BUFFERS
	print_backtrace(stack_text, sizeof(stack_text), 30
		, p->ContextRecord);
#elif defined __FUNCTION__
	strcpy(stack_text, __FUNCTION__);
#else
	strcpy(stack_text, "<stack traces disabled>");
#endif

	int const code = p->ExceptionRecord->ExceptionCode;
	char const* name = "<unknown exception>";
	switch (code)
	{
#define EXC(x) case x: name = #x; break
		EXC(EXCEPTION_ACCESS_VIOLATION);
		EXC(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
		EXC(EXCEPTION_BREAKPOINT);
		EXC(EXCEPTION_DATATYPE_MISALIGNMENT);
		EXC(EXCEPTION_FLT_DENORMAL_OPERAND);
		EXC(EXCEPTION_FLT_DIVIDE_BY_ZERO);
		EXC(EXCEPTION_FLT_INEXACT_RESULT);
		EXC(EXCEPTION_FLT_INVALID_OPERATION);
		EXC(EXCEPTION_FLT_OVERFLOW);
		EXC(EXCEPTION_FLT_STACK_CHECK);
		EXC(EXCEPTION_FLT_UNDERFLOW);
		EXC(EXCEPTION_ILLEGAL_INSTRUCTION);
		EXC(EXCEPTION_IN_PAGE_ERROR);
		EXC(EXCEPTION_INT_DIVIDE_BY_ZERO);
		EXC(EXCEPTION_INT_OVERFLOW);
		EXC(EXCEPTION_INVALID_DISPOSITION);
		EXC(EXCEPTION_NONCONTINUABLE_EXCEPTION);
		EXC(EXCEPTION_PRIV_INSTRUCTION);
		EXC(EXCEPTION_SINGLE_STEP);
		EXC(EXCEPTION_STACK_OVERFLOW);
#undef EXC
	};

	std::printf("exception: (0x%x) %s caught:\n%s\n"
		, code, name, stack_text);

	output_test_log_to_terminal();

	exit(code);
}

#else

void sig_handler(int sig)
{
	char stack_text[10000];

#if TORRENT_USE_ASSERTS \
	|| defined TORRENT_ASIO_DEBUGGING \
	|| defined TORRENT_PROFILE_CALLS \
	|| defined TORRENT_DEBUG_BUFFERS
	print_backtrace(stack_text, sizeof(stack_text), 30);
#elif defined __FUNCTION__
	strcpy(stack_text, __FUNCTION__);
#else
	strcpy(stack_text, "<stack traces disabled>");
#endif
	char const* name = "<unknown signal>";
	switch (sig)
	{
#define SIG(x) case x: name = #x; break
		SIG(SIGSEGV);
#ifdef SIGBUS
		SIG(SIGBUS);
#endif
		SIG(SIGINT);
		SIG(SIGTERM);
		SIG(SIGILL);
		SIG(SIGABRT);
		SIG(SIGFPE);
#ifdef SIGSYS
		SIG(SIGSYS);
#endif
#undef SIG
	};
	std::printf("signal: (%d) %s caught:\n%s\n"
		, sig, name, stack_text);

	output_test_log_to_terminal();

	exit(128 + sig);
}

#endif // _WIN32

void print_usage(char const* executable)
{
	std::printf("%s [options] [tests...]\n"
		"\n"
		"OPTIONS:\n"
		"-h,--help            show this help\n"
		"-l,--list            list the tests available to run\n"
		"-k,--keep            keep files created by the test\n"
		"                     regardless of whether it passed or not\n"
		"-n,--no-redirect     don't redirect test output to\n"
		"                     temporary file, but let it go straight\n"
		"                     to stdout\n"
		"--stderr-redirect    also redirect stderr in addition to stdout\n"
		"\n"
		"for tests, specify one or more test names as printed\n"
		"by -l. If no test is specified, all tests are run\n", executable);
}

EXPORT int main(int argc, char const* argv[])
{
	char const* executable = argv[0];
	// skip executable name
	++argv;
	--argc;

	// pick up options
	while (argc > 0 && argv[0][0] == '-')
	{
		if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0)
		{
			print_usage(executable);
			return 0;
		}

		if (strcmp(argv[0], "-l") == 0 || strcmp(argv[0], "--list") == 0)
		{
			std::printf("TESTS:\n");
			for (int i = 0; i < _g_num_unit_tests; ++i)
			{
				std::printf(" - %s\n", _g_unit_tests[i].name);
			}
			return 0;
		}

		if (strcmp(argv[0], "-n") == 0 || strcmp(argv[0], "--no-redirect") == 0)
		{
			redirect_stdout = false;
		}

		if (strcmp(argv[0], "--stderr-redirect") == 0)
		{
			redirect_stderr = true;
		}

		if (strcmp(argv[0], "-k") == 0 || strcmp(argv[0], "--keep") == 0)
		{
			keep_files = true;
		}
		++argv;
		--argc;
	}

	std::set<std::string> tests_to_run;
	bool filter = false;

	for (int i = 0; i < argc; ++i)
	{
		tests_to_run.insert(argv[i]);
		filter = true;
	}

#ifdef O_NONBLOCK
	// on darwin, stdout is set to non-blocking mode by default
	// which sometimes causes tests to fail with EAGAIN just
	// by printing logs
	int flags = fcntl(fileno(stdout), F_GETFL, 0);
	fcntl(fileno(stdout), F_SETFL, flags & ~O_NONBLOCK);
	flags = fcntl(fileno(stderr), F_GETFL, 0);
	fcntl(fileno(stderr), F_SETFL, flags & ~O_NONBLOCK);
#endif

#ifdef _WIN32
	// try to suppress hanging the process by windows displaying
	// modal dialogs.
	SetErrorMode(SEM_NOALIGNMENTFAULTEXCEPT
		| SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	SetUnhandledExceptionFilter(&seh_exception_handler);

#ifdef _DEBUG
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

#else

	signal(SIGSEGV, &sig_handler);
#ifdef SIGBUS
	signal(SIGBUS, &sig_handler);
#endif
	signal(SIGILL, &sig_handler);
	signal(SIGINT, &sig_handler);
	signal(SIGABRT, &sig_handler);
	signal(SIGFPE, &sig_handler);
#ifdef SIGSYS
	signal(SIGSYS, &sig_handler);
#endif

#endif // _WIN32

	int process_id = -1;
#ifdef _WIN32
	process_id = _getpid();
#else
	process_id = getpid();
#endif
	char dir[40];
	std::snprintf(dir, sizeof(dir), "test_tmp_%u", process_id);
	std::string test_dir = complete(dir);
	error_code ec;
	create_directory(test_dir, ec);
	if (ec)
	{
		std::printf("Failed to create test directory: %s\n", ec.message().c_str());
		return 1;
	}
	int ret;
#ifdef TORRENT_WINDOWS
	SetCurrentDirectoryA(dir);
#else
	ret = chdir(dir);
	if (ret != 0)
	{
		std::printf("failed to change directory to \"%s\": %s"
			, dir, strerror(errno));
		return 1;
	}
#endif
	std::printf("test: %s\ncwd = \"%s\"\nrnd: %x\n"
		, executable, test_dir.c_str(), libtorrent::random(0xffffffff));

	int total_failures = 0;

	if (_g_num_unit_tests == 0)
	{
		std::printf("\x1b[31mTEST_ERROR: no unit tests registered\x1b[0m\n");
		return 1;
	}

	if (redirect_stdout) old_stdout = dup(fileno(stdout));
	if (redirect_stderr) old_stderr = dup(fileno(stderr));

	int num_run = 0;
	for (int i = 0; i < _g_num_unit_tests; ++i)
	{
		if (filter && tests_to_run.count(_g_unit_tests[i].name) == 0)
			continue;

		unit_test_t& t = _g_unit_tests[i];

		if (redirect_stdout || redirect_stderr)
		{
			// redirect test output to a temporary file
			fflush(stdout);
			fflush(stderr);

			FILE* f = tmpfile();
			if (f != nullptr)
			{
				int ret1 = 0;
				if (redirect_stdout) ret1 |= dup2(fileno(f), fileno(stdout));
				if (redirect_stderr) ret1 |= dup2(fileno(f), fileno(stderr));
				if (ret1 >= 0)
				{
					t.output = f;
				}
				else
				{
					std::printf("failed to redirect output: (%d) %s\n"
						, errno, strerror(errno));
				}
			}
			else
			{
				std::printf("failed to create temporary file for redirecting "
					"output: (%d) %s\n", errno, strerror(errno));
			}
		}

		// get proper interleaving of stderr and stdout
		setbuf(stdout, nullptr);
		setbuf(stderr, nullptr);

		_g_test_idx = i;
		current_test = &t;

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif

			_g_test_failures = 0;
			(*t.fun)();
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (boost::system::system_error const& e)
		{
			char buf[200];
			std::snprintf(buf, sizeof(buf), "TEST_ERROR: Terminated with system_error: (%d) [%s] \"%s\""
				, e.code().value()
				, e.code().category().name()
				, e.code().message().c_str());
			report_failure(buf, __FILE__, __LINE__);
		}
		catch (std::exception const& e)
		{
			char buf[200];
			std::snprintf(buf, sizeof(buf), "TEST_ERROR: Terminated with exception: \"%s\"", e.what());
			report_failure(buf, __FILE__, __LINE__);
		}
		catch (...)
		{
			report_failure("TEST_ERROR: Terminated with unknown exception", __FILE__, __LINE__);
		}
#endif

		if (!tests_to_run.empty()) tests_to_run.erase(t.name);

		if (_g_test_failures > 0)
		{
			output_test_log_to_terminal();
		}

		t.num_failures = _g_test_failures;
		t.run = true;
		total_failures += _g_test_failures;
		++num_run;

		if (redirect_stdout && t.output)
			fclose(t.output);
	}

	if (redirect_stdout) dup2(old_stdout, fileno(stdout));
	if (redirect_stderr) dup2(old_stderr, fileno(stderr));

	if (!tests_to_run.empty())
	{
		std::printf("\x1b[1mUNKONWN tests:\x1b[0m\n");
		for (std::set<std::string>::iterator i = tests_to_run.begin()
			, end(tests_to_run.end()); i != end; ++i)
		{
			std::printf("  %s\n", i->c_str());
		}
	}

	if (num_run == 0)
	{
		std::printf("\x1b[31mTEST_ERROR: no unit tests run\x1b[0m\n");
		return 1;
	}

	// just in case of premature exits
	// make sure we try to clean up some
	stop_udp_tracker();
	stop_all_proxies();
	stop_web_server();
	stop_peer();
	stop_dht();

	if (redirect_stdout) fflush(stdout);
	if (redirect_stderr) fflush(stderr);

	ret = print_failures();
#if !defined TORRENT_LOGGING
	if (ret == 0 && !keep_files)
	{
		remove_all(test_dir, ec);
		if (ec)
			std::printf("failed to remove test dir: %s\n", ec.message().c_str());
	}
#endif

	return total_failures ? 333 : 0;
}

