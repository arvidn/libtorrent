/*

Copyright (c) 2007-2014, Arvid Norberg
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
#include "libtorrent/peer_id.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/deadline_timer.hpp"

#include <boost/function/function2.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
#include <stdio.h>
#endif

namespace libtorrent
{

typedef boost::function<void(tcp::endpoint, sha1_hash)> peer_callback_t;

class lsd : public intrusive_ptr_base<lsd>
{
public:
	lsd(io_service& ios, address const& listen_interface
		, peer_callback_t const& cb);
	~lsd();

//	void rebind(address const& listen_interface);

	void announce(sha1_hash const& ih, int listen_port, bool broadcast = false);
	void close();

private:

	void announce_impl(sha1_hash const& ih, int listen_port
		, bool broadcast, int retry_count);
	void resend_announce(error_code const& e, sha1_hash const& ih
		, int listen_port, int retry_count);
	void on_announce(udp::endpoint const& from, char* buffer
		, std::size_t bytes_transferred);

	peer_callback_t m_callback;

	// the udp socket used to send and receive
	// multicast messages on
	broadcast_socket m_socket;
#if TORRENT_USE_IPV6
	broadcast_socket m_socket6;
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

	bool m_disabled;
#if TORRENT_USE_IPV6
	bool m_disabled6;
#endif
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	FILE* m_log;
#endif
};

}


#endif

