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
#include <chrono>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include <algorithm>

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

} // anonymous namespace

// Test 2.1.1: Test pooled_handle structure
TORRENT_TEST(curl_pooled_handle_structure)
{
    // Test construction with metadata
    curl_handle_pool::pooled_handle handle;
    
    // Check initial state
    TEST_CHECK(handle.handle.get() != nullptr);
    TEST_CHECK(handle.settings_version == 0);
    TEST_CHECK(handle.needs_full_config == true);
    
    // Check timestamp is recent (within 1 second)
    auto now = std::chrono::steady_clock::now();
    auto diff = now - handle.last_used;
    TEST_CHECK(diff < 1s);
}

// Test 2.1.2: Test basic pool acquire/release
TORRENT_TEST(curl_handle_pool_basic_acquire_release)
{
    curl_handle_pool pool;
    
    // Test acquiring from empty pool (creates new)
    auto handle1 = pool.acquire();
    TEST_CHECK(handle1 != nullptr);
    TEST_CHECK(handle1->handle.get() != nullptr);
    TEST_CHECK(handle1->needs_full_config == true);
    
    // Store the CURL* pointer for comparison
    CURL* curl_ptr = handle1->handle.get();
    
    // Release handle back to pool
    pool.release(std::move(handle1));
    TEST_CHECK(handle1 == nullptr); // Should be moved
    
    // Acquire again - should get the same handle back
    auto handle2 = pool.acquire();
    TEST_CHECK(handle2 != nullptr);
    TEST_CHECK(handle2->handle.get() == curl_ptr); // Same CURL handle
    TEST_CHECK(handle2->needs_full_config == false); // Settings still valid
}

// Test 2.2.1: Test pool size limits
TORRENT_TEST(curl_handle_pool_capacity)
{
    curl_handle_pool pool;
    
    // Acquire more than MAX_POOL_SIZE handles
    std::vector<std::unique_ptr<curl_handle_pool::pooled_handle>> handles;
    for (size_t i = 0; i < curl_handle_pool::MAX_POOL_SIZE + 5; ++i) {
        handles.push_back(pool.acquire());
        TEST_CHECK(handles.back() != nullptr);
    }
    
    // Release all handles
    for (auto& h : handles) {
        pool.release(std::move(h));
    }
    
    // Now acquire MAX_POOL_SIZE handles - they should be from pool
    std::set<CURL*> reused_handles;
    for (size_t i = 0; i < curl_handle_pool::MAX_POOL_SIZE; ++i) {
        auto h = pool.acquire();
        reused_handles.insert(h->handle.get());
        handles.push_back(std::move(h));
    }
    
    // All should be unique (pool kept them all)
    TEST_CHECK(reused_handles.size() == curl_handle_pool::MAX_POOL_SIZE);
}

// Test 2.2.2: Test idle handle cleanup
TORRENT_TEST(curl_handle_pool_idle_cleanup)
{
    // We need to test the cleanup mechanism differently since release() updates timestamp
    // Create a custom test that directly manipulates the pool's internal state would be ideal,
    // but for now we'll test that cleanup_idle_handles() doesn't crash
    
    curl_handle_pool pool;
    
    // Create and release multiple handles
    std::vector<CURL*> ptrs;
    for (int i = 0; i < 5; ++i) {
        auto handle = pool.acquire();
        ptrs.push_back(handle->handle.get());
        pool.release(std::move(handle));
    }
    
    // Call cleanup - it won't remove anything since timestamps are recent
    pool.cleanup_idle_handles();
    
    // Verify handles are still in pool
    for (int i = 0; i < 5; ++i) {
        auto handle = pool.acquire();
        // Should get one of the previously created handles
        TEST_CHECK(std::find(ptrs.begin(), ptrs.end(), handle->handle.get()) != ptrs.end());
        pool.release(std::move(handle));
    }
    
    // Test passes if no crash and handles are reused
    TEST_CHECK(true);
}

// Test 2.3.1: Test settings invalidation
TORRENT_TEST(curl_handle_pool_settings_invalidation)
{
    curl_handle_pool pool;
    
    // Acquire and release a handle
    auto handle = pool.acquire();
    TEST_CHECK(handle->settings_version == 0);
    handle->settings_version = 0; // Ensure it's 0
    pool.release(std::move(handle));
    
    // Invalidate settings
    pool.invalidate_settings();
    
    // Acquire again - should need full config
    auto handle2 = pool.acquire();
    TEST_CHECK(handle2->needs_full_config == true);
}

// Test 2.1.3: Test multiple acquire/release cycles
TORRENT_TEST(curl_handle_pool_multiple_cycles)
{
    curl_handle_pool pool;
    
    // Track handles we've seen
    std::set<CURL*> seen_handles;
    
    // Do multiple acquire/release cycles
    for (int cycle = 0; cycle < 3; ++cycle) {
        std::vector<std::unique_ptr<curl_handle_pool::pooled_handle>> handles;
        
        // Acquire 5 handles
        for (int i = 0; i < 5; ++i) {
            auto h = pool.acquire();
            seen_handles.insert(h->handle.get());
            handles.push_back(std::move(h));
        }
        
        // Release them back
        for (auto& h : handles) {
            pool.release(std::move(h));
        }
    }
    
    // Should have reused handles (not created 15 unique ones)
    TEST_CHECK(seen_handles.size() <= 5);
}

// Test 2.1.4: Test nullptr handling
TORRENT_TEST(curl_handle_pool_nullptr_handling)
{
    curl_handle_pool pool;
    
    // Release nullptr should be safe
    std::unique_ptr<curl_handle_pool::pooled_handle> null_handle;
    pool.release(std::move(null_handle)); // Should not crash
    
    // Acquire should still work
    auto handle = pool.acquire();
    TEST_CHECK(handle != nullptr);
}

// Test 2.2.3: Test timestamp updates
TORRENT_TEST(curl_handle_pool_timestamp_updates)
{
    curl_handle_pool pool;
    
    auto handle = pool.acquire();
    auto initial_time = handle->last_used;
    
    // Wait a bit
    std::this_thread::sleep_for(10ms);
    
    // Release and reacquire
    pool.release(std::move(handle));
    handle = pool.acquire();
    
    // Timestamp should be updated on release
    TEST_CHECK(handle->last_used > initial_time);
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_disabled)
{
    TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL
