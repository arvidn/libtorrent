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

#ifndef TORRENT_SSL_SESSION_CACHE_INTERFACE_HPP
#define TORRENT_SSL_SESSION_CACHE_INTERFACE_HPP

#include "libtorrent/config.hpp"
#include <string>

#ifdef TORRENT_USE_OPENSSL
typedef struct ssl_session_st SSL_SESSION;
#endif

namespace libtorrent::aux {

// Non-template interface for SSL session caching
// This allows C callbacks to interact with the templated connection pool
class ssl_session_cache_interface {
public:
	virtual ~ssl_session_cache_interface() = default;

#ifdef TORRENT_USE_OPENSSL
	// Takes ownership
	virtual void store_ssl_session(std::string const& hostname, SSL_SESSION* session) = 0;

	[[nodiscard]] virtual SSL_SESSION* get_cached_ssl_session(std::string const& hostname) const = 0;

	virtual void remove_ssl_session(SSL_SESSION* session) = 0;
#endif
};

} // namespace libtorrent::aux

#endif // TORRENT_SSL_SESSION_CACHE_INTERFACE_HPP