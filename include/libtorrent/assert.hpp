/*

Copyright (c) 2007-2008, 2010-2011, 2013-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ASSERT_HPP_INCLUDED
#define TORRENT_ASSERT_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"

#if TORRENT_USE_ASSERTS \
	|| defined TORRENT_ASIO_DEBUGGING \
	|| defined TORRENT_PROFILE_CALLS \
	|| defined TORRENT_DEBUG_BUFFERS

#include <string>
namespace lt {
std::string demangle(char const* name);
TORRENT_EXPORT void print_backtrace(char* out, int len, int max_depth = 0, void* ctx = nullptr);
}
#endif

// this is to disable the warning of conditional expressions
// being constant in msvc
#ifdef _MSC_VER
#define TORRENT_WHILE_0  \
	__pragma( warning(push) ) \
	__pragma( warning(disable:4127) ) \
	while (false) \
	__pragma( warning(pop) )
#else
#define TORRENT_WHILE_0 while (false)
#endif


namespace lt {
// declarations of the two functions

// internal
TORRENT_EXPORT void assert_print(char const* fmt, ...) TORRENT_FORMAT(1,2);

// internal
TORRENT_EXPORT void assert_fail(const char* expr, int line
	, char const* file, char const* function, char const* val, int kind = 0);

}

#if TORRENT_USE_ASSERTS

#ifdef TORRENT_PRODUCTION_ASSERTS
extern TORRENT_EXPORT char const* libtorrent_assert_log;
#endif

#if TORRENT_USE_IOSTREAM
#include <sstream>
#endif

#ifndef TORRENT_USE_SYSTEM_ASSERTS

#define TORRENT_ASSERT_PRECOND(x) \
	do { if (x) {} else lt::assert_fail(#x, __LINE__, __FILE__, __func__, nullptr, 1); } TORRENT_WHILE_0

#define TORRENT_ASSERT(x) \
	do { if (x) {} else lt::assert_fail(#x, __LINE__, __FILE__, __func__, nullptr, 0); } TORRENT_WHILE_0

#if TORRENT_USE_IOSTREAM
#define TORRENT_ASSERT_VAL(x, y) \
	do { if (x) {} else { std::stringstream __s__; __s__ << #y ": " << y; \
	lt::assert_fail(#x, __LINE__, __FILE__, __func__, __s__.str().c_str(), 0); } } TORRENT_WHILE_0

#define TORRENT_ASSERT_FAIL_VAL(y) \
	do { std::stringstream __s__; __s__ << #y ": " << y; \
	lt::assert_fail("<unconditional>", __LINE__, __FILE__, __func__, __s__.str().c_str(), 0); } TORRENT_WHILE_0

#else
#define TORRENT_ASSERT_VAL(x, y) TORRENT_ASSERT(x)
#define TORRENT_ASSERT_FAIL_VAL(x) TORRENT_ASSERT_FAIL()
#endif

#define TORRENT_ASSERT_FAIL() \
	lt::assert_fail("<unconditional>", __LINE__, __FILE__, __func__, nullptr, 0)

#else
#include <cassert>
#define TORRENT_ASSERT_PRECOND(x) assert(x)
#define TORRENT_ASSERT(x) assert(x)
#define TORRENT_ASSERT_VAL(x, y) assert(x)
#define TORRENT_ASSERT_FAIL_VAL(x) assert(false)
#define TORRENT_ASSERT_FAIL() assert(false)
#endif

#else // TORRENT_USE_ASSERTS

#define TORRENT_ASSERT_PRECOND(a) do {} TORRENT_WHILE_0
#define TORRENT_ASSERT(a) do {} TORRENT_WHILE_0
#define TORRENT_ASSERT_VAL(a, b) do {} TORRENT_WHILE_0
#define TORRENT_ASSERT_FAIL_VAL(a) do {} TORRENT_WHILE_0
#define TORRENT_ASSERT_FAIL() do {} TORRENT_WHILE_0

#endif // TORRENT_USE_ASSERTS

#endif // TORRENT_ASSERT_HPP_INCLUDED
