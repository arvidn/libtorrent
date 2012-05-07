/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_SESSION_INTERFACE_HPP_INCLUDED
#define TORRENT_SESSION_INTERFACE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/peer_id.hpp"
#include <boost/weak_ptr.hpp>
#include <boost/function.hpp>

#include "libtorrent/address.hpp"

#ifndef TORRENT_DISABLE_DHT	
#include "libtorrent/socket.hpp"
#endif

namespace libtorrent
{
	struct peer_connection;
	struct torrent;
	struct proxy_settings;
	struct write_some_job;
	struct pe_settings;
}

namespace libtorrent { namespace aux
{
	struct session_interface
	{
		virtual int session_time() const = 0;
	
		virtual bool is_paused() const = 0;
		virtual bool is_aborted() const = 0;
		virtual int num_uploads() const = 0;
		virtual void unchoke_peer(peer_connection& c) = 0;
		virtual void choke_peer(peer_connection& c) = 0;
		virtual void trigger_optimistic_unchoke() = 0;
		virtual void trigger_unchoke() = 0;

		virtual boost::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) = 0;

		virtual void inc_disk_queue(int channel) = 0;
		virtual void dec_disk_queue(int channel) = 0;

		virtual peer_id const& get_peer_id() const = 0;

		// cork a peer and schedule a delayed uncork
		// does nothing if the peer is already corked
		virtual void cork_burst(peer_connection* p) = 0;

		virtual void close_connection(peer_connection* p, error_code const& ec) = 0;
		virtual int num_connections() const = 0;

		virtual char* allocate_buffer() = 0;
		virtual int send_buffer_size() const = 0;

		virtual boost::uint16_t listen_port() const = 0;
		virtual boost::uint16_t ssl_listen_port() const = 0;

		// used to (potentially) issue socket write calls onto multiple threads
		virtual void post_socket_write_job(write_some_job& j) = 0;

		// when binding outgoing connections, this provides a round-robin
		// port selection
		virtual int next_port() = 0;

		virtual void subscribe_to_disk(boost::function<void()> const& cb) = 0;
		virtual bool exceeded_cache_use() const = 0;

		// TODO: it would be nice to not have this be part of session_interface
		virtual void set_proxy(proxy_settings const& s) = 0;
		virtual proxy_settings const& proxy() const = 0;
		virtual void set_external_address(address const& ip
			, int source_type, address const& source) = 0;
		virtual tcp::endpoint get_ipv6_interface() const = 0;
		virtual tcp::endpoint get_ipv4_interface() const = 0;

#ifndef TORRENT_DISABLE_ENCRYPTION
			virtual pe_settings const& get_pe_settings() const = 0;
#endif

#ifndef TORRENT_DISABLE_DHT
		virtual void add_dht_node(udp::endpoint n) = 0;
		virtual int external_udp_port() const = 0;
#endif

#ifndef TORRENT_DISABLE_GEO_IP
		virtual bool has_country_db() const = 0;
		virtual char const* country_for_ip(address const& a) = 0;
#endif

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		virtual bool is_network_thread() const = 0;
		virtual bool has_peer(peer_connection const* p) const = 0;
#endif
	};
}}

#endif

