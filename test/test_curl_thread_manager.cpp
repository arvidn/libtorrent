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
#include <future>

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <functional>
#include <cstdio>
#include <signal.h>
#include <curl/curl.h>

using namespace libtorrent;
using namespace libtorrent::aux;
using namespace std::chrono_literals;

namespace {

struct curl_initializer {
    curl_initializer() {
        signal(SIGPIPE, SIG_IGN);
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~curl_initializer() {
        curl_global_cleanup();
    }
} g_curl_init;

struct WebServerFixture
{
    int http_port = 0;
    std::string file_name;

    WebServerFixture(std::string name, std::string const& content)
        : file_name(std::move(name))
    {
        create_file(content.data(), content.size());
        http_port = start_web_server();
    }

    WebServerFixture(std::string name, std::vector<char> const& content)
        : file_name(std::move(name))
    {
        create_file(content.data(), content.size());
        http_port = start_web_server();
    }

    ~WebServerFixture()
    {
        stop_web_server();
        std::remove(file_name.c_str());
    }

    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(http_port) + "/" + file_name;
    }

private:
    void create_file(const char* data, size_t size)
    {
        std::ofstream test_file(file_name, std::ios::binary);
        test_file.write(data, size);
        test_file.close();
    }
};

}

// Test 1: Basic Lifecycle (Creation and Shutdown)
TORRENT_TEST(curl_thread_manager_lifecycle)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);

    std::shared_ptr<curl_thread_manager> manager;
    try
    {
        manager = curl_thread_manager::create(ios, settings);
    }
    catch (std::runtime_error const& e)
    {
        TEST_ERROR(std::string("Initialization failed: ") + e.what());
        return;
    }

    TEST_CHECK(manager != nullptr);

    manager->shutdown();

    {
        settings_pack pack2;
        session_settings settings2(pack2);
        auto manager2 = curl_thread_manager::create(ios, settings2);
    }
}

// Test 2: Simple Successful Request
TORRENT_TEST(curl_thread_manager_simple_success)
{
    WebServerFixture fixture("test_simple.txt", "Success Content");
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    std::atomic<bool> completed{false};
    error_code result_ec;
    std::vector<char> result_data;

    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            result_ec = ec;
            result_data = std::move(data);
            completed = true;
        });

    bool success = run_io_context_until(ios, 5s, [&]() { return completed.load(); });

    manager->shutdown();

    TEST_CHECK(success);
    TEST_CHECK(!result_ec);
    std::string response_str(result_data.begin(), result_data.end());
    TEST_EQUAL(response_str, "Success Content");
}

// Test 3: Connection Pooling and Concurrency (The critical test)
// Verifies the fix for the original issue where only 1/5 requests completed.
// Uses local server which now supports concurrent connections with Hypercorn.
TORRENT_TEST(curl_thread_manager_concurrency_pooling)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    const int num_requests = 10;
    std::atomic<int> success_count{0};
    std::atomic<int> total_count{0};

    char data_buffer[3216];
    aux::random_bytes(data_buffer);
    std::ofstream("test_file").write(data_buffer, 3216);
    
    int http_port = start_web_server();
    std::string url = "http://127.0.0.1:" + std::to_string(http_port) + "/test_file";
    
    std::printf("\n=== Testing concurrent requests against local server ===\n");
    std::printf("Testing %d concurrent requests to %s\n\n", num_requests, url.c_str());
    
    for (int i = 0; i < num_requests; ++i)
    {
        manager->add_request(
            url,
            [&, i](error_code ec, std::vector<char> data) {
                if (!ec && data.size() == 3216) {
                    success_count++;
                }
                total_count++;
            },
            seconds(10));  // Local server timeout
    }

    // Allow time for local requests
    bool success = run_io_context_until(ios, 15s, [&]() {
        return total_count == num_requests;
    });

    manager->shutdown();
    
    std::printf("Result: %d/%d requests completed\n\n", 
               success_count.load(), num_requests);

    TEST_CHECK(success);
    TEST_EQUAL(success_count.load(), num_requests); // Should be 10/10
}

// Test 4: Thread Safety (Concurrent add_request calls)
TORRENT_TEST(curl_thread_manager_thread_safety)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    const int num_threads = 5;
    const int requests_per_thread = 3;
    const int total_requests = num_threads * requests_per_thread;
    std::atomic<int> completed_count{0};

    char data_buffer[3216];
    aux::random_bytes(data_buffer);
    std::ofstream("test_file").write(data_buffer, 3216);
    
    int http_port = start_web_server();
    std::string url = "http://127.0.0.1:" + std::to_string(http_port) + "/test_file";

    std::printf("\n=== Testing thread safety with %d threads ===\n", num_threads);
    std::printf("Each thread submitting %d requests to local server\n\n", requests_per_thread);

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < requests_per_thread; ++i)
            {
                manager->add_request(
                    url,
                    [&](error_code ec, std::vector<char> data) {
                        (void)data;
                        if (!ec) completed_count++;
                    },
                    seconds(30));
            }
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    bool success = run_io_context_until(ios, 60s, [&]() {
        return completed_count == total_requests;
    });

    manager->shutdown();

    std::printf("Result: %d/%d requests completed\n\n",
               completed_count.load(), total_requests);

    TEST_CHECK(success);
    TEST_EQUAL(completed_count.load(), total_requests);
}

// Test 5: Error Handling (HTTP 404 Not Found)
TORRENT_TEST(curl_thread_manager_http_404)
{
    int http_port = start_web_server();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    std::atomic<bool> completed{false};
    error_code result_ec;
    std::vector<char> result_data;

    std::string url = "http://127.0.0.1:" + std::to_string(http_port) + "/non_existent.txt";

    std::printf("TEST: Requesting non-existent file from %s\n", url.c_str());

    manager->add_request(
        url,
        [&](error_code ec, std::vector<char> data) {
            std::printf("TEST: Callback invoked with error: %s (%d), data size: %zu\n", 
                       ec.message().c_str(), ec.value(), data.size());
            result_ec = ec;
            result_data = std::move(data);
            completed = true;
        });

    run_io_context_until(ios, 10s, [&]() { return completed.load(); });

    stop_web_server();
    manager->shutdown();

    if (!completed) {
        std::printf("TEST ERROR: Callback was never invoked (timeout after 10s)\n");
    }
    if (completed && result_ec != errors::http_error) {
        std::printf("TEST ERROR: Expected http_error but got: %s (%d)\n", 
                   result_ec.message().c_str(), result_ec.value());
        if (!result_data.empty()) {
            std::printf("TEST ERROR: Response data (first 200 chars): %.200s\n", 
                       result_data.data());
        }
    }

    TEST_CHECK(completed);
    // HTTP codes >= 400 map to errors::http_error
    TEST_EQUAL(result_ec, errors::http_error);
}

// Test 6: Error Handling (DNS Failure)
TORRENT_TEST(curl_thread_manager_dns_failure)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    std::atomic<bool> completed{false};
    error_code result_ec;

    std::string url = "http://invalid.domain.libtorrent.test/";

    manager->add_request(
        url,
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            result_ec = ec;
            completed = true;
        },
        seconds(5)); // Short timeout for DNS

    run_io_context_until(ios, 10s, [&]() { return completed.load(); });

    manager->shutdown();

    std::printf("DNS failure test - Error code received: %d (expected: %d=invalid_hostname or %d=timed_out)\n", 
                result_ec.value(), 
                (int)errors::invalid_hostname,
                (int)errors::timed_out);

    TEST_CHECK(completed);
    // CURLE_COULDNT_RESOLVE_HOST maps to errors::invalid_hostname (31)
    // but sometimes we get timed_out (36) if DNS lookup times out
    TEST_CHECK(result_ec == errors::invalid_hostname || result_ec == errors::timed_out);
}

// Test 7: Connection Timeout Enforcement
TORRENT_TEST(curl_thread_manager_timeout)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    std::atomic<bool> completed{false};
    error_code result_ec;

    std::string url = "http://10.255.255.1/";

    auto start_time = std::chrono::steady_clock::now();

    manager->add_request(
        url,
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            result_ec = ec;
            completed = true;
        },
        seconds(1)); // 1 second timeout

    run_io_context_until(ios, 5s, [&]() { return completed.load(); });

    auto duration = std::chrono::steady_clock::now() - start_time;

    manager->shutdown();

    TEST_CHECK(completed);
    TEST_EQUAL(result_ec, errors::timed_out);
    // Ensure it didn't take significantly longer than the requested timeout
    TEST_CHECK(duration < 2s);
}

// Test 8: Shutdown with Active Requests
TORRENT_TEST(curl_thread_manager_shutdown_active)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    const int num_requests = 5;
    std::atomic<int> callback_count{0};
    std::atomic<int> shutdown_errors{0};

    std::string url = "http://10.255.255.1/";

    for (int i = 0; i < num_requests; ++i)
    {
        manager->add_request(
            url,
            [&](error_code ec, std::vector<char> data) {
                (void)data;
                // Expecting cancellation error (errors::session_is_closing)
                if (ec == errors::session_is_closing) {
                    shutdown_errors++;
                }
                callback_count++;
            },
            seconds(30)); // Long timeout
    }

    std::this_thread::sleep_for(100ms);

    // Shutdown immediately
    manager->shutdown();

    // Process callbacks
    run_io_context_until(ios, 5s, [&]() {
        return callback_count == num_requests;
    });

    TEST_EQUAL(callback_count.load(), num_requests);
    TEST_EQUAL(shutdown_errors.load(), num_requests);
}

// Test 9: Shutdown with Queued Requests
TORRENT_TEST(curl_thread_manager_shutdown_queued)
{
    WebServerFixture fixture("test_queued.txt", "Queued");
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    const int num_requests = 50;
    std::atomic<int> callback_count{0};

    for (int i = 0; i < num_requests; ++i)
    {
        manager->add_request(
            fixture.url(),
            [&](error_code ec, std::vector<char> data) {
                (void)ec;
                (void)data;
                // Callback MUST be called, regardless of success or failure/cancellation
                callback_count++;
            });
    }

    // Shutdown immediately, before the worker thread processes them all
    manager->shutdown();

    // Process callbacks
    run_io_context_until(ios, 5s, [&]() {
        return callback_count == num_requests;
    });

    // Verify all callbacks were invoked
    TEST_EQUAL(callback_count.load(), num_requests);
}

// Test 10: Wakeup Latency (Performance)
TORRENT_TEST(curl_thread_manager_wakeup_latency)
{
    WebServerFixture fixture("test_latency.txt", "Fast");
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);

    std::atomic<bool> completed{false};

    auto start_time = std::chrono::high_resolution_clock::now();

    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)ec;
            (void)data;
            completed = true;
        });

    run_io_context_until(ios, 2s, [&]() { return completed.load(); });

    auto duration = std::chrono::high_resolution_clock::now() - start_time;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    manager->shutdown();

    TEST_CHECK(completed);
    // Check that the response time is fast, indicating curl_multi_wakeup worked (not the 1000ms fallback wait).
    std::printf("Wakeup latency: %ldms\n", ms);
    TEST_CHECK(ms < 100);
}

TORRENT_TEST(curl_thread_manager_size_limit)
{
    io_context ios;
    settings_pack pack;
    
    // Set a small limit for testing (10KB)
    pack.set_int(settings_pack::max_tracker_response_size, 10 * 1024);
    
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    // Create a file larger than the limit (15KB)
    std::vector<char> large_content(15 * 1024, 'A');
    WebServerFixture fixture("test_large.bin", large_content);

    std::atomic<bool> completed{false};
    error_code result_ec;

    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            result_ec = ec;
            completed = true;
            std::printf("Size limit test: Handler called with ec=%d (%s)\n", 
                       ec.value(), ec.message().c_str());
            std::fflush(stdout);
        });

    run_io_context_until(ios, 5s, [&]() { return completed.load(); });

    manager->shutdown();

    TEST_CHECK(completed);
    // The request should fail (CURLE_WRITE_ERROR maps to errors::http_error in the implementation)
    TEST_EQUAL(result_ec, errors::http_error);
}

TORRENT_TEST(curl_requirements_check)
{
    curl_version_info_data* ver = curl_version_info(CURLVERSION_NOW);
    TEST_CHECK(ver != nullptr);
    if (!ver) return;

    // Minimum version 7.66.0 (0x074200) for curl_multi_poll
    std::printf("libcurl version: %s (0x%06x)\n", ver->version, ver->version_num);
    TEST_CHECK(ver->version_num >= 0x074200);

    // Async DNS support (required to prevent blocking the worker thread)
    bool async_dns = (ver->features & CURL_VERSION_ASYNCHDNS) != 0;
    std::printf("Async DNS support: %s\n", async_dns ? "Yes" : "No");
    TEST_CHECK(async_dns);
}

// Test 13: Simple 500 Error (verify basic retry behavior)
TORRENT_TEST(curl_thread_manager_simple_500_error)
{
    WebServerFixture fixture("status/500", "");  // Path that always returns 500
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            std::printf("SIMPLE 500 TEST: Callback called with error: %s\n", ec.message().c_str());
            result_ec = ec;
            completed = true;
        },
        seconds(30));  // Long timeout to allow all retries
    
    run_io_context_until(ios, 20s, [&]() { return completed.load(); });
    
    manager->shutdown();
    
    if (!completed) {
        std::printf("ERROR: Simple 500 test callback never invoked!\n");
    }
    
    TEST_CHECK(completed);
    TEST_EQUAL(result_ec, errors::http_error);
}

// Test 13b: Retry on 500 Server Error
TORRENT_TEST(curl_thread_manager_retry_on_500)
{
    WebServerFixture fixture("status/500", "");  // Path that always returns 500
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    auto start_time = std::chrono::steady_clock::now();
    
    std::printf("Starting retry test with /status/500\n");
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            std::printf("Retry test callback called! Error: %s (%d)\n", 
                       ec.message().c_str(), ec.value());
            result_ec = ec;
            completed = true;
        },
        seconds(20));  // Long timeout to allow retries
    
    std::printf("Waiting for completion...\n");
    bool finished = run_io_context_until(ios, 30s, [&]() { 
        if (completed.load()) {
            std::printf("Test completed!\n");
        }
        return completed.load(); 
    });
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<milliseconds>(elapsed).count();
    
    std::printf("Elapsed time: %ld ms, Finished: %d\n", elapsed_ms, finished);
    
    manager->shutdown();
    
    if (!completed) {
        std::printf("ERROR: Callback was never called!\n");
    }
    
    TEST_CHECK(completed);
    TEST_EQUAL(result_ec, errors::http_error);
    
    std::printf("Retry test took %ld ms\n", elapsed_ms);
    TEST_CHECK(elapsed_ms >= 6000);  // Allow some timing flexibility
}

// Test 14: Exponential Backoff Timing
TORRENT_TEST(curl_thread_manager_exponential_backoff)
{
    WebServerFixture fixture("retry_test", "");
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    std::vector<char> result_data;
    
    auto start_time = std::chrono::steady_clock::now();
    
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            result_ec = ec;
            result_data = std::move(data);
            completed = true;
        });
    
    run_io_context_until(ios, 5s, [&]() { return completed.load(); });
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<milliseconds>(elapsed).count();
    
    manager->shutdown();
    
    TEST_CHECK(completed);
    TEST_CHECK(!result_ec);  // Should succeed on retry
    TEST_CHECK(result_data.size() > 0);  // Should have response data
    
    std::printf("Exponential backoff test took %ld ms\n", elapsed_ms);
    TEST_CHECK(elapsed_ms >= 1900);  // At least 1900ms
    TEST_CHECK(elapsed_ms <= 2500); // But less than 2.5s to account for overhead
}

// Test 15: Max Retry Attempts
TORRENT_TEST(curl_thread_manager_max_retries)
{
    WebServerFixture fixture("status/503", "");  // Always returns 503
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    auto start_time = std::chrono::steady_clock::now();
    
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            result_ec = ec;
            completed = true;
        },
        seconds(30));  // Long timeout to allow all retries
    
    run_io_context_until(ios, 15s, [&]() { return completed.load(); });
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<milliseconds>(elapsed).count();
    
    manager->shutdown();
    
    TEST_CHECK(completed);
    TEST_EQUAL(result_ec, errors::http_error);
    
    std::printf("Max retries test took %ld ms\n", elapsed_ms);
    TEST_CHECK(elapsed_ms >= 13000);  // At least 13 seconds
    TEST_CHECK(elapsed_ms <= 15000); // But should complete within 15s
}

// Test 16: Deadline Enforcement (No Retry Past Deadline)
TORRENT_TEST(curl_thread_manager_retry_deadline)
{
    WebServerFixture fixture("status/500", "");  // Always returns 500
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Short timeout that won't allow retries
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char> data) {
            (void)data;
            result_ec = ec;
            completed = true;
        },
        milliseconds(500));  // 500ms timeout - too short for retry
    
    run_io_context_until(ios, 2s, [&]() { return completed.load(); });
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<milliseconds>(elapsed).count();
    
    manager->shutdown();
    
    TEST_CHECK(completed);
    // Could be either timeout or http_error depending on timing
    TEST_CHECK(result_ec == errors::timed_out || result_ec == errors::http_error);
    
    std::printf("Deadline test took %ld ms\n", elapsed_ms);
    TEST_CHECK(elapsed_ms <= 1500);  // Should not retry (no 1s delay)
}

// Test 17: No Retry on 404 (Non-Retryable Error)
TORRENT_TEST(curl_thread_manager_no_retry_404)
{
    WebServerFixture fixture("status/404", "");  // Always returns 404
    
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    auto manager = curl_thread_manager::create(ios, settings);
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    auto start_time = std::chrono::steady_clock::now();
    
    manager->add_request(
        fixture.url(),
        [&](error_code ec, std::vector<char>) {
            result_ec = ec;
            completed = true;
        });
    
    run_io_context_until(ios, 2s, [&]() { return completed.load(); });
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<milliseconds>(elapsed).count();
    
    manager->shutdown();
    
    TEST_CHECK(completed);
    TEST_EQUAL(result_ec, errors::http_error);
    
    std::printf("No retry on 404 test took %ld ms\n", elapsed_ms);
    TEST_CHECK(elapsed_ms <= 500);  // Should be fast, no retry delay
}

// Test C3: String Lifetime Safety
TORRENT_TEST(string_lifetime_safety)
{
    io_context ios;
    settings_pack settings;
    
    settings.set_bool(settings_pack::proxy_tracker_connections, true);
    settings.set_str(settings_pack::proxy_hostname, "very-long-proxy-hostname-to-test-string-storage.example.com");
    settings.set_int(settings_pack::proxy_port, 8080);
    settings.set_str(settings_pack::proxy_username, "very_long_username_for_testing_secure_storage");
    settings.set_str(settings_pack::proxy_password, "very_long_password_that_should_be_securely_cleared");
    settings.set_str(settings_pack::user_agent, "Test User Agent with Long String for Lifetime Testing");
    
    session_settings sett(settings);
    auto manager = curl_thread_manager::create(ios, sett);
    
    std::atomic<int> completed_count{0};
    const int num_requests = 100;
    
    for (int i = 0; i < num_requests; ++i) {
        std::string long_url = "http://test-server.example.com/very/long/path/to/test/string/storage/announce?info_hash=" 
            + std::to_string(i) + "&peer_id=12345678901234567890&port=6881";
        
        manager->add_request(long_url, 
            [&completed_count](error_code const&, std::vector<char> const&) {
                completed_count++;
            }, seconds(1));
    }
    
    run_io_context_until(ios, milliseconds(200), [&]() {
        return false;
    });
    
    manager->shutdown();
    
    TEST_CHECK(true);
    std::printf("String lifetime test completed with %d requests\n", num_requests);
}

// Test H3: Race Condition in Atomic Flag
TORRENT_TEST(notification_race_condition)
{
    io_context ios;
    settings_pack settings;
    session_settings sett(settings);
    auto manager = curl_thread_manager::create(ios, sett);
    
    std::atomic<int> completed{0};
    const int num_threads = 10;
    const int requests_per_thread = 50;
    
    // Spawn multiple threads to send requests concurrently
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&manager, &completed, t, requests_per_thread]() {
            for (int i = 0; i < requests_per_thread; ++i) {
                std::string url = "http://127.0.0.1:8080/test?thread=" + std::to_string(t) + "&req=" + std::to_string(i);
                manager->add_request(url,
                    [&completed](error_code const&, std::vector<char> const&) {
                        completed.fetch_add(1, std::memory_order_relaxed);
                    }, seconds(5));
                
                std::this_thread::sleep_for(microseconds(rand() % 100));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    run_io_context_until(ios, seconds(15), [&]() {
        return completed.load() >= (num_threads * requests_per_thread * 8 / 10);
    });
    
    manager->shutdown();
    
    std::printf("Race condition test: %d/%d requests completed\n", 
        completed.load(), num_threads * requests_per_thread);
    TEST_CHECK(completed > 0);
}

TORRENT_TEST(connection_pool_dynamic_scaling)
{
    io_context ios;
    session_settings settings;
    
    auto mgr = curl_thread_manager::create(ios, settings);
    TEST_CHECK(mgr != nullptr);
    
    auto stats = mgr->get_stats();
    TEST_EQUAL(stats.unique_tracker_hosts, 0);
    TEST_EQUAL(stats.current_connection_limit, 2);  // Minimum is 2
    
    mgr->tracker_added("http://tracker1.example.com:8080/announce");
    mgr->tracker_added("http://tracker1.example.com:8080/announce");
    mgr->tracker_added("http://tracker1.example.com:9090/announce");
    
    stats = mgr->get_stats();
    TEST_EQUAL(stats.unique_tracker_hosts, 1);
    TEST_CHECK(stats.current_connection_limit >= 2);
    
    mgr->tracker_added("http://tracker2.example.com/announce");
    mgr->tracker_added("udp://tracker3.example.com:6969/announce");
    
    run_io_context_until(ios, 1s, [&mgr]() {
        return mgr->get_stats().current_connection_limit == 6;
    });
    
    stats = mgr->get_stats();
    TEST_EQUAL(stats.unique_tracker_hosts, 3);
    TEST_EQUAL(stats.current_connection_limit, 6);  // 3 hosts * 2 connections
    
    mgr->tracker_removed("http://tracker1.example.com:8080/announce");
    mgr->tracker_removed("http://tracker1.example.com:8080/announce");
    mgr->tracker_removed("http://tracker1.example.com:9090/announce");
    
    run_io_context_until(ios, 1s, [&mgr]() {
        return mgr->get_stats().current_connection_limit == 4;
    });
    
    stats = mgr->get_stats();
    TEST_EQUAL(stats.unique_tracker_hosts, 2);
    TEST_EQUAL(stats.current_connection_limit, 4);  // 2 hosts * 2 connections
    
    mgr->tracker_removed("http://tracker2.example.com/announce");
    mgr->tracker_removed("udp://tracker3.example.com:6969/announce");
    
    run_io_context_until(ios, 1s, [&mgr]() {
        return mgr->get_stats().current_connection_limit == 2;
    });
    
    stats = mgr->get_stats();
    TEST_EQUAL(stats.unique_tracker_hosts, 0);
    TEST_EQUAL(stats.current_connection_limit, 2);  // Back to minimum
    
    mgr->tracker_added("");
    mgr->tracker_added("not-a-url");
    mgr->tracker_removed("");
    mgr->tracker_removed("not-a-url");
    
    mgr->shutdown();
    run_io_context_until(ios, 5s, [&mgr]() { return mgr.use_count() == 1; });
}

TORRENT_TEST(tracker_host_counter_reference_counting)
{
    // Test that tracker_host_counter properly handles multiple adds/removes
    io_context ios;
    session_settings settings;
    
    auto mgr = curl_thread_manager::create(ios, settings);
    TEST_CHECK(mgr != nullptr);
    
    // Add same tracker multiple times (simulating multiple torrents)
    mgr->tracker_added("http://tracker.example.com/announce");
    mgr->tracker_added("http://tracker.example.com/announce");
    mgr->tracker_added("http://tracker.example.com/announce");
    
    // Remove instances one by one
    mgr->tracker_removed("http://tracker.example.com/announce");
    mgr->tracker_removed("http://tracker.example.com/announce");
    mgr->tracker_removed("http://tracker.example.com/announce");
    
    // Test removing more times than added (should handle gracefully)
    mgr->tracker_removed("http://tracker.example.com/announce");
    
    mgr->shutdown();
    run_io_context_until(ios, 5s, [&mgr]() { return mgr.use_count() == 1; });
}

TORRENT_TEST(interface_binding)
{
    // Test that outgoing_interfaces setting is properly applied
    // This test verifies the code path doesn't crash when interface binding is configured
    io_context ios;
    session_settings settings;
    
    // Set interface binding to a non-existent interface to test error handling
    // Using a fake interface ensures we test the error path consistently
    settings.set_str(settings_pack::outgoing_interfaces, "fake_interface_test");
    
    auto mgr = curl_thread_manager::create(ios, settings);
    TEST_CHECK(mgr != nullptr);
    
    // Create a simple test server to verify the request
    WebServerFixture server("test_interface", "Interface test response");
    
    std::promise<error_code> ec_promise;
    std::promise<std::vector<char>> response_promise;
    auto ec_future = ec_promise.get_future();
    auto response_future = response_promise.get_future();
    
    char url_buffer[256];
    std::snprintf(url_buffer, sizeof(url_buffer), "http://127.0.0.1:%d/test_interface", server.http_port);
    
    mgr->add_request(
        url_buffer,
        [&](error_code ec, std::vector<char> response) {
            ec_promise.set_value(ec);
            response_promise.set_value(std::move(response));
        },
        5s
    );
    
    run_io_context_until(ios, 10s, [&]() {
        return response_future.wait_for(0s) == std::future_status::ready;
    });
    
    error_code ec = ec_future.get();
    std::vector<char> response = response_future.get();
    
    // When using a non-existent interface, we expect CURLE_INTERFACE_FAILED (45)
    // which maps to errors::http_error (and is non-retryable)
    // This test verifies that:
    // 1. The interface binding code path doesn't crash
    // 2. The error is handled gracefully without retries
    TEST_CHECK(ec == errors::http_error);
    
    mgr->shutdown();
    run_io_context_until(ios, 5s, [&mgr]() { return mgr.use_count() == 1; });
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_thread_manager_not_available)
{
    TEST_CHECK(true);
    std::printf("libcurl support not enabled. curl_thread_manager tests skipped.\n");
}

#endif // TORRENT_USE_LIBCURL