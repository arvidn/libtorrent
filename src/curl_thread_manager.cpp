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

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/curl_handle_wrappers.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include <boost/asio/post.hpp>
#include <curl/curl.h>

// Suppress recursive macro expansion warning from curl's own headers
// curl defines macros like: #define curl_easy_setopt(h,o,p) curl_easy_setopt(h,o,p)
// This is their design choice for type checking, not a code quality issue
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif

#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/resource.h>
#endif

namespace libtorrent::aux {

// Structure to hold request data with proper lifetime management
// This is allocated with new and stored via CURLOPT_PRIVATE to ensure
// the response buffer stays alive throughout the transfer
struct curl_transfer_data {
    curl_request request;
    std::shared_ptr<response_data> response;
    // Use pooled handle for reuse optimization
    std::unique_ptr<curl_handle_pool::pooled_handle> pooled_handle;

    // SAFETY: Store strings that libcurl may reference after curl_easy_setopt
    // These must outlive the curl handle's lifetime
    std::string url_storage;
    std::string user_agent_storage;
    std::string proxy_hostname_storage;
    std::string proxy_username_storage;
    std::string proxy_password_storage;
    std::string interface_storage;

    // Legacy constructor for non-pooled handles (backward compatibility)
    explicit curl_transfer_data(curl_request&& req)
        : request(std::move(req))
        , response(request.response)
        , pooled_handle(std::make_unique<curl_handle_pool::pooled_handle>())
        , url_storage(request.url)
    {}

    // Constructor accepting pooled handle
    curl_transfer_data(curl_request&& req,
                      std::unique_ptr<curl_handle_pool::pooled_handle> handle)
        : request(std::move(req))
        , response(request.response)
        , pooled_handle(std::move(handle))
        , url_storage(request.url)
    {}

    // Accessor for easy handle
    CURL* easy_get() const {
        return pooled_handle ? pooled_handle->handle.get() : nullptr;
    }

    curl_easy_handle& easy() {
        return pooled_handle->handle;
    }

    ~curl_transfer_data() {
        // SECURITY: Clear sensitive data from memory using secure_clear pattern
        // This prevents the data from being recovered from freed memory
        if (!proxy_password_storage.empty()) {
            std::fill(proxy_password_storage.begin(), proxy_password_storage.end(), '\0');
            proxy_password_storage.clear();
        }
        if (!proxy_username_storage.empty()) {
            std::fill(proxy_username_storage.begin(), proxy_username_storage.end(), '\0');
            proxy_username_storage.clear();
        }
    }

    // Disable copy to prevent accidental credential duplication
    curl_transfer_data(const curl_transfer_data&) = delete;
    curl_transfer_data& operator=(const curl_transfer_data&) = delete;

    // Move is OK as it transfers ownership
    curl_transfer_data(curl_transfer_data&&) = default;
    curl_transfer_data& operator=(curl_transfer_data&&) = default;
};

void tracker_host_counter::add_tracker(const std::string& url) {
    error_code ec;
    auto [scheme, auth, host, port, path] = parse_url_components(url, ec);
    if (ec || host.empty()) return;

    std::scoped_lock<std::mutex> lock(m_mutex);
    m_tracker_ref_counts[host]++;
}

void tracker_host_counter::remove_tracker(const std::string& url) {
    error_code ec;
    auto [scheme, auth, host, port, path] = parse_url_components(url, ec);
    if (ec || host.empty()) return;

    std::scoped_lock<std::mutex> lock(m_mutex);
    if (auto it = m_tracker_ref_counts.find(host); it != m_tracker_ref_counts.end()) {
        if (--it->second == 0) {
            m_tracker_ref_counts.erase(it);
        }
    }
}

void curl_thread_manager::tracker_added(const std::string& url) {
    m_tracker_counter.add_tracker(url);
    long new_limit = calculate_optimal_connections();
    m_new_connection_limit.store(new_limit);
    m_pool_needs_update.store(true);
    wakeup_curl_thread();
}

void curl_thread_manager::tracker_removed(const std::string& url) {
    m_tracker_counter.remove_tracker(url);
    long new_limit = calculate_optimal_connections();
    m_new_connection_limit.store(new_limit);
    m_pool_needs_update.store(true);
    wakeup_curl_thread();
}

namespace {

    size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept {
        try {
            auto* data = static_cast<response_data*>(userdata);
            if (!data) return 0;

            std::vector<char>* buffer = &data->buffer;
            size_t total = size * nmemb;

            // Check against dynamic size limit
            if (buffer->size() + total > data->max_size) {
                return 0;
            }

            // SAFETY: Pre-reserve space to minimize allocation failure risk
            size_t new_size = buffer->size() + total;

            // Explicit try-catch for reserve() to handle std::bad_alloc
            try {
                if (buffer->capacity() < new_size) {
                    buffer->reserve(new_size);
                }
            } catch (const std::bad_alloc&) {
                return 0;
            }

            buffer->insert(buffer->end(), ptr, ptr + total);
            return total;

        } catch (const std::bad_alloc&) {
            return 0;
        } catch (...) {
            // CRITICAL: Catch ALL exceptions to maintain noexcept guarantee
            // Without this, std::terminate would be called
            return 0;
        }
    }

    error_code curl_error_to_libtorrent(CURLcode code) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif
        switch(code) {
            case CURLE_OK:
                return {};
            case CURLE_OPERATION_TIMEDOUT:
                return errors::timed_out;
            case CURLE_COULDNT_CONNECT:
                return errors::http_error;
            case CURLE_COULDNT_RESOLVE_HOST:
                return errors::invalid_hostname;
            case CURLE_COULDNT_RESOLVE_PROXY:
                return errors::invalid_hostname;
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
                return errors::invalid_ssl_cert;
            case CURLE_OUT_OF_MEMORY:
                return errors::no_memory;
            case CURLE_WRITE_ERROR:
                return errors::http_error;
            case CURLE_FILESIZE_EXCEEDED:
                return errors::http_error;
            case CURLE_UNSUPPORTED_PROTOCOL:
                return errors::unsupported_url_protocol;
            default:
                return errors::http_error;
        }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    }

    bool is_retryable_error(error_code const& ec) {
        return ec == errors::timed_out ||
               ec == errors::http_error ||
               ec == errors::invalid_hostname;
    }

    // Helper function to configure proxy settings
    void configure_proxy(CURL* easy, session_settings const& settings, curl_transfer_data* transfer_data) {
        // Check if proxy should be used for tracker connections
        if (!settings.get_bool(settings_pack::proxy_tracker_connections)) {
            return;
        }

        int proxy_type = settings.get_int(settings_pack::proxy_type);
        std::string proxy_host = settings.get_str(settings_pack::proxy_hostname);
        int proxy_port = settings.get_int(settings_pack::proxy_port);

        if (proxy_type == settings_pack::none || proxy_host.empty() || proxy_port <= 0) {
            return;
        }

        // Configure proxy host and port
        // SAFETY: Store proxy hostname for lifetime safety
        transfer_data->proxy_hostname_storage = proxy_host;
        curl_easy_setopt(easy, CURLOPT_PROXY, transfer_data->proxy_hostname_storage.c_str());
        curl_easy_setopt(easy, CURLOPT_PROXYPORT, static_cast<long>(proxy_port));

        // Configure proxy type
        long curl_proxy_type = CURLPROXY_HTTP;
        bool requires_auth = false;

        switch (proxy_type) {
            case settings_pack::socks4:
                // Use SOCKS4A for remote hostname resolution
                curl_proxy_type = CURLPROXY_SOCKS4A;
                break;
            case settings_pack::socks5:
                // Use SOCKS5_HOSTNAME for remote hostname resolution
                curl_proxy_type = CURLPROXY_SOCKS5_HOSTNAME;
                break;
            case settings_pack::socks5_pw:
                curl_proxy_type = CURLPROXY_SOCKS5_HOSTNAME;
                requires_auth = true;
                break;
            case settings_pack::http:
                curl_proxy_type = CURLPROXY_HTTP;
                break;
            case settings_pack::http_pw:
                curl_proxy_type = CURLPROXY_HTTP;
                requires_auth = true;
                break;
            default:
                return; // Unknown/unsupported proxy type
        }

        curl_easy_setopt(easy, CURLOPT_PROXYTYPE, curl_proxy_type);

        // Configure proxy authentication if required
        if (requires_auth) {
            std::string username = settings.get_str(settings_pack::proxy_username);
            std::string password = settings.get_str(settings_pack::proxy_password);

            if (!username.empty()) {
                // SECURITY FIX: Use separate username/password options instead of concatenation
                // SAFETY: Store credentials for lifetime safety
                transfer_data->proxy_username_storage = username;
                transfer_data->proxy_password_storage = password;

                curl_easy_setopt(easy, CURLOPT_PROXYUSERNAME, transfer_data->proxy_username_storage.c_str());
                curl_easy_setopt(easy, CURLOPT_PROXYPASSWORD, transfer_data->proxy_password_storage.c_str());

                // Clear sensitive data from local variables immediately
                username.assign(username.size(), '\0');
                password.assign(password.size(), '\0');

                // Use safe authentication methods for HTTP proxy
                if (proxy_type == settings_pack::http_pw) {
                    curl_easy_setopt(easy, CURLOPT_PROXYAUTH, CURLAUTH_ANYSAFE);
                }
            }
        }

        // Security: Only force proxy for internal addresses if explicitly configured
        // This prevents exposing internal traffic to external proxies (SSRF protection)
        if (settings.get_bool(settings_pack::proxy_force_internal_addresses)) {
            // User explicitly wants to proxy internal addresses (e.g., for testing)
            curl_easy_setopt(easy, CURLOPT_NOPROXY, "");
#ifndef TORRENT_DISABLE_LOGGING
            static std::once_flag force_proxy_flag;
            std::call_once(force_proxy_flag, [] {
                std::fprintf(stderr, "WARNING: Forcing proxy for all addresses including localhost (proxy_force_internal_addresses=true)\n");
            });
#endif
        }
    }

    void configure_ssl(CURL* easy, session_settings const& settings, std::string const& ca_cert_path) {

        bool verify_peer = settings.get_bool(settings_pack::tracker_ssl_verify_peer);
        bool verify_host = settings.get_bool(settings_pack::tracker_ssl_verify_host);

        if (verify_peer) {
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        } else {
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);

            // WARNING: SSL certificate verification disabled
#ifndef TORRENT_DISABLE_LOGGING
            static std::once_flag peer_warning_flag;
            std::call_once(peer_warning_flag, [] {
                std::fprintf(stderr, "WARNING: SSL certificate verification disabled for tracker connections\n");
            });
#endif

            // In production builds, log a more severe warning
            #ifdef TORRENT_PRODUCTION
            static std::once_flag prod_peer_warning_flag;
#ifndef TORRENT_DISABLE_LOGGING
            std::call_once(prod_peer_warning_flag, [] {
                std::fprintf(stderr, "SECURITY WARNING: SSL peer verification disabled in production build!\n");
            });
#endif
            #endif
        }

        if (verify_host) {
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
        } else {
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);

            // WARNING: SSL hostname verification disabled
            #ifndef TORRENT_DISABLE_LOGGING
            static std::once_flag host_warning_flag;
#ifndef TORRENT_DISABLE_LOGGING
            std::call_once(host_warning_flag, [] {
                std::fprintf(stderr, "WARNING: SSL hostname verification disabled for tracker connections\n");
            });
#endif
            #endif
        }

        // SECURITY FIX: Enforce minimum TLS version
        int min_tls_version = settings.get_int(settings_pack::tracker_min_tls_version);
        long curl_tls_version = CURL_SSLVERSION_TLSv1_2; // Default to TLS 1.2

        // Map libtorrent TLS version to libcurl constants
        // 0x0301 = TLS 1.0, 0x0302 = TLS 1.1, 0x0303 = TLS 1.2, 0x0304 = TLS 1.3
        switch (min_tls_version) {
            case 0x0302: // TLS 1.1 - DEPRECATED (RFC 8996)
                // SECURITY: TLS 1.1 is cryptographically broken, upgrade to 1.2
#ifndef TORRENT_DISABLE_LOGGING
                static std::once_flag tls11_warning_flag;
#ifndef TORRENT_DISABLE_LOGGING
                std::call_once(tls11_warning_flag, [] {
                    std::fprintf(stderr, "WARNING: TLS 1.1 requested but upgrading to 1.2 (TLS 1.1 is deprecated)\n");
                });
#endif
#endif
                // Fall through to TLS 1.2
                [[fallthrough]];
            case 0x0303: // TLS 1.2
                curl_tls_version = CURL_SSLVERSION_TLSv1_2;
                break;
            case 0x0304: // TLS 1.3
#ifdef CURL_SSLVERSION_TLSv1_3
                curl_tls_version = CURL_SSLVERSION_TLSv1_3;
#else
                curl_tls_version = CURL_SSLVERSION_TLSv1_2; // Fallback
#endif
                break;
            default:
                // Default to TLS 1.2 minimum for security
                curl_tls_version = CURL_SSLVERSION_TLSv1_2;
        }

        curl_easy_setopt(easy, CURLOPT_SSLVERSION, curl_tls_version);

        // SECURITY FIX: Configure strong cipher suites
        // Only allow modern, secure ciphers
        curl_easy_setopt(easy, CURLOPT_SSL_CIPHER_LIST,
            "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:"
            "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:"
            "!aNULL:!eNULL:!EXPORT:!DES:!MD5:!PSK:!RC4:!3DES:!DSS");

        // Set custom CA certificate bundle if provided
        // Use stored path to avoid string lifetime issues
        if (!ca_cert_path.empty()) {
            curl_easy_setopt(easy, CURLOPT_CAINFO, ca_cert_path.c_str());

            #ifndef TORRENT_DISABLE_LOGGING
            static std::once_flag ca_cert_flag;
#ifndef TORRENT_DISABLE_LOGGING
            std::call_once(ca_cert_flag, [&ca_cert_path] {
                std::fprintf(stderr, "Using custom CA certificate bundle: %s\n", ca_cert_path.c_str());
            });
#endif
            #endif
        }
    }
}

std::shared_ptr<curl_thread_manager> curl_thread_manager::create(
    io_context& ios, session_settings const& settings)
{
    auto manager = std::shared_ptr<curl_thread_manager>(
        new curl_thread_manager(ios, settings));

    // FIX Issue 1: Break shared_ptr reference cycle.
    // The original code captured a shared_ptr in the thread lambda, causing a leak.
    // We use the raw pointer instead. This is safe because the destructor joins the thread.
    manager->m_curl_thread = std::thread(
        &curl_thread_manager::curl_thread_func, manager.get());

    // Wait for thread to be fully initialized with a timeout
    // This prevents deadlock during session initialization when io_context isn't running yet
    {
        std::unique_lock<std::mutex> lock(manager->m_init_mutex);
        // Wait up to 100ms for initialization
        // If io_context isn't running yet, initialization will complete later
        manager->m_init_cv.wait_for(lock, std::chrono::milliseconds(100), [&manager]{
            return manager->m_init_status != InitStatus::Pending;
        });

        // Check if initialization failed (only if status changed from Pending)
        if (manager->m_init_status == InitStatus::Failed) {
            // Join the failed thread before throwing
            if (manager->m_curl_thread.joinable()) {
                manager->m_curl_thread.join();
            }
            throw std::runtime_error("Failed to initialize curl multi handle");
        }
        // If still pending, that's OK - initialization will complete asynchronously
    }

    return manager;
}

curl_thread_manager::curl_thread_manager(io_context& ios, session_settings const& settings)
    : m_ios(ios)
    , m_settings(settings)
    , m_ca_cert_path(settings.get_str(settings_pack::tracker_ca_certificate))
    , m_wakeup_timer(ios)
{
    // Ensure curl is initialized globally (thread-safe with std::once_flag)
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, []() {
        curl_global_init(CURL_GLOBAL_ALL);
    });

    // Verify libcurl version at runtime
    curl_version_info_data* ver = curl_version_info(CURLVERSION_NOW);
    if (!ver || ver->version_num < 0x074200) { // 7.66.0
        throw std::runtime_error("libcurl 7.66.0+ required for curl_multi_poll, found: "
            + std::string(ver ? ver->version : "unknown"));
    }

    // Verify async DNS support
    if (!(ver->features & CURL_VERSION_ASYNCHDNS)) {
        throw std::runtime_error("libcurl must be built with async DNS support (c-ares or threaded resolver)");
    }
}

curl_thread_manager::~curl_thread_manager() {
    shutdown();
}

void curl_thread_manager::shutdown() {
    // Signal shutdown
    m_shutting_down = true;

    // Cancel timer synchronously (safe because we're about to join the thread)
    // The timer will be destroyed automatically when this object is destroyed
    m_wakeup_timer.cancel(); // cancel() returns number of cancelled operations
    m_timer_running = false;

    // Wake up the thread if it's waiting (direct wakeup for immediate shutdown)
    perform_wakeup();

    // Wait for thread to finish
    if (m_curl_thread.joinable()) {
        m_curl_thread.join();
    }

    // Process any remaining queued requests with error
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        while (!m_request_queue.empty()) {
            auto req = std::move(m_request_queue.front());
            m_request_queue.pop();

            // Post error to completion handler
            boost::asio::post(m_ios, [handler = req.completion_handler]() {
                handler(errors::session_is_closing, std::vector<char>{});
            });
        }
    }

    // Process any remaining retry requests
    // Safe to access without lock as worker thread has joined
    for (auto& item : m_retry_queue) {
        // Post error to completion handler for each pending retry
        boost::asio::post(m_ios, [handler = item.request.completion_handler]() {
            handler(errors::session_is_closing, std::vector<char>{});
        });
    }
    m_retry_queue.clear();
}

void curl_thread_manager::wakeup_curl_thread() {
    // Simple delegation to perform_wakeup for backward compatibility
    // The actual batching is now handled via process_queue_notification
    perform_wakeup();
}

void curl_thread_manager::perform_wakeup() {
    // FIX: Allow wakeup during shutdown to prevent deadlock
    // The curl thread needs to wake up from curl_multi_poll() to check
    // the shutdown flag and exit cleanly

    CURLM* multi = m_multi_handle.load();
    if (multi) {
        CURLMcode rc = curl_multi_wakeup(multi);
        // Ignore errors during shutdown (CURLM_BAD_HANDLE is expected)
        if (rc != CURLM_OK && rc != CURLM_BAD_HANDLE) {
            // Log error but don't throw - wakeup is best-effort
        }
    }
}

void curl_thread_manager::process_queue_notification() {
    // This runs ONLY on the IO thread

    // CRITICAL: Do NOT clear m_notification_pending here
    // It must remain true until the timer fires to avoid lost notifications

    if (m_shutting_down.load(std::memory_order_acquire)) {
        return;
    }

    if (m_timer_running) {
        // Timer is already running, will check m_notification_pending when it fires
        return;
    }

    // Start the timer for batching
    m_timer_running = true;
    m_wakeup_timer.expires_after(WAKEUP_DELAY);

    // Use async_wait with proper lifetime management
    m_wakeup_timer.async_wait([self = shared_from_this()](boost::system::error_code const& ec) {
        self->on_timer(ec);
    });
}

void curl_thread_manager::on_timer(boost::system::error_code const& ec) {
    // This runs ONLY on the IO thread

    // Timer has completed
    m_timer_running = false;

    if (ec || m_shutting_down.load()) {
        // Timer was cancelled or we're shutting down
        return;
    }

    // The batch window has closed, wake up the curl thread
    perform_wakeup();

    // CRITICAL FIX: Atomically check AND clear the flag
    // This ensures we don't lose notifications that arrived during the batch window
    // Use acquire-release semantics for proper cross-thread synchronization
    if (m_notification_pending.exchange(false, std::memory_order_acq_rel)) {
        // Requests arrived during the batch window, start next batch
        // Restart the timer for the next batch
        m_timer_running = true;
        m_wakeup_timer.expires_after(WAKEUP_DELAY);
        m_wakeup_timer.async_wait([self = shared_from_this()](boost::system::error_code const& timer_ec) {
            self->on_timer(timer_ec);
        });
    }
}

void curl_thread_manager::add_request(
    std::string const& url,
    std::function<void(error_code, std::vector<char>)> handler,
    time_duration timeout)
{
    if (m_shutting_down) {
        // Call handler with error immediately
        boost::asio::post(m_ios, [handler]() {
            handler(errors::session_is_closing, std::vector<char>{});
        });
        return;
    }

    curl_request req;
    req.url = url;
    req.completion_handler = handler;
    req.deadline = clock_type::now() + timeout;

    // Set dynamic size limit from settings
    // Default to 128KB if not set (consistent with typical tracker response size)
    int max_size = m_settings.get_int(settings_pack::max_tracker_response_size);
    if (max_size <= 0) {
        max_size = 128 * 1024; // Default 128KB
    }

    // Use memory pool for response buffer allocation
    req.response = m_buffer_pool.acquire(static_cast<size_t>(max_size));
    req.response->max_size = static_cast<size_t>(max_size);

    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_request_queue.push(std::move(req));
        m_total_requests++;
    }

    // Increment version counter for debugging/monitoring
    m_notification_version.fetch_add(1, std::memory_order_relaxed);

    // Notify I/O thread for batched wakeup with proper lifetime management
    // Try to set notification flag from false to true
    bool expected = false;
    if (m_notification_pending.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
        // Post notification with shared_from_this for safety
        boost::asio::post(m_ios, [self = shared_from_this()]() {
            self->process_queue_notification();
        });
    }
}

std::vector<curl_request> curl_thread_manager::swap_pending_requests() {
    std::vector<curl_request> local_queue;

    // Minimize lock duration - just swap queues
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        while (!m_request_queue.empty()) {
            local_queue.push_back(std::move(m_request_queue.front()));
            m_request_queue.pop();
        }
    }

    return local_queue;
}

// Centralized configuration for CURL handles
bool curl_thread_manager::configure_handle(CURL* easy, curl_request const& req, curl_transfer_data* transfer_data) {

    // Calculate timeout based on deadline
    auto now = clock_type::now();
    if (now >= req.deadline) return false; // Already timed out

    auto timeout_ms = std::chrono::duration_cast<milliseconds>(req.deadline - now).count();
    long timeout_sec = std::max<long>(1L, static_cast<long>(timeout_ms / 1000));

    // Basic configuration
    // SAFETY: Use stored URL from transfer_data to ensure lifetime safety
    curl_easy_setopt(easy, CURLOPT_URL, transfer_data->url_storage.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    // NOTE: CURLOPT_WRITEDATA is now set by the caller with transfer_data->response.get()
    // This ensures proper lifetime management of the response buffer

    // Set user-agent for tracker requests
    std::string user_agent = m_settings.get_str(settings_pack::user_agent);
    if (!user_agent.empty()) {
        // SAFETY: Store user agent for lifetime safety
        transfer_data->user_agent_storage = std::move(user_agent);
        curl_easy_setopt(easy, CURLOPT_USERAGENT, transfer_data->user_agent_storage.c_str());
    }

    // Network interface binding (if configured)
    std::string outgoing_interface = m_settings.get_str(settings_pack::outgoing_interfaces);
    if (!outgoing_interface.empty()) {
        // Parse comma-separated list and select one
        // (simplified - actual implementation should rotate through interfaces)
        size_t comma_pos = outgoing_interface.find(',');
        if (comma_pos != std::string::npos) {
            outgoing_interface = outgoing_interface.substr(0, comma_pos);
        }
        // SAFETY: Store interface for lifetime safety
        transfer_data->interface_storage = std::move(outgoing_interface);
        curl_easy_setopt(easy, CURLOPT_INTERFACE, transfer_data->interface_storage.c_str());
    }

    curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, std::min(10L, timeout_sec));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L); // Essential for multi-threading

    // PERFORMANCE: Configure DNS caching to reduce lookup overhead
    // Default to 5 minutes (300 seconds) cache timeout
    curl_easy_setopt(easy, CURLOPT_DNS_CACHE_TIMEOUT, 300L);

    // Note: DNS-over-HTTPS (DOH) could be added in the future via CURLOPT_DOH_URL
    // if libtorrent adds a doh_url setting. For now, use system DNS.

    // SECURITY FIX: Add response size limits for headers too
    curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE,
        static_cast<curl_off_t>(req.response->max_size));

    // DoS protection: Set more aggressive timeouts
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 10L);  // 10 bytes/sec minimum
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 30L);   // For 30 seconds

    // Restrict to HTTP and HTTPS only for security
#ifdef CURLOPT_PROTOCOLS_STR
    // Use newer API if available (libcurl 7.85.0+)
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    // Fall back to older API - suppress deprecation warning as this is for older curl versions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
#endif

    // Enable connection reuse (TCP Keepalive)
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 60L);

    // HTTP/2 support if available
#ifdef CURL_HTTP_VERSION_2_0
    if (m_settings.get_bool(settings_pack::enable_http2_trackers)) {
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

        // Enable ALPN for HTTP/2 negotiation
        #ifdef CURLOPT_SSL_ENABLE_ALPN
        curl_easy_setopt(easy, CURLOPT_SSL_ENABLE_ALPN, 1L);
        #endif

        // Set HTTP/2 window size for better flow control
        #ifdef CURLOPT_HTTP2_WINDOW_SIZE
        curl_easy_setopt(easy, CURLOPT_HTTP2_WINDOW_SIZE, 10485760L); // 10MB window
        #endif

        // Disable HTTP2 Server push
        #ifdef CURLOPT_HTTP2_SERVER_PUSH
        curl_easy_setopt(easy, CURLOPT_HTTP2_SERVER_PUSH, 0L);
        #endif

    }
#endif

    // Configure Proxy settings
    configure_proxy(easy, m_settings, transfer_data);

    // Configure SSL/TLS settings
    configure_ssl(easy, m_settings, m_ca_cert_path);

    return true;
}

// Clear all request-specific state from previous request
void curl_thread_manager::clear_request_state(CURL* easy) {
    // Clear HTTP headers - MUST be done to prevent header leakage
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, nullptr);

    // Clear POST/PUT state
    curl_easy_setopt(easy, CURLOPT_POST, 0L);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, -1L);

    // Clear custom request method
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, nullptr);

    // Reset to GET (libcurl default)
    curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);

    // Clear any authentication
    curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_NONE);
    curl_easy_setopt(easy, CURLOPT_USERPWD, nullptr);

    // Clear upload-related settings
    curl_easy_setopt(easy, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(easy, CURLOPT_INFILESIZE_LARGE, -1LL);
}

// Configure session-wide settings that rarely change
void curl_thread_manager::configure_session_settings(CURL* easy, curl_share_handle const& share) {
    // Attach share handle for DNS and SSL session sharing
    curl_easy_setopt(easy, CURLOPT_SHARE, share.get());

    // Basic settings that don't change between requests
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L); // Essential for multi-threading

    // Safe redirect handling: Allow redirects with restrictions
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);  // Limit to prevent infinite loops

    // Restrict initial protocols to HTTP and HTTPS only for security
#ifdef CURLOPT_PROTOCOLS_STR
    // Use newer API if available (libcurl 7.85.0+)
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    // Fall back to older API - suppress deprecation warning as this is for older curl versions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
#endif

    // Restrict redirect protocols to HTTP and HTTPS only (prevents file://, ftp://, etc.)
#ifdef CURLOPT_REDIR_PROTOCOLS_STR
    // Use newer API if available (libcurl 7.85.0+)
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    // Fall back to older API
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
#endif

    // Prevent sending authentication credentials to redirect targets for security
    curl_easy_setopt(easy, CURLOPT_UNRESTRICTED_AUTH, 0L);

    // Enable connection reuse (TCP Keepalive)
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 60L);

    // PERFORMANCE: Configure DNS caching to reduce lookup overhead
    // Default to 5 minutes (300 seconds) cache timeout
    curl_easy_setopt(easy, CURLOPT_DNS_CACHE_TIMEOUT, 300L);

    // DoS protection: Set more aggressive timeouts
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 10L);  // 10 bytes/sec minimum
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 30L);   // For 30 seconds

    // HTTP/2 support if available
#ifdef CURL_HTTP_VERSION_2_0
    if (m_settings.get_bool(settings_pack::enable_http2_trackers)) {
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

        // Enable ALPN for HTTP/2 negotiation
        #ifdef CURLOPT_SSL_ENABLE_ALPN
        curl_easy_setopt(easy, CURLOPT_SSL_ENABLE_ALPN, 1L);
        #endif

        // Set HTTP/2 window size for better flow control
        #ifdef CURLOPT_HTTP2_WINDOW_SIZE
        curl_easy_setopt(easy, CURLOPT_HTTP2_WINDOW_SIZE, 10485760L); // 10MB window
        #endif

        // Disable HTTP2 Server push
        #ifdef CURLOPT_HTTP2_SERVER_PUSH
        curl_easy_setopt(easy, CURLOPT_HTTP2_SERVER_PUSH, 0L);
        #endif
    }
#endif

    // Configure SSL/TLS settings
    configure_ssl(easy, m_settings, m_ca_cert_path);
}

// Configure request-specific settings
void curl_thread_manager::configure_request_settings(CURL* easy,
                                                     curl_request const& req,
                                                     curl_transfer_data* transfer_data) {
    // First clear any state from previous request
    clear_request_state(easy);

    // Calculate timeout based on deadline
    auto now = clock_type::now();
    if (now >= req.deadline) return; // Already timed out

    auto timeout_ms = std::chrono::duration_cast<milliseconds>(req.deadline - now).count();
    long timeout_sec = std::max<long>(1L, static_cast<long>(timeout_ms / 1000));

    // Request-specific settings
    // SAFETY: Use stored URL from transfer_data to ensure lifetime safety
    curl_easy_setopt(easy, CURLOPT_URL, transfer_data->url_storage.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);

    curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, std::min(10L, timeout_sec));

    // SECURITY FIX: Add response size limits for headers too
    curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE,
        static_cast<curl_off_t>(req.response->max_size));

    // Set user-agent for tracker requests
    std::string user_agent = m_settings.get_str(settings_pack::user_agent);
    if (!user_agent.empty()) {
        // SAFETY: Store user agent for lifetime safety
        transfer_data->user_agent_storage = std::move(user_agent);
        curl_easy_setopt(easy, CURLOPT_USERAGENT, transfer_data->user_agent_storage.c_str());
    }

    // Network interface binding (if configured)
    std::string outgoing_interface = m_settings.get_str(settings_pack::outgoing_interfaces);
    if (!outgoing_interface.empty()) {
        // Parse comma-separated list and select one
        // (simplified - actual implementation should rotate through interfaces)
        size_t comma_pos = outgoing_interface.find(',');
        if (comma_pos != std::string::npos) {
            outgoing_interface = outgoing_interface.substr(0, comma_pos);
        }
        // SAFETY: Store interface for lifetime safety
        transfer_data->interface_storage = std::move(outgoing_interface);
        curl_easy_setopt(easy, CURLOPT_INTERFACE, transfer_data->interface_storage.c_str());
    }

    // Configure Proxy settings
    configure_proxy(easy, m_settings, transfer_data);
}

void curl_thread_manager::curl_thread_func() {
        try {
            // Set thread name for debugging (platform-specific)
            #ifdef __linux__
            pthread_setname_np(pthread_self(), "lt-curl");
            #elif defined(__APPLE__)
            pthread_setname_np("lt-curl");
            #elif defined(_WIN32)
            // Windows thread naming requires different approach
            #endif

            // CRITICAL: Share handle must be created first (destroyed last via RAII)
            curl_share_handle share;

            // Configure what to share - DNS and SSL session caching
            share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
            share.setopt(CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

            // Create handle pool for reuse
            curl_handle_pool pool;

            // Create multi handle AFTER share handle (destroyed before share via RAII)
            curl_multi_handle multi;
            // The curl_multi_handle constructor throws on failure, so no need for explicit check

        // Configure multi handle for connection pooling with dynamic scaling
        // Check if HTTP/2 is enabled to set appropriate connection limits
        bool http2_enabled = m_settings.get_bool(settings_pack::enable_http2_trackers);

        // Initial configuration with current tracker count
        long max_connections = calculate_optimal_connections();
        m_current_connection_limit.store(max_connections);

        // IMPORTANT: Always limit to 2 connections per host (HTTP/1.1 standard)
        curl_multi_setopt(multi.get(), CURLMOPT_MAX_HOST_CONNECTIONS, 2L);

        if (http2_enabled) {
            // HTTP/2: Enable multiplexing
            #ifdef CURLPIPE_MULTIPLEX
            curl_multi_setopt(multi.get(), CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
            #endif

            #ifdef CURLMOPT_MAX_CONCURRENT_STREAMS
            // Set max concurrent streams per connection
            curl_multi_setopt(multi.get(), CURLMOPT_MAX_CONCURRENT_STREAMS, 100L);
            #endif
        } else {
            // HTTP/1.1: No multiplexing
            #ifdef CURLPIPE_HTTP1
            // Enable HTTP/1.1 pipelining if available (though many servers don't support it)
            curl_multi_setopt(multi.get(), CURLMOPT_PIPELINING, CURLPIPE_HTTP1);
            #else
            curl_multi_setopt(multi.get(), CURLMOPT_PIPELINING, CURLPIPE_NOTHING);
            #endif
        }

        // Set initial total connection limit based on tracker count
        curl_multi_setopt(multi.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, max_connections);

        // Add connection pool monitoring (reduces penalty for wrong Content-Length)
        #ifdef CURLMOPT_CONTENT_LENGTH_PENALTY_SIZE
        curl_multi_setopt(multi.get(), CURLMOPT_CONTENT_LENGTH_PENALTY_SIZE, 0L);
        #endif

        // Store multi handle for wakeup mechanism
        m_multi_handle = multi.get();

        // Signal that initialization is successful
        {
            std::scoped_lock<std::mutex> lock(m_init_mutex);
            m_init_status = InitStatus::Success;
        }
        m_init_cv.notify_one();

        // Track last cleanup time for periodic maintenance
        auto last_cleanup = clock_type::now();

        // Main event loop
        while (true) {
            // ALWAYS process pending requests from queue first (before checking stop signal)
            auto pending = swap_pending_requests();
            bool new_requests_added = false;

            for (auto& req : pending) {
                // Create transfer data with RAII shared_ptr for automatic memory management
                auto context = std::make_shared<curl_request_context>();
                context->request = std::move(req);

                try {
                    // Acquire handle from pool for reuse
                    auto pooled_handle = pool.acquire();
                    context->transfer_data = std::make_shared<curl_transfer_data>(
                        std::move(context->request), std::move(pooled_handle));
                    context->request = std::move(context->transfer_data->request);  // Move back after transfer_data init
                } catch (const std::exception&) {
                    // Failed to create CURL handle
                    boost::asio::post(m_ios, [handler = context->request.completion_handler]() {
                        handler(errors::no_memory, std::vector<char>{});
                    });
                    continue;
                }

                CURL* easy = context->transfer_data->easy_get();

                // Check if handle needs full configuration
                bool needs_full_config = context->transfer_data->pooled_handle->needs_full_config;

                if (needs_full_config) {
                    // Apply session-wide settings (expensive, only when needed)
                    configure_session_settings(easy, share);
                    context->transfer_data->pooled_handle->needs_full_config = false;
                }

                // Always apply request-specific settings
                configure_request_settings(easy, context->request, context->transfer_data.get());

                // Pass raw buffer pointer (safe - shared_ptr keeps it alive)
                curl_easy_setopt(easy, CURLOPT_WRITEDATA, context->transfer_data->response.get());

                // Store raw pointer for CURLOPT_PRIVATE (we keep shared ownership via m_active_requests)
                curl_easy_setopt(easy, CURLOPT_PRIVATE, context.get());

                CURLMcode add_result = curl_multi_add_handle(multi.get(), easy);
                if (add_result != CURLM_OK) {
                    // No manual delete needed - shared_ptr handles cleanup
                    boost::asio::post(m_ios, [handler = context->request.completion_handler]() {
                        handler(errors::no_memory, std::vector<char>{});
                    });
                } else {
                    m_active_requests[easy] = context;  // Store shared_ptr for automatic cleanup
                    new_requests_added = true;  // Mark that we added a new request
                }
            }

            // Process retry queue
            auto now = clock_type::now();

            // Track if we add any retries back to curl_multi
            bool retries_added = false;

            // Use iterator-based approach with multiset for safe extraction
            auto it = m_retry_queue.begin();

            // Only print debug if we have retries that are ready to process
            if (it != m_retry_queue.end() && it->scheduled_time <= now) {
// Removed verbose debug logging
            }

            while (it != m_retry_queue.end() && it->scheduled_time <= now) {
                // Extract with structured binding
                auto [scheduled_time, request] = *it;
                // Erase before processing to maintain queue consistency
                it = m_retry_queue.erase(it);
                curl_request req = std::move(request);

// Removed verbose debug logging

                // Check if still within deadline
                if (now >= req.deadline) {
                    // Timeout - don't retry
                    boost::asio::post(m_ios, [handler = req.completion_handler]() {
                        handler(errors::timed_out, std::vector<char>{});
                    });
                    continue;
                }

                // Create transfer data with RAII shared_ptr for automatic memory management
                auto context = std::make_shared<curl_request_context>();
                context->request = std::move(req);

                try {
                    // Acquire handle from pool for retry request
                    auto pooled_handle = pool.acquire();
                    context->transfer_data = std::make_shared<curl_transfer_data>(
                        std::move(context->request), std::move(pooled_handle));
                    context->request = std::move(context->transfer_data->request);  // Move back after transfer_data init
                } catch (const std::exception&) {
                    // Failed to create CURL handle
                    boost::asio::post(m_ios, [handler = context->request.completion_handler]() {
                        handler(errors::no_memory, std::vector<char>{});
                    });
                    continue;
                }

                CURL* easy = context->transfer_data->easy_get();

                // Check if handle needs full configuration
                bool needs_full_config = context->transfer_data->pooled_handle->needs_full_config;

                if (needs_full_config) {
                    // Apply session-wide settings (expensive, only when needed)
                    configure_session_settings(easy, share);
                    context->transfer_data->pooled_handle->needs_full_config = false;
                }

                // Always apply request-specific settings
                configure_request_settings(easy, context->request, context->transfer_data.get());

                // Pass raw buffer pointer (safe - shared_ptr keeps it alive)
                curl_easy_setopt(easy, CURLOPT_WRITEDATA, context->transfer_data->response.get());

                // Store raw pointer for CURLOPT_PRIVATE (we keep shared ownership via m_active_requests)
                curl_easy_setopt(easy, CURLOPT_PRIVATE, context.get());

                // Handle potential failure of curl_multi_add_handle
                CURLMcode add_result = curl_multi_add_handle(multi.get(), easy);
                if (add_result != CURLM_OK) {
                    // No manual delete needed - shared_ptr handles cleanup
                    boost::asio::post(m_ios, [handler = context->request.completion_handler]() {
                        handler(errors::no_memory, std::vector<char>{}); // Assume memory issue
                    });
                } else {
                    m_active_requests[easy] = context;  // Store shared_ptr for automatic cleanup
                    m_retried_requests++;
                    retries_added = true;  // Mark that we added a retry
                }
            }

            // --- SECTION 2: Perform Transfers (The core fix for connection pooling) ---
            // Drive the state machine. Repeat if completions occurred to start queued requests immediately.
            // Also perform immediately if we just added new requests or retries.

            int running = 0;
            bool call_again = new_requests_added || retries_added;  // Force perform if we added any handles

            do {

                CURLMcode mc = curl_multi_perform(multi.get(), &running);

                // Check for completed transfers immediately after perform
                int completed = process_completions(multi.get(), pool);

                // Process completions silently

                if (completed > 0) {
                    // If completions happened, slots are free. Force immediate re-perform.
                    call_again = true;
                } else {
                    call_again = false;
                }

                // Check for errors. CURLM_OK is the success code.
                if (mc != CURLM_OK) {
                    // We only tolerate the deprecated CURLM_CALL_MULTI_PERFORM if defined
                    #ifdef CURLM_CALL_MULTI_PERFORM
                    if (mc == CURLM_CALL_MULTI_PERFORM) {
                        // This means curl wants us to call it again immediately.
                    } else
                    #endif
                    {
                        // Handle actual error
                        break; // Break the do-while loop
                    }
                }

            // Repeat if explicitly requested by curl (deprecated) or if we processed completions.
            #ifdef CURLM_CALL_MULTI_PERFORM
            } while (mc == CURLM_CALL_MULTI_PERFORM || call_again);
            #else
            } while (call_again);
            #endif

            // --- SECTION 3: Wait for Activity ---

            // Check for shutdown - cancel active transfers if shutting down
            if (m_shutting_down) {
                // Cancel all active transfers by removing them from curl_multi
                for (auto& pair : m_active_requests) {
                    CURL* easy = pair.first;
                    auto& context = pair.second;

                    curl_multi_remove_handle(multi.get(), easy);

                    // Post error callback for canceled request
                    auto handler = context->request.completion_handler;
                    boost::asio::post(m_ios, [handler]() {
                        handler(errors::session_is_closing, std::vector<char>{});
                    });
                }
                m_active_requests.clear();

                // Cancel all retry queue items as well
                for (auto& item : m_retry_queue) {
                    auto handler = item.request.completion_handler;
                    boost::asio::post(m_ios, [handler]() {
                        handler(errors::session_is_closing, std::vector<char>{});
                    });
                }
                m_retry_queue.clear();

                // Now exit - all requests canceled
                break;
            }

            // Periodic idle handle cleanup when we have no active requests
            // This is the ideal time since we're not busy processing
            if (running == 0 && m_active_requests.empty()) {
                auto cleanup_time = clock_type::now();
                if (cleanup_time - last_cleanup > seconds(30)) {
                    pool.cleanup_idle_handles();
                    last_cleanup = cleanup_time;
                }
            }

            // Calculate proper timeout to prevent 100% CPU usage
            long wait_ms = calculate_wait_timeout(multi.get());

            // During shutdown with active transfers, use shorter timeout for responsiveness
            if (m_shutting_down && running > 0) {
                wait_ms = std::min(wait_ms, 100L);  // Check more frequently during shutdown
            }

            // Wait for socket activity, timeout, or wakeup
            int numfds = 0;

            // CRITICAL FIX for 100% CPU bug:
            // Use curl_multi_poll() instead of curl_multi_wait()
            // curl_multi_poll() properly respects the timeout even when there are no
            // file descriptors to monitor, preventing the busy-wait loop.
            // This requires libcurl 7.66.0+ which we verify in the constructor.

            CURLMcode mc = curl_multi_poll(multi.get(), nullptr, 0, static_cast<int>(wait_ms), &numfds);

            if (mc != CURLM_OK) {
                // Log error but continue
                // Brief sleep to prevent spin on persistent error
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Check if connection pool needs immediate update (from tracker add/remove)
            if (m_pool_needs_update.exchange(false)) {
                long new_limit = m_new_connection_limit.load();
                if (new_limit != max_connections) {
                    max_connections = new_limit;
                    m_current_connection_limit.store(max_connections);
                    curl_multi_setopt(multi.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, max_connections);
#ifndef TORRENT_DISABLE_LOGGING
                    std::fprintf(stderr, "*** CURL_TRACKER adjusted pool: %ld connections\n", max_connections);
#endif
                }
            }
        }

        // Cleanup on shutdown - no manual cleanup needed with shared_ptr
        for (auto& pair : m_active_requests) {
            CURL* easy = pair.first;
            auto context = pair.second;  // shared_ptr keeps context alive

            curl_multi_remove_handle(multi.get(), easy);

            // Notify completion with error
            boost::asio::post(m_ios, [handler = context->request.completion_handler]() {
                handler(errors::session_is_closing, std::vector<char>{});
            });
        }
        // shared_ptrs automatically clean up when m_active_requests is cleared

        // Clear multi handle reference
        m_multi_handle = nullptr;
        // RAII curl_multi_handle destructor will clean up automatically

        } catch (const std::exception& e) {
            // Log error and notify main thread
#ifndef TORRENT_DISABLE_LOGGING
            std::fprintf(stderr, "curl thread error: %s\n", e.what());
#endif

            // Signal initialization failure if we haven't initialized yet
            {
                std::scoped_lock<std::mutex> lock(m_init_mutex);
                if (m_init_status == InitStatus::Pending) {
                    m_init_status = InitStatus::Failed;
                }
            }
            m_init_cv.notify_one();

            // Clear multi handle reference
            // Note: RAII curl_multi_handle will automatically clean up
            m_multi_handle = nullptr;
    } catch (...) {
        // Handle unknown exceptions
#ifndef TORRENT_DISABLE_LOGGING
        std::fprintf(stderr, "curl thread: unknown error\n");
#endif

        // Signal initialization failure if we haven't initialized yet
        {
            std::scoped_lock<std::mutex> lock(m_init_mutex);
            if (m_init_status == InitStatus::Pending) {
                m_init_status = InitStatus::Failed;
            }
        }
        m_init_cv.notify_one();

        // Clean up multi handle if it exists
        CURLM* multi = m_multi_handle.load();
        if (multi) {
            m_multi_handle = nullptr;
            curl_multi_cleanup(multi);
        }
    }
}

int curl_thread_manager::process_completions(CURLM* multi, curl_handle_pool& pool) {
    CURLMsg* msg;
    int msgs_left;
    int completed_count = 0;

    // CORRECT: Loop while reading messages from curl
    while ((msg = curl_multi_info_read(multi, &msgs_left))) {
        // DEBUG: Log any completed message
        if (msg->msg == CURLMSG_DONE) {
// Removed verbose debug logging
        }
        if (msg->msg != CURLMSG_DONE) continue;

        CURL* easy = msg->easy_handle;
        completed_count++;
        CURLcode result = msg->data.result;

        // Retrieve context pointer (no delete needed - shared_ptr manages lifetime)
        curl_request_context* raw_context = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &raw_context);

        // Find the request in our tracking map
        auto it = m_active_requests.find(easy);
        if (it == m_active_requests.end() || !raw_context) {
            // Handle unexpected state: message received for unknown handle
            curl_multi_remove_handle(multi, easy);
            // No manual delete needed - shared_ptr cleanup happens automatically
            continue;
        }

        auto context = it->second;  // Get shared_ptr (keeps object alive)
        curl_request& req = context->request;
        m_active_requests.erase(it);  // Remove from map (shared_ptr still valid)

        // Remove from multi handle
        curl_multi_remove_handle(multi, easy);

        // Return handle to pool for reuse
        if (context->transfer_data && context->transfer_data->pooled_handle) {
            pool.release(std::move(context->transfer_data->pooled_handle));
        }

        // Determine error code
        error_code ec = curl_error_to_libtorrent(result);

        // Handle CURL write errors silently

        if (!ec) {
            // Check HTTP status code
            long response_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);

            if (response_code >= 400) {
                ec = errors::http_error;

                // Consider retry for server errors
                if (response_code >= 500 && req.retry_count < req.max_retries) {
// Retry silently
                    // No manual cleanup needed - shared_ptr handles everything
                    schedule_retry(std::move(req));
                    continue;
                } else if (response_code >= 500) {
// Exhausted retries silently
                }
            }
        } else if (is_retryable_error(ec)) {
            // Consider retry for transient errors
            // Exclude non-retryable curl codes: permanent failures that won't be fixed by retrying
            if (result != CURLE_WRITE_ERROR &&
                result != CURLE_FILESIZE_EXCEEDED &&
                result != CURLE_UNSUPPORTED_PROTOCOL &&
                result != CURLE_COULDNT_RESOLVE_PROXY &&
                result != CURLE_COULDNT_RESOLVE_HOST &&  // DNS failures won't be fixed by retry
                result != CURLE_COULDNT_CONNECT &&  // Connection failures mean server is down
                result != CURLE_INTERFACE_FAILED &&  // Interface binding errors won't be fixed by retry
                req.retry_count < req.max_retries &&
                clock_type::now() < req.deadline) {
                // No manual cleanup needed - shared_ptr handles everything
                schedule_retry(std::move(req));
                continue;
            }
        }

        // Update metrics
        if (ec) {
            m_failed_requests++;
        } else {
            m_completed_requests++;
        }

        // Post completion to io_context (thread-safe)
        // Move the buffer out of the transfer_data's response structure
        std::vector<char> response = std::move(context->transfer_data->response->buffer);
        auto handler = req.completion_handler;

        // No manual cleanup needed - shared_ptr handles everything when context goes out of scope

        // Post completion handler silently

        boost::asio::post(m_ios,
            [h = handler, e = ec, resp = std::move(response)]() {
                h(e, resp);
            });
    }

    return completed_count;
}

void curl_thread_manager::schedule_retry(curl_request req) {
    // Exponential backoff
    req.retry_count++;
    req.retry_delay *= 2;

    // Cap maximum delay at 30 seconds
    req.retry_delay = std::min(req.retry_delay, milliseconds(30000));

    // Clear response buffer for retry
    req.response->buffer.clear();

    // Schedule retry
    auto retry_time = clock_type::now() + req.retry_delay;

    // Don't retry past deadline
    if (retry_time >= req.deadline) {
        // Give up - timeout
        boost::asio::post(m_ios, [handler = req.completion_handler]() {
            handler(errors::timed_out, std::vector<char>{});
        });
        return;
    }

    // Insert into multiset (safe, no const_cast needed)
    m_retry_queue.insert({retry_time, std::move(req)});

    // Note: No need to wake the curl thread here. The thread will wake up
    // naturally when its timeout expires, and calculate_wait_timeout() will
    // ensure it wakes up when the retry is ready.
}

// CRITICAL FIX: Calculate proper wait timeout to prevent 100% CPU usage when idle
long curl_thread_manager::calculate_wait_timeout(CURLM* multi) const {
    // Step 1: Get libcurl's internal timeout recommendation
    long curl_timeout_ms = -1;
    curl_multi_timeout(multi, &curl_timeout_ms);

    // Step 2: Calculate application-level timeout (retry queue)
    long app_timeout_ms = -1;
    if (!m_retry_queue.empty()) {
        auto now = clock_type::now();
        auto next_retry = m_retry_queue.begin()->scheduled_time;
        if (next_retry > now) {
            app_timeout_ms = std::chrono::duration_cast<milliseconds>(
                next_retry - now).count();
        } else {
            app_timeout_ms = 0; // Retry is ready now
        }
    }

    // Step 3: Determine final timeout
    long wait_ms;

    // If we have active transfers or pending work
    if (!m_active_requests.empty()) {
        // Use libcurl's timeout if valid, otherwise small default
        if (curl_timeout_ms >= 0) {
            wait_ms = curl_timeout_ms;
        } else {
            wait_ms = 100; // 100ms default for active transfers
        }

        // Consider app timeout if we have retries pending
        if (app_timeout_ms >= 0 && app_timeout_ms < wait_ms) {
            wait_ms = app_timeout_ms;
        }
    }
    // If we're truly idle (no active transfers)
    else if (app_timeout_ms >= 0) {
        // Wait until next retry
        wait_ms = app_timeout_ms;
    }
    else {
        // CRITICAL: When completely idle, wait indefinitely (or very long)
        // curl_multi_wakeup() will interrupt this when new work arrives
        wait_ms = 60000; // 60 seconds - will be interrupted by wakeup
    }

    // Safety: Never return negative timeout
    return std::max(0L, wait_ms);
}

// Calculate optimal number of connections based on tracker count for dynamic scaling
long curl_thread_manager::calculate_optimal_connections() const {
    // Get current unique tracker count (O(1) operation)
    size_t unique_trackers = m_tracker_counter.unique_count();

    // Strategy: 2 connections per unique tracker (HTTP/1.1 standard)
    // With HTTP/2, these 2 connections can multiplex many streams
    long tracker_based = static_cast<long>(unique_trackers * 2);

    // Get system limits as a safety cap
    long system_limit = 1000; // Conservative default

#ifdef _WIN32
    // Windows: Query actual handle limit
    system_limit = _getmaxstdio();
    if (system_limit < 256) {
        _setmaxstdio(2048);
        system_limit = 2048;
    }
#else
    // POSIX systems - check file descriptor limit
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        // Use 25% of system limit for libcurl
        system_limit = static_cast<long>(rlim.rlim_cur) / 4;
    }
#endif

    // Apply reasonable limits
    long optimal = tracker_based;
    optimal = std::max(2L, optimal);            // Minimum 2 connections
    optimal = std::min(optimal, system_limit);  // Respect system limits
    optimal = std::min(optimal, 100L);          // Max 100 (50 unique trackers)

    // Removed verbose connection pool logging

    return optimal;
}

// Get statistics for monitoring
curl_thread_stats curl_thread_manager::get_stats() const {
    curl_thread_stats stats;
    stats.unique_tracker_hosts = m_tracker_counter.unique_count();
    stats.current_connection_limit = m_current_connection_limit.load();

    // These need mutex protection to access safely
    {
        std::scoped_lock<std::mutex> lock(m_queue_mutex);
        stats.queued_requests = m_request_queue.size();
    }
    stats.active_requests = m_active_requests.size();

    return stats;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace libtorrent::aux

#endif // TORRENT_USE_LIBCURL
