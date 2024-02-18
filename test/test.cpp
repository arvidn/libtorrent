/*

Copyright (c) 2015-2017, 2019-2020, 2022, Arvid Norberg
Copyright (c) 2017, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <vector>
#include "libtorrent/aux_/array.hpp"

#include "test.hpp"

namespace unit_test {

lt::aux::array<unit_test_t, 1024> g_unit_tests;
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
