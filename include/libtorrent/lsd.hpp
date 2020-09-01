/*

Copyright (c) 2007, 2009, 2012, 2014-2015, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_LSD_HPP
#define TORRENT_LSD_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/aux_/lsd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/address.hpp"

namespace libtorrent {

struct lsd : std::enable_shared_from_this<lsd>
{
	lsd(io_context& ios, aux::lsd_callback& cb
		, address listen_address, address netmask);
	~lsd();

	void start(error_code& ec);

	void announce(sha1_hash const& ih, int listen_port);
	void close();

private:

	std::shared_ptr<lsd> self() { return shared_from_this(); }

	void announce_impl(sha1_hash const& ih, int listen_port, int retry_count);
	void resend_announce(error_code const& e, sha1_hash const& info_hash
		, int listen_port, int retry_count);
	void on_announce(error_code const& ec);

	aux::lsd_callback& m_callback;

	address m_listen_address;
	address m_netmask;

	udp::socket m_socket;

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const;
	void debug_log(char const* fmt, ...) const TORRENT_FORMAT(2, 3);
#endif

	// used to resend udp packets in case
	// they time out
	deadline_timer m_broadcast_timer;

	// this is a random (presumably unique)
	// ID for this LSD node. It is used to
	// ignore our own broadcast messages.
	// There's no point in adding ourselves
	// as a peer
	int m_cookie;

	bool m_disabled = false;
};

}

#endif
