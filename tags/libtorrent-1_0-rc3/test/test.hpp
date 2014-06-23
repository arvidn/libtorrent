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

#include <boost/config.hpp>
#include <exception>
#include <sstream>

#include "libtorrent/config.hpp"

#if defined TORRENT_BUILDING_TEST_SHARED
#define EXPORT BOOST_SYMBOL_EXPORT
#elif defined TORRENT_LINK_TEST_SHARED
#define EXPORT BOOST_SYMBOL_IMPORT
#else
#define EXPORT
#endif

// the unit tests need access to these.
// they are built into the unit test library as well.
// not exported from libtorrent itself
extern "C"
{
	int EXPORT ed25519_create_seed(unsigned char *seed);
	void EXPORT ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key, const unsigned char *seed);
	void EXPORT ed25519_sign(unsigned char *signature, const unsigned char *message, size_t message_len, const unsigned char *public_key, const unsigned char *private_key);
	int EXPORT ed25519_verify(const unsigned char *signature, const unsigned char *message, size_t message_len, const unsigned char *public_key);
	void EXPORT ed25519_add_scalar(unsigned char *public_key, unsigned char *private_key, const unsigned char *scalar);
	void EXPORT ed25519_key_exchange(unsigned char *shared_secret, const unsigned char *public_key, const unsigned char *private_key);
}

void EXPORT report_failure(char const* err, char const* file, int line);
int EXPORT print_failures();

#if defined(_MSC_VER)
#define COUNTER_GUARD(x)
#else
#define COUNTER_GUARD(type) \
    struct BOOST_PP_CAT(type, _counter_guard) \
    { \
        ~BOOST_PP_CAT(type, _counter_guard()) \
        { \
            TEST_CHECK(counted_type<type>::count == 0); \
        } \
    } BOOST_PP_CAT(type, _guard)
#endif

#define TEST_REPORT_AUX(x, line, file) \
	report_failure(x, line, file)

#ifdef BOOST_NO_EXCEPTIONS
#define TEST_CHECK(x) \
	if (!(x)) \
		TEST_REPORT_AUX("TEST_CHECK failed: \"" #x "\"", __FILE__, __LINE__);
#define TEST_EQUAL(x, y) \
	if (x != y) { \
		std::stringstream s__; \
		s__ << "TEST_EQUAL_ERROR:\n" #x ": " << x << "\nexpected: " << y; \
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
		if (x != y) { \
			std::stringstream s__; \
			s__ << "TEST_EQUAL_ERROR: " #x ": " << x << " expected: " << y; \
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
	TEST_REPORT_AUX((std::string("ERROR: \"") + x + "\"").c_str(), __FILE__, __LINE__)

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

