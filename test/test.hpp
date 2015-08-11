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

#ifndef TEST_HPP
#define TEST_HPP

#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"

#include <boost/config.hpp>
#include <exception>
#include <sstream>
#include <vector>
#include <boost/preprocessor/cat.hpp>

#include "libtorrent/config.hpp"

// tests are expected to even test deprecated functionality. There is no point
// in warning about deprecated use in any of the tests.

#if defined __clang__
#pragma clang diagnostic ignored "-Wdeprecated"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#elif defined __GNUC__
#pragma GCC diagnostic ignored "-Wdeprecated"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#elif defined _MSC_VER
#pragma warning(disable : 4996)
#endif

#if defined TORRENT_BUILDING_TEST_SHARED
#define EXPORT BOOST_SYMBOL_EXPORT
#elif defined TORRENT_LINK_TEST_SHARED
#define EXPORT BOOST_SYMBOL_IMPORT
#else
#define EXPORT
#endif

void EXPORT report_failure(char const* err, char const* file, int line);
int EXPORT print_failures();
int EXPORT test_counter();

typedef void (*unit_test_fun_t)();

struct unit_test_t
{
	unit_test_fun_t fun;
	char const* name;
	int num_failures;
	bool run;
	FILE* output;
};

extern unit_test_t EXPORT _g_unit_tests[1024];
extern int EXPORT _g_num_unit_tests;
extern int EXPORT _g_test_failures;

#define TORRENT_TEST(test_name) \
	static void BOOST_PP_CAT(unit_test_, test_name)(); \
	static struct BOOST_PP_CAT(register_class_, test_name) { \
		BOOST_PP_CAT(register_class_, test_name) () { \
			unit_test_t& t = _g_unit_tests[_g_num_unit_tests]; \
			t.fun = &BOOST_PP_CAT(unit_test_, test_name); \
			t.name = __FILE__ "." #test_name; \
			t.num_failures = 0; \
			t.run = false; \
			t.output = NULL; \
			_g_num_unit_tests++; \
		} \
	} BOOST_PP_CAT(_static_registrar_, test_name); \
	static void BOOST_PP_CAT(unit_test_, test_name)()

#define TEST_REPORT_AUX(x, line, file) \
	report_failure(x, line, file)

#ifdef BOOST_NO_EXCEPTIONS
#define TEST_CHECK(x) \
	if (!(x)) \
		TEST_REPORT_AUX("TEST_CHECK failed: \"" #x "\"", __FILE__, __LINE__);
#define TEST_EQUAL(x, y) \
	if ((x) != (y)) { \
		std::stringstream s__; \
		s__ << "TEST_EQUAL_ERROR:\n" #x ": " << (x) << "\nexpected: " << (y); \
		TEST_REPORT_AUX(s__.str().c_str(), __FILE__, __LINE__); \
	}
#else
#define TEST_CHECK(x) \
	try \
	{ \
		if (!(x)) \
			TEST_REPORT_AUX("TEST_CHECK failed: \"" #x "\"", __FILE__, __LINE__); \
	} \
	catch (std::exception& e) \
	{ \
		TEST_ERROR("Exception thrown: " #x " :" + std::string(e.what())); \
	} \
	catch (...) \
	{ \
		TEST_ERROR("Exception thrown: " #x); \
	}

#define TEST_EQUAL(x, y) \
	try { \
		if ((x) != (y)) { \
			std::stringstream s__; \
			s__ << "TEST_EQUAL_ERROR: " #x ": " << (x) << " expected: " << (y); \
			TEST_REPORT_AUX(s__.str().c_str(), __FILE__, __LINE__); \
		} \
	} \
	catch (std::exception& e) \
	{ \
		TEST_ERROR("Exception thrown: " #x " :" + std::string(e.what())); \
	} \
	catch (...) \
	{ \
		TEST_ERROR("Exception thrown: " #x); \
	}
#endif

#define TEST_ERROR(x) \
	TEST_REPORT_AUX((std::string("ERROR: \"") + (x) + "\"").c_str(), __FILE__, __LINE__)

#define TEST_NOTHROW(x) \
	try \
	{ \
		x; \
	} \
	catch (...) \
	{ \
		TEST_ERROR("Exception thrown: " #x); \
	}

#endif // TEST_HPP

