/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_LSD_HPP
#define LIBTORRENT_LSD_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include "libtorrent/sha1_hash.hpp"

namespace libtorrent { namespace aux {
	struct TORRENT_EXTRA_EXPORT lsd_callback
	{
		virtual void on_lsd_peer(tcp::endpoint const& peer, sha1_hash const& ih) = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_lsd() const = 0;
		virtual void log_lsd(char const* msg) const = 0;
#endif

	protected:
		~lsd_callback() {}
	};
}}

#endif // LIBTORRENT_LSD_HPP
