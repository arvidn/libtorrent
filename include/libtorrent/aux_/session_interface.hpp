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
#include "libtorrent/fwd.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/session_udp_sockets.hpp" // for transport
#include "libtorrent/session_types.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/link.hpp" // for torrent_list_index_t

#include <functional>
#include <memory>

#ifdef TORRENT_USE_OPENSSL
// there is no forward declaration header for asio
namespace boost {
namespace asio {
namespace ssl {
	class context;
}
}
}
#endif

namespace libtorrent {

	class peer_connection;
	class torrent;
	struct peer_class_set;
	struct bandwidth_channel;
	struct bandwidth_manager;
	struct peer_class_pool;
	struct disk_observer;
	struct torrent_peer;
	class alert_manager;
	struct disk_interface;
	struct tracker_request;
	struct request_callback;
	struct utp_socket_manager;
	struct external_ip;
	struct torrent_peer_allocator_interface;
	struct counters;
	struct resolver_interface;

	// hidden
	using queue_position_t = aux::strong_typedef<int, struct queue_position_tag>;

	constexpr queue_position_t no_pos{-1};
	constexpr queue_position_t last_pos{(std::numeric_limits<int>::max)()};

#ifndef TORRENT_DISABLE_DHT
namespace dht {

		struct dht_tracker;
	}
#endif
}

namespace libtorrent {
namespace aux {

	struct proxy_settings;
	struct session_settings;
	struct socket_type;

	using ip_source_t = flags::bitfield_flag<std::uint8_t, struct ip_source_tag>;

#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
	// This is the basic logging and debug interface offered by the session.
	// a release build with logging disabled (which is the default) will
	// not have this class at all
	struct TORRENT_EXTRA_EXPORT session_logger
	{
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log() const = 0;
		virtual void session_log(char const* fmt, ...) const TORRENT_FORMAT(2,3) = 0;
#endif

#if TORRENT_USE_ASSERTS
		virtual bool is_single_thread() const = 0;
		virtual bool has_peer(peer_connection const* p) const = 0;
		virtual bool any_torrent_has_peer(peer_connection const* p) const = 0;
		virtual bool is_posting_torrent_updates() const = 0;
#endif
	protected:
		~session_logger() {}
	};
#endif // TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS

	// TODO: 2 make this interface a lot smaller. It could be split up into
	// several smaller interfaces. Each subsystem could then limit the size
	// of the mock object to test it.
	struct TORRENT_EXTRA_EXPORT session_interface
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		: session_logger
#endif
	{

		// TODO: 2 the IP voting mechanism should be factored out
		// to its own class, not part of the session
		// and these constants should move too

		// the logic in ip_voter relies on more reliable sources are represented
		// by more significant bits
		static constexpr ip_source_t source_dht = 1_bit;
		static constexpr ip_source_t source_peer = 2_bit;
		static constexpr ip_source_t source_tracker = 3_bit;
		static constexpr ip_source_t source_router = 4_bit;

		virtual void set_external_address(tcp::endpoint const& local_endpoint
			, address const& ip
			, ip_source_t source_type, address const& source) = 0;
		virtual external_ip external_address() const = 0;

		virtual disk_interface& disk_thread() = 0;

		virtual alert_manager& alerts() = 0;

		virtual torrent_peer_allocator_interface& get_peer_allocator() = 0;
		virtual io_service& get_io_service() = 0;
		virtual resolver_interface& get_resolver() = 0;

		virtual bool has_connection(peer_connection* p) const = 0;
		virtual void insert_peer(std::shared_ptr<peer_connection> const& c) = 0;

		virtual void remove_torrent(torrent_handle const& h, remove_flags_t options = {}) = 0;
		virtual void remove_torrent_impl(std::shared_ptr<torrent> tptr, remove_flags_t options) = 0;

		// port filter
		virtual port_filter const& get_port_filter() const = 0;
		virtual void ban_ip(address addr) = 0;

		virtual std::uint16_t session_time() const = 0;
		virtual time_point session_start_time() const = 0;

		virtual bool is_aborted() const = 0;
		virtual int num_uploads() const = 0;
		virtual bool preemptive_unchoke() const = 0;
		virtual void trigger_optimistic_unchoke() noexcept = 0;
		virtual void trigger_unchoke() noexcept = 0;

		virtual std::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) const = 0;
		virtual std::weak_ptr<torrent> find_disconnect_candidate_torrent() const = 0;
		virtual std::shared_ptr<torrent> delay_load_torrent(sha1_hash const& info_hash
			, peer_connection* pc) = 0;
		virtual void insert_torrent(sha1_hash const& ih, std::shared_ptr<torrent> const& t
#if TORRENT_ABI_VERSION == 1
			, std::string uuid
#endif
			) = 0;
#if TORRENT_ABI_VERSION == 1
		//deprecated in 1.2
		virtual void insert_uuid_torrent(std::string uuid, std::shared_ptr<torrent> const& t) = 0;
#endif
		virtual void set_queue_position(torrent* t, queue_position_t p) = 0;
		virtual int num_torrents() const = 0;

		virtual void close_connection(peer_connection* p) noexcept = 0;
		virtual int num_connections() const = 0;

		virtual void deferred_submit_jobs() = 0;

		virtual std::uint16_t listen_port() const = 0;
		virtual std::uint16_t ssl_listen_port() const = 0;

		virtual int listen_port(aux::transport ssl, address const& local_addr) = 0;

		virtual void for_each_listen_socket(std::function<void(aux::listen_socket_handle const&)> f) = 0;

		// ask for which interface and port to bind outgoing peer connections on
		virtual tcp::endpoint bind_outgoing_socket(socket_type& s, address const&
			remote_address, error_code& ec) const = 0;
		virtual bool verify_bound_address(address const& addr, bool utp
			, error_code& ec) = 0;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		virtual std::vector<std::shared_ptr<torrent>> find_collection(
			std::string const& collection) const = 0;
#endif

		// TODO: it would be nice to not have this be part of session_interface
		virtual proxy_settings proxy() const = 0;

#if TORRENT_USE_I2P
		virtual proxy_settings i2p_proxy() const = 0;
		virtual char const* i2p_session() const = 0;
#endif

		virtual void prioritize_connections(std::weak_ptr<torrent> t) = 0;

		virtual void trigger_auto_manage() = 0;

		virtual void apply_settings_pack(std::shared_ptr<settings_pack> pack) = 0;
		virtual session_settings const& settings() const = 0;

		// the tracker request object must be moved in
		virtual void queue_tracker_request(tracker_request&& req
			, std::weak_ptr<request_callback> c) = 0;
		void queue_tracker_request(tracker_request const& req
			, std::weak_ptr<request_callback> c) = delete;

		// peer-classes
		virtual void set_peer_classes(peer_class_set* s, address const& a, int st) = 0;
		virtual peer_class_pool const& peer_classes() const = 0;
		virtual peer_class_pool& peer_classes() = 0;
		virtual bool ignore_unchoke_slots_set(peer_class_set const& set) const = 0;
		virtual int copy_pertinent_channels(peer_class_set const& set
			, int channel, bandwidth_channel** dst, int m) = 0;
		virtual int use_quota_overhead(peer_class_set& set, int amount_down, int amount_up) = 0;

		virtual bandwidth_manager* get_bandwidth_manager(int channel) = 0;

		virtual void sent_bytes(int bytes_payload, int bytes_protocol) = 0;
		virtual void received_bytes(int bytes_payload, int bytes_protocol) = 0;
		virtual void trancieve_ip_packet(int bytes, bool ipv6) = 0;
		virtual void sent_syn(bool ipv6) = 0;
		virtual void received_synack(bool ipv6) = 0;

		// this is the set of (subscribed) torrents that have changed
		// their states since the last time the user requested updates.
		static constexpr torrent_list_index_t torrent_state_updates{0};

			// all torrents that want to be ticked every second
		static constexpr torrent_list_index_t torrent_want_tick{1};

			// all torrents that want more peers and are still downloading
			// these typically have higher priority when connecting peers
		static constexpr torrent_list_index_t torrent_want_peers_download{2};

			// all torrents that want more peers and are finished downloading
		static constexpr torrent_list_index_t torrent_want_peers_finished{3};

			// torrents that want auto-scrape (only paused auto-managed ones)
		static constexpr torrent_list_index_t torrent_want_scrape{4};

			// auto-managed torrents by state. Only these torrents are considered
			// when recalculating auto-managed torrents. started auto managed
			// torrents that are inactive are not part of these lists, because they
			// are not considered for auto managing (they are left started
			// unconditionally)
		static constexpr torrent_list_index_t torrent_downloading_auto_managed{5};
		static constexpr torrent_list_index_t torrent_seeding_auto_managed{6};
		static constexpr torrent_list_index_t torrent_checking_auto_managed{7};

		static constexpr std::size_t num_torrent_lists = 8;

		virtual aux::vector<torrent*>& torrent_list(torrent_list_index_t i) = 0;

		virtual bool has_lsd() const = 0;
		virtual void announce_lsd(sha1_hash const& ih, int port) = 0;
		virtual libtorrent::utp_socket_manager* utp_socket_manager() = 0;
		virtual void inc_boost_connections() = 0;
		virtual std::vector<block_info>& block_info_storage() = 0;

#ifdef TORRENT_USE_OPENSSL
		virtual libtorrent::utp_socket_manager* ssl_utp_socket_manager() = 0;
		virtual boost::asio::ssl::context* ssl_ctx() = 0 ;
#endif

#if !defined TORRENT_DISABLE_ENCRYPTION
		virtual torrent const* find_encrypted_torrent(
			sha1_hash const& info_hash, sha1_hash const& xor_mask) = 0;
		virtual void add_obfuscated_hash(sha1_hash const& obfuscated
			, std::weak_ptr<torrent> const& t) = 0;
#endif

#ifndef TORRENT_DISABLE_DHT
		virtual bool announce_dht() const = 0;
		virtual void add_dht_node(udp::endpoint const& n) = 0;
		virtual bool has_dht() const = 0;
		virtual int external_udp_port(address const& local_address) const = 0;
		virtual dht::dht_tracker* dht() = 0;
		virtual void prioritize_dht(std::weak_ptr<torrent> t) = 0;
#endif

		virtual counters& stats_counters() = 0;
		virtual void received_buffer(int size) = 0;
		virtual void sent_buffer(int size) = 0;

#if TORRENT_USE_ASSERTS
		virtual bool verify_queue_position(torrent const*, queue_position_t) = 0;
#endif

		virtual ~session_interface() {}
	};
}}

#endif
