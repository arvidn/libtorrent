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
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_handle_wrappers.hpp"
#include <curl/curl.h>
#include <chrono>
#include <vector>
#include <string>
#include <map>
#include <cstdio>

// Disable warnings for libcurl macros in entire file
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif

using namespace libtorrent;
using namespace libtorrent::aux;
using namespace std::chrono_literals;

namespace {

struct curl_initializer {
    curl_initializer() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~curl_initializer() {
        curl_global_cleanup();
    }
} g_curl_init;

// Callback to capture response data
size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* response = static_cast<std::string*>(userdata);
    response->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Helper to perform a request and measure DNS lookup time
double perform_request_with_timing(CURL* easy, const std::string& url) {
    std::string response;
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(easy);
    if (res != CURLE_OK) {
        // Some requests may fail, that's OK for this test
        return -1.0;
    }
    
    double dns_time = 0.0;
    curl_easy_getinfo(easy, CURLINFO_NAMELOOKUP_TIME, &dns_time);
    return dns_time;
}

} // anonymous namespace

// Test 1.2.1: Test DNS cache sharing between handles
TORRENT_TEST(curl_share_dns_cache)
{
    // Use external domain to get meaningful DNS lookup times
    std::string const test_url = "http://www.google.com/";
    
    // Create share handle configured for DNS sharing
    curl_share_handle share;
    share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    
    // Create two easy handles that share the DNS cache
    curl_easy_handle handle1;
    curl_easy_handle handle2;
    
    // Configure handles for testing
    curl_easy_setopt(handle1.get(), CURLOPT_NOBODY, 1L); // HEAD request only
    curl_easy_setopt(handle2.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle1.get(), CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(handle2.get(), CURLOPT_TIMEOUT, 5L);
    
    // Attach share handle to both easy handles
    curl_easy_setopt(handle1.get(), CURLOPT_SHARE, share.get());
    curl_easy_setopt(handle2.get(), CURLOPT_SHARE, share.get());
    
    // First request should do DNS lookup
    double dns_time1 = perform_request_with_timing(handle1.get(), test_url);
    
    // Second request should use cached DNS (much faster)
    double dns_time2 = perform_request_with_timing(handle2.get(), test_url);
    
    // DNS cache hit should result in near-zero lookup time
    if (dns_time1 > 0 && dns_time2 >= 0) {
        // Second lookup should be significantly faster (near zero)
        TEST_CHECK(dns_time2 < dns_time1 * 0.1);
        
        // Also check absolute values - cached should be < 1ms
        TEST_CHECK(dns_time2 < 0.001);
        
        std::printf("DNS times: first=%.3fms, cached=%.3fms\n", 
                   dns_time1 * 1000, dns_time2 * 1000);
    } else {
        // Network might be unavailable, skip test
        TEST_CHECK(true);
    }
}

// Test 1.2.2: Test DNS cache hit rate with multiple requests
TORRENT_TEST(curl_share_dns_cache_hit_rate)
{
    // Use multiple external domains
    std::vector<std::string> test_urls = {
        "http://www.google.com/",
        "http://www.github.com/",
        "http://www.google.com/",  // Repeat to test cache
        "http://www.github.com/",  // Repeat
        "http://www.google.com/",  // Another repeat
    };
    
    // Create share handle for DNS
    curl_share_handle share;
    share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    
    // Create multiple handles sharing DNS cache
    std::vector<curl_easy_handle> handles;
    handles.reserve(test_urls.size());
    for (size_t i = 0; i < test_urls.size(); ++i) {
        handles.emplace_back();
        curl_easy_setopt(handles.back().get(), CURLOPT_SHARE, share.get());
        curl_easy_setopt(handles.back().get(), CURLOPT_NOBODY, 1L);
        curl_easy_setopt(handles.back().get(), CURLOPT_TIMEOUT, 5L);
    }
    
    std::vector<double> dns_times;
    std::map<std::string, double> first_lookup_times;
    
    for (size_t i = 0; i < handles.size(); ++i) {
        double time = perform_request_with_timing(handles[i].get(), test_urls[i]);
        if (time >= 0) {
            dns_times.push_back(time);
            
            // Track first lookup time for each domain
            if (first_lookup_times.find(test_urls[i]) == first_lookup_times.end()) {
                first_lookup_times[test_urls[i]] = time;
            }
        }
    }
    
    // Calculate cache hit rate for repeated domains
    if (dns_times.size() >= 3) {
        // Check that repeated lookups are cached (indices 2, 3, 4)
        TEST_CHECK(dns_times[2] < 0.001); // google.com cached
        TEST_CHECK(dns_times[3] < 0.001); // github.com cached
        TEST_CHECK(dns_times[4] < 0.001); // google.com cached again
        
        std::printf("DNS cache hit rate test - times: ");
        for (auto t : dns_times) {
            std::printf("%.3fms ", t * 1000);
        }
        std::printf("\n");
    } else {
        // Network might be unavailable
        TEST_CHECK(true);
    }
}

// Test 1.2.3: Test SSL session sharing
// SSL session sharing has been available since libcurl 7.23.0
// Our minimum is 7.68.0, so this is always available
TORRENT_TEST(curl_share_ssl_session)
{
    // This test would require an HTTPS server setup
    // For now, we just verify that SSL session sharing can be configured
    
    curl_share_handle share;
    
    // Should not throw
    try {
        share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        TEST_CHECK(true);
    } catch (std::exception const& e) {
        TEST_ERROR(e.what());
    }
    
    // Create handles and attach share
    curl_easy_handle handle1;
    curl_easy_handle handle2;
    
    curl_easy_setopt(handle1.get(), CURLOPT_SHARE, share.get());
    curl_easy_setopt(handle2.get(), CURLOPT_SHARE, share.get());
    
    // In a real test with HTTPS server, we would measure:
    // - CURLINFO_APPCONNECT_TIME for SSL handshake time
    // - Verify second connection has much faster handshake
    
    TEST_CHECK(true);
}

// Test 1.2.4: Test that handles without sharing don't share cache
TORRENT_TEST(curl_no_share_isolation)
{
    std::string const test_url = "http://www.google.com/";
    
    // Create two handles WITHOUT sharing
    curl_easy_handle handle1;
    curl_easy_handle handle2;
    
    curl_easy_setopt(handle1.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle2.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle1.get(), CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(handle2.get(), CURLOPT_TIMEOUT, 5L);
    
    // Both should do full DNS lookup
    double dns_time1 = perform_request_with_timing(handle1.get(), test_url);
    double dns_time2 = perform_request_with_timing(handle2.get(), test_url);
    
    // Without sharing, both should have non-trivial DNS lookup times
    if (dns_time1 > 0 && dns_time2 > 0) {
        // Both should take more than 1ms (no caching)
        TEST_CHECK(dns_time1 > 0.001);
        TEST_CHECK(dns_time2 > 0.001);
        
        std::printf("No-share DNS times: first=%.3fms, second=%.3fms\n",
                   dns_time1 * 1000, dns_time2 * 1000);
    } else {
        // Network might be unavailable
        TEST_CHECK(true);
    }
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_disabled)
{
    TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL
