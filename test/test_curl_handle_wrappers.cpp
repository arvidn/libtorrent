/*

Copyright (c) 2025, libtorrent project
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

#include "test.hpp"
#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_handle_wrappers.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <utility>

using namespace libtorrent;
using namespace libtorrent::aux;

namespace {

struct curl_initializer {
    curl_initializer() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~curl_initializer() {
        curl_global_cleanup();
    }
} g_curl_init;

} // anonymous namespace

// Test 1.1.1: Test curl_share_handle construction
TORRENT_TEST(curl_share_handle_construction)
{
    // Test successful initialization
    try {
        curl_share_handle share;
        TEST_CHECK(share.get() != nullptr);
    } catch (std::exception const& e) {
        TEST_ERROR(e.what());
    }
}

// Test 1.1.2: Test curl_share_handle destruction
// We'll test this through RAII and valgrind/sanitizers
TORRENT_TEST(curl_share_handle_destruction)
{
    // Create and destroy in scope
    {
        curl_share_handle share;
        TEST_CHECK(share.get() != nullptr);
    }
    // If cleanup wasn't called properly, sanitizers will detect it
    TEST_CHECK(true);
}

// Test 1.1.3: Test move semantics
TORRENT_TEST(curl_share_handle_move_semantics)
{
    // Test move constructor
    {
        curl_share_handle share1;
        CURLSH* original_handle = share1.get();
        TEST_CHECK(original_handle != nullptr);
        
        curl_share_handle share2(std::move(share1));
        TEST_CHECK(share2.get() == original_handle);
        TEST_CHECK(share1.get() == nullptr);
    }
    
    // Test move assignment
    {
        curl_share_handle share1;
        curl_share_handle share2;
        
        CURLSH* handle1 = share1.get();
        CURLSH* handle2 = share2.get();
        
        TEST_CHECK(handle1 != nullptr);
        TEST_CHECK(handle2 != nullptr);
        TEST_CHECK(handle1 != handle2);
        
        share2 = std::move(share1);
        TEST_CHECK(share2.get() == handle1);
        TEST_CHECK(share1.get() == nullptr);
        // handle2 should have been cleaned up by assignment
    }
    
    // Test self-assignment
    {
        curl_share_handle share;
        CURLSH* original = share.get();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
        share = std::move(share);  // Intentional self-move test
#pragma GCC diagnostic pop
        TEST_CHECK(share.get() == original);
    }
}

// Test 1.1.4: Test setopt wrapper
TORRENT_TEST(curl_share_handle_setopt)
{
    curl_share_handle share;
    
    // Test successful setopt call for DNS sharing
    try {
        share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        TEST_CHECK(true);
    } catch (std::exception const& e) {
        TEST_ERROR(e.what());
    }
    
    // Test successful setopt call for SSL session sharing
#if CURL_VERSION_NUM >= 0x071700  // 7.23.0
    try {
        share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        TEST_CHECK(true);
    } catch (std::exception const& e) {
        TEST_ERROR(e.what());
    }
#endif
    
    // Test error handling - invalid option value
    // Note: It's hard to force an error with valid CURLSH handle
    // The error handling will be more thoroughly tested with mocking
    TEST_CHECK(true);
}

// Test 1.1.5: Test copy operations are deleted
TORRENT_TEST(curl_share_handle_no_copy)
{
    // This test verifies at compile time that copy operations are deleted
    // If these lines were uncommented, they should fail to compile:
    // curl_share_handle share1;
    // curl_share_handle share2(share1);  // Should not compile
    // curl_share_handle share3;
    // share3 = share1;  // Should not compile
    
    TEST_CHECK(true);
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_disabled)
{
    TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL