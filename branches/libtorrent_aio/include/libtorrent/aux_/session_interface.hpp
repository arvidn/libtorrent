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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
#include <boost/shared_ptr.hpp>
#endif

namespace libtorrent
{
	class peer_connection;
	class torrent;
	struct proxy_settings;
	struct write_some_job;
	struct pe_settings;
	struct peer_class_set;
	struct bandwidth_channel;
	struct bandwidth_manager;
	struct peer_class_pool;
	struct disk_observer;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	struct logger;
#endif
	struct torrent_peer;
}

namespace libtorrent { namespace aux
{
	struct session_interface
	{
		// --- vv used by policy vv ----
		enum peer_type_t
		{
			ipv4_peer,
			ipv6_peer,
			i2p_peer
		};
		virtual torrent_peer* allocate_peer_entry(int type) = 0;
		virtual void free_peer_entry(torrent_peer* p) = 0;
		// --- ^^ used by policy ^^ ----

		virtual int session_time() const = 0;
	
		virtual bool is_paused() const = 0;
		virtual bool is_aborted() const = 0;
		virtual int num_uploads() const = 0;
		virtual void unchoke_peer(peer_connection& c) = 0;
		virtual void choke_peer(peer_connection& c) = 0;
		virtual void trigger_optimistic_unchoke() = 0;
		virtual void trigger_unchoke() = 0;

		virtual boost::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) = 0;
		virtual boost::shared_ptr<torrent> delay_load_torrent(sha1_hash const& info_hash
			, peer_connection* pc) = 0;

		virtual void inc_disk_queue(int channel) = 0;
		virtual void dec_disk_queue(int channel) = 0;

		virtual peer_id const& get_peer_id() const = 0;

		// cork a peer and schedule a delayed uncork
		// does nothing if the peer is already corked
		virtual void cork_burst(peer_connection* p) = 0;

		virtual void close_connection(peer_connection* p, error_code const& ec, bool cancel_with_cq) = 0;
		virtual int num_connections() const = 0;

		virtual char* allocate_buffer() = 0;
		virtual void free_buffer(char* buf) = 0;
		virtual int send_buffer_size() const = 0;

		virtual void deferred_submit_jobs() = 0;

		virtual boost::uint16_t listen_port() const = 0;
		virtual boost::uint16_t ssl_listen_port() const = 0;

		// used to (potentially) issue socket write calls onto multiple threads
		virtual void post_socket_write_job(write_some_job& j) = 0;

		// when binding outgoing connections, this provides a round-robin
		// port selection
		virtual int next_port() = 0;

		// load the specified torrent. also evict one torrent, except
		// for the one specified, if we are at the limit of loaded torrents
		virtual bool load_torrent(torrent* t) = 0;

		// bump the specified torrent to make it the most recently used one
		// in the torrent LRU (i.e. the least likely to get unloaded)
		virtual void bump_torrent(torrent* t, bool back = true) = 0;

		virtual void subscribe_to_disk(disk_observer* o) = 0;
		virtual bool exceeded_cache_use() const = 0;

		// ask for which interface and port to bind outgoing peer connections on
		virtual tcp::endpoint get_interface() const = 0;

		// TODO: it would be nice to not have this be part of session_interface
		virtual void set_proxy(proxy_settings const& s) = 0;
		virtual proxy_settings const& proxy() const = 0;
		virtual void set_external_address(address const& ip
			, int source_type, address const& source) = 0;
		virtual tcp::endpoint get_ipv6_interface() const = 0;
		virtual tcp::endpoint get_ipv4_interface() const = 0;

		// peer-classes
		virtual void set_peer_classes(peer_class_set* s, address const& a, int st) = 0;
		virtual peer_class_pool const& peer_classes() const = 0;
		virtual bool ignore_unchoke_slots_set(peer_class_set const& set) const = 0;
		virtual int copy_pertinent_channels(peer_class_set const& set
			, int channel, bandwidth_channel** dst, int max) = 0;
		virtual int use_quota_overhead(peer_class_set& set, int amount_down, int amount_up) = 0;

		virtual bandwidth_manager* get_bandwidth_manager(int channel) = 0;

		virtual void sent_bytes(int bytes_payload, int bytes_protocol) = 0;
		virtual void received_bytes(int bytes_payload, int bytes_protocol) = 0;
		virtual void trancieve_ip_packet(int bytes, bool ipv6) = 0;
		virtual void sent_syn(bool ipv6) = 0;
		virtual void received_synack(bool ipv6) = 0;
		
		// half-open
		virtual void half_open_done(int ticket) = 0;

		virtual int peak_up_rate() const = 0;

#ifndef TORRENT_DISABLE_ENCRYPTION
			virtual pe_settings const& get_pe_settings() const = 0;
			virtual torrent const* find_encrypted_torrent(
				sha1_hash const& info_hash, sha1_hash const& xor_mask) = 0;
#endif

#ifndef TORRENT_DISABLE_DHT
		virtual void add_dht_node(udp::endpoint n) = 0;
		virtual bool has_dht() const = 0;
		virtual int external_udp_port() const = 0;
#endif

#ifndef TORRENT_DISABLE_GEO_IP
		virtual bool has_country_db() const = 0;
		virtual char const* country_for_ip(address const& a) = 0;
		virtual std::string as_name_for_ip(address const& a) = 0;
		virtual int as_for_ip(address const& a) = 0;
		virtual std::pair<const int, int>* lookup_as(int as) = 0;
#endif

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		virtual bool is_single_thread() const = 0;
		virtual bool has_peer(peer_connection const* p) const = 0;
		virtual bool any_torrent_has_peer(peer_connection const* p) const = 0;
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		virtual boost::shared_ptr<logger> create_log(std::string const& name
			, int instance, bool append = true) = 0;
		virtual void session_log(char const* fmt, ...) const = 0;
		virtual void log_all_torrents(peer_connection* p) = 0;
		virtual std::string get_log_path() const = 0;
#endif

		enum stats_counter_t
		{
			// the number of peers that were disconnected this
			// tick due to protocol error
			error_peers,
			disconnected_peers,
			eof_peers,
			connreset_peers,
			connrefused_peers,
			connaborted_peers,
			perm_peers,
			buffer_peers,
			unreachable_peers,
			broken_pipe_peers,
			addrinuse_peers,
			no_access_peers,
			invalid_arg_peers,
			aborted_peers,

			piece_requests,
			max_piece_requests,
			invalid_piece_requests,
			choked_piece_requests,
			cancelled_piece_requests,
			piece_rejects,
			error_incoming_peers,
			error_outgoing_peers,
			error_rc4_peers,
			error_encrypted_peers,
			error_tcp_peers,
			error_utp_peers,
			// the number of times the piece picker fell through
			// to the end-game mode
			end_game_piece_picker_blocks,
			piece_picker_blocks,
			piece_picker_loops,
			piece_picks,
			reject_piece_picks,
			unchoke_piece_picks,
			incoming_redundant_piece_picks,
			incoming_piece_picks,
			end_game_piece_picks,
			snubbed_piece_picks,

			// reasons to disconnect peers
			connect_timeouts,
			uninteresting_peers,
			timeout_peers,
			no_memory_peers,
			too_many_peers,
			transport_timeout_peers,
			num_banned_peers,
			connection_attempts,
			banned_for_hash_failure,

			// counts events where the network
			// thread wakes up
			on_read_counter,
			on_write_counter,
			on_tick_counter,
			on_lsd_counter,
			on_lsd_peer_counter,
			on_udp_counter,
			on_accept_counter,
			on_disk_queue_counter,
			on_disk_counter,

			torrent_evicted_counter,

			num_stats_counters
		};

		enum stats_gauges_t
		{
			num_checking_torrents = num_stats_counters,
			num_stopped_torrents,
			num_upload_only_torrents,
			num_downloading_torrents,
			num_seeding_torrents,
			num_queued_seeding_torrents,
			num_queued_download_torrents,
			num_error_torrents,

			// these counter indices deliberatly
			// match the order of socket type IDs
			// defined in socket_type.hpp.
			num_tcp_peers,
			num_socks5_peers,
			num_http_proxy_peers,
			num_utp_peers,
			num_i2p_peers,
			num_ssl_peers,
			num_ssl_socks5_peers,
			num_ssl_http_proxy_peers,
			num_ssl_utp_peers,

			num_counters,
			num_gauge_counters = num_counters - num_stats_counters
		};

		virtual void inc_stats_counter(int c, int value = 1) = 0;
		virtual void received_buffer(int size) = 0;
		virtual void sent_buffer(int size) = 0;
	};
}}

#endif

