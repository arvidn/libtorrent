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

int test_main();

#include "libtorrent/assert.hpp"
#include "libtorrent/file.hpp"
#include <signal.h>

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
	srand(total_microseconds(time_now_hires() - min_time()));
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

#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		test_main();
#ifndef BOOST_NO_EXCEPTIONS
	}
	catch (std::exception const& e)
	{
		std::cerr << "Terminated with exception: \"" << e.what() << "\"\n";
		tests_failure = true;
	}
	catch (...)
	{
		std::cerr << "Terminated with unknown exception\n";
		tests_failure = true;
	}
#endif
	fflush(stdout);
	fflush(stderr);
	remove_all(test_dir, ec);
	if (ec)
		fprintf(stderr, "failed to remove test dir: %s\n", ec.message().c_str());
	return tests_failure ? 1 : 0;
}

