/*

Copyright (c) 2015-2017, 2019-2020, 2022, Arvid Norberg
Copyright (c) 2017, Andrei Kurushin
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

#include "test.hpp"

namespace unit_test {

unit_test_t g_unit_tests[1024];
int g_num_unit_tests = 0;
int g_test_failures = 0; // flushed at start of every unit
int g_test_idx = 0;

namespace {
std::vector<std::string> failure_strings;
}

int test_counter()
{
	return g_test_idx;
}

void report_failure(char const* err, char const* file, int line)
{
	char buf[2000];
	std::snprintf(buf, sizeof(buf), "\x1b[41m***** %s:%d \"%s\" *****\x1b[0m\n", file, line, err);
	std::printf("\n%s\n", buf);
	failure_strings.push_back(buf);
	++g_test_failures;
}

int print_failures()
{
	int longest_name = 0;
	for (int i = 0; i < g_num_unit_tests; ++i)
	{
		int len = int(strlen(g_unit_tests[i].name));
		if (len > longest_name) longest_name = len;
	}

	std::printf("\n\n");
	int total_num_failures = 0;

	for (int i = 0; i < g_num_unit_tests; ++i)
	{
		if (g_unit_tests[i].run == false) continue;

		if (g_unit_tests[i].num_failures == 0)
		{
			std::printf("\x1b[32m[%-*s] ***PASS***\n"
				, longest_name, g_unit_tests[i].name);
		}
		else
		{
			total_num_failures += g_unit_tests[i].num_failures;
			std::printf("\x1b[31m[%-*s] %d FAILURES\n"
				, longest_name
				, g_unit_tests[i].name
				, g_unit_tests[i].num_failures);
		}
	}

	std::printf("\x1b[0m");

	if (total_num_failures > 0)
		std::printf("\n\n\x1b[41m   == %d TEST(S) FAILED ==\x1b[0m\n\n\n"
			, total_num_failures);
	return total_num_failures;
}

} // unit_test
