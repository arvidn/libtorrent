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

#ifndef TORRENT_CURL_HANDLE_WRAPPERS_HPP
#define TORRENT_CURL_HANDLE_WRAPPERS_HPP

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <utility>
#include <string>

namespace libtorrent { namespace aux {

// RAII wrapper for CURL easy handle
class curl_easy_handle {
public:
    explicit curl_easy_handle() 
        : m_handle(curl_easy_init()) {
        if (!m_handle) {
            throw std::runtime_error("Failed to create CURL easy handle");
        }
    }
    
    ~curl_easy_handle() noexcept {
        if (m_handle) {
            curl_easy_cleanup(m_handle);
        }
    }
    
    curl_easy_handle(const curl_easy_handle&) = delete;
    curl_easy_handle& operator=(const curl_easy_handle&) = delete;
    
    curl_easy_handle(curl_easy_handle&& other) noexcept 
        : m_handle(std::exchange(other.m_handle, nullptr)) {}
    
    curl_easy_handle& operator=(curl_easy_handle&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                curl_easy_cleanup(m_handle);
            }
            m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }
    
    CURL* get() const noexcept { return m_handle; }
    CURL* release() noexcept { 
        return std::exchange(m_handle, nullptr); 
    }
    
    // Convenience setopt wrapper with type safety
    template<typename T>
    void setopt(CURLoption option, T value) {
        CURLcode res = curl_easy_setopt(m_handle, option, value);
        if (res != CURLE_OK) {
            throw std::runtime_error("curl_easy_setopt failed: " 
                + std::string(curl_easy_strerror(res)));
        }
    }

private:
    CURL* m_handle;
};

// RAII wrapper for CURL multi handle
class curl_multi_handle {
public:
    explicit curl_multi_handle() 
        : m_handle(curl_multi_init()) {
        if (!m_handle) {
            throw std::runtime_error("Failed to create CURL multi handle");
        }
    }
    
    ~curl_multi_handle() noexcept {
        if (m_handle) {
            curl_multi_cleanup(m_handle);
        }
    }
    
    curl_multi_handle(const curl_multi_handle&) = delete;
    curl_multi_handle& operator=(const curl_multi_handle&) = delete;
    
    curl_multi_handle(curl_multi_handle&& other) noexcept 
        : m_handle(std::exchange(other.m_handle, nullptr)) {}
    
    curl_multi_handle& operator=(curl_multi_handle&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                curl_multi_cleanup(m_handle);
            }
            m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }
    
    CURLM* get() const noexcept { return m_handle; }
    CURLM* release() noexcept { 
        return std::exchange(m_handle, nullptr); 
    }
    
    void setopt(CURLMoption option, long value) {
        CURLMcode res = curl_multi_setopt(m_handle, option, value);
        if (res != CURLM_OK) {
            throw std::runtime_error("curl_multi_setopt failed: " 
                + std::string(curl_multi_strerror(res)));
        }
    }
    
    // Add handle with automatic cleanup on failure
    void add_handle(CURL* easy) {
        CURLMcode res = curl_multi_add_handle(m_handle, easy);
        if (res != CURLM_OK) {
            throw std::runtime_error("curl_multi_add_handle failed: " 
                + std::string(curl_multi_strerror(res)));
        }
    }
    
    void remove_handle(CURL* easy) {
        curl_multi_remove_handle(m_handle, easy);
    }

private:
    CURLM* m_handle;
};

// Deleter for unique_ptr with custom cleanup
struct curl_easy_deleter {
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};

struct curl_multi_deleter {
    void operator()(CURLM* handle) const noexcept {
        if (handle) curl_multi_cleanup(handle);
    }
};

// Type aliases for unique_ptr usage
using curl_easy_ptr = std::unique_ptr<CURL, curl_easy_deleter>;
using curl_multi_ptr = std::unique_ptr<CURLM, curl_multi_deleter>;

}} // namespace libtorrent::aux

#endif // TORRENT_USE_LIBCURL

#endif // TORRENT_CURL_HANDLE_WRAPPERS_HPP