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

#ifndef TORRENT_CURL_THREAD_MANAGER_HPP
#define TORRENT_CURL_THREAD_MANAGER_HPP

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include <curl/curl.h>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <vector>
#include <set>

namespace libtorrent::aux {

// Statistics for monitoring curl thread manager
struct curl_thread_stats {
    std::size_t unique_tracker_hosts;     // Number of unique tracker hostnames
    long current_connection_limit;        // Current max connections setting
    std::size_t active_requests;          // Currently processing requests
    std::size_t queued_requests;          // Requests waiting to be processed
};

// Structure to hold response buffer and size limit for write_callback
// This must be heap-allocated (e.g., via shared_ptr) because the curl_request
// object containing it might be moved after its address is passed to libcurl.
struct response_data {
    std::vector<char> buffer;
    size_t max_size;
};

// Request wrapper for thread communication
struct curl_request {
    std::string url;
    std::shared_ptr<response_data> response;
    std::function<void(error_code, std::vector<char>)> completion_handler;
    time_point deadline;
    int retry_count = 0;
    int max_retries = 3;
    milliseconds retry_delay{1000};  // Initial retry delay (exponential backoff)
};

struct curl_transfer_data;

// RAII context for request lifetime management
struct curl_request_context {
    std::shared_ptr<curl_transfer_data> transfer_data;
    curl_request request;
};

// Efficient incremental tracker host counting for dynamic connection pool scaling
class tracker_host_counter {
private:
    std::unordered_map<std::string, int> m_tracker_ref_counts;
    mutable std::mutex m_mutex;
    
public:
    // Called when a tracker is added to any torrent
    void add_tracker(const std::string& url);
    
    // Called when a tracker is removed from any torrent
    void remove_tracker(const std::string& url);
    
    // Get current unique host count
    [[nodiscard]] size_t unique_count() const {
        std::scoped_lock lock(m_mutex);
        return m_tracker_ref_counts.size();
    }
    
    // Clear all counts (for shutdown)
    void clear() {
        std::scoped_lock lock(m_mutex);
        m_tracker_ref_counts.clear();
    }
};

class TORRENT_EXPORT curl_thread_manager : public std::enable_shared_from_this<curl_thread_manager> {
public:
    static std::shared_ptr<curl_thread_manager> create(
        io_context& ios, session_settings const& settings);
    
    ~curl_thread_manager();
    
    void add_request(
        std::string const& url,
        std::function<void(error_code, std::vector<char>)> handler,
        time_duration timeout = seconds(30));
    
    void shutdown();
    
    void tracker_added(const std::string& url);
    void tracker_removed(const std::string& url);
    
    [[nodiscard]] curl_thread_stats get_stats() const;
    
private:
    curl_thread_manager(io_context& ios, session_settings const& settings);
    
    void curl_thread_func();
    
    bool configure_handle(CURL* easy, curl_request const& req, curl_transfer_data* transfer_data);
    
    // Process completed transfers. Returns the number of completions processed.
    int process_completions(CURLM* multi);
    
    void schedule_retry(curl_request req);
    
    [[nodiscard]] long calculate_wait_timeout(CURLM* multi) const;
    
    [[nodiscard]] long calculate_optimal_connections() const;
    
    std::vector<curl_request> swap_pending_requests();
    
    // Wakeup the curl thread (requires libcurl 7.68.0+)
    void wakeup_curl_thread();
    
    void process_queue_notification();
    void on_timer(boost::system::error_code const& ec);
    void perform_wakeup();
    
private:
    // Memory pool for response buffers with fine-grained locking
    class response_buffer_pool {
    public:
        inline static constexpr size_t SMALL_BUFFER_SIZE = 2048;     // 2KB - covers 90% of tracker responses
        inline static constexpr size_t MEDIUM_BUFFER_SIZE = 8192;    // 8KB - covers 8% of tracker responses
        inline static constexpr size_t LARGE_BUFFER_SIZE = 65536;    // 64KB - covers 2% of tracker responses
        
        std::shared_ptr<response_data> acquire(size_t expected_size) {
            if (expected_size <= SMALL_BUFFER_SIZE) {
                std::lock_guard<std::mutex> lock(m_small_mutex);
                return acquire_from_pool(m_small_pool, SMALL_BUFFER_SIZE, expected_size);
            } else if (expected_size <= MEDIUM_BUFFER_SIZE) {
                std::lock_guard<std::mutex> lock(m_medium_mutex);
                return acquire_from_pool(m_medium_pool, MEDIUM_BUFFER_SIZE, expected_size);
            } else {
                std::lock_guard<std::mutex> lock(m_large_mutex);
                return acquire_from_pool(m_large_pool, LARGE_BUFFER_SIZE, expected_size);
            }
        }
        
        void release(std::shared_ptr<response_data> buffer) {
            if (!buffer) return;
            
            size_t capacity = buffer->buffer.capacity();
            
            if (capacity <= SMALL_BUFFER_SIZE) {
                std::lock_guard<std::mutex> lock(m_small_mutex);
                if (m_small_pool.size() < MAX_SMALL_POOL_SIZE) {
                    buffer->buffer.clear();
                    m_small_pool.push_back(std::move(buffer));
                }
            } else if (capacity <= MEDIUM_BUFFER_SIZE) {
                std::lock_guard<std::mutex> lock(m_medium_mutex);
                if (m_medium_pool.size() < MAX_MEDIUM_POOL_SIZE) {
                    buffer->buffer.clear();
                    m_medium_pool.push_back(std::move(buffer));
                }
            } else if (capacity <= LARGE_BUFFER_SIZE) {
                std::lock_guard<std::mutex> lock(m_large_mutex);
                if (m_large_pool.size() < MAX_LARGE_POOL_SIZE) {
                    buffer->buffer.clear();
                    m_large_pool.push_back(std::move(buffer));
                }
            }
            // Buffers larger than LARGE_BUFFER_SIZE are not pooled
        }
        
    private:
        // Pool sizes optimized for HTTP/2 (HTTP/1.1 will naturally use less)
        // With 1000 concurrent streams: 900 small + 80 medium + 20 large
        inline static constexpr size_t MAX_SMALL_POOL_SIZE = 900;   // 1.8MB when full
        inline static constexpr size_t MAX_MEDIUM_POOL_SIZE = 80;   // 640KB when full  
        inline static constexpr size_t MAX_LARGE_POOL_SIZE = 20;    // 1.3MB when full
        
        std::shared_ptr<response_data> acquire_from_pool(
            std::vector<std::shared_ptr<response_data>>& pool,
            size_t reserve_size,
            size_t max_size) 
        {
            if (!pool.empty()) {
                auto buffer = std::move(pool.back());
                pool.pop_back();
                buffer->buffer.clear();
                buffer->max_size = max_size;
                return buffer;
            }
            
            auto buffer = std::make_shared<response_data>();
            buffer->buffer.reserve(reserve_size);
            buffer->max_size = max_size;
            return buffer;
        }
        
        // Fine-grained mutexes to reduce contention
        std::mutex m_small_mutex;
        std::mutex m_medium_mutex;
        std::mutex m_large_mutex;
        
        std::vector<std::shared_ptr<response_data>> m_small_pool;
        std::vector<std::shared_ptr<response_data>> m_medium_pool;
        std::vector<std::shared_ptr<response_data>> m_large_pool;
    };

private:
    io_context& m_ios;
    session_settings const& m_settings;
    
    // Store CA certificate path to prevent string lifetime issues
    std::string m_ca_cert_path;
    
    std::thread m_curl_thread;
    
    enum class InitStatus {
        Pending,
        Success,
        Failed
    };
    std::mutex m_init_mutex;
    std::condition_variable m_init_cv;
    InitStatus m_init_status = InitStatus::Pending;
    
    mutable std::mutex m_queue_mutex;
    std::queue<curl_request> m_request_queue;
    
    // Multi handle (thread-safe for curl_multi_wakeup)
    std::atomic<CURLM*> m_multi_handle{nullptr};
    
    std::atomic<bool> m_shutting_down{false};
    
    // Timer-based wakeup batching mechanism
    static constexpr auto WAKEUP_DELAY = std::chrono::milliseconds(5);
    deadline_timer m_wakeup_timer;              // Accessed ONLY on IO thread
    bool m_timer_running = false;               // Accessed ONLY on IO thread  
    std::atomic<bool> m_notification_pending{false}; // Cross-thread notification
    std::atomic<uint64_t> m_notification_version{0}; // Version counter for debugging
    
    // Active requests tracking (only accessed from curl thread)
    std::unordered_map<CURL*, std::shared_ptr<curl_request_context>> m_active_requests;
    
    // Retry queue (only accessed from curl thread)
    // Using multiset for safe element extraction without const_cast
    struct retry_item {
        time_point scheduled_time;
        curl_request request;
        
        // Comparison operator for multiset ordering (earliest time first)
        bool operator<(const retry_item& other) const {
            return scheduled_time < other.scheduled_time;
        }
    };
    std::multiset<retry_item> m_retry_queue;
    
    std::atomic<int> m_total_requests{0};
    std::atomic<int> m_completed_requests{0};
    std::atomic<int> m_failed_requests{0};
    std::atomic<int> m_retried_requests{0};
    
    response_buffer_pool m_buffer_pool;
    
    tracker_host_counter m_tracker_counter;
    
    mutable std::atomic<long> m_current_connection_limit{10};
    
    // Flag to indicate pool needs immediate update (removes 30s delay)
    std::atomic<bool> m_pool_needs_update{false};
    
    // New connection limit to apply (calculated in tracker_added/removed)
    std::atomic<long> m_new_connection_limit{10};
};

} // namespace libtorrent::aux

#endif // TORRENT_USE_LIBCURL

#endif // TORRENT_CURL_THREAD_MANAGER_HPP