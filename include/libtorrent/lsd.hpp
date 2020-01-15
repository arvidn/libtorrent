/*

Copyright (c) 2007-2018, Arvid Norberg
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

#ifndef TORRENT_LSD_HPP
#define TORRENT_LSD_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/aux_/lsd.hpp"

namespace libtorrent {

struct lsd : std::enable_shared_from_this<lsd>
{
	lsd(io_service& ios, aux::lsd_callback& cb
		, address const& listen_address, address const& netmask);
	~lsd();

	void start(error_code& ec);

	void announce(sha1_hash const& ih, int listen_port);
	void close();

private:

	std::shared_ptr<lsd> self() { return shared_from_this(); }

	void announce_impl(sha1_hash const& ih, int listen_port, int retry_count);
	void resend_announce(error_code const& e, sha1_hash const& ih
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
