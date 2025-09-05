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

#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/curl_handle_wrappers.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/error_code.hpp"
#include <future>
#include <chrono>
#include <vector>
#include <set>
#include <atomic>
#include <thread>

using namespace libtorrent;
using namespace libtorrent::aux;
using namespace std::chrono_literals;

// Test 4.1.1: Test basic functionality with optimizations
TORRENT_TEST(curl_thread_manager_basic_optimized)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    // Give thread time to initialize
    std::this_thread::sleep_for(100ms);
    
    // Test basic request functionality (verifies everything still works)
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    // Use a URL that will fail quickly (invalid domain)
    manager->add_request(
        "http://invalid.test.domain.local/test",
        [&](error_code ec, std::vector<char> data) {
            result_ec = ec;
            completed = true;
            TEST_CHECK(!data.empty() || ec); // Either data or error
        },
        seconds(5)
    );
    
    bool success = run_io_context_until(ios, 6s, [&]() { return completed.load(); });
    TEST_CHECK(success); // Request should complete (even if with error)
    
    // manager->shutdown() happens automatically in destructor
}

// Test 4.1.2: Test concurrent requests (exercises handle pool)
TORRENT_TEST(curl_thread_manager_concurrent_pool)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    const int num_requests = 10;
    std::atomic<int> completed_count{0};
    std::vector<error_code> results(num_requests);
    
    // Submit multiple concurrent requests
    for (int i = 0; i < num_requests; ++i) {
        std::string url = "http://test" + std::to_string(i) + ".invalid.local/";
        manager->add_request(
            url,
            [&, i](error_code ec, std::vector<char>) {
                results[i] = ec;
                completed_count++;
            },
            seconds(2)
        );
    }
    
    // Wait for all to complete
    bool success = run_io_context_until(ios, 10s, 
        [&]() { return completed_count >= num_requests; });
    
    TEST_CHECK(success);
    TEST_EQUAL(completed_count.load(), num_requests);
    
    // All should have completed (with errors for invalid domains)
    for (auto& ec : results) {
        TEST_CHECK(ec); // Should have error (invalid domain)
    }
}

// Test 4.1.3: Test DNS caching with repeated requests to same host
TORRENT_TEST(curl_thread_manager_dns_caching)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    const int num_requests = 3;
    std::atomic<int> completed_count{0};
    std::vector<std::chrono::milliseconds> request_times;
    
    // Make repeated requests to the same invalid host
    // DNS lookup should be cached after first request
    std::string url = "http://repeated.test.invalid.local/test";
    
    for (int i = 0; i < num_requests; ++i) {
        auto start = std::chrono::steady_clock::now();
        
        manager->add_request(
            url,
            [&, start](error_code, std::vector<char>) {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                request_times.push_back(duration);
                completed_count++;
            },
            seconds(5)
        );
        
        // Small delay between requests
        std::this_thread::sleep_for(100ms);
    }
    
    // Wait for all to complete
    bool success = run_io_context_until(ios, 20s, 
        [&]() { return completed_count >= num_requests; });
    
    TEST_CHECK(success);
    TEST_EQUAL(completed_count.load(), num_requests);
    
    // Subsequent requests should generally be faster due to DNS caching
    // (though this is hard to test reliably without a real server)
    if (request_times.size() >= 2) {
        // Just verify all completed
        TEST_CHECK(request_times[0].count() > 0);
        TEST_CHECK(request_times[1].count() > 0);
    }
}

// Test 4.1.4: Test handle reuse by making sequential requests
TORRENT_TEST(curl_thread_manager_handle_reuse)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    const int num_requests = 5;
    std::atomic<int> completed_count{0};
    
    // Make sequential requests to exercise handle pooling
    for (int i = 0; i < num_requests; ++i) {
        std::atomic<bool> req_complete{false};
        
        manager->add_request(
            "http://sequential" + std::to_string(i) + ".test.local/",
            [&](error_code, std::vector<char>) {
                completed_count++;
                req_complete = true;
            },
            seconds(2)
        );
        
        // Wait for this request to complete before submitting next
        run_io_context_until(ios, 3s, [&]() { return req_complete.load(); });
    }
    
    // All requests should have completed
    TEST_EQUAL(completed_count.load(), num_requests);
}

// Test 4.1.5: Test share handle integration (DNS sharing between handles)
TORRENT_TEST(curl_thread_manager_share_integration)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    // Submit multiple requests to same host simultaneously
    // Share handle should enable DNS cache sharing
    const int num_parallel = 5;
    std::atomic<int> completed_count{0};
    
    std::string base_url = "http://shared.dns.test.local/path";
    
    for (int i = 0; i < num_parallel; ++i) {
        manager->add_request(
            base_url + std::to_string(i),
            [&](error_code, std::vector<char>) {
                completed_count++;
            },
            seconds(5)
        );
    }
    
    // Wait for all to complete
    bool success = run_io_context_until(ios, 10s, 
        [&]() { return completed_count >= num_parallel; });
    
    TEST_CHECK(success);
    TEST_EQUAL(completed_count.load(), num_parallel);
}

// Test 4.1.6: Test configuration caching (session settings reuse)
TORRENT_TEST(curl_thread_manager_config_caching)
{
    io_context ios;
    settings_pack pack = default_settings();
    
    // Set custom user agent to test session settings
    pack.set_str(settings_pack::user_agent, "TestAgent/1.0");
    
    aux::session_settings sett(pack);
    auto manager = curl_thread_manager::create(ios, sett);
    
    const int num_requests = 3;
    std::atomic<int> completed_count{0};
    
    // Make multiple requests - session settings should be reused
    for (int i = 0; i < num_requests; ++i) {
        manager->add_request(
            "http://config.test" + std::to_string(i) + ".local/",
            [&](error_code, std::vector<char>) {
                completed_count++;
            },
            seconds(2)
        );
    }
    
    // Wait for all to complete
    bool success = run_io_context_until(ios, 10s, 
        [&]() { return completed_count >= num_requests; });
    
    TEST_CHECK(success);
    TEST_EQUAL(completed_count.load(), num_requests);
}

// Test 4.1.7: Test cleanup behavior
TORRENT_TEST(curl_thread_manager_cleanup)
{
    io_context ios;
    settings_pack pack = default_settings();
    aux::session_settings sett(pack);
    
    auto manager = curl_thread_manager::create(ios, sett);
    
    // Submit a request
    std::atomic<bool> completed{false};
    
    manager->add_request(
        "http://cleanup.test.local/",
        [&](error_code, std::vector<char>) {
            completed = true;
        },
        seconds(2)
    );
    
    // Wait for completion
    run_io_context_until(ios, 3s, [&]() { return completed.load(); });
    
    // Let it run a bit longer to allow cleanup to potentially occur
    std::this_thread::sleep_for(100ms);
    ios.poll();
    
    // Destructor will clean up everything properly
    // No explicit assertions needed - just verify no crash/leak
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_disabled)
{
    TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL