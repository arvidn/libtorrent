/*

Copyright (c) 2010, 2014-2020, Arvid Norberg
Copyright (c) 2017, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RESOLVER_INTERFACE_HPP_INCLUDE
#define TORRENT_RESOLVER_INTERFACE_HPP_INCLUDE

#include <vector>
#include <functional>

#include "libtorrent/error_code.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/flags.hpp"

namespace lt::aux {

// hidden
using resolver_flags = flags::bitfield_flag<std::uint8_t, struct resolver_flag_tag>;

struct TORRENT_EXTRA_EXPORT resolver_interface
{
	using callback_t = std::function<void(error_code const&, std::vector<address> const&)>;

	// this flag will make async_resolve() only use the cache and fail if we
	// don't have a cache entry, regardless of how old it is. This is usefull
	// when completing the lookup quickly is more important than accuracy,
	// like on shutdown
	static inline constexpr resolver_flags cache_only = 0_bit;

	// set this flag for lookups that are not critical during shutdown. i.e.
	// for looking up tracker names _except_ when stopping a tracker.
	static inline constexpr resolver_flags abort_on_shutdown = 1_bit;

	virtual void async_resolve(std::string const& host, resolver_flags flags
		, callback_t h) = 0;

	virtual void abort() = 0;

	virtual void set_cache_timeout(seconds timeout) = 0;

protected:
	~resolver_interface() {}
};

}

#endif
