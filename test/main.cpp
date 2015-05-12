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
#include "setup_transfer.hpp" // for tests_failure
#include "dht_server.hpp" // for stop_dht
#include "peer_server.hpp" // for stop_peer
#include "udp_tracker.hpp" // for stop_udp_tracker

int test_main();

#include "libtorrent/assert.hpp"
#include "libtorrent/file.hpp"
#include <signal.h>

#ifdef WIN32
#include <windows.h> // fot SetErrorMode
#endif

void sig_handler(int sig)
{
	char stack_text[10000];

#if (defined TORRENT_DEBUG && !TORRENT_NO_ASSERTS) || TORRENT_RELEASE_ASSERTS
	print_backtrace(stack_text, sizeof(stack_text), 30);
#elif defined __FUNCTION__
	strcat(stack_text, __FUNCTION__);
#else
	stack_text[0] = 0;
#endif
	char const* sig_name = 0;
	switch (sig)
	{
#define SIG(x) case x: sig_name = #x; break
		SIG(SIGSEGV);
#ifdef SIGBUS
		SIG(SIGBUS);
#endif
		SIG(SIGILL);
		SIG(SIGABRT);
		SIG(SIGFPE);
#ifdef SIGSYS
		SIG(SIGSYS);
#endif
#undef SIG
	};
	fprintf(stderr, "signal: %s caught:\n%s\n", sig_name, stack_text);
	exit(138);
}

using namespace libtorrent;

int main()
{
#ifdef WIN32
	// try to suppress hanging the process by windows displaying
	// modal dialogs.
	SetErrorMode(SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOALIGNMENTFAULTEXCEPT
		| SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

	srand((total_microseconds(time_now_hires() - min_time())) & 0x7fffffff);
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
	signal(SIGABRT, &sig_handler);
	signal(SIGFPE, &sig_handler);
#ifdef SIGSYS
	signal(SIGSYS, &sig_handler);
#endif

	char dir[40];
	snprintf(dir, sizeof(dir), "test_tmp_%u", rand());
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

#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		test_main();
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

	// just in case of premature exits
	// make sure we try to clean up some
	stop_udp_tracker();
	stop_all_proxies();
	stop_web_server();
	stop_peer();
	stop_dht();

	fflush(stdout);
	fflush(stderr);

	int ret = print_failures();
#if !defined TORRENT_LOGGING && !defined TORRENT_VERBOSE_LOGGING
	if (ret == 0)
	{
		remove_all(test_dir, ec);
		if (ec)
			fprintf(stderr, "failed to remove test dir: %s\n", ec.message().c_str());
	}
#endif

	return ret ? 333 : 0;
}

