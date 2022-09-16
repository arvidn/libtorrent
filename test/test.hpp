/*

Copyright (c) 2005, 2008-2009, 2013-2020, 2022, Arvid Norberg
Copyright (c) 2015, Steven Siloti
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
#include <cstdio> // for std::snprintf
#include <cinttypes> // for PRId64 et.al.

#include <boost/preprocessor/cat.hpp>

#include "libtorrent/config.hpp"

// tests are expected to even test deprecated functionality. There is no point
// in warning about deprecated use in any of the tests.
// the unreachable code warnings are disabled since the test macros may
// sometimes have conditions that are known at compile time
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

#if defined TORRENT_BUILDING_TEST_SHARED
#define EXPORT BOOST_SYMBOL_EXPORT
#elif defined TORRENT_LINK_TEST_SHARED
#define EXPORT BOOST_SYMBOL_IMPORT
#else
#define EXPORT
#endif

namespace unit_test {

void EXPORT report_failure(char const* err, char const* file, int line);
int EXPORT print_failures();
int EXPORT test_counter();
void EXPORT reset_output();

using unit_test_fun_t = void (*)();

struct unit_test_t
{
	unit_test_fun_t fun;
	char const* name;
	int num_failures;
	bool run;
	FILE* output;
};

extern unit_test_t EXPORT g_unit_tests[1024];
extern int EXPORT g_num_unit_tests;
extern int EXPORT g_test_failures;
extern int g_test_idx;

} // unit_test

#define TORRENT_TEST(test_name) \
	static void BOOST_PP_CAT(unit_test_, test_name)(); \
	static struct BOOST_PP_CAT(register_class_, test_name) { \
		BOOST_PP_CAT(register_class_, test_name) () { \
			auto& t = ::unit_test::g_unit_tests[::unit_test::g_num_unit_tests]; \
			t.fun = &BOOST_PP_CAT(unit_test_, test_name); \
			t.name = __FILE__ "." #test_name; \
			t.num_failures = 0; \
			t.run = false; \
			t.output = nullptr; \
			::unit_test::g_num_unit_tests++; \
		} \
	} BOOST_PP_CAT(g_static_registrar_for, test_name); \
	static void BOOST_PP_CAT(unit_test_, test_name)()

#define TEST_REPORT_AUX(x, line, file) \
	unit_test::report_failure(x, line, file)

#ifdef BOOST_NO_EXCEPTIONS
#define TEST_CHECK(x) \
	do if (!(x)) { \
		TEST_REPORT_AUX("TEST_ERROR: check failed: \"" #x "\"", __FILE__, __LINE__); \
	} while (false)
#define TEST_EQUAL(x, y) \
	do if ((x) != (y)) { \
		std::stringstream _s_; \
		_s_ << "TEST_ERROR: equal check failed:\n" #x ": " << (x) << "\nexpected: " << (y); \
		TEST_REPORT_AUX(_s_.str().c_str(), __FILE__, __LINE__); \
	} while (false)
#define TEST_NE(x, y) \
	do if ((x) == (y)) { \
		std::stringstream _s_; \
		_s_ << "TEST_ERROR: not equal check failed:\n" #x ": " << (x) << "\nexpected not equal to: " << (y); \
		TEST_REPORT_AUX(_s_.str().c_str(), __FILE__, __LINE__); \
	} while (false)
#else
#define TEST_CHECK(x) \
	do try \
	{ \
		if (!(x)) \
			TEST_REPORT_AUX("TEST_ERROR: check failed: \"" #x "\"", __FILE__, __LINE__); \
	} \
	catch (std::exception const& _e) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x " :" + std::string(_e.what())); \
	} \
	catch (...) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x); \
	} while (false)

#define TEST_EQUAL(x, y) \
	do try { \
		auto const x_ = x; \
		auto const y_ = y; \
		if (x_ != y_) { \
			std::stringstream _s_; \
			_s_ << "TEST_ERROR: " #x ": " << x_ << " expected: " << y_; \
			TEST_REPORT_AUX(_s_.str().c_str(), __FILE__, __LINE__); \
		} \
	} \
	catch (std::exception const& _e) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x " :" + std::string(_e.what())); \
	} \
	catch (...) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x); \
	} while (false)
#define TEST_NE(x, y) \
	do try { \
		auto const x_ = x; \
		auto const y_ = y; \
		if (x_ == y_) { \
			std::stringstream _s_; \
			_s_ << "TEST_ERROR: " #x ": " << x_ << " expected not equal to: " << y_; \
			TEST_REPORT_AUX(_s_.str().c_str(), __FILE__, __LINE__); \
		} \
	} \
	catch (std::exception const& _e) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x " :" + std::string(_e.what())); \
	} \
	catch (...) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x); \
	} while (false)
#endif

#define TEST_ERROR(x) \
	TEST_REPORT_AUX((std::string("TEST_ERROR: \"") + (x) + "\"").c_str(), __FILE__, __LINE__)

#define TEST_NOTHROW(x) \
	do try \
	{ \
		x; \
	} \
	catch (...) \
	{ \
		TEST_ERROR("TEST_ERROR: Exception thrown: " #x); \
	} while (false)

#define TEST_THROW(x) \
	do try \
	{ \
		x; \
		TEST_ERROR("No exception thrown: " #x); \
	} \
	catch (...) {} while (false)

#endif // TEST_HPP

