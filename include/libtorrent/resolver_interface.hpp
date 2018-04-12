/*

Copyright (c) 2013-2018, Arvid Norberg
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

#ifndef TORRENT_RESOLVER_INTERFACE_HPP_INCLUDE
#define TORRENT_RESOLVER_INTERFACE_HPP_INCLUDE

#include <vector>
#include <functional>

#include "libtorrent/error_code.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

// hidden
using resolver_flags = flags::bitfield_flag<std::uint8_t, struct resolver_flag_tag>;

struct TORRENT_EXTRA_EXPORT resolver_interface
{
	using callback_t = std::function<void(error_code const&, std::vector<address> const&)>;

	// this flag will make async_resolve() only use the cache and fail if we
	// don't have a cache entry, regardless of how old it is. This is usefull
	// when completing the lookup quickly is more important than accuracy,
	// like on shutdown
	static constexpr resolver_flags cache_only = 0_bit;

	// set this flag for lookups that are not critical during shutdown. i.e.
	// for looking up tracker names _except_ when stopping a tracker.
	static constexpr resolver_flags abort_on_shutdown = 1_bit;

	virtual void async_resolve(std::string const& host, resolver_flags flags
		, callback_t const& h) = 0;

	virtual void abort() = 0;

	virtual void set_cache_timeout(seconds timeout) = 0;

protected:
	~resolver_interface() {}
};

}

#endif
