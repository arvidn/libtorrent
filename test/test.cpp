/*

Copyright (c) 2008-2015, Arvid Norberg
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

#include <vector>
#include <stdio.h> // for tmpfile()
#include "test.hpp"

unit_test_t _g_unit_tests[1024];
int _g_num_unit_tests = 0;
int _g_test_failures = 0;
int _g_test_idx = 0;

static std::vector<std::string> failure_strings;

int test_counter()
{
	return _g_test_idx;
}

void report_failure(char const* err, char const* file, int line)
{
	char buf[500];
	snprintf(buf, sizeof(buf), "\x1b[41m***** %s:%d \"%s\" *****\x1b[0m\n", file, line, err);
	fprintf(stderr, "\n%s\n", buf);
	failure_strings.push_back(buf);
	++_g_test_failures;
}

int print_failures()
{
	int longest_name = 0;
	for (int i = 0; i < _g_num_unit_tests; ++i)
	{
		int len = strlen(_g_unit_tests[i].name);
		if (len > longest_name) longest_name = len;
	}

	fprintf(stderr, "\n\n");

	for (int i = 0; i < _g_num_unit_tests; ++i)
	{
		if (_g_unit_tests[i].run == false) continue;

		if (_g_unit_tests[i].num_failures == 0)
		{
			fprintf(stderr, "\x1b[32m[%-*s] ***PASS***\n"
				, longest_name, _g_unit_tests[i].name);
		}
		else
		{
			fprintf(stderr, "\x1b[31m[%-*s] %d FAILURES\n"
				, longest_name
				, _g_unit_tests[i].name
				, _g_unit_tests[i].num_failures);
		}
	}

	fprintf(stderr, "\x1b[0m");

	if (_g_test_failures > 0)
		fprintf(stderr, "\n\n\x1b[41m   == %d TEST(S) FAILED ==\x1b[0m\n\n\n", _g_test_failures);
	return _g_test_failures;
}

