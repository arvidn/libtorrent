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
#include <stdio.h>
#include <stdlib.h> // for exit()
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "setup_transfer.hpp" // for _g_test_failures
#include "test.hpp"
#include "dht_server.hpp" // for stop_dht
#include "peer_server.hpp" // for stop_peer
#include "udp_tracker.hpp" // for stop_udp_tracker

#include "libtorrent/assert.hpp"
#include "libtorrent/file.hpp"
#include <signal.h>

#ifdef WIN32
#include <windows.h> // fot SetErrorMode
#include <io.h> // for _dup and _dup2
#include <process.h> // for _getpid

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
bool redirect_output = true;
bool keep_files = false;

extern int _g_test_idx;

// the current tests file descriptor
unit_test_t* current_test = NULL;

void output_test_log_to_terminal()
{
	if (current_test == NULL || old_stdout == -1 || old_stderr == -1
		|| !redirect_output || current_test->output == NULL)
		return;

	fflush(stdout);
	fflush(stderr);
	dup2(old_stdout, fileno(stdout));
	dup2(old_stderr, fileno(stderr));

	fseek(current_test->output, 0, SEEK_SET);
	fprintf(stderr, "\x1b[1m[%s]\x1b[0m\n\n", current_test->name);
	char buf[4096];
	int size = 0;
	do {
		size = fread(buf, 1, sizeof(buf), current_test->output);
		if (size > 0) fwrite(buf, 1, size, stderr);
	} while (size > 0);
}

void sig_handler(int sig)
{
	char stack_text[10000];

#if (defined TORRENT_DEBUG && TORRENT_USE_ASSERTS) \
	|| defined TORRENT_ASIO_DEBUGGING \
	|| defined TORRENT_PROFILE_CALLS \
	|| defined TORRENT_RELEASE_ASSERTS \
	|| defined TORRENT_DEBUG_BUFFERS
	print_backtrace(stack_text, sizeof(stack_text), 30);
#elif defined __FUNCTION__
	strcat(stack_text, __FUNCTION__);
#else
	stack_text[0] = 0;
	strcat(stack_text, "<stack traces disabled>");
#endif
	char const* sig_name = 0;
	switch (sig)
	{
#define SIG(x) case x: sig_name = #x; break
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
	fprintf(stderr, "signal: %s caught:\n%s\n", sig_name, stack_text);

	output_test_log_to_terminal();

#ifdef WIN32
	exit(sig);
#else
	exit(128 + sig);
#endif
}

void print_usage(char const* executable)
{
	printf("%s [options] [tests...]\n"
		"\n"
		"OPTIONS:\n"
		"-h,--help           show this help\n"
		"-l,--list           list the tests available to run\n"
		"-k,--keep           keep files created by the test\n"
		"                    regardless of whether it passed or not\n"
		"-n,--no-redirect    don't redirect test output to\n"
		"                    temporary file, but let it go straight\n"
		"                    to stdout\n"
		"\n"
		"for tests, specify one or more test names as printed\n"
		"by -l. If no test is specified, all tests are run\n", executable);
}

#ifdef WIN32
LONG WINAPI seh_exception_handler(LPEXCEPTION_POINTERS p)
{
	int sig = p->ExceptionRecord->ExceptionCode;
	fprintf(stderr, "SEH exception: %u\n"
		, p->ExceptionRecord->ExceptionCode);
	sig_handler(sig);
	exit(sig);
}
#endif

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
			printf("TESTS:\n");
			for (int i = 0; i < _g_num_unit_tests; ++i)
			{
				printf(" - %s\n", _g_unit_tests[i].name);
			}
			return 0;
		}

		if (strcmp(argv[0], "-n") == 0 || strcmp(argv[0], "--no-redirect") == 0)
		{
			redirect_output = false;
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

#ifdef WIN32
	// try to suppress hanging the process by windows displaying
	// modal dialogs.
	SetErrorMode( SEM_NOALIGNMENTFAULTEXCEPT
		| SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	SetUnhandledExceptionFilter(&seh_exception_handler);
#endif

#ifdef O_NONBLOCK
	// on darwin, stdout is set to non-blocking mode by default
	// which sometimes causes tests to fail with EAGAIN just
	// by printing logs
	int flags = fcntl(fileno(stdout), F_GETFL, 0);
	fcntl(fileno(stdout), F_SETFL, flags & ~O_NONBLOCK);
	flags = fcntl(fileno(stderr), F_GETFL, 0);
	fcntl(fileno(stderr), F_SETFL, flags & ~O_NONBLOCK);
#endif

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

	int process_id = -1;
#ifdef _WIN32
	process_id = _getpid();
#else
	process_id = getpid();
#endif
	char dir[40];
	snprintf(dir, sizeof(dir), "test_tmp_%u", process_id);
	std::string test_dir = complete(dir);
	error_code ec;
	create_directory(test_dir, ec);
	if (ec)
	{
		fprintf(stderr, "Failed to create test directory: %s\n", ec.message().c_str());
		return 1;
	}
#ifdef TORRENT_WINDOWS
	SetCurrentDirectoryA(dir);
#else
	chdir(dir);
#endif
	fprintf(stderr, "cwd = \"%s\"\n", test_dir.c_str());

	int total_failures = 0;

	if (_g_num_unit_tests == 0)
	{
		fprintf(stderr, "\x1b[31mERROR: no unit tests registered\x1b[0m\n");
		return 1;
	}

	if (redirect_output)
	{
		old_stdout = dup(fileno(stdout));
		old_stderr = dup(fileno(stderr));
	}

	int num_run = 0;
	for (int i = 0; i < _g_num_unit_tests; ++i)
	{
		if (filter && tests_to_run.count(_g_unit_tests[i].name) == 0)
			continue;

		unit_test_t& t = _g_unit_tests[i];

		if (redirect_output)
		{
			// redirect test output to a temporary file
			fflush(stdout);
			fflush(stderr);

			FILE* f = tmpfile();
			if (f != NULL)
			{
				int ret1 = dup2(fileno(f), fileno(stdout));
				dup2(fileno(f), fileno(stderr));
				if (ret1 >= 0)
				{
					t.output = f;
				}
				else
				{
					fprintf(stderr, "failed to redirect output: (%d) %s\n"
						, errno, strerror(errno));
				}
			}
			else
			{
				fprintf(stderr, "failed to create temporary file for redirecting "
					"output: (%d) %s\n", errno, strerror(errno));
			}
		}

		// get proper interleaving of stderr and stdout
		setbuf(stdout, NULL);
		setbuf(stderr, NULL);

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
		catch (std::exception const& e)
		{
			char buf[200];
			snprintf(buf, sizeof(buf), "Terminated with exception: \"%s\"", e.what());
			report_failure(buf, __FILE__, __LINE__);
		}
		catch (...)
		{
			report_failure("Terminated with unknown exception", __FILE__, __LINE__);
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

		if (redirect_output && t.output)
		{
			fclose(t.output);
		}
	}

	if (redirect_output)
	{
		dup2(old_stdout, fileno(stdout));
		dup2(old_stderr, fileno(stderr));
	}

	if (!tests_to_run.empty())
	{
		fprintf(stderr, "\x1b[1mUNKONWN tests:\x1b[0m\n");
		for (std::set<std::string>::iterator i = tests_to_run.begin()
			, end(tests_to_run.end()); i != end; ++i)
		{
			fprintf(stderr, "  %s\n", i->c_str());
		}
	}

	if (num_run == 0)
	{
		fprintf(stderr, "\x1b[31mERROR: no unit tests run\x1b[0m\n");
		return 1;
	}

	// just in case of premature exits
	// make sure we try to clean up some
	stop_udp_tracker();
	stop_all_proxies();
	stop_web_server();
	stop_peer();
	stop_dht();

	if (redirect_output)
	{
		fflush(stdout);
		fflush(stderr);
	}

	int ret = print_failures();
#if !defined TORRENT_LOGGING
	if (ret == 0 && !keep_files)
	{
		remove_all(test_dir, ec);
		if (ec)
			fprintf(stderr, "failed to remove test dir: %s\n", ec.message().c_str());
	}
#endif

	return total_failures ? 333 : 0;
}

