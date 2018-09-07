/*

Copyright (c) 2012-2018, Arvid Norberg
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

#ifndef DHT_OBSERVER_HPP
#define DHT_OBSERVER_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/kademlia/msg.hpp"
#include "libtorrent/aux_/session_udp_sockets.hpp" // for transport

namespace libtorrent {

class entry;

namespace aux { struct listen_socket_handle; }

namespace dht {

	struct TORRENT_EXTRA_EXPORT dht_logger
	{
#ifndef TORRENT_DISABLE_LOGGING
		enum module_t
		{
			tracker,
			node,
			routing_table,
			rpc_manager,
			traversal
		};

		enum message_direction_t
		{
			incoming_message,
			outgoing_message
		};

		virtual bool should_log(module_t m) const = 0;
		virtual void log(module_t m, char const* fmt, ...) TORRENT_FORMAT(3,4) = 0;
		virtual void log_packet(message_direction_t dir, span<char const> pkt
			, udp::endpoint const& node) = 0;
#endif

	protected:
		~dht_logger() = default;
	};

	struct TORRENT_EXTRA_EXPORT dht_observer : dht_logger
	{
		virtual void set_external_address(aux::listen_socket_handle const& iface
			, address const& addr, address const& source) = 0;
		virtual int get_listen_port(aux::transport ssl, aux::listen_socket_handle const& s) = 0;
		virtual void get_peers(sha1_hash const& ih) = 0;
		virtual void outgoing_get_peers(sha1_hash const& target
			, sha1_hash const& sent_target, udp::endpoint const& ep) = 0;
		virtual void announce(sha1_hash const& ih, address const& addr, int port) = 0;
		virtual bool on_dht_request(string_view query
			, dht::msg const& request, entry& response) = 0;

	protected:
		~dht_observer() = default;
	};
}}

#endif
