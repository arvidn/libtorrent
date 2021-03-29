/*

Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/alert_manager.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/torrent_peer_allocator.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/peer_class.hpp"

#if TORRENT_USE_SSL
#include "libtorrent/aux_/ssl.hpp"
#endif

#include "libtorrent/io_context.hpp"

#include <cstdio>

namespace lt {

struct session_mock : aux::session_interface
{
	session_mock(boost::asio::io_context& ioc)
		: _io_context(ioc)
#if TORRENT_USE_SSL
		, _ssl_context(aux::ssl::context::tls_client)
#endif
		, _alerts(1000, alert_category::all)
		, _resolver(_io_context)
		, _start_time(clock_type::now())
		, _disk_io(disabled_disk_io_constructor(_io_context, _session_settings, _counters))
	{}

	void set_external_address(tcp::endpoint const&, address const&, aux::ip_source_t, address const&) override {}
	aux::external_ip external_address() const override { return {}; }

	disk_interface& disk_thread() override { return *_disk_io; }

	aux::alert_manager& alerts() override { return _alerts; }

	aux::torrent_peer_allocator_interface& get_peer_allocator() override { return _torrent_peer_allocator; }
	boost::asio::io_context& get_context() override { return _io_context; }
	aux::resolver_interface& get_resolver() override { return _resolver; }

	bool has_connection(aux::peer_connection*) const override { return false; }
	void insert_peer(std::shared_ptr<aux::peer_connection> const&) override {}

	void remove_torrent(torrent_handle const&, remove_flags_t) override {}
	void remove_torrent_impl(std::shared_ptr<aux::torrent>, remove_flags_t) override {}

	port_filter const& get_port_filter() const override { return _port_filter; }
	void ban_ip(address) override {}

	std::uint16_t session_time() const override { return 0; }
	time_point session_start_time() const override { return _start_time; }

	bool is_aborted() const override { return false; }
	int num_uploads() const override { return 0; }
	bool preemptive_unchoke() const override { return false; }
	void trigger_optimistic_unchoke() noexcept override {}
	void trigger_unchoke() noexcept override {}

	std::weak_ptr<aux::torrent> find_torrent(info_hash_t const&) const override { return std::weak_ptr<aux::torrent>(); }
	std::weak_ptr<aux::torrent> find_disconnect_candidate_torrent() const override { return std::weak_ptr<aux::torrent>(); }
	std::shared_ptr<aux::torrent> delay_load_torrent(info_hash_t const&, aux::peer_connection*) override { return nullptr; }
	void insert_torrent(info_hash_t const&, std::shared_ptr<aux::torrent> const&) override {}
	void update_torrent_info_hash(std::shared_ptr<aux::torrent> const&, info_hash_t const&) override {}
	void set_queue_position(aux::torrent*, queue_position_t) override {}
	int num_torrents() const override { return 1; }

	void close_connection(aux::peer_connection*) noexcept override {}
	int num_connections() const override { return 0; }

	void deferred_submit_jobs() override {}

	std::uint16_t listen_port() const override { return 0; }
	std::uint16_t ssl_listen_port() const override { return 0; }

	int listen_port(aux::transport, address const&) override { return 0; }

	void for_each_listen_socket(std::function<void(aux::listen_socket_handle const&)>) override {}

	tcp::endpoint bind_outgoing_socket(aux::socket_type&, address const&, error_code&) const override { return {}; }
	bool verify_bound_address(address const&, bool, error_code&) override { return false; }

	aux::proxy_settings proxy() const override { return {}; }

	void prioritize_connections(std::weak_ptr<aux::torrent>) override {}

	void trigger_auto_manage() override {}

	void apply_settings_pack(std::shared_ptr<settings_pack>) override {}
	aux::session_settings const& settings() const override { return _session_settings; }

	void queue_tracker_request(aux::tracker_request, std::weak_ptr<aux::request_callback>) override {}

	void set_peer_classes(aux::peer_class_set*, address const&, socket_type_t) override {}
	peer_class_pool const& peer_classes() const override { return _peer_class_pool; }
	peer_class_pool& peer_classes() override { return _peer_class_pool; }
	bool ignore_unchoke_slots_set(aux::peer_class_set const&) const override { return false; }
	int copy_pertinent_channels(aux::peer_class_set const&, int, aux::bandwidth_channel**, int) override { return 0; }
	std::uint8_t use_quota_overhead(aux::peer_class_set&, int, int) override { return 0; }

	aux::bandwidth_manager* get_bandwidth_manager(int) override { return nullptr; }

	void sent_bytes(int, int) override {}
	void received_bytes(int, int) override {}
	void trancieve_ip_packet(int, bool) override {}
	void sent_syn(bool) override {}
	void received_synack(bool) override {}

	aux::vector<aux::torrent*>& torrent_list(aux::torrent_list_index_t) override { return _torrent_list; }

	bool has_lsd() const override { return false; }
	void announce_lsd(sha1_hash const&, int) override {}
	aux::utp_socket_manager* utp_socket_manager() override { return nullptr; }
#if TORRENT_USE_SSL
	aux::utp_socket_manager* ssl_utp_socket_manager() override { return nullptr; }
#endif
	void inc_boost_connections() override {}
	std::vector<block_info>& block_info_storage() override { return _block_info_list; }

#if TORRENT_USE_SSL
	aux::ssl::context* ssl_ctx() override { return &_ssl_context; }
#endif

	counters& stats_counters() override { return _counters; }
	void received_buffer(int) override {}
	void sent_buffer(int) override {}

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::vector<std::shared_ptr<aux::torrent>> find_collection(std::string const&) const override { return {}; }
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	aux::torrent const* find_encrypted_torrent(sha1_hash const&, sha1_hash const&) override { return nullptr; }
#endif

#if TORRENT_USE_I2P
	aux::proxy_settings i2p_proxy() const override { return {}; }
	char const* i2p_session() const override { return nullptr; }
#endif

#ifndef TORRENT_DISABLE_DHT
	bool announce_dht() const override { return false; }
	void add_dht_node(udp::endpoint const&) override {}
	bool has_dht() const override { return false; }
	int external_udp_port(address const&) const override { return 0; }
	dht::dht_tracker* dht() override { return nullptr; }
	void prioritize_dht(std::weak_ptr<aux::torrent>) override {}
#endif

#if TORRENT_USE_ASSERTS
	bool verify_queue_position(aux::torrent const*, queue_position_t) override { return false; }
	bool is_single_thread() const override { return true; }
	bool has_peer(aux::peer_connection const*) const override { return false; }
	bool any_torrent_has_peer(aux::peer_connection const*) const override { return false; }
	bool is_posting_torrent_updates() const override { return false; }
#endif

#ifndef TORRENT_DISABLE_LOGGING
	// session_logger
	bool should_log() const override { return true; }
	void session_log(char const* fmt, ...) const override TORRENT_FORMAT(2,3)
	{
		if (!_alerts.should_post<log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		_alerts.emplace_alert<log_alert>(fmt, v);
		va_end(v);
	}
#endif

	// utils for tests
	aux::session_settings& mutable_settings() { return _session_settings; }
	void print_alerts(time_point start_time)
	{
		std::vector<alert*> as;
		_alerts.get_all(as);

		for (std::vector<alert*>::iterator i = as.begin()
			, end(as.end()); i != end; ++i)
		{
			alert* a = *i;
			time_duration d = a->timestamp() - start_time;
			std::uint32_t millis = std::uint32_t(duration_cast<milliseconds>(d).count());
			std::printf("%4d.%03d: %-25s %s\n", millis / 1000, millis % 1000
				, a->what()
				, a->message().c_str());
		}
	}

	boost::asio::io_context& _io_context;
#if TORRENT_USE_SSL
	aux::ssl::context _ssl_context;
#endif

	mutable aux::alert_manager _alerts;
	aux::resolver _resolver;
	aux::session_settings _session_settings;
	aux::torrent_peer_allocator _torrent_peer_allocator;
	port_filter _port_filter;
	counters _counters;
	peer_class_pool _peer_class_pool;
	time_point _start_time;

	std::unique_ptr<disk_interface> _disk_io;

	aux::vector<aux::torrent*> _torrent_list;
	std::vector<block_info> _block_info_list;
};

}
