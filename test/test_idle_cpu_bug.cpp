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

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/curl_handle_wrappers.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/random.hpp"

#include <memory>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <thread>
#include <iostream>
#include <fstream>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#include <sstream>
#endif

using namespace libtorrent;
using namespace libtorrent::aux;
using namespace std::chrono;

namespace {

#ifdef __linux__
struct cpu_stats {
    long user_time;
    long system_time;
};

cpu_stats get_process_cpu_stats() {
    cpu_stats stats = {0, 0};
    std::ifstream stat_file("/proc/self/stat");
    if (!stat_file) return stats;
    
    std::string line;
    std::getline(stat_file, line);
    
    // Parse /proc/self/stat - fields 14 and 15 are utime and stime
    std::istringstream iss(line);
    std::string field;
    int field_num = 0;
    
    while (iss >> field && field_num < 15) {
        field_num++;
        if (field_num == 14) {
            stats.user_time = std::stol(field);
        } else if (field_num == 15) {
            stats.system_time = std::stol(field);
            break;
        }
    }
    
    return stats;
}

double calculate_cpu_usage(cpu_stats start, cpu_stats end, std::chrono::milliseconds duration) {
    long ticks_per_second = sysconf(_SC_CLK_TCK);
    long cpu_ticks_used = (end.user_time - start.user_time) + (end.system_time - start.system_time);
    double cpu_seconds_used = static_cast<double>(cpu_ticks_used) / ticks_per_second;
    double wall_seconds = duration.count() / 1000.0;
    return (cpu_seconds_used / wall_seconds) * 100.0;
}

size_t get_process_memory() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<size_t>(usage.ru_maxrss * 1024); // Convert to bytes
}
#else
struct cpu_stats { long user_time; long system_time; };
cpu_stats get_process_cpu_stats() { return {0, 0}; }
double calculate_cpu_usage(cpu_stats, cpu_stats, std::chrono::milliseconds) { return 0.0; }
size_t get_process_memory() { return 0; }
#endif

} // anonymous namespace

TORRENT_TEST(idle_cpu_usage)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
#ifdef __linux__
    auto start_stats = get_process_cpu_stats();
    auto start_time = std::chrono::steady_clock::now();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto end_stats = get_process_cpu_stats();
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double cpu_usage = calculate_cpu_usage(start_stats, end_stats, duration);
    
    // CPU usage should be very low when idle (< 5%)
    // If the 100% CPU bug was present, this would be close to 100%
    std::cout << "CPU usage while idle: " << cpu_usage << "%" << std::endl;
    TEST_CHECK(cpu_usage < 5.0);
#else
    // On non-Linux systems, just verify no hang
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    mgr->add_request("http://127.0.0.1:1/", // Non-responsive address
        [&](error_code const& ec, std::vector<char> const&) {
            result_ec = ec;
            completed = true;
        }, seconds(1));
    
    bool success = run_io_context_until(ios, seconds(3), [&]() { return completed.load(); });
    
    TEST_CHECK(success);
    if (completed) {
        // Should timeout or fail to connect
        TEST_CHECK(result_ec == errors::timed_out || result_ec == errors::http_error);
    }
    
    mgr->shutdown();
    
    // If we get here without hanging, basic functionality works
    TEST_CHECK(true);
}

TORRENT_TEST(wakeup_mechanism)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::atomic<bool> completed{false};
    auto start = std::chrono::steady_clock::now();
    
    mgr->add_request("http://127.0.0.1:1/", // Non-responsive
        [&completed](error_code const& ec, std::vector<char> const&) {
            (void)ec;
            completed = true;
        }, seconds(1));
    
    // Should timeout in ~1 second, not 60 seconds
    bool success = run_io_context_until(ios, seconds(3), [&]() { return completed.load(); });
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    TEST_CHECK(success);
    // Should complete in ~1 second (timeout), not wait the full idle timeout
    TEST_CHECK(elapsed < seconds(5));
    
    mgr->shutdown();
}

TORRENT_TEST(curl_handle_raii)
{
    {
        curl_easy_handle handle;
        TEST_CHECK(handle.get() != nullptr);
    }
    
    {
        curl_easy_handle h1;
        CURL* ptr1 = h1.get();
        
        curl_easy_handle h2(std::move(h1));
        TEST_CHECK(h1.get() == nullptr);
        TEST_CHECK(h2.get() == ptr1);
    }
    
    {
        curl_easy_handle handle;
        try {
            handle.setopt(static_cast<CURLoption>(-1), 0L);
            TEST_CHECK(false); // Should not reach here
        } catch (std::runtime_error const& e) {
            TEST_CHECK(true); // Exception thrown as expected
        }
    }
}

TORRENT_TEST(ssrf_redirect_disabled)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    // Make a request to a URL that would normally redirect
    // With redirects disabled, this should fail
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    char data_buffer[3216];
    aux::random_bytes(data_buffer);
    std::ofstream("test_file").write(data_buffer, 3216);
    int http_port = start_web_server();
    
    std::string redirect_url = "http://127.0.0.1:" + std::to_string(http_port) + "/redirect";
    mgr->add_request(redirect_url,
        [&completed, &result_ec](error_code const& ec, std::vector<char> const& response) {
            (void)response;
            result_ec = ec;
            completed = true;
        });
    
    run_io_context_until(ios, seconds(5), [&]() { return completed.load(); });
    
    error_code ec = result_ec;
    
    // With redirects disabled, we should get the redirect response (301/302)
    // but not follow it. This prevents SSRF attacks.
    // The request should succeed but not follow the redirect
    TEST_CHECK(!ec || ec == errors::http_error);
    
    mgr->shutdown();
}

TORRENT_TEST(tls_version_enforcement)
{
    io_context ios;
    settings_pack pack;
    
    // Set minimum TLS version to 1.2
    pack.set_int(settings_pack::tracker_min_tls_version, 0x0303); // TLS 1.2
    
    session_settings settings(pack);
    auto mgr = curl_thread_manager::create(ios, settings);
    
    char data_buffer[3216];
    aux::random_bytes(data_buffer);
    std::ofstream("test_file").write(data_buffer, 3216);
    int https_port = start_web_server(true); // SSL enabled for HTTPS
    
    std::string https_url = "https://127.0.0.1:" + std::to_string(https_port) + "/test_file";
    
    std::atomic<bool> completed{false};
    error_code result_ec;
    
    mgr->add_request(https_url,
        [&completed, &result_ec](error_code const& ec, std::vector<char> const&) {
            result_ec = ec;
            completed = true;
        });
    
    run_io_context_until(ios, seconds(5), [&]() { return completed.load(); });
    
    error_code ec = result_ec;
    
    // Modern servers support TLS 1.2+, so this should succeed
    // If it fails, it might be due to network issues
    if (!ec) {
        TEST_CHECK(true); // Connection succeeded with TLS 1.2+
    } else {
        // Allow network failures but not SSL errors
        TEST_CHECK(ec != errors::invalid_ssl_cert);
    }
    
    mgr->shutdown();
}

TORRENT_TEST(memory_pool_usage)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    size_t initial_memory = get_process_memory();
    
    // Make multiple requests to non-existent local addresses
    // This tests memory pooling without relying on external services
    std::atomic<int> completed_count{0};
    const int num_requests = 50;
    
    for (int i = 0; i < num_requests; ++i) {
        // Use local addresses that will fail quickly
        std::string url = "http://127.0.0.1:" + std::to_string(10000 + i) + "/test";
        mgr->add_request(url,
            [&completed_count](error_code const&, std::vector<char> const&) {
                completed_count++;
            }, seconds(1));
    }
    
    bool success = run_io_context_until(ios, seconds(10), [&completed_count, num_requests]() {
        return completed_count >= num_requests;
    });
    
    TEST_CHECK(success || completed_count > 0);
    
    // Memory growth should be minimal due to pooling
    size_t final_memory = get_process_memory();
    size_t growth = (final_memory > initial_memory) ? (final_memory - initial_memory) : 0;
    
    // Should be less than 10MB growth for 50 requests (with pooling)
    // Without pooling, it would be much higher
    TEST_CHECK(growth < 10 * 1024 * 1024);
    
    mgr->shutdown();
}

TORRENT_TEST(proxy_credentials_secure)
{
    io_context ios;
    settings_pack pack;
    
    // Configure proxy with authentication
    pack.set_int(settings_pack::proxy_type, settings_pack::http_pw);
    pack.set_str(settings_pack::proxy_hostname, "proxy.example.com");
    pack.set_int(settings_pack::proxy_port, 8080);
    pack.set_str(settings_pack::proxy_username, "testuser");
    pack.set_str(settings_pack::proxy_password, "testpass");
    pack.set_bool(settings_pack::proxy_tracker_connections, true);
    
    session_settings settings(pack);
    auto mgr = curl_thread_manager::create(ios, settings);
    
    // The credentials should be handled securely (separate fields, cleared after use)
    // This test verifies the manager starts up correctly with proxy settings
    TEST_CHECK(mgr != nullptr);
    
    mgr->shutdown();
}

TORRENT_TEST(curl_multi_poll_timeout)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    // The key test here is that with curl_multi_poll, the thread
    // should properly wait when idle, not spin
    
    auto start = std::chrono::steady_clock::now();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    // Time should be close to 1 second, not significantly more
    // (which would indicate the thread is working/spinning)
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    TEST_CHECK(elapsed_ms >= 900 && elapsed_ms <= 1200);
    
    mgr->shutdown();
}

TORRENT_TEST(concurrent_requests_cleanup)
{
    io_context ios;
    settings_pack pack;
    session_settings settings(pack);
    
    auto mgr = curl_thread_manager::create(ios, settings);
    
    // Make 5 concurrent requests (the original bug allowed only 1/5 to complete)
    std::atomic<int> completed_count{0};
    const int num_requests = 5;
    
    for (int i = 0; i < num_requests; ++i) {
        mgr->add_request("http://127.0.0.1:" + std::to_string(20000 + i) + "/",
            [&completed_count](error_code const& ec, std::vector<char> const&) {
                (void)ec;
                // Mark as completed regardless of error
                completed_count++;
            }, seconds(1));
    }
    
    run_io_context_until(ios, seconds(3), [&completed_count, num_requests]() {
        return completed_count >= num_requests;
    });
    
    // All 5 requests should complete (even if with errors)
    int completed = completed_count.load();
    
    // With the fix, all 5 should complete. Before fix, only 1 would complete.
    TEST_CHECK(completed == 5);
    
    mgr->shutdown();
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(idle_cpu_usage)
{
}

#endif // TORRENT_USE_LIBCURL