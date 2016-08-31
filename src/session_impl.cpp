/*

Copyright (c) 2006-2016, Arvid Norberg, Magnus Jonsson
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

#include "libtorrent/config.hpp"

#include <ctime>
#include <algorithm>
#include <cctype>
#include <algorithm>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <functional>

#if TORRENT_USE_INVARIANT_CHECKS
#include <unordered_set>
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/function_equal.hpp>
#include <boost/make_shared.hpp>
#include <boost/asio/ip/v6_only.hpp>

#if TORRENT_USE_RLIMIT

#include <sys/resource.h>
// capture this here where warnings are disabled (the macro generates warnings)
const rlim_t rlim_infinity = RLIM_INFINITY;
#endif // TORRENT_USE_RLIMIT

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/aux_/openssl.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#ifndef TORRENT_DISABLE_DHT
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/types.hpp"
#endif
#include "libtorrent/enum_net.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/lsd.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/build_config.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/choker.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/platform_util.hpp"
#include "libtorrent/aux_/bind_to_device.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex

#ifndef TORRENT_DISABLE_LOGGING

#include "libtorrent/socket_io.hpp"

// for logging stat layout
#include "libtorrent/stat.hpp"

// for logging the size of DHT structures
#ifndef TORRENT_DISABLE_DHT
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/item.hpp>
#endif // TORRENT_DISABLE_DHT

#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"

#endif // TORRENT_DISABLE_LOGGING

#ifdef TORRENT_USE_LIBGCRYPT

extern "C" {
GCRY_THREAD_OPTION_PTHREAD_IMPL;
}

namespace
{
	// libgcrypt requires this to initialize the library
	struct gcrypt_setup
	{
		gcrypt_setup()
		{
			gcry_check_version(0);
			gcry_error_t e = gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
			if (e != 0) std::fprintf(stderr, "libcrypt ERROR: %s\n", gcry_strerror(e));
			e = gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
			if (e != 0) std::fprintf(stderr, "initialization finished error: %s\n", gcry_strerror(e));
		}
	} gcrypt_global_constructor;
}

#endif // TORRENT_USE_LIBGCRYPT

#ifdef TORRENT_USE_OPENSSL

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace
{
	// openssl requires this to clean up internal
	// structures it allocates
	struct openssl_cleanup
	{
		~openssl_cleanup() { CRYPTO_cleanup_all_ex_data(); }
	} openssl_global_destructor;
}

#endif // TORRENT_USE_OPENSSL

#ifdef TORRENT_WINDOWS
// for ERROR_SEM_TIMEOUT
#include <winerror.h>
#endif

using boost::shared_ptr;
using boost::weak_ptr;
using libtorrent::aux::session_impl;
using namespace std::placeholders;

#ifdef BOOST_NO_EXCEPTIONS
namespace boost {
	void throw_exception(std::exception const& e) { ::abort(); }
}
#endif

namespace libtorrent {

#if defined TORRENT_ASIO_DEBUGGING
	std::map<std::string, async_t> _async_ops;
	std::deque<wakeup_t> _wakeups;
	int _async_ops_nthreads = 0;
	std::mutex _async_ops_mutex;
#endif

namespace aux {

	void session_impl::init_peer_class_filter(bool unlimited_local)
	{
		// set the default peer_class_filter to use the local peer class
		// for peers on local networks
		std::uint32_t lfilter = 1 << m_local_peer_class;
		std::uint32_t gfilter = 1 << m_global_class;

		struct class_mapping
		{
			char const* first;
			char const* last;
			std::uint32_t filter;
		};

		static const class_mapping v4_classes[] =
		{
			// everything
			{"0.0.0.0", "255.255.255.255", gfilter},
			// local networks
			{"10.0.0.0", "10.255.255.255", lfilter},
			{"172.16.0.0", "172.16.255.255", lfilter},
			{"192.168.0.0", "192.168.255.255", lfilter},
			// link-local
			{"169.254.0.0", "169.254.255.255", lfilter},
			// loop-back
			{"127.0.0.0", "127.255.255.255", lfilter},
		};

#if TORRENT_USE_IPV6
		static const class_mapping v6_classes[] =
		{
			// everything
			{"::0", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", gfilter},
			// link-local
			{"fe80::", "febf::ffff:ffff:ffff:ffff:ffff:ffff:ffff", lfilter},
			// loop-back
			{"::1", "::1", lfilter},
		};
#endif

		class_mapping const* p = v4_classes;
		int len = sizeof(v4_classes) / sizeof(v4_classes[0]);
		if (!unlimited_local) len = 1;
		for (int i = 0; i < len; ++i)
		{
			error_code ec;
			address_v4 begin = address_v4::from_string(p[i].first, ec);
			address_v4 end = address_v4::from_string(p[i].last, ec);
			if (ec) continue;
			m_peer_class_filter.add_rule(begin, end, p[i].filter);
		}
#if TORRENT_USE_IPV6
		p = v6_classes;
		len = sizeof(v6_classes) / sizeof(v6_classes[0]);
		if (!unlimited_local) len = 1;
		for (int i = 0; i < len; ++i)
		{
			error_code ec;
			address_v6 begin = address_v6::from_string(p[i].first, ec);
			address_v6 end = address_v6::from_string(p[i].last, ec);
			if (ec) continue;
			m_peer_class_filter.add_rule(begin, end, p[i].filter);
		}
#endif
	}

#if defined TORRENT_USE_OPENSSL && OPENSSL_VERSION_NUMBER >= 0x90812f
	namespace {
	// when running bittorrent over SSL, the SNI (server name indication)
	// extension is used to know which torrent the incoming connection is
	// trying to connect to. The 40 first bytes in the name is expected to
	// be the hex encoded info-hash
	int servername_callback(SSL* s, int* ad, void* arg)
	{
		TORRENT_UNUSED(ad);

		session_impl* ses = reinterpret_cast<session_impl*>(arg);
		const char* servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);

		if (!servername || strlen(servername) < 40)
			return SSL_TLSEXT_ERR_ALERT_FATAL;

		sha1_hash info_hash;
		bool valid = aux::from_hex({servername, 40}, info_hash.data());

		// the server name is not a valid hex-encoded info-hash
		if (!valid)
			return SSL_TLSEXT_ERR_ALERT_FATAL;

		// see if there is a torrent with this info-hash
		std::shared_ptr<torrent> t = ses->find_torrent(info_hash).lock();

		// if there isn't, fail
		if (!t) return SSL_TLSEXT_ERR_ALERT_FATAL;

		// if the torrent we found isn't an SSL torrent, also fail.
		if (!t->is_ssl_torrent()) return SSL_TLSEXT_ERR_ALERT_FATAL;

		// if the torrent doesn't have an SSL context and should not allow
		// incoming SSL connections
		if (!t->ssl_ctx()) return SSL_TLSEXT_ERR_ALERT_FATAL;

		// use this torrent's certificate
		SSL_CTX *torrent_context = t->ssl_ctx()->native_handle();

		SSL_set_SSL_CTX(s, torrent_context);
		SSL_set_verify(s, SSL_CTX_get_verify_mode(torrent_context), SSL_CTX_get_verify_callback(torrent_context));

		return SSL_TLSEXT_ERR_OK;
	}
	} // anonymous namespace
#endif

	session_impl::session_impl(io_service& ios)
		: m_io_service(ios)
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_ctx(m_io_service, boost::asio::ssl::context::sslv23)
#endif
		, m_alerts(m_settings.get_int(settings_pack::alert_queue_size), alert::all_categories)
		, m_disk_thread(m_io_service, m_stats_counters
			, static_cast<uncork_interface*>(this))
		, m_download_rate(peer_connection::download_channel)
		, m_upload_rate(peer_connection::upload_channel)
		, m_tracker_manager(
			std::bind(&session_impl::send_udp_packet, this, false, _1, _2, _3, _4)
			, std::bind(&session_impl::send_udp_packet_hostname, this, _1, _2, _3, _4, _5)
			, m_stats_counters
			, m_host_resolver
			, m_settings
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, *this
#endif
			)
		, m_work(io_service::work(m_io_service))
#if TORRENT_USE_I2P
		, m_i2p_conn(m_io_service)
#endif
		, m_created(clock_type::now())
		, m_last_tick(m_created)
		, m_last_second_tick(m_created - milliseconds(900))
		, m_last_choke(m_created)
		, m_last_auto_manage(m_created)
#ifndef TORRENT_DISABLE_DHT
		, m_dht_announce_timer(m_io_service)
#endif
		, m_utp_socket_manager(
			std::bind(&session_impl::send_udp_packet, this, false, _1, _2, _3, _4)
			, std::bind(&session_impl::incoming_connection, this, _1)
			, m_io_service
			, m_settings, m_stats_counters, nullptr)
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_utp_socket_manager(
			std::bind(&session_impl::send_udp_packet, this, true, _1, _2, _3, _4)
			, std::bind(&session_impl::on_incoming_utp_ssl, this, _1)
			, m_io_service
			, m_settings, m_stats_counters
			, &m_ssl_ctx)
#endif
		, m_timer(m_io_service)
		, m_lsd_announce_timer(m_io_service)
		, m_host_resolver(m_io_service)
	{
		update_time_now();
	}

	// This function is called by the creating thread, not in the message loop's
	// io_service thread.
	// TODO: 2 is there a reason not to move all of this into init()? and just
	// post it to the io_service?
	void session_impl::start_session(settings_pack const& pack)
	{
		if (pack.has_val(settings_pack::alert_mask))
		{
			m_alerts.set_alert_mask(pack.get_int(settings_pack::alert_mask));
		}

#ifndef TORRENT_DISABLE_LOGGING
		session_log("start session");
#endif

		error_code ec;
#ifdef TORRENT_USE_OPENSSL
		m_ssl_ctx.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
#if OPENSSL_VERSION_NUMBER >= 0x90812f
		aux::openssl_set_tlsext_servername_callback(m_ssl_ctx.native_handle()
			, servername_callback);
		aux::openssl_set_tlsext_servername_arg(m_ssl_ctx.native_handle(), this);
#endif // OPENSSL_VERSION_NUMBER
#endif

#ifndef TORRENT_DISABLE_DHT
		m_next_dht_torrent = m_torrents.begin();
#endif
		m_next_lsd_torrent = m_torrents.begin();

		m_global_class = m_classes.new_peer_class("global");
		m_tcp_peer_class = m_classes.new_peer_class("tcp");
		m_local_peer_class = m_classes.new_peer_class("local");
		// local peers are always unchoked
		m_classes.at(m_local_peer_class)->ignore_unchoke_slots = true;
		// local peers are allowed to exceed the normal connection
		// limit by 50%
		m_classes.at(m_local_peer_class)->connection_limit_factor = 150;

		TORRENT_ASSERT(m_global_class == session::global_peer_class_id);
		TORRENT_ASSERT(m_tcp_peer_class == session::tcp_peer_class_id);
		TORRENT_ASSERT(m_local_peer_class == session::local_peer_class_id);

		init_peer_class_filter(true);

		// TCP, SSL/TCP and I2P connections should be assigned the TCP peer class
		m_peer_class_type_filter.add(peer_class_type_filter::tcp_socket, m_tcp_peer_class);
		m_peer_class_type_filter.add(peer_class_type_filter::ssl_tcp_socket, m_tcp_peer_class);
		m_peer_class_type_filter.add(peer_class_type_filter::i2p_socket, m_tcp_peer_class);

#ifndef TORRENT_DISABLE_LOGGING

		session_log("config: %s version: %s revision: %s"
			, TORRENT_CFG_STRING
			, LIBTORRENT_VERSION
			, LIBTORRENT_REVISION);

#endif // TORRENT_DISABLE_LOGGING

		// ---- auto-cap max connections ----
		int max_files = max_open_files();
		// deduct some margin for epoll/kqueue, log files,
		// futexes, shared objects etc.
		// 80% of the available file descriptors should go to connections
		m_settings.set_int(settings_pack::connections_limit, (std::min)(
			m_settings.get_int(settings_pack::connections_limit)
			, (std::max)(5, (max_files - 20) * 8 / 10)));
		// 20% goes towards regular files (see disk_io_thread)
#ifndef TORRENT_DISABLE_LOGGING
		session_log("   max connections: %d", m_settings.get_int(settings_pack::connections_limit));
		session_log("   max files: %d", max_files);
		session_log(" generated peer ID: %s", m_peer_id.to_string().c_str());
#endif

		boost::shared_ptr<settings_pack> copy = boost::make_shared<settings_pack>(pack);
		m_io_service.post(std::bind(&session_impl::init, this, copy));
	}

	void session_impl::init(boost::shared_ptr<settings_pack> pack)
	{
		// this is a debug facility
		// see single_threaded in debug.hpp
		thread_started();

		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_LOGGING
		// this alert is a bit special. Since it's so verbose it's not only
		// filtered by its own alert type (log_alert) but also whether session
		// stats alerts are actually enabled. Without session_stats alerts the
		// headers aren't very useful anyway
		if (m_alerts.should_post<log_alert>()
			&& m_alerts.should_post<session_stats_alert>())
		{
			session_log(" *** session thread init");

			// this specific output is parsed by tools/parse_session_stats.py
			// if this is changed, that parser should also be changed
			std::string stats_header = "session stats header: ";
			std::vector<stats_metric> stats = session_stats_metrics();
			std::sort(stats.begin(), stats.end()
				, [] (stats_metric const& lhs, stats_metric const& rhs)
				{ return lhs.value_index < rhs.value_index; });
			for (int i = 0; i < stats.size(); ++i)
			{
				if (i > 0) stats_header += ", ";
				stats_header += stats[i].name;
			}
			m_alerts.emplace_alert<log_alert>(stats_header.c_str());
		}
#endif

		// this is where we should set up all async operations. This
		// is called from within the network thread as opposed to the
		// constructor which is called from the main thread

#if defined TORRENT_ASIO_DEBUGGING
		async_inc_threads();
		add_outstanding_async("session_impl::on_tick");
#endif
		error_code ec;
		m_io_service.post(std::bind(&session_impl::on_tick, this, ec));

		ADD_OUTSTANDING_ASYNC("session_impl::on_lsd_announce");
		int delay = (std::max)(m_settings.get_int(settings_pack::local_service_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait(
			std::bind(&session_impl::on_lsd_announce, this, _1));
		TORRENT_ASSERT(!ec);

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" done starting session");
#endif

		apply_settings_pack_impl(*pack, true);

		// call update_* after settings set initialized

#ifndef TORRENT_NO_DEPRECATE
		update_local_download_rate();
		update_local_upload_rate();
#endif
		update_download_rate();
		update_upload_rate();
		update_connections_limit();
		update_unchoke_limit();
		update_disk_threads();
		update_upnp();
		update_natpmp();
		update_lsd();
		update_dht();
		update_peer_fingerprint();
		update_dht_bootstrap_nodes();
#ifndef TORRENT_DISABLE_DHT
		update_dht_announce_interval();
#endif
	}

	void session_impl::async_resolve(std::string const& host, int flags
		, session_interface::callback_t const& h)
	{
		m_host_resolver.async_resolve(host, flags, h);
	}

	void session_impl::save_state(entry* eptr, std::uint32_t flags) const
	{
		TORRENT_ASSERT(is_single_thread());

		entry& e = *eptr;
		// make it a dict
		e.dict();

		if (flags & session::save_settings)
		{
			entry::dictionary_type& sett = e["settings"].dict();
			save_settings_to_dict(m_settings, sett);
		}

#ifndef TORRENT_DISABLE_DHT
		if (flags & session::save_dht_settings)
		{
			entry::dictionary_type& dht_sett = e["dht"].dict();

			dht_sett["max_peers_reply"] = m_dht_settings.max_peers_reply;
			dht_sett["search_branching"] = m_dht_settings.search_branching;
			dht_sett["max_fail_count"] = m_dht_settings.max_fail_count;
			dht_sett["max_torrents"] = m_dht_settings.max_torrents;
			dht_sett["max_dht_items"] = m_dht_settings.max_dht_items;
			dht_sett["max_peers"] = m_dht_settings.max_peers;
			dht_sett["max_torrent_search_reply"] = m_dht_settings.max_torrent_search_reply;
			dht_sett["restrict_routing_ips"] = m_dht_settings.restrict_routing_ips;
			dht_sett["restrict_search_ips"] = m_dht_settings.restrict_search_ips;
			dht_sett["extended_routing_table"] = m_dht_settings.extended_routing_table;
			dht_sett["aggressive_lookups"] = m_dht_settings.aggressive_lookups;
			dht_sett["privacy_lookups"] = m_dht_settings.privacy_lookups;
			dht_sett["enforce_node_id"] = m_dht_settings.enforce_node_id;
			dht_sett["ignore_dark_internet"] = m_dht_settings.ignore_dark_internet;
			dht_sett["block_timeout"] = m_dht_settings.block_timeout;
			dht_sett["block_ratelimit"] = m_dht_settings.block_ratelimit;
			dht_sett["read_only"] = m_dht_settings.read_only;
			dht_sett["item_lifetime"] = m_dht_settings.item_lifetime;
		}

		if (m_dht && (flags & session::save_dht_state))
		{
			e["dht state"] = m_dht->state();
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_ses_extensions[plugins_all_idx])
		{
			TORRENT_TRY {
				ext->save_state(*eptr);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif
	}

	proxy_settings session_impl::proxy() const
	{
		return proxy_settings(m_settings);
	}

	void session_impl::load_state(bdecode_node const* e
		, std::uint32_t const flags = 0xffffffff)
	{
		TORRENT_ASSERT(is_single_thread());

		bdecode_node settings;
		if (e->type() != bdecode_node::dict_t) return;

#ifndef TORRENT_DISABLE_DHT
		bool need_update_dht = false;
		// load from the old settings names
		if (flags & session::save_dht_settings)
		{
			settings = e->dict_find_dict("dht");
			if (settings)
			{
				bdecode_node val;
				val = settings.dict_find_int("max_peers_reply");
				if (val) m_dht_settings.max_peers_reply = val.int_value();
				val = settings.dict_find_int("search_branching");
				if (val) m_dht_settings.search_branching = val.int_value();
				val = settings.dict_find_int("max_fail_count");
				if (val) m_dht_settings.max_fail_count = val.int_value();
				val = settings.dict_find_int("max_torrents");
				if (val) m_dht_settings.max_torrents = val.int_value();
				val = settings.dict_find_int("max_dht_items");
				if (val) m_dht_settings.max_dht_items = val.int_value();
				val = settings.dict_find_int("max_peers");
				if (val) m_dht_settings.max_peers = val.int_value();
				val = settings.dict_find_int("max_torrent_search_reply");
				if (val) m_dht_settings.max_torrent_search_reply = val.int_value();
				val = settings.dict_find_int("restrict_routing_ips");
				if (val) m_dht_settings.restrict_routing_ips = (val.int_value() != 0);
				val = settings.dict_find_int("restrict_search_ips");
				if (val) m_dht_settings.restrict_search_ips = (val.int_value() != 0);
				val = settings.dict_find_int("extended_routing_table");
				if (val) m_dht_settings.extended_routing_table = (val.int_value() != 0);
				val = settings.dict_find_int("aggressive_lookups");
				if (val) m_dht_settings.aggressive_lookups = (val.int_value() != 0);
				val = settings.dict_find_int("privacy_lookups");
				if (val) m_dht_settings.privacy_lookups = (val.int_value() != 0);
				val = settings.dict_find_int("enforce_node_id");
				if (val) m_dht_settings.enforce_node_id = (val.int_value() != 0);
				val = settings.dict_find_int("ignore_dark_internet");
				if (val) m_dht_settings.ignore_dark_internet = (val.int_value() != 0);
				val = settings.dict_find_int("block_timeout");
				if (val) m_dht_settings.block_timeout = val.int_value();
				val = settings.dict_find_int("block_ratelimit");
				if (val) m_dht_settings.block_ratelimit = val.int_value();
				val = settings.dict_find_int("read_only");
				if (val) m_dht_settings.read_only = (val.int_value() != 0);
				val = settings.dict_find_int("item_lifetime");
				if (val) m_dht_settings.item_lifetime = val.int_value();
			}
		}

		if (flags & session::save_dht_state)
		{
			settings = e->dict_find_dict("dht state");
			if (settings)
			{
				m_dht_state = settings;
				need_update_dht = true;
			}
		}
#endif

#ifndef TORRENT_NO_DEPRECATE
		bool need_update_proxy = false;
		if (flags & session::save_proxy)
		{
			settings = e->dict_find_dict("proxy");
			if (settings)
			{
				bdecode_node val;
				val = settings.dict_find_int("port");
				if (val) m_settings.set_int(settings_pack::proxy_port, val.int_value());
				val = settings.dict_find_int("type");
				if (val) m_settings.set_int(settings_pack::proxy_type, val.int_value());
				val = settings.dict_find_int("proxy_hostnames");
				if (val) m_settings.set_bool(settings_pack::proxy_hostnames, val.int_value() != 0);
				val = settings.dict_find_int("proxy_peer_connections");
				if (val) m_settings.set_bool(settings_pack::proxy_peer_connections, val.int_value() != 0);
				val = settings.dict_find_string("hostname");
				if (val) m_settings.set_str(settings_pack::proxy_hostname, val.string_value().to_string());
				val = settings.dict_find_string("password");
				if (val) m_settings.set_str(settings_pack::proxy_password, val.string_value().to_string());
				val = settings.dict_find_string("username");
				if (val) m_settings.set_str(settings_pack::proxy_username, val.string_value().to_string());
				need_update_proxy = true;
			}
		}

		settings = e->dict_find_dict("encryption");
		if (settings)
		{
			bdecode_node val;
			val = settings.dict_find_int("prefer_rc4");
			if (val) m_settings.set_bool(settings_pack::prefer_rc4, val.int_value() != 0);
			val = settings.dict_find_int("out_enc_policy");
			if (val) m_settings.set_int(settings_pack::out_enc_policy, val.int_value());
			val = settings.dict_find_int("in_enc_policy");
			if (val) m_settings.set_int(settings_pack::in_enc_policy, val.int_value());
			val = settings.dict_find_int("allowed_enc_level");
			if (val) m_settings.set_int(settings_pack::allowed_enc_level, val.int_value());
		}
#endif

		if (flags & session::save_settings)
		{
			settings = e->dict_find_dict("settings");
			if (settings)
			{
				// apply_settings_pack will update dht and proxy
				boost::shared_ptr<settings_pack> pack = load_pack_from_dict(settings);
				apply_settings_pack(pack);
#ifndef TORRENT_DISABLE_DHT
				need_update_dht = false;
#endif
#ifndef TORRENT_NO_DEPRECATE
				need_update_proxy = false;
#endif
			}
		}

#ifndef TORRENT_DISABLE_DHT
		if (need_update_dht) update_dht();
#endif
#ifndef TORRENT_NO_DEPRECATE
		if (need_update_proxy) update_proxy();
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_ses_extensions[plugins_all_idx])
		{
			TORRENT_TRY {
				ext->load_state(*e);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif
	}

#ifndef TORRENT_DISABLE_EXTENSIONS

	void session_impl::add_extension(ext_function_t ext)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(ext);

		add_ses_extension(std::make_shared<session_plugin_wrapper>(ext));
	}

	void session_impl::add_ses_extension(std::shared_ptr<plugin> ext)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT_VAL(ext, ext);

		std::uint32_t const features = ext->implemented_features();

		m_ses_extensions[plugins_all_idx].push_back(ext);

		if (features & plugin::optimistic_unchoke_feature)
			m_ses_extensions[plugins_optimistic_unchoke_idx].push_back(ext);
		if (features & plugin::tick_feature)
			m_ses_extensions[plugins_tick_idx].push_back(ext);
		if (features & plugin::dht_request_feature)
			m_ses_extensions[plugins_dht_request_idx].push_back(ext);
		if (features & plugin::alert_feature)
			m_alerts.add_extension(ext);
		session_handle h(this);
		ext->added(h);
	}

#endif // TORRENT_DISABLE_EXTENSIONS

	void session_impl::pause()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_paused) return;
#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** session paused ***");
#endif
		m_paused = true;
		for (auto& te : m_torrents)
		{
			te.second->set_session_paused(true);
		}
	}

	void session_impl::resume()
	{
		TORRENT_ASSERT(is_single_thread());

		if (!m_paused) return;
		m_paused = false;

		for (auto& te : m_torrents)
		{
			te.second->set_session_paused(false);
		}
	}

	void session_impl::abort()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;
#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** ABORT CALLED ***");
#endif

		// at this point we cannot call the notify function anymore, since the
		// session will become invalid.
		m_alerts.set_notify_function(std::function<void()>());

		// this will cancel requests that are not critical for shutting down
		// cleanly. i.e. essentially tracker hostname lookups that we're not
		// about to send event=stopped to
		m_host_resolver.abort();

		// abort the main thread
		m_abort = true;
		error_code ec;
#if TORRENT_USE_I2P
		m_i2p_conn.close(ec);
#endif
		stop_lsd();
		stop_upnp();
		stop_natpmp();
#ifndef TORRENT_DISABLE_DHT
		stop_dht();
		m_dht_announce_timer.cancel(ec);
#endif
		m_lsd_announce_timer.cancel(ec);

		for (auto const& s : m_incoming_sockets)
		{
			s->close(ec);
			TORRENT_ASSERT(!ec);
		}
		m_incoming_sockets.clear();

		// close the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->sock)
			{
				i->sock->close(ec);
				TORRENT_ASSERT(!ec);
			}

			// TODO: 3 closing the udp sockets here means that
			// the uTP connections cannot be closed gracefully
			if (i->udp_sock)
			{
				i->udp_sock->close();
			}
		}
		if (m_socks_listen_socket && m_socks_listen_socket->is_open())
		{
			m_socks_listen_socket->close(ec);
			TORRENT_ASSERT(!ec);
		}
		m_socks_listen_socket.reset();

#if TORRENT_USE_I2P
		if (m_i2p_listen_socket && m_i2p_listen_socket->is_open())
		{
			m_i2p_listen_socket->close(ec);
			TORRENT_ASSERT(!ec);
		}
		m_i2p_listen_socket.reset();
#endif

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" aborting all torrents (%d)", int(m_torrents.size()));
#endif
		// abort all torrents
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->abort();
		}
		m_torrents.clear();

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" aborting all tracker requests");
#endif
		m_tracker_manager.abort_all_requests();

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" aborting all connections (%d)", int(m_connections.size()));
#endif
		// abort all connections
		while (!m_connections.empty())
		{
#if TORRENT_USE_ASSERTS
			int conn = int(m_connections.size());
#endif
			(*m_connections.begin())->disconnect(errors::stopping_torrent, op_bittorrent);
			TORRENT_ASSERT_VAL(conn == int(m_connections.size()) + 1, conn);
		}

		// we need to give all the sockets an opportunity to actually have their handlers
		// called and cancelled before we continue the shutdown. This is a bit
		// complicated, if there are no "undead" peers, it's safe tor resume the
		// shutdown, but if there are, we have to wait for them to be cleared out
		// first. In session_impl::on_tick() we check them periodically. If we're
		// shutting down and we remove the last one, we'll initiate
		// shutdown_stage2 from there.
		if (m_undead_peers.empty())
		{
			m_io_service.post(std::bind(&session_impl::abort_stage2, this));
		}
	}

	void session_impl::abort_stage2()
	{
		m_download_rate.close();
		m_upload_rate.close();

		// it's OK to detach the threads here. The disk_io_thread
		// has an internal counter and won't release the network
		// thread until they're all dead (via m_work).
		m_disk_thread.abort(false);

		// now it's OK for the network thread to exit
		m_work.reset();
	}

	bool session_impl::has_connection(peer_connection* p) const
	{
		return m_connections.find(p->self()) != m_connections.end();
	}

	void session_impl::insert_peer(boost::shared_ptr<peer_connection> const& c)
	{
		TORRENT_ASSERT(!c->m_in_constructor);
		m_connections.insert(c);
	}

	void session_impl::set_port_filter(port_filter const& f)
	{
		m_port_filter = f;
		if (m_settings.get_bool(settings_pack::no_connect_privileged_ports))
			m_port_filter.add_rule(0, 1024, port_filter::blocked);
		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->port_filter_updated();
	}

	void session_impl::set_ip_filter(boost::shared_ptr<ip_filter> const& f)
	{
		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->set_ip_filter(m_ip_filter);
	}

	void session_impl::ban_ip(address addr)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ip_filter) m_ip_filter = boost::make_shared<ip_filter>();
		m_ip_filter->add_rule(addr, addr, ip_filter::blocked);
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->set_ip_filter(m_ip_filter);
	}

	ip_filter const& session_impl::get_ip_filter()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ip_filter) m_ip_filter = boost::make_shared<ip_filter>();
		return *m_ip_filter;
	}

	port_filter const& session_impl::get_port_filter() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_port_filter;
	}

	namespace
	{

	template <class Socket>
	void set_socket_buffer_size(Socket& s, session_settings const& sett, error_code& ec)
	{
		int const snd_size = sett.get_int(settings_pack::send_socket_buffer_size);
		if (snd_size)
		{
			typename Socket::send_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != snd_size)
			{
				typename Socket::send_buffer_size option(snd_size);
				s.set_option(option, ec);
				if (ec)
				{
					// restore previous value
					s.set_option(prev_option, ec);
					return;
				}
			}
		}
		int const recv_size = sett.get_int(settings_pack::recv_socket_buffer_size);
		if (recv_size)
		{
			typename Socket::receive_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != recv_size)
			{
				typename Socket::receive_buffer_size option(recv_size);
				s.set_option(option, ec);
				if (ec)
				{
					// restore previous value
					s.set_option(prev_option, ec);
					return;
				}
			}
		}
	}

	} // anonymous namespace

	int session_impl::create_peer_class(char const* name)
	{
		TORRENT_ASSERT(is_single_thread());
		return m_classes.new_peer_class(name);
	}

	void session_impl::delete_peer_class(int cid)
	{
		TORRENT_ASSERT(is_single_thread());
		// if you hit this assert, you're deleting a non-existent peer class
		TORRENT_ASSERT(m_classes.at(cid));
		if (m_classes.at(cid) == nullptr) return;
		m_classes.decref(cid);
	}

	peer_class_info session_impl::get_peer_class(int cid)
	{
		peer_class_info ret;
		peer_class* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT(pc);
		if (pc == nullptr)
		{
#if TORRENT_USE_INVARIANT_CHECKS
			// make it obvious that the return value is undefined
			ret.upload_limit = 0xf0f0f0f;
			ret.download_limit = 0xf0f0f0f;
			ret.label.resize(20);
			url_random(&ret.label[0], &ret.label[0] + 20);
			ret.ignore_unchoke_slots = false;
			ret.connection_limit_factor = 0xf0f0f0f;
			ret.upload_priority = 0xf0f0f0f;
			ret.download_priority = 0xf0f0f0f;
#endif
			return ret;
		}

		pc->get_info(&ret);
		return ret;
	}

	void session_impl::queue_tracker_request(tracker_request& req
		, std::weak_ptr<request_callback> c)
	{
		req.listen_port = listen_port();
		if (m_key) req.key = m_key;

#ifdef TORRENT_USE_OPENSSL
		// SSL torrents use the SSL listen port
		// TODO: 2 this need to be more thought through. There isn't necessarily
		// just _one_ SSL listen port, which one we use depends on which interface
		// we announce from.
		if (req.ssl_ctx) req.listen_port = ssl_listen_port();
		req.ssl_ctx = &m_ssl_ctx;
#endif
#if TORRENT_USE_I2P
		if (!m_settings.get_str(settings_pack::i2p_hostname).empty())
		{
			req.i2pconn = &m_i2p_conn;
		}
#endif

//TODO: should there be an option to announce once per listen interface?

		m_tracker_manager.queue_request(get_io_service(), req, c);
	}

	void session_impl::set_peer_class(int cid, peer_class_info const& pci)
	{
		peer_class* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT(pc);
		if (pc == nullptr) return;

		pc->set_info(&pci);
	}

	void session_impl::set_peer_class_filter(ip_filter const& f)
	{
		INVARIANT_CHECK;
		m_peer_class_filter = f;
	}

	ip_filter const& session_impl::get_peer_class_filter() const
	{
		return m_peer_class_filter;
	}

	void session_impl::set_peer_class_type_filter(peer_class_type_filter f)
	{
		m_peer_class_type_filter = f;
	}

	peer_class_type_filter session_impl::get_peer_class_type_filter()
	{
		return m_peer_class_type_filter;
	}

	void session_impl::set_peer_classes(peer_class_set* s, address const& a, int st)
	{
		std::uint32_t peer_class_mask = m_peer_class_filter.access(a);

		// assign peer class based on socket type
		static const int mapping[] = { 0, 0, 0, 0, 1, 4, 2, 2, 2, 3};
		int socket_type = mapping[st];
		// filter peer classes based on type
		peer_class_mask = m_peer_class_type_filter.apply(socket_type, peer_class_mask);

		for (peer_class_t i = 0; peer_class_mask; peer_class_mask >>= 1, ++i)
		{
			if ((peer_class_mask & 1) == 0) continue;

			// if you hit this assert, your peer class filter contains
			// a bitmask referencing a non-existent peer class
			TORRENT_ASSERT_PRECOND(m_classes.at(i));

			if (m_classes.at(i) == nullptr) continue;
			s->add_class(m_classes, i);
		}
	}

	bool session_impl::ignore_unchoke_slots_set(peer_class_set const& set) const
	{
		int num = set.num_classes();
		for (int i = 0; i < num; ++i)
		{
			peer_class const* pc = m_classes.at(set.class_at(i));
			if (pc == nullptr) continue;
			if (pc->ignore_unchoke_slots) return true;
		}
		return false;
	}

	bandwidth_manager* session_impl::get_bandwidth_manager(int channel)
	{
		return (channel == peer_connection::download_channel)
			? &m_download_rate : &m_upload_rate;
	}

	// the back argument determines whether this bump causes the torrent
	// to be the most recently used or the least recently used. Putting
	// the torrent at the back of the queue makes it the most recently
	// used and the least likely to be evicted. This is the default.
	// if back is false, the torrent is moved to the front of the queue,
	// and made the most likely to be evicted. This is used for torrents
	// that are paused, to give up their slot among the loaded torrents
	void session_impl::bump_torrent(torrent* t, bool back)
	{
		if (t->is_aborted()) return;

		bool new_torrent = false;

		// if t is the only torrent in the LRU list, both
		// its prev and next links will be nullptr, even though
		// it's already in the list. Cover this case by also
		// checking to see if it's the first item
		if (t->next != nullptr || t->prev != nullptr || m_torrent_lru.front() == t)
		{
#if TORRENT_USE_ASSERTS
			torrent* i = m_torrent_lru.front();
			while (i != nullptr && i != t) i = i->next;
			TORRENT_ASSERT(i == t);
#endif

			// this torrent is in the list already.
			// first remove it
			m_torrent_lru.erase(t);
		}
		else
		{
			new_torrent = true;
		}

		// pinned torrents should not be part of the LRU, since
		// the LRU is only used to evict torrents
		if (t->is_pinned()) return;

		if (back)
			m_torrent_lru.push_back(t);
		else
			m_torrent_lru.push_front(t);

		if (new_torrent) evict_torrents_except(t);
	}

	void session_impl::evict_torrent(torrent* t)
	{
		TORRENT_ASSERT(!t->is_pinned());

		// if there's no user-load function set, we cannot evict
		// torrents. The feature is not enabled
		if (!m_user_load_torrent) return;

		// if it's already evicted, there's nothing to do
		if (!t->is_loaded() || !t->should_be_loaded()) return;

		TORRENT_ASSERT(t->next != nullptr || t->prev != nullptr || m_torrent_lru.front() == t);

#if TORRENT_USE_ASSERTS
		torrent* i = m_torrent_lru.front();
		while (i != nullptr && i != t) i = i->next;
		TORRENT_ASSERT(i == t);
#endif

		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);

		// 0 means unlimited, never evict anything
		if (loaded_limit == 0) return;

		if (m_torrent_lru.size() > loaded_limit)
		{
			// just evict the torrent
			m_stats_counters.inc_stats_counter(counters::torrent_evicted_counter);
			TORRENT_ASSERT(t->is_pinned() == false);
			t->unload();
			m_torrent_lru.erase(t);
			return;
		}

		// move this torrent to be the first to be evicted whenever
		// another torrent need its slot
		bump_torrent(t, false);
	}

	void session_impl::evict_torrents_except(torrent* ignore)
	{
		if (!m_user_load_torrent) return;

		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);

		// 0 means unlimited, never evict anything
		if (loaded_limit == 0) return;

		// if the torrent we're ignoring (i.e. making room for), allow
		// one more torrent in the list.
		if (ignore->next != nullptr || ignore->prev != nullptr || m_torrent_lru.front() == ignore)
		{
#if TORRENT_USE_ASSERTS
			torrent* i = m_torrent_lru.front();
			while (i != nullptr && i != ignore) i = i->next;
			TORRENT_ASSERT(i == ignore);
#endif
			++loaded_limit;
		}

		while (m_torrent_lru.size() >= loaded_limit)
		{
			// we're at the limit of loaded torrents. Find the least important
			// torrent and unload it. This is done with an LRU.
			torrent* i = m_torrent_lru.front();

			if (i == ignore)
			{
				i = i->next;
				if (i == nullptr) break;
			}
			m_stats_counters.inc_stats_counter(counters::torrent_evicted_counter);
			TORRENT_ASSERT(i->is_pinned() == false);
			i->unload();
			m_torrent_lru.erase(i);
		}
	}

	bool session_impl::load_torrent(torrent* t)
	{
		TORRENT_ASSERT(is_single_thread());
		evict_torrents_except(t);

		// we wouldn't be loading the torrent if it was already
		// in the LRU (and loaded)
		TORRENT_ASSERT(t->next == nullptr && t->prev == nullptr && m_torrent_lru.front() != t);
		TORRENT_ASSERT(m_user_load_torrent);

		// now, load t into RAM
		std::vector<char> buffer;
		error_code ec;
		m_user_load_torrent(t->info_hash(), buffer, ec);
		if (ec)
		{
			t->set_error(ec, torrent_status::error_file_metadata);
			t->pause(false);
			return false;
		}
		bool ret = t->load(buffer);
		if (ret) bump_torrent(t);
		return ret;
	}

	void session_impl::deferred_submit_jobs()
	{
		if (m_deferred_submit_disk_jobs) return;
		m_deferred_submit_disk_jobs = true;
		m_io_service.post(std::bind(&session_impl::submit_disk_jobs, this));
	}

	void session_impl::submit_disk_jobs()
	{
		TORRENT_ASSERT(m_deferred_submit_disk_jobs);
		m_deferred_submit_disk_jobs = false;
		m_disk_thread.submit_jobs();
	}

	// copies pointers to bandwidth channels from the peer classes
	// into the array. Only bandwidth channels with a bandwidth limit
	// is considered pertinent and copied
	// returns the number of pointers copied
	// channel is upload_channel or download_channel
	int session_impl::copy_pertinent_channels(peer_class_set const& set
		, int channel, bandwidth_channel** dst, int max)
	{
		int num_channels = set.num_classes();
		int num_copied = 0;
		for (int i = 0; i < num_channels; ++i)
		{
			peer_class* pc = m_classes.at(set.class_at(i));
			TORRENT_ASSERT(pc);
			if (pc == nullptr) continue;
			bandwidth_channel* chan = &pc->channel[channel];
			// no need to include channels that don't have any bandwidth limits
			if (chan->throttle() == 0) continue;
			dst[num_copied] = chan;
			++num_copied;
			if (num_copied == max) break;
		}
		return num_copied;
	}

	bool session_impl::use_quota_overhead(bandwidth_channel* ch, int amount)
	{
		ch->use_quota(amount);
		return (ch->throttle() > 0 && ch->throttle() < amount);
	}

	int session_impl::use_quota_overhead(peer_class_set& set, int amount_down, int amount_up)
	{
		int ret = 0;
		int num = set.num_classes();
		for (int i = 0; i < num; ++i)
		{
			peer_class* p = m_classes.at(set.class_at(i));
			if (p == nullptr) continue;

			bandwidth_channel* ch = &p->channel[peer_connection::download_channel];
			if (use_quota_overhead(ch, amount_down))
				ret |= 1 << peer_connection::download_channel;
			ch = &p->channel[peer_connection::upload_channel];
			if (use_quota_overhead(ch, amount_up))
				ret |= 1 << peer_connection::upload_channel;
		}
		return ret;
	}

	// session_impl is responsible for deleting 'pack'
	void session_impl::apply_settings_pack(boost::shared_ptr<settings_pack> pack)
	{
		apply_settings_pack_impl(*pack);
	}

	settings_pack session_impl::get_settings() const
	{
		settings_pack ret;
		// TODO: it would be nice to reserve() these vectors up front
		for (int i = settings_pack::string_type_base;
			i < settings_pack::max_string_setting_internal; ++i)
		{
			ret.set_str(i, m_settings.get_str(i));
		}
		for (int i = settings_pack::int_type_base;
			i < settings_pack::max_int_setting_internal; ++i)
		{
			ret.set_int(i, m_settings.get_int(i));
		}
		for (int i = settings_pack::bool_type_base;
			i < settings_pack::max_bool_setting_internal; ++i)
		{
			ret.set_bool(i, m_settings.get_bool(i));
		}
		return ret;
	}

	void session_impl::apply_settings_pack_impl(settings_pack const& pack
		, bool const init)
	{
		bool const reopen_listen_port =
#ifndef TORRENT_NO_DEPRECATE
			(pack.has_val(settings_pack::ssl_listen)
				&& pack.get_int(settings_pack::ssl_listen)
					!= m_settings.get_int(settings_pack::ssl_listen))
			||
#endif
			(pack.has_val(settings_pack::listen_interfaces)
				&& pack.get_str(settings_pack::listen_interfaces)
					!= m_settings.get_str(settings_pack::listen_interfaces));

		apply_pack(&pack, m_settings, this);
		m_disk_thread.set_settings(&pack, m_alerts);

		if (init)
			update_listen_interfaces();

		if (init || reopen_listen_port)
		{
			reopen_listen_sockets();
		}
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::set_settings(libtorrent::session_settings const& s)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<settings_pack> p = load_pack_from_struct(m_settings, s);
		apply_settings_pack(p);
	}

	libtorrent::session_settings session_impl::deprecated_settings() const
	{
		libtorrent::session_settings ret;

		load_struct_from_settings(m_settings, ret);
		return ret;
	}
#endif

	// TODO: 3 try to remove these functions. They are misleading and not very
	// useful. Anything using these should probably be fixed to do something more
	// multi-homed friendly
	tcp::endpoint session_impl::get_ipv6_interface() const
	{
#if TORRENT_USE_IPV6
		for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (!i->local_endpoint.address().is_v6()) continue;
			return tcp::endpoint(i->local_endpoint.address(), i->tcp_external_port);
		}
#endif
		return tcp::endpoint();
	}

	tcp::endpoint session_impl::get_ipv4_interface() const
	{
		for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (!i->local_endpoint.address().is_v4()) continue;
			return tcp::endpoint(i->local_endpoint.address(), i->tcp_external_port);
		}
		return tcp::endpoint();
	}

	enum { listen_no_system_port = 0x02 };

	listen_socket_t session_impl::setup_listener(std::string const& device
		, tcp::endpoint bind_ep, int flags, error_code& ec)
	{
		int retries = m_settings.get_int(settings_pack::max_retry_port_bind);

#ifndef TORRENT_DISABLE_LOGGING
		session_log("attempting to to open listen socket to: %s on device: %s flags: %x"
			, print_endpoint(bind_ep).c_str(), device.c_str(), flags);
#endif

		listen_socket_t ret;
		ret.ssl = (flags & open_ssl_socket) != 0;
		int last_op = 0;
		listen_failed_alert::socket_type_t const sock_type
			= (flags & open_ssl_socket)
			? listen_failed_alert::tcp_ssl
			: listen_failed_alert::tcp;

		// if we're in force-proxy mode, don't open TCP listen sockets. We cannot
		// accept connections on our local machine in this case.
		// TODO: 3 the logic in this if-block should be factored out into a
		// separate function. At least most of it
		if (!m_settings.get_bool(settings_pack::force_proxy))
		{
			ret.sock = std::make_shared<tcp::acceptor>(m_io_service);
			ret.sock->open(bind_ep.protocol(), ec);
			last_op = listen_failed_alert::open;
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("failed to open socket: %s"
					, ec.message().c_str());
#endif

				if (m_alerts.should_post<listen_failed_alert>())
					m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep, last_op
						, ec, sock_type);
				return ret;
			}

#ifdef TORRENT_WINDOWS
			{
				// this is best-effort. ignore errors
				error_code err;
				ret.sock->set_option(exclusive_address_use(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err)
				{
					session_log("failed enable exclusive address use on listen socket: %s"
						, err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
			}
#endif // TORRENT_WINDOWS

			{
				// this is best-effort. ignore errors
				error_code err;
				ret.sock->set_option(tcp::acceptor::reuse_address(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err)
				{
					session_log("failed enable reuse-address on listen socket: %s"
						, err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
			}

#if TORRENT_USE_IPV6
			if (bind_ep.address().is_v6())
			{
				error_code err; // ignore errors here
				ret.sock->set_option(boost::asio::ip::v6_only(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err)
				{
					session_log("failed enable v6 only on listen socket: %s"
						, err.message().c_str());
				}
#endif // LOGGING

#ifdef TORRENT_WINDOWS
				// enable Teredo on windows
				ret.sock->set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err)
				{
					session_log("failed enable IPv6 unrestricted protection level on "
						"listen socket: %s", err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
#endif // TORRENT_WINDOWS
			}
#endif // TORRENT_USE_IPV6

			if (!device.empty())
			{
				// we have an actual device we're interested in listening on, if we
				// have SO_BINDTODEVICE functionality, use it now.
#if TORRENT_HAS_BINDTODEVICE
				ret.sock->set_option(bind_to_device(device.c_str()), ec);
				if (ec)
				{
#ifndef TORRENT_DISABLE_LOGGING
					session_log("bind to device failed (device: %s): %s"
						, device.c_str(), ec.message().c_str());
#endif // TORRENT_DISABLE_LOGGING

					last_op = listen_failed_alert::bind_to_device;
					if (m_alerts.should_post<listen_failed_alert>())
					{
						m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep
							, last_op, ec, sock_type);
					}
					return ret;
				}
#endif
			}

			ret.sock->bind(bind_ep, ec);
			last_op = listen_failed_alert::bind;

			while (ec == error_code(error::address_in_use) && retries > 0)
			{
				TORRENT_ASSERT_VAL(ec, ec);
#ifndef TORRENT_DISABLE_LOGGING
				error_code ignore;
				session_log("failed to bind listen socket to: %s on device: %s :"
					" [%s] (%d) %s (retries: %d)"
					, print_endpoint(bind_ep).c_str()
					, device.c_str()
					, ec.category().name(), ec.value(), ec.message().c_str()
					, retries);
#endif
				ec.clear();
				--retries;
				bind_ep.port(bind_ep.port() + 1);
				ret.sock->bind(bind_ep, ec);
			}

			if (ec == error_code(error::address_in_use)
				&& !(flags & listen_no_system_port))
			{
				// instead of giving up, try let the OS pick a port
				bind_ep.port(0);
				ec.clear();
				ret.sock->bind(bind_ep, ec);
				last_op = listen_failed_alert::bind;
			}

			if (ec)
			{
				// not even that worked, give up

#ifndef TORRENT_DISABLE_LOGGING
				error_code ignore;
				session_log("failed to bind listen socket to: %s on device: %s :"
					" [%s] (%d) %s (giving up)"
					, print_endpoint(bind_ep).c_str()
					, device.c_str()
					, ec.category().name(), ec.value(), ec.message().c_str());
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep
						, last_op, ec, sock_type);
				}
				ret.sock.reset();
				return ret;
			}
			ret.local_endpoint = ret.sock->local_endpoint(ec);
			last_op = listen_failed_alert::get_socket_name;
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("get_sockname failed on listen socket: %s"
					, ec.message().c_str());
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep
						, last_op, ec, sock_type);
				}
				return ret;
			}
			ret.tcp_external_port = ret.local_endpoint.port();
			TORRENT_ASSERT(ret.tcp_external_port == bind_ep.port()
				|| bind_ep.port() == 0);

			ret.sock->listen(m_settings.get_int(settings_pack::listen_queue_size), ec);
			last_op = listen_failed_alert::listen;

			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("cannot listen on interface \"%s\": %s"
					, device.c_str(), ec.message().c_str());
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep
						, last_op, ec, sock_type);
				}
				return ret;
			}
		} // force-proxy mode

		ret.udp_sock = std::make_shared<udp_socket>(m_io_service);
#if TORRENT_HAS_BINDTODEVICE
		if (!device.empty())
		{
			ret.udp_sock->set_option(bind_to_device(device.c_str()), ec);
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("bind to device failed (device: %s): %s"
					, device.c_str(), ec.message().c_str());
#endif // TORRENT_DISABLE_LOGGING

				last_op = listen_failed_alert::bind_to_device;
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(device, bind_ep
						, last_op, ec, sock_type);
				}
				return ret;
			}
		}
#endif
		ret.udp_sock->bind(udp::endpoint(bind_ep.address(), bind_ep.port())
			, ec);

		last_op = listen_failed_alert::bind;
		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("failed to open UDP socket: %s: %s"
				, device.c_str(), ec.message().c_str());
#endif

			listen_failed_alert::socket_type_t const udp_sock_type
				= (flags & open_ssl_socket)
				? listen_failed_alert::utp_ssl
				: listen_failed_alert::udp;

			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.emplace_alert<listen_failed_alert>(device
					, bind_ep, last_op, ec, udp_sock_type);

			return ret;
		}
		ret.udp_external_port = ret.udp_sock->local_port();

		error_code err;
		set_socket_buffer_size(*ret.udp_sock, m_settings, err);
		if (err)
		{
			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.emplace_alert<udp_error_alert>(ret.udp_sock->local_endpoint(ec), err);
		}

		ret.udp_sock->set_force_proxy(m_settings.get_bool(settings_pack::force_proxy));

		// TODO: 2 use a handler allocator here
		ADD_OUTSTANDING_ASYNC("session_impl::on_udp_packet");
		ret.udp_sock->async_read(std::bind(&session_impl::on_udp_packet
			, this, std::weak_ptr<udp_socket>(ret.udp_sock), ret.ssl, _1));

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" listening on: %s TCP port: %d UDP port: %d"
			, bind_ep.address().to_string().c_str()
			, ret.tcp_external_port, ret.udp_external_port);
#endif
		return ret;
	}

	void session_impl::reopen_listen_sockets()
	{
#ifndef TORRENT_DISABLE_LOGGING
		session_log("open listen port");
#endif

		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(!m_abort);
		int flags = m_settings.get_bool(settings_pack::listen_system_port_fallback)
			? 0 : listen_no_system_port;
		error_code ec;

		// close the open listen sockets
		// close the listen sockets
#ifndef TORRENT_DISABLE_LOGGING
		session_log("closing all listen sockets");
#endif
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->sock) i->sock->close(ec);
			if (i->udp_sock) i->udp_sock->close();
		}

		m_listen_sockets.clear();
		m_stats_counters.set_value(counters::has_incoming_connections, 0);
		ec.clear();

		if (m_abort) return;

		for (int i = 0; i < m_listen_interfaces.size(); ++i)
		{
			std::string const& device = m_listen_interfaces[i].device;
			int const port = m_listen_interfaces[i].port;
			bool const ssl = m_listen_interfaces[i].ssl;

#ifndef TORRENT_USE_OPENSSL
			if (ssl)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("attempted to listen ssl with no library support on device: \"%s\""
					, device.c_str());
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(device
						, listen_failed_alert::open
						, boost::asio::error::operation_not_supported
						, listen_failed_alert::tcp_ssl);
				}
				continue;
			}
#endif

			// now we have a device to bind to. This device may actually just be an
			// IP address or a device name. In case it's a device name, we want to
			// (potentially) end up binding a socket for each IP address associated
			// with that device.

			// First, check to see if it's an IP address
			error_code err;
			address const adr = address::from_string(device.c_str(), err);
			if (!err)
			{
				listen_socket_t const s = setup_listener("", tcp::endpoint(adr, port)
					, flags | (ssl ? open_ssl_socket : 0), ec);

				if (!ec && s.sock)
				{
					m_listen_sockets.push_back(s);
				}
			}
			else
			{
				// this is the case where device names a network device. We need to
				// enumerate all IPs associated with this device

				// TODO: 3 only run this once, not every turn through the loop
				std::vector<ip_interface> ifs = enum_net_interfaces(m_io_service, ec);
				if (ec)
				{
#ifndef TORRENT_DISABLE_LOGGING
					session_log("failed to enumerate IPs on device: \"%s\": %s"
						, device.c_str(), ec.message().c_str());
#endif
					if (m_alerts.should_post<listen_failed_alert>())
					{
						m_alerts.emplace_alert<listen_failed_alert>(device
							, listen_failed_alert::enum_if, ec
							, listen_failed_alert::tcp);
					}
					continue;
				}

				for (int k = 0; k < int(ifs.size()); ++k)
				{
					// we're looking for a specific interface, and its address
					// (which must be of the same family as the address we're
					// connecting to)
					if (device != ifs[k].name) continue;

					listen_socket_t const s = setup_listener(device
						, tcp::endpoint(ifs[k].interface_address, port)
						, flags | (ssl ? open_ssl_socket : 0), ec);

					if (!ec && s.sock)
					{
						m_listen_sockets.push_back(s);
					}
				}
			}
		}

		if (m_listen_sockets.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("giving up on binding listen sockets");
#endif
			return;
		}

		// now, send out listen_succeeded_alert for the listen sockets we are
		// listening on
		if (m_alerts.should_post<listen_succeeded_alert>())
		{
			for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
				, end(m_listen_sockets.end()); i != end; ++i)
			{
				error_code err;
				if (i->sock)
				{
					tcp::endpoint const tcp_ep = i->sock->local_endpoint(err);
					if (!err)
					{
						listen_succeeded_alert::socket_type_t const socket_type
							= i->ssl
							? listen_succeeded_alert::tcp_ssl
							: listen_succeeded_alert::tcp;

						m_alerts.emplace_alert<listen_succeeded_alert>(
							tcp_ep , socket_type);
					}
				}

				if (i->udp_sock)
				{
					udp::endpoint const udp_ep = i->udp_sock->local_endpoint(err);
					if (!err && i->udp_sock->is_open())
					{
						listen_succeeded_alert::socket_type_t const socket_type
							= i->ssl
							? listen_succeeded_alert::utp_ssl
							: listen_succeeded_alert::udp;

						m_alerts.emplace_alert<listen_succeeded_alert>(
							udp_ep, socket_type);
					}
				}
			}
		}

		if (m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			update_peer_tos();
		}

		ec.clear();

		// initiate accepting on the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->sock) async_accept(i->sock, i->ssl);
			remap_ports(remap_natpmp_and_upnp, *i);
		}

		open_new_incoming_socks_connection();
#if TORRENT_USE_I2P
		open_new_incoming_i2p_connection();
#endif
	}

	namespace {
		template <typename MapProtocol, typename ProtoType, typename EndpointType>
		void map_port(MapProtocol& m, ProtoType protocol, EndpointType const& ep
			, int& map_handle)
		{
			if (map_handle != -1) m.delete_mapping(map_handle);
			map_handle = -1;

			// only update this mapping if we actually have a socket listening
			if (ep.address() != address())
				map_handle = m.add_mapping(protocol, ep.port(), ep.port());
		}
	}

	void session_impl::remap_ports(remap_port_mask_t const mask
		, listen_socket_t& s)
	{
		error_code ec;
		tcp::endpoint const tcp_ep = s.sock ? s.sock->local_endpoint(ec) : tcp::endpoint();
		udp::endpoint const udp_ep = s.udp_sock ? s.udp_sock->local_endpoint(ec) : udp::endpoint();

		if ((mask & remap_natpmp) && m_natpmp)
		{
			map_port(*m_natpmp, natpmp::tcp, tcp_ep, s.tcp_port_mapping[0]);
			map_port(*m_natpmp, natpmp::udp, udp_ep, s.udp_port_mapping[0]);
		}
		if ((mask & remap_upnp) && m_upnp)
		{
			map_port(*m_upnp, upnp::tcp, tcp_ep, s.tcp_port_mapping[1]);
			map_port(*m_upnp, upnp::udp, udp_ep, s.udp_port_mapping[1]);
		}
	}

	void session_impl::open_new_incoming_socks_connection()
	{
		int const proxy_type = m_settings.get_int(settings_pack::proxy_type);

		if (proxy_type != settings_pack::socks5
			&& proxy_type != settings_pack::socks5_pw
			&& proxy_type != settings_pack::socks4)
			return;

		if (m_socks_listen_socket) return;

		m_socks_listen_socket = std::make_shared<socket_type>(m_io_service);
		bool const ret = instantiate_connection(m_io_service, proxy()
			, *m_socks_listen_socket, nullptr, nullptr, false, false);
		TORRENT_ASSERT_VAL(ret, ret);
		TORRENT_UNUSED(ret);

		ADD_OUTSTANDING_ASYNC("session_impl::on_socks_listen");
		socks5_stream& s = *m_socks_listen_socket->get<socks5_stream>();

		m_socks_listen_port = listen_port();
		if (m_socks_listen_port == 0) m_socks_listen_port = 2000 + random(60000);
		s.async_listen(tcp::endpoint(address_v4::any(), m_socks_listen_port)
			, std::bind(&session_impl::on_socks_listen, this
				, m_socks_listen_socket, _1));
	}

	void session_impl::on_socks_listen(std::shared_ptr<socket_type> const& sock
		, error_code const& e)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_socks_listen");
#endif

		TORRENT_ASSERT(sock == m_socks_listen_socket || !m_socks_listen_socket);

		if (e)
		{
			m_socks_listen_socket.reset();
			if (e == boost::asio::error::operation_aborted) return;
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.emplace_alert<listen_failed_alert>("socks5"
					, listen_failed_alert::accept, e
					, listen_failed_alert::socks5);
			return;
		}

		error_code ec;
		tcp::endpoint ep = sock->local_endpoint(ec);
		TORRENT_ASSERT(!ec);
		TORRENT_UNUSED(ec);

		if (m_alerts.should_post<listen_succeeded_alert>())
			m_alerts.emplace_alert<listen_succeeded_alert>(
				ep, listen_succeeded_alert::socks5);

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_socks_accept");
#endif
		socks5_stream& s = *m_socks_listen_socket->get<socks5_stream>();
		s.async_accept(std::bind(&session_impl::on_socks_accept, this
				, m_socks_listen_socket, _1));
	}

	void session_impl::on_socks_accept(std::shared_ptr<socket_type> const& s
		, error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_socks_accept");
		TORRENT_ASSERT(s == m_socks_listen_socket || !m_socks_listen_socket);
		m_socks_listen_socket.reset();
		if (e == boost::asio::error::operation_aborted) return;
		if (e)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.emplace_alert<listen_failed_alert>("socks5"
					, listen_failed_alert::accept, e
					, listen_failed_alert::socks5);
			return;
		}
		open_new_incoming_socks_connection();
		incoming_connection(s);
	}

	void session_impl::update_i2p_bridge()
	{
		// we need this socket to be open before we
		// can make name lookups for trackers for instance.
		// pause the session now and resume it once we've
		// established the i2p SAM connection
#if TORRENT_USE_I2P
		if (m_settings.get_str(settings_pack::i2p_hostname).empty())
		{
			error_code ec;
			m_i2p_conn.close(ec);
			return;
		}
		m_i2p_conn.open(m_settings.get_str(settings_pack::i2p_hostname)
			, m_settings.get_int(settings_pack::i2p_port)
			, std::bind(&session_impl::on_i2p_open, this, _1));
#endif
	}

#if TORRENT_USE_I2P

	proxy_settings session_impl::i2p_proxy() const
	{
		proxy_settings ret;

		ret.hostname = m_settings.get_str(settings_pack::i2p_hostname);
		ret.type = settings_pack::i2p_proxy;
		ret.port = m_settings.get_int(settings_pack::i2p_port);
		return ret;
	}

	void session_impl::on_i2p_open(error_code const& ec)
	{
		if (ec)
		{
			if (m_alerts.should_post<i2p_alert>())
				m_alerts.emplace_alert<i2p_alert>(ec);

#ifndef TORRENT_DISABLE_LOGGING
			session_log("i2p open failed (%d) %s", ec.value(), ec.message().c_str());
#endif
		}
		// now that we have our i2p connection established
		// it's OK to start torrents and use this socket to
		// do i2p name lookups

		open_new_incoming_i2p_connection();
	}

	void session_impl::open_new_incoming_i2p_connection()
	{
		if (!m_i2p_conn.is_open()) return;

		if (m_i2p_listen_socket) return;

		m_i2p_listen_socket = std::shared_ptr<socket_type>(new socket_type(m_io_service));
		bool ret = instantiate_connection(m_io_service, m_i2p_conn.proxy()
			, *m_i2p_listen_socket, nullptr, nullptr, true, false);
		TORRENT_ASSERT_VAL(ret, ret);
		TORRENT_UNUSED(ret);

		ADD_OUTSTANDING_ASYNC("session_impl::on_i2p_accept");
		i2p_stream& s = *m_i2p_listen_socket->get<i2p_stream>();
		s.set_command(i2p_stream::cmd_accept);
		s.set_session_id(m_i2p_conn.session_id());

		s.async_connect(tcp::endpoint()
			, std::bind(&session_impl::on_i2p_accept, this, m_i2p_listen_socket, _1));
	}

	void session_impl::on_i2p_accept(std::shared_ptr<socket_type> const& s
		, error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_i2p_accept");
		m_i2p_listen_socket.reset();
		if (e == boost::asio::error::operation_aborted) return;
		if (e)
		{
			if (m_alerts.should_post<listen_failed_alert>())
			{
				m_alerts.emplace_alert<listen_failed_alert>("i2p"
					, listen_failed_alert::accept
					, e, listen_failed_alert::i2p);
			}
#ifndef TORRENT_DISABLE_LOGGING
			session_log("i2p SAM connection failure: %s", e.message().c_str());
#endif
			return;
		}
		open_new_incoming_i2p_connection();
		incoming_connection(s);
	}
#endif

	void session_impl::send_udp_packet_hostname(char const* hostname
		, int const port
		, span<char const> p
		, error_code& ec
		, int const flags)
	{
		// for now, just pick the first socket with a matching address family
		// TODO: 3 for proper multi-homed support, we may want to do something
		// else here. Probably let the caller decide which interface to send over
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (!i->udp_sock) continue;
			if (i->ssl) continue;

			i->udp_sock->send_hostname(hostname, port, p, ec, flags);

			if ((ec == error::would_block
					|| ec == error::try_again)
				&& !i->udp_write_blocked)
			{
				i->udp_write_blocked = true;
				ADD_OUTSTANDING_ASYNC("session_impl::on_udp_writeable");
				i->udp_sock->async_write(std::bind(&session_impl::on_udp_writeable
					, this, std::weak_ptr<udp_socket>(i->udp_sock), _1));
			}
			return;
		}
		ec = boost::asio::error::operation_not_supported;
	}

	void session_impl::send_udp_packet(bool const ssl
		, udp::endpoint const& ep
		, span<char const> p
		, error_code& ec
		, int const flags)
	{
		// for now, just pick the first socket with a matching address family
		// TODO: 3 for proper multi-homed support, we may want to do something
		// else here. Probably let the caller decide which interface to send over
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->ssl != ssl) continue;
			if (!i->udp_sock) continue;
			if (i->local_endpoint.address().is_v4() != ep.address().is_v4())
				continue;

			i->udp_sock->send(ep, p, ec, flags);

			if ((ec == error::would_block
					|| ec == error::try_again)
				&& !i->udp_write_blocked)
			{
				i->udp_write_blocked = true;
				ADD_OUTSTANDING_ASYNC("session_impl::on_udp_writeable");
				i->udp_sock->async_write(std::bind(&session_impl::on_udp_writeable
					, this, std::weak_ptr<udp_socket>(i->udp_sock), _1));
			}
			return;
		}
		ec = boost::asio::error::operation_not_supported;
	}

	void session_impl::on_udp_writeable(std::weak_ptr<udp_socket> s, error_code const& ec)
	{
		COMPLETE_ASYNC("session_impl::on_udp_writeable");
		if (ec) return;

		std::shared_ptr<udp_socket> sock = s.lock();
		if (!sock) return;

		std::list<listen_socket_t>::iterator i = std::find_if(
			m_listen_sockets.begin(), m_listen_sockets.end()
			, [&sock] (listen_socket_t const& ls) { return ls.udp_sock == sock; });

		if (i == m_listen_sockets.end()) return;

		i->udp_write_blocked = false;

		// notify the utp socket manager it can start sending on the socket again
		struct utp_socket_manager& mgr =
#ifdef TORRENT_USE_OPENSSL
			i->ssl ? m_ssl_utp_socket_manager :
#endif
			m_utp_socket_manager;

		mgr.writable();
	}


	void session_impl::on_udp_packet(std::weak_ptr<udp_socket> const& socket
		, bool const ssl, error_code const& ec)
	{
		COMPLETE_ASYNC("session_impl::on_udp_packet");
		if (ec)
		{
			std::shared_ptr<udp_socket> s = socket.lock();
			udp::endpoint ep;
			error_code best_effort;
			if (s) ep = s->local_endpoint(best_effort);

			// don't bubble up operation aborted errors to the user
			if (ec != boost::asio::error::operation_aborted
				&& ec != boost::asio::error::bad_descriptor
				&& m_alerts.should_post<udp_error_alert>())
			{
				m_alerts.emplace_alert<udp_error_alert>(ep, ec);
			}

#ifndef TORRENT_DISABLE_LOGGING
			session_log("UDP error: %s (%d) %s"
				, print_endpoint(ep).c_str(), ec.value(), ec.message().c_str());
#endif
			return;
		}

		m_stats_counters.inc_stats_counter(counters::on_udp_counter);

		std::shared_ptr<udp_socket> s = socket.lock();
		if (!s) return;

		struct utp_socket_manager& mgr =
#ifdef TORRENT_USE_OPENSSL
			ssl ? m_ssl_utp_socket_manager :
#endif
			m_utp_socket_manager;

		for (;;)
		{
			std::array<udp_socket::packet, 50> p;
			error_code err;
			int const num_packets = s->read(p, err);

			for (int i = 0; i < num_packets; ++i)
			{
				udp_socket::packet& packet = p[i];

				if (packet.error)
				{
					// TODO: 3 it would be neat if the utp socket manager would
					// handle ICMP errors too

#ifndef TORRENT_DISABLE_DHT
					if (m_dht)
						m_dht->incoming_error(packet.error, packet.from);
#endif

					m_tracker_manager.incoming_error(packet.error, packet.from);
					continue;
				}

				span<char const> const buf = packet.data;

				// give the uTP socket manager first dis on the packet. Presumably
				// the majority of packets are uTP packets.
				if (!mgr.incoming_packet(packet.from, buf))
				{
					// if it wasn't a uTP packet, try the other users of the UDP
					// socket
					bool handled = false;
#ifndef TORRENT_DISABLE_DHT
					if (m_dht && buf.size() > 20 && buf.front() == 'd' && buf.back() == 'e')
					{
						handled = m_dht->incoming_packet(packet.from, buf.data(), int(buf.size()));
					}
#endif

					if (!handled)
					{
						m_tracker_manager.incoming_packet(packet.from, buf);
					}
				}
			}

			if (err == error::would_block || err == error::try_again)
			{
				// there are no more packets on the socket
				break;
			}

			if (err)
			{
				error_code best_effort;
				udp::endpoint ep = s->local_endpoint(best_effort);

				if (err != boost::asio::error::operation_aborted
					&& m_alerts.should_post<udp_error_alert>())
					m_alerts.emplace_alert<udp_error_alert>(ep, err);

#ifndef TORRENT_DISABLE_LOGGING
				session_log("UDP error: %s (%d) %s"
					, print_endpoint(ep).c_str(), ec.value(), ec.message().c_str());
#endif

				// any error other than these ones are considered fatal errors, and
				// we won't read from the socket again
				if (err != boost::asio::error::host_unreachable
					&& err != boost::asio::error::fault
					&& err != boost::asio::error::connection_reset
					&& err != boost::asio::error::connection_refused
					&& err != boost::asio::error::connection_aborted
					&& err != boost::asio::error::operation_aborted
					&& err != boost::asio::error::network_reset
					&& err != boost::asio::error::network_unreachable
#ifdef _WIN32
					// ERROR_MORE_DATA means the same thing as EMSGSIZE
					&& err != error_code(ERROR_MORE_DATA, system_category())
					&& err != error_code(ERROR_HOST_UNREACHABLE, system_category())
					&& err != error_code(ERROR_PORT_UNREACHABLE, system_category())
					&& err != error_code(ERROR_RETRY, system_category())
					&& err != error_code(ERROR_NETWORK_UNREACHABLE, system_category())
					&& err != error_code(ERROR_CONNECTION_REFUSED, system_category())
					&& err != error_code(ERROR_CONNECTION_ABORTED, system_category())
#endif
					&& err != boost::asio::error::message_size)
				{
					// fatal errors. Don't try to read from this socket again
					mgr.socket_drained();
					return;
				}
				// non-fatal UDP errors get here, we should re-issue the read.
				continue;
			}
		}

		mgr.socket_drained();

		ADD_OUTSTANDING_ASYNC("session_impl::on_udp_packet");
		s->async_read(std::bind(&session_impl::on_udp_packet
			, this, socket, ssl, _1));
	}

	void session_impl::async_accept(std::shared_ptr<tcp::acceptor> const& listener, bool ssl)
	{
		TORRENT_ASSERT(!m_abort);
		std::shared_ptr<socket_type> c = std::make_shared<socket_type>(m_io_service);
		tcp::socket* str = nullptr;

#ifdef TORRENT_USE_OPENSSL
		if (ssl)
		{
			// accept connections initializing the SSL connection to
			// use the generic m_ssl_ctx context. However, since it has
			// the servername callback set on it, we will switch away from
			// this context into a specific torrent once we start handshaking
			c->instantiate<ssl_stream<tcp::socket> >(m_io_service, &m_ssl_ctx);
			str = &c->get<ssl_stream<tcp::socket> >()->next_layer();
		}
		else
#endif
		{
			c->instantiate<tcp::socket>(m_io_service);
			str = c->get<tcp::socket>();
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_accept_connection");

#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASSERT(ssl == is_ssl(*c));
#endif

		listener->async_accept(*str
			, std::bind(&session_impl::on_accept_connection, this, c
			, std::weak_ptr<tcp::acceptor>(listener), _1, ssl));
	}

	void session_impl::on_accept_connection(std::shared_ptr<socket_type> const& s
		, std::weak_ptr<tcp::acceptor> listen_socket, error_code const& e
		, bool const ssl)
	{
		COMPLETE_ASYNC("session_impl::on_accept_connection");
		m_stats_counters.inc_stats_counter(counters::on_accept_counter);
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<tcp::acceptor> listener = listen_socket.lock();
		if (!listener) return;

		if (e == boost::asio::error::operation_aborted) return;

		if (m_abort) return;

		error_code ec;
		if (e)
		{
			tcp::endpoint ep = listener->local_endpoint(ec);
#ifndef TORRENT_DISABLE_LOGGING
			session_log("error accepting connection on '%s': %s"
				, print_endpoint(ep).c_str(), e.message().c_str());
#endif
#ifdef TORRENT_WINDOWS
			// Windows sometimes generates this error. It seems to be
			// non-fatal and we have to do another async_accept.
			if (e.value() == ERROR_SEM_TIMEOUT)
			{
				async_accept(listener, ssl);
				return;
			}
#endif
#ifdef TORRENT_BSD
			// Leopard sometimes generates an "invalid argument" error. It seems to be
			// non-fatal and we have to do another async_accept.
			if (e.value() == EINVAL)
			{
				async_accept(listener, ssl);
				return;
			}
#endif
			if (e == boost::system::errc::too_many_files_open)
			{
				// if we failed to accept an incoming connection
				// because we have too many files open, try again
				// and lower the number of file descriptors used
				// elsewere.
				if (m_settings.get_int(settings_pack::connections_limit) > 10)
				{
					// now, disconnect a random peer
					torrent_map::iterator i = std::max_element(m_torrents.begin(), m_torrents.end()
						, [](torrent_map::value_type const& lhs, torrent_map::value_type const& rhs)
						{ return lhs.second->num_peers() < rhs.second->num_peers(); });

					if (m_alerts.should_post<performance_alert>())
						m_alerts.emplace_alert<performance_alert>(
							torrent_handle(), performance_alert::too_few_file_descriptors);

					if (i != m_torrents.end())
					{
						i->second->disconnect_peers(1, e);
					}

					m_settings.set_int(settings_pack::connections_limit, int(m_connections.size()));
				}
				// try again, but still alert the user of the problem
				async_accept(listener, ssl);
			}
			if (m_alerts.should_post<listen_failed_alert>())
			{
				error_code err;
				m_alerts.emplace_alert<listen_failed_alert>(ep.address().to_string(err)
					, ep, listen_failed_alert::accept, e
					, ssl ? listen_failed_alert::tcp_ssl : listen_failed_alert::tcp);
			}
			return;
		}
		async_accept(listener, ssl);

#ifdef TORRENT_USE_OPENSSL
		if (ssl)
		{
			TORRENT_ASSERT(is_ssl(*s));

			// for SSL connections, incoming_connection() is called
			// after the handshake is done
			ADD_OUTSTANDING_ASYNC("session_impl::ssl_handshake");
			s->get<ssl_stream<tcp::socket>>()->async_accept_handshake(
				std::bind(&session_impl::ssl_handshake, this, _1, s));
			m_incoming_sockets.insert(s);
		}
		else
#endif
		{
			incoming_connection(s);
		}
	}

#ifdef TORRENT_USE_OPENSSL

	void session_impl::on_incoming_utp_ssl(std::shared_ptr<socket_type> const& s)
	{
		TORRENT_ASSERT(is_ssl(*s));

		// for SSL connections, incoming_connection() is called
		// after the handshake is done
		ADD_OUTSTANDING_ASYNC("session_impl::ssl_handshake");
		s->get<ssl_stream<utp_stream>>()->async_accept_handshake(
			std::bind(&session_impl::ssl_handshake, this, _1, s));
		m_incoming_sockets.insert(s);
	}

	// to test SSL connections, one can use this openssl command template:
	//
	// openssl s_client -cert <client-cert>.pem -key <client-private-key>.pem
	//   -CAfile <torrent-cert>.pem  -debug -connect 127.0.0.1:4433 -tls1
	//   -servername <hex-encoded-info-hash>

	void session_impl::ssl_handshake(error_code const& ec, std::shared_ptr<socket_type> s)
	{
		COMPLETE_ASYNC("session_impl::ssl_handshake");
		TORRENT_ASSERT(is_ssl(*s));

		m_incoming_sockets.erase(s);

		error_code e;
		tcp::endpoint endp = s->remote_endpoint(e);
		if (e) return;

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** peer SSL handshake done [ ip: %s ec: %s socket: %s ]"
			, print_endpoint(endp).c_str(), ec.message().c_str(), s->type_name());
#endif

		if (ec)
		{
			if (m_alerts.should_post<peer_error_alert>())
			{
				m_alerts.emplace_alert<peer_error_alert>(torrent_handle(), endp
					, peer_id(), op_ssl_handshake, ec);
			}
			return;
		}

		incoming_connection(s);
	}

#endif // TORRENT_USE_OPENSSL

	void session_impl::incoming_connection(std::shared_ptr<socket_type> const& s)
	{
		TORRENT_ASSERT(is_single_thread());

#ifdef TORRENT_USE_OPENSSL
		// add the current time to the PRNG, to add more unpredictability
		std::uint64_t now = clock_type::now().time_since_epoch().count();
		// assume 12 bits of entropy (i.e. about 8 milliseconds)
		RAND_add(&now, 8, 1.5);
#endif

		if (m_paused)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log(" <== INCOMING CONNECTION [ ignored, paused ]");
#endif
			return;
		}

		error_code ec;
		// we got a connection request!
		tcp::endpoint endp = s->remote_endpoint(ec);

		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log(" <== INCOMING CONNECTION FAILED, could "
				"not retrieve remote endpoint: %s"
				, ec.message().c_str());
#endif
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" <== INCOMING CONNECTION %s type: %s"
			, print_endpoint(endp).c_str(), s->type_name());
#endif

		if (!m_settings.get_bool(settings_pack::enable_incoming_utp)
			&& is_utp(*s))
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("    rejected uTP connection");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
					, endp, peer_blocked_alert::utp_disabled);
			return;
		}

		if (!m_settings.get_bool(settings_pack::enable_incoming_tcp)
			&& s->get<tcp::socket>())
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("    rejected TCP connection");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
					, endp, peer_blocked_alert::tcp_disabled);
			return;
		}

		// if there are outgoing interfaces specified, verify this
		// peer is correctly bound to on of them
		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
		{
			tcp::endpoint local = s->local_endpoint(ec);
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("    rejected connection: (%d) %s", ec.value()
					, ec.message().c_str());
#endif
				return;
			}
			if (!verify_bound_address(local.address()
				, is_utp(*s), ec))
			{
				if (ec)
				{
#ifndef TORRENT_DISABLE_LOGGING
					session_log("    rejected connection, not allowed local interface: (%d) %s"
						, ec.value(), ec.message().c_str());
#endif
					return;
				}

#ifndef TORRENT_DISABLE_LOGGING
				error_code err;
				session_log("    rejected connection, not allowed local interface: %s"
					, local.address().to_string(err).c_str());
#endif
				if (m_alerts.should_post<peer_blocked_alert>())
					m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
						, endp, peer_blocked_alert::invalid_local_interface);
				return;
			}
		}

		// local addresses do not count, since it's likely
		// coming from our own client through local service discovery
		// and it does not reflect whether or not a router is open
		// for incoming connections or not.
		if (!is_local(endp.address()))
			m_stats_counters.set_value(counters::has_incoming_connections, 1);

		// this filter is ignored if a single torrent
		// is set to ignore the filter, since this peer might be
		// for that torrent
		if (m_stats_counters[counters::non_filter_torrents] == 0
			&& m_ip_filter
			&& (m_ip_filter->access(endp.address()) & ip_filter::blocked))
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("filtered blocked ip");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
					, endp, peer_blocked_alert::ip_filter);
			return;
		}

		// check if we have any active torrents
		// if we don't reject the connection
		if (m_torrents.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log(" There are no torrents, disconnect");
#endif
			return;
		}

		// figure out which peer classes this is connections has,
		// to get connection_limit_factor
		peer_class_set pcs;
		set_peer_classes(&pcs, endp.address(), s->type());
		int connection_limit_factor = 0;
		for (int i = 0; i < pcs.num_classes(); ++i)
		{
			int pc = pcs.class_at(i);
			if (m_classes.at(pc) == nullptr) continue;
			int f = m_classes.at(pc)->connection_limit_factor;
			if (connection_limit_factor < f) connection_limit_factor = f;
		}
		if (connection_limit_factor == 0) connection_limit_factor = 100;

		std::uint64_t limit = m_settings.get_int(settings_pack::connections_limit);
		limit = limit * 100 / connection_limit_factor;

		// don't allow more connections than the max setting
		// weighed by the peer class' setting
		bool reject = num_connections() >= limit + m_settings.get_int(settings_pack::connections_slack);

		if (reject)
		{
			if (m_alerts.should_post<peer_disconnected_alert>())
			{
				m_alerts.emplace_alert<peer_disconnected_alert>(torrent_handle(), endp, peer_id()
						, op_bittorrent, s->type()
						, error_code(errors::too_many_connections, get_libtorrent_category())
						, close_no_reason);
			}
#ifndef TORRENT_DISABLE_LOGGING
			session_log("number of connections limit exceeded (conns: %d, limit: %d, slack: %d), connection rejected"
				, num_connections(), m_settings.get_int(settings_pack::connections_limit)
				, m_settings.get_int(settings_pack::connections_slack));
#endif
			return;
		}

		// if we don't have any active torrents, there's no
		// point in accepting this connection. If, however,
		// the setting to start up queued torrents when they
		// get an incoming connection is enabled, we cannot
		// perform this check.
		if (!m_settings.get_bool(settings_pack::incoming_starts_queued_torrents))
		{
			bool has_active_torrent = false;
			for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				if (!i->second->is_torrent_paused())
				{
					has_active_torrent = true;
					break;
				}
			}
			if (!has_active_torrent)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log(" There are no _active_ torrents, disconnect");
#endif
				return;
			}
		}

		m_stats_counters.inc_stats_counter(counters::incoming_connections);

		if (m_alerts.should_post<incoming_connection_alert>())
			m_alerts.emplace_alert<incoming_connection_alert>(s->type(), endp);

		setup_socket_buffers(*s);

		peer_connection_args pack;
		pack.ses = this;
		pack.sett = &m_settings;
		pack.stats_counters = &m_stats_counters;
		pack.allocator = this;
		pack.disk_thread = &m_disk_thread;
		pack.ios = &m_io_service;
		pack.tor = std::weak_ptr<torrent>();
		pack.s = s;
		pack.endp = endp;
		pack.peerinfo = nullptr;

		boost::shared_ptr<peer_connection> c
			= boost::make_shared<bt_peer_connection>(boost::cref(pack)
				, get_peer_id());
#if TORRENT_USE_ASSERTS
		c->m_in_constructor = false;
#endif

		if (!c->is_disconnecting())
		{
			// in case we've exceeded the limit, let this peer know that
			// as soon as it's received the handshake, it needs to either
			// disconnect or pick another peer to disconnect
			if (num_connections() >= limit)
				c->peer_exceeds_limit();

			TORRENT_ASSERT(!c->m_in_constructor);
			m_connections.insert(c);
			c->start();
		}
	}

	void session_impl::setup_socket_buffers(socket_type& s)
	{
		error_code ec;
		set_socket_buffer_size(s, m_settings, ec);
	}

	// if cancel_with_cq is set, the peer connection is
	// currently expected to be scheduled for a connection
	// with the connection queue, and should be cancelled
	// TODO: should this function take a shared_ptr instead?
	void session_impl::close_connection(peer_connection* p
		, error_code const& ec)
	{
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<peer_connection> sp(p->self());

		// someone else is holding a reference, it's important that
		// it's destructed from the network thread. Make sure the
		// last reference is held by the network thread.
		if (!sp.unique())
			m_undead_peers.push_back(sp);

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" CLOSING CONNECTION %s : %s"
			, print_endpoint(p->remote()).c_str(), ec.message().c_str());
#else
		TORRENT_UNUSED(ec);
#endif

		TORRENT_ASSERT(p->is_disconnecting());

		TORRENT_ASSERT(sp.use_count() > 0);

		connection_map::iterator i = m_connections.find(sp);
		// make sure the next disk peer round-robin cursor stays valid
		if (i != m_connections.end()) m_connections.erase(i);
	}

	void session_impl::set_peer_id(peer_id const& id)
	{
		m_peer_id = id;
	}

	void session_impl::set_key(int key)
	{
		m_key = key;
	}

	int session_impl::next_port() const
	{
		int start = m_settings.get_int(settings_pack::outgoing_port);
		int num = m_settings.get_int(settings_pack::num_outgoing_ports);
		std::pair<int, int> out_ports(start, start + num);
		if (m_next_port < out_ports.first || m_next_port > out_ports.second)
			m_next_port = out_ports.first;

		int port = m_next_port;
		++m_next_port;
		if (m_next_port > out_ports.second) m_next_port = out_ports.first;
#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** BINDING OUTGOING CONNECTION [ port: %d ]", port);
#endif
		return port;
	}

	int session_impl::rate_limit(peer_class_t c, int channel) const
	{
		TORRENT_ASSERT(channel >= 0 && channel <= 1);
		if (channel < 0 || channel > 1) return 0;

		peer_class const* pc = m_classes.at(c);
		if (pc == nullptr) return 0;
		return pc->channel[channel].throttle();
	}

	int session_impl::upload_rate_limit(peer_class_t c) const
	{
		return rate_limit(c, peer_connection::upload_channel);
	}

	int session_impl::download_rate_limit(peer_class_t c) const
	{
		return rate_limit(c, peer_connection::download_channel);
	}

	void session_impl::set_rate_limit(peer_class_t c, int channel, int limit)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(limit >= -1);
		TORRENT_ASSERT(channel >= 0 && channel <= 1);

		if (channel < 0 || channel > 1) return;

		peer_class* pc = m_classes.at(c);
		if (pc == nullptr) return;
		if (limit <= 0) limit = 0;
		pc->channel[channel].throttle(limit);
	}

	void session_impl::set_upload_rate_limit(peer_class_t c, int limit)
	{
		set_rate_limit(c, peer_connection::upload_channel, limit);
	}

	void session_impl::set_download_rate_limit(peer_class_t c, int limit)
	{
		set_rate_limit(c, peer_connection::download_channel, limit);
	}

#if TORRENT_USE_ASSERTS
	bool session_impl::has_peer(peer_connection const* p) const
	{
		TORRENT_ASSERT(is_single_thread());
		return std::any_of(m_connections.begin(), m_connections.end()
			, [p] (boost::shared_ptr<peer_connection> const& pr)
			{ return pr.get() == p; });
	}

	bool session_impl::any_torrent_has_peer(peer_connection const* p) const
	{
		for (auto& pe : m_torrents)
			if (pe.second->has_peer(p)) return true;
		return false;
	}
#endif

	void session_impl::sent_bytes(int bytes_payload, int bytes_protocol)
	{
		m_stats_counters.inc_stats_counter(counters::sent_bytes
			, bytes_payload + bytes_protocol);
		m_stats_counters.inc_stats_counter(counters::sent_payload_bytes
			, bytes_payload);

		m_stat.sent_bytes(bytes_payload, bytes_protocol);
	}

	void session_impl::received_bytes(int bytes_payload, int bytes_protocol)
	{
		m_stats_counters.inc_stats_counter(counters::recv_bytes
			, bytes_payload + bytes_protocol);
		m_stats_counters.inc_stats_counter(counters::recv_payload_bytes
			, bytes_payload);

		m_stat.received_bytes(bytes_payload, bytes_protocol);
	}

	void session_impl::trancieve_ip_packet(int bytes, bool ipv6)
	{
		m_stat.trancieve_ip_packet(bytes, ipv6);
	}

	void session_impl::sent_syn(bool ipv6)
	{
		m_stat.sent_syn(ipv6);
	}

	void session_impl::received_synack(bool ipv6)
	{
		m_stat.received_synack(ipv6);
	}

	void session_impl::on_tick(error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_tick");
		m_stats_counters.inc_stats_counter(counters::on_tick_counter);

		TORRENT_ASSERT(is_single_thread());

		// submit all disk jobs when we leave this function
		deferred_submit_jobs();

		aux::update_time_now();
		time_point now = aux::time_now();

		// remove undead peers that only have this list as their reference keeping them alive
		if (!m_undead_peers.empty())
		{
			std::vector<boost::shared_ptr<peer_connection> >::iterator remove_it
				= std::remove_if(m_undead_peers.begin(), m_undead_peers.end()
				, std::bind(&boost::shared_ptr<peer_connection>::unique, _1));
			m_undead_peers.erase(remove_it, m_undead_peers.end());
			if (m_undead_peers.empty())
			{
				// we just removed our last "undead" peer (i.e. a peer connection
				// that had some external reference to it). It's now safe to
				// shut-down
				if (m_abort)
				{
					m_io_service.post(std::bind(&session_impl::abort_stage2, this));
				}
			}
		}

// too expensive
//		INVARIANT_CHECK;

		// we have to keep ticking the utp socket manager
		// until they're all closed
		if (m_abort)
		{
			if (m_utp_socket_manager.num_sockets() == 0
#ifdef TORRENT_USE_OPENSSL
				&& m_ssl_utp_socket_manager.num_sockets() == 0
#endif
				&& m_undead_peers.empty())
			{
				return;
			}
#if defined TORRENT_ASIO_DEBUGGING
			std::fprintf(stderr, "uTP sockets: %d ssl-uTP sockets: %d undead-peers left: %d\n"
				, m_utp_socket_manager.num_sockets()
#ifdef TORRENT_USE_OPENSSL
				, m_ssl_utp_socket_manager.num_sockets()
#else
				, 0
#endif
				, int(m_undead_peers.size()));
#endif
		}

		if (e == boost::asio::error::operation_aborted) return;

		if (e)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("*** TICK TIMER FAILED %s", e.message().c_str());
#endif
			std::abort();
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_tick");
		error_code ec;
		m_timer.expires_at(now + milliseconds(m_settings.get_int(settings_pack::tick_interval)), ec);
		m_timer.async_wait(make_tick_handler(std::bind(&session_impl::on_tick, this, _1)));

		m_download_rate.update_quotas(now - m_last_tick);
		m_upload_rate.update_quotas(now - m_last_tick);

		m_last_tick = now;

		m_utp_socket_manager.tick(now);
#ifdef TORRENT_USE_OPENSSL
		m_ssl_utp_socket_manager.tick(now);
#endif

		// only tick the following once per second
		if (now - m_last_second_tick < seconds(1)) return;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht
			&& m_dht_interval_update_torrents < 40
			&& m_dht_interval_update_torrents != int(m_torrents.size()))
			update_dht_announce_interval();
#endif

		int tick_interval_ms = int(total_milliseconds(now - m_last_second_tick));
		m_last_second_tick = now;
		m_tick_residual += tick_interval_ms - 1000;

		std::int32_t const stime = session_time();
		if (stime > 65000)
		{
			// we're getting close to the point where our timestamps
			// in torrent_peer are wrapping. We need to step all counters back
			// four hours. This means that any timestamp that refers to a time
			// more than 18.2 - 4 = 14.2 hours ago, will be incremented to refer to
			// 14.2 hours ago.

			m_created += hours(4);

			const int four_hours = 60 * 60 * 4;
			for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				i->second->step_session_time(four_hours);
			}
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_ses_extensions[plugins_tick_idx])
		{
			TORRENT_TRY {
				ext->on_tick();
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		// don't do any of the following while we're shutting down
		if (m_abort) return;

		switch (m_settings.get_int(settings_pack::mixed_mode_algorithm))
		{
			case settings_pack::prefer_tcp:
				set_upload_rate_limit(m_tcp_peer_class, 0);
				set_download_rate_limit(m_tcp_peer_class, 0);
				break;
			case settings_pack::peer_proportional:
				{
					int num_peers[2][2] = {{0, 0}, {0, 0}};
					for (connection_map::iterator i = m_connections.begin()
						, end(m_connections.end());i != end; ++i)
					{
						peer_connection& p = *(*i);
						if (p.in_handshake()) continue;
						int protocol = 0;
						if (is_utp(*p.get_socket())) protocol = 1;

						if (p.download_queue().size() + p.request_queue().size() > 0)
							++num_peers[protocol][peer_connection::download_channel];
						if (p.upload_queue().size() > 0)
							++num_peers[protocol][peer_connection::upload_channel];
					}

					peer_class* pc = m_classes.at(m_tcp_peer_class);
					bandwidth_channel* tcp_channel = pc->channel;
					int stat_rate[] = {m_stat.upload_rate(), m_stat.download_rate() };
					// never throttle below this
					int lower_limit[] = {5000, 30000};

					for (int i = 0; i < 2; ++i)
					{
						// if there are no uploading uTP peers, don't throttle TCP up
						if (num_peers[1][i] == 0)
						{
							tcp_channel[i].throttle(0);
						}
						else
						{
							if (num_peers[0][i] == 0) num_peers[0][i] = 1;
							int total_peers = num_peers[0][i] + num_peers[1][i];
							// this are 64 bits since it's multiplied by the number
							// of peers, which otherwise might overflow an int
							std::uint64_t rate = stat_rate[i];
							tcp_channel[i].throttle((std::max)(int(rate * num_peers[0][i] / total_peers), lower_limit[i]));
						}
					}
				}
				break;
		}

		// --------------------------------------------------------------
		// auto managed torrent
		// --------------------------------------------------------------
		if (!m_paused) m_auto_manage_time_scaler--;
		if (m_auto_manage_time_scaler < 0)
		{
			INVARIANT_CHECK;
			m_auto_manage_time_scaler = settings().get_int(settings_pack::auto_manage_interval);
			recalculate_auto_managed_torrents();
		}

		// --------------------------------------------------------------
		// check for incoming connections that might have timed out
		// --------------------------------------------------------------

		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* p = (*i).get();
			++i;
			// ignore connections that already have a torrent, since they
			// are ticked through the torrents' second_tick
			if (!p->associated_torrent().expired()) continue;

			// TODO: have a separate list for these connections, instead of having to loop through all of them
			int timeout = m_settings.get_int(settings_pack::handshake_timeout);
#if TORRENT_USE_I2P
			timeout *= is_i2p(*p->get_socket()) ? 4 : 1;
#endif
			if (m_last_tick - p->connected_time () > seconds(timeout))
				p->disconnect(errors::timed_out, op_bittorrent);
		}

		// --------------------------------------------------------------
		// second_tick every torrent (that wants it)
		// --------------------------------------------------------------

#if TORRENT_DEBUG_STREAMING > 0
		std::printf("\033[2J\033[0;0H");
#endif

		std::vector<torrent*>& want_tick = m_torrent_lists[torrent_want_tick];
		for (int i = 0; i < int(want_tick.size()); ++i)
		{
			torrent& t = *want_tick[i];
			TORRENT_ASSERT(t.want_tick());
			TORRENT_ASSERT(!t.is_aborted());

			t.second_tick(tick_interval_ms);

			// if the call to second_tick caused the torrent
			// to no longer want to be ticked (i.e. it was
			// removed from the list) we need to back up the counter
			// to not miss the torrent after it
			if (!t.want_tick()) --i;
		}

		// TODO: this should apply to all bandwidth channels
		if (m_settings.get_bool(settings_pack::rate_limit_ip_overhead))
		{
			int up_limit = upload_rate_limit(m_global_class);
			int down_limit = download_rate_limit(m_global_class);

			if (down_limit > 0
				&& m_stat.download_ip_overhead() >= down_limit
				&& m_alerts.should_post<performance_alert>())
			{
				m_alerts.emplace_alert<performance_alert>(torrent_handle()
					, performance_alert::download_limit_too_low);
			}

			if (up_limit > 0
				&& m_stat.upload_ip_overhead() >= up_limit
				&& m_alerts.should_post<performance_alert>())
			{
				m_alerts.emplace_alert<performance_alert>(torrent_handle()
					, performance_alert::upload_limit_too_low);
			}
		}

		m_peak_up_rate = (std::max)(m_stat.upload_rate(), m_peak_up_rate);
		m_peak_down_rate = (std::max)(m_stat.download_rate(), m_peak_down_rate);

		m_stat.second_tick(tick_interval_ms);

		// --------------------------------------------------------------
		// scrape paused torrents that are auto managed
		// (unless the session is paused)
		// --------------------------------------------------------------
		if (!m_paused)
		{
			INVARIANT_CHECK;
			--m_auto_scrape_time_scaler;
			if (m_auto_scrape_time_scaler <= 0)
			{
				std::vector<torrent*>& want_scrape = m_torrent_lists[torrent_want_scrape];
				m_auto_scrape_time_scaler = m_settings.get_int(settings_pack::auto_scrape_interval)
					/ (std::max)(1, int(want_scrape.size()));
				if (m_auto_scrape_time_scaler < m_settings.get_int(settings_pack::auto_scrape_min_interval))
					m_auto_scrape_time_scaler = m_settings.get_int(settings_pack::auto_scrape_min_interval);

				if (!want_scrape.empty() && !m_abort)
				{
					if (m_next_scrape_torrent >= int(want_scrape.size()))
						m_next_scrape_torrent = 0;

					torrent& t = *want_scrape[m_next_scrape_torrent];
					TORRENT_ASSERT(t.is_paused() && t.is_auto_managed());

					// false means it's not triggered by the user, but automatically
					// by libtorrent
					t.scrape_tracker(-1, false);

					++m_next_scrape_torrent;
					if (m_next_scrape_torrent >= int(want_scrape.size()))
						m_next_scrape_torrent = 0;

				}
			}
		}

		// --------------------------------------------------------------
		// connect new peers
		// --------------------------------------------------------------

		try_connect_more_peers();

		// --------------------------------------------------------------
		// unchoke set calculations
		// --------------------------------------------------------------
		m_unchoke_time_scaler--;
		if (m_unchoke_time_scaler <= 0 && !m_connections.empty())
		{
			m_unchoke_time_scaler = settings().get_int(settings_pack::unchoke_interval);
			recalculate_unchoke_slots();
		}

		// --------------------------------------------------------------
		// optimistic unchoke calculation
		// --------------------------------------------------------------
		m_optimistic_unchoke_time_scaler--;
		if (m_optimistic_unchoke_time_scaler <= 0)
		{
			m_optimistic_unchoke_time_scaler
				= settings().get_int(settings_pack::optimistic_unchoke_interval);
			recalculate_optimistic_unchoke_slots();
		}

		// --------------------------------------------------------------
		// disconnect peers when we have too many
		// --------------------------------------------------------------
		--m_disconnect_time_scaler;
		if (m_disconnect_time_scaler <= 0)
		{
			m_disconnect_time_scaler = m_settings.get_int(settings_pack::peer_turnover_interval);

			// if the connections_limit is too low, the disconnect
			// logic is disabled, since it is too disruptive
			if (m_settings.get_int(settings_pack::connections_limit) > 5)
			{
				if (num_connections() >= m_settings.get_int(settings_pack::connections_limit)
					* m_settings.get_int(settings_pack::peer_turnover_cutoff) / 100
					&& !m_torrents.empty())
				{
					// every 90 seconds, disconnect the worst peers
					// if we have reached the connection limit
					torrent_map::iterator i = std::max_element(m_torrents.begin(), m_torrents.end()
						, [] (torrent_map::value_type const& lhs, torrent_map::value_type const& rhs)
						{ return lhs.second->num_peers() < rhs.second->num_peers(); });

					TORRENT_ASSERT(i != m_torrents.end());
					int peers_to_disconnect = (std::min)((std::max)(
						int(i->second->num_peers() * m_settings.get_int(settings_pack::peer_turnover) / 100), 1)
						, i->second->num_connect_candidates());
					i->second->disconnect_peers(peers_to_disconnect
						, error_code(errors::optimistic_disconnect, get_libtorrent_category()));
				}
				else
				{
					// if we haven't reached the global max. see if any torrent
					// has reached its local limit
					for (torrent_map::iterator i = m_torrents.begin()
						, end(m_torrents.end()); i != end; ++i)
					{
						std::shared_ptr<torrent> t = i->second;

						// ths disconnect logic is disabled for torrents with
						// too low connection limit
						if (t->num_peers() < t->max_connections()
							* m_settings.get_int(settings_pack::peer_turnover_cutoff) / 100
							|| t->max_connections() < 6)
							continue;

						int peers_to_disconnect = (std::min)((std::max)(int(t->num_peers()
							* m_settings.get_int(settings_pack::peer_turnover) / 100), 1)
							, t->num_connect_candidates());
						t->disconnect_peers(peers_to_disconnect
							, error_code(errors::optimistic_disconnect, get_libtorrent_category()));
					}
				}
			}
		}

		m_tick_residual = m_tick_residual % 1000;
//		m_peer_pool.release_memory();
	}

	namespace {
	// returns the index of the first set bit.
	int log2(std::uint32_t v)
	{
// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
		static const int MultiplyDeBruijnBitPosition[32] =
		{
			0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
			8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
		};

		v |= v >> 1; // first round down to one less than a power of 2
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;

		return MultiplyDeBruijnBitPosition[std::uint32_t(v * 0x07C4ACDDU) >> 27];
	}

	} // anonymous namespace

	void session_impl::received_buffer(int s)
	{
		int index = (std::min)(log2(s >> 3), 17);
		m_stats_counters.inc_stats_counter(counters::socket_recv_size3 + index);
	}

	void session_impl::sent_buffer(int s)
	{
		int index = (std::min)(log2(s >> 3), 17);
		m_stats_counters.inc_stats_counter(counters::socket_send_size3 + index);
	}

	void session_impl::prioritize_connections(std::weak_ptr<torrent> t)
	{
		m_prio_torrents.push_back(std::make_pair(t, 10));
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::add_dht_node(udp::endpoint n)
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_dht) m_dht->add_node(n);
		else m_dht_nodes.push_back(n);
	}

	bool session_impl::has_dht() const
	{
		return m_dht.get() != nullptr;
	}

	void session_impl::prioritize_dht(std::weak_ptr<torrent> t)
	{
		TORRENT_ASSERT(!m_abort);
		if (m_abort) return;

		TORRENT_ASSERT(m_dht);
		m_dht_torrents.push_back(t);
#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<torrent> tor = t.lock();
		if (tor)
			session_log("prioritizing DHT announce: \"%s\"", tor->name().c_str());
#endif
		// trigger a DHT announce right away if we just added a new torrent and
		// there's no back-log. in the timer handler, as long as there are more
		// high priority torrents to be announced to the DHT, it will keep the
		// timer interval short until all torrents have been announced.
		if (m_dht_torrents.size() == 1)
		{
			ADD_OUTSTANDING_ASYNC("session_impl::on_dht_announce");
			error_code ec;
			m_dht_announce_timer.expires_from_now(seconds(0), ec);
			m_dht_announce_timer.async_wait(
				std::bind(&session_impl::on_dht_announce, this, _1));
		}
	}

	void session_impl::on_dht_announce(error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_dht_announce");
		TORRENT_ASSERT(is_single_thread());
		if (e)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("aborting DHT announce timer (%d): %s"
				, e.value(), e.message().c_str());
#endif
			return;
		}

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("aborting DHT announce timer: m_abort set");
#endif
			return;
		}

		if (!m_dht)
		{
			m_dht_torrents.clear();
			return;
		}

		TORRENT_ASSERT(m_dht);

		// announce to DHT every 15 minutes
		int delay = (std::max)(m_settings.get_int(settings_pack::dht_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);

		if (!m_dht_torrents.empty())
		{
			// we have prioritized torrents that need
			// an initial DHT announce. Don't wait too long
			// until we announce those.
			delay = (std::min)(4, delay);
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_announce");
		error_code ec;
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait(
			std::bind(&session_impl::on_dht_announce, this, _1));

		if (!m_dht_torrents.empty())
		{
			std::shared_ptr<torrent> t;
			do
			{
				t = m_dht_torrents.front().lock();
				m_dht_torrents.pop_front();
			} while (!t && !m_dht_torrents.empty());

			if (t)
			{
				t->dht_announce();
				return;
			}
		}
		if (m_torrents.empty()) return;

		if (m_next_dht_torrent == m_torrents.end())
			m_next_dht_torrent = m_torrents.begin();
		m_next_dht_torrent->second->dht_announce();
		// TODO: 2 make a list for torrents that want to be announced on the DHT so we
		// don't have to loop over all torrents, just to find the ones that want to announce
		++m_next_dht_torrent;
		if (m_next_dht_torrent == m_torrents.end())
			m_next_dht_torrent = m_torrents.begin();
	}
#endif

	void session_impl::on_lsd_announce(error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_lsd_announce");
		m_stats_counters.inc_stats_counter(counters::on_lsd_counter);
		TORRENT_ASSERT(is_single_thread());
		if (e) return;

		if (m_abort) return;

		ADD_OUTSTANDING_ASYNC("session_impl::on_lsd_announce");
		// announce on local network every 5 minutes
		int delay = (std::max)(m_settings.get_int(settings_pack::local_service_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		error_code ec;
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait(
			std::bind(&session_impl::on_lsd_announce, this, _1));

		if (m_torrents.empty()) return;

		if (m_next_lsd_torrent == m_torrents.end())
			m_next_lsd_torrent = m_torrents.begin();
		m_next_lsd_torrent->second->lsd_announce();
		++m_next_lsd_torrent;
		if (m_next_lsd_torrent == m_torrents.end())
			m_next_lsd_torrent = m_torrents.begin();
	}

	void session_impl::auto_manage_checking_torrents(std::vector<torrent*>& list
		, int& limit)
	{
		for (std::vector<torrent*>::iterator i = list.begin()
			, end(list.end()); i != end; ++i)
		{
			torrent* t = *i;

			TORRENT_ASSERT(t->state() == torrent_status::checking_files);
			TORRENT_ASSERT(t->is_auto_managed());
			if (limit <= 0)
			{
				t->pause();
			}
			else
			{
				t->resume();
				t->start_checking();
				--limit;
			}
		}
	}

	void session_impl::auto_manage_torrents(std::vector<torrent*>& list
		, int& dht_limit, int& tracker_limit
		, int& lsd_limit, int& hard_limit, int type_limit)
	{
		for (std::vector<torrent*>::iterator i = list.begin()
			, end(list.end()); i != end; ++i)
		{
			torrent* t = *i;

			TORRENT_ASSERT(t->state() != torrent_status::checking_files);

			// inactive torrents don't count (and if you configured them to do so,
			// the torrent won't say it's inactive)
			if (hard_limit > 0 && t->is_inactive())
			{
				t->set_announce_to_dht(--dht_limit >= 0);
				t->set_announce_to_trackers(--tracker_limit >= 0);
				t->set_announce_to_lsd(--lsd_limit >= 0);

				--hard_limit;
#ifndef TORRENT_DISABLE_LOGGING
				if (t->is_torrent_paused())
					t->log_to_all_peers("auto manager starting (inactive) torrent");
#endif
				t->set_paused(false);
				continue;
			}

			if (type_limit > 0 && hard_limit > 0)
			{
				t->set_announce_to_dht(--dht_limit >= 0);
				t->set_announce_to_trackers(--tracker_limit >= 0);
				t->set_announce_to_lsd(--lsd_limit >= 0);

				--hard_limit;
				--type_limit;
#ifndef TORRENT_DISABLE_LOGGING
				if (t->is_torrent_paused())
					t->log_to_all_peers("auto manager starting torrent");
#endif
				t->set_paused(false);
				continue;
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (!t->is_torrent_paused())
				t->log_to_all_peers("auto manager pausing torrent");
#endif
			// use graceful pause for auto-managed torrents
			t->set_paused(true, torrent::flag_graceful_pause
				| torrent::flag_clear_disk_cache);
			t->set_announce_to_dht(false);
			t->set_announce_to_trackers(false);
			t->set_announce_to_lsd(false);
		}
	}

	int session_impl::get_int_setting(int n) const
	{
		int const v = settings().get_int(n);
		if (v < 0) return (std::numeric_limits<int>::max)();
		return v;
	}

	void session_impl::recalculate_auto_managed_torrents()
	{
		INVARIANT_CHECK;

		m_last_auto_manage = time_now();
		m_need_auto_manage = false;

		if (m_paused) return;

		// make copies of the lists of torrents that we want to consider for auto
		// management. We need copies because they will be sorted.
		std::vector<torrent*> checking
			= torrent_list(session_interface::torrent_checking_auto_managed);
		std::vector<torrent*> downloaders
			= torrent_list(session_interface::torrent_downloading_auto_managed);
		std::vector<torrent*> seeds
			= torrent_list(session_interface::torrent_seeding_auto_managed);

		// these counters are set to the number of torrents
		// of each kind we're allowed to have active
		int downloading_limit = get_int_setting(settings_pack::active_downloads);
		int seeding_limit = get_int_setting(settings_pack::active_seeds);
		int checking_limit = get_int_setting(settings_pack::active_checking);
		int dht_limit = get_int_setting(settings_pack::active_dht_limit);
		int tracker_limit = get_int_setting(settings_pack::active_tracker_limit);
		int lsd_limit = get_int_setting(settings_pack::active_lsd_limit);
		int hard_limit = get_int_setting(settings_pack::active_limit);

		// if hard_limit is <= 0, all torrents in these lists should be paused.
		// The order is not relevant
		if (hard_limit > 0)
		{
			// we only need to sort the first n torrents here, where n is the number
			// of checking torrents we allow. The rest of the list is still used to
			// make sure the remaining torrents are paused, but their order is not
			// relevant
			std::partial_sort(checking.begin(), checking.begin() +
				(std::min)(checking_limit, int(checking.size())), checking.end()
				, [](torrent const* lhs, torrent const* rhs)
				{ return lhs->sequence_number() < rhs->sequence_number(); });

			std::partial_sort(downloaders.begin(), downloaders.begin() +
				(std::min)(hard_limit, int(downloaders.size())), downloaders.end()
				, [](torrent const* lhs, torrent const* rhs)
				{ return lhs->sequence_number() < rhs->sequence_number(); });

			std::partial_sort(seeds.begin(), seeds.begin() +
				(std::min)(hard_limit, int(seeds.size())), seeds.end()
				, [this](torrent const* lhs, torrent const* rhs)
				{ return lhs->seed_rank(m_settings) > rhs->seed_rank(m_settings); });
		}

		auto_manage_checking_torrents(checking, checking_limit);

		if (settings().get_bool(settings_pack::auto_manage_prefer_seeds))
		{
			auto_manage_torrents(seeds, dht_limit, tracker_limit, lsd_limit
				, hard_limit, seeding_limit);
			auto_manage_torrents(downloaders, dht_limit, tracker_limit, lsd_limit
				, hard_limit, downloading_limit);
		}
		else
		{
			auto_manage_torrents(downloaders, dht_limit, tracker_limit, lsd_limit
				, hard_limit, downloading_limit);
			auto_manage_torrents(seeds, dht_limit, tracker_limit, lsd_limit
				, hard_limit, seeding_limit);
		}
	}

	namespace {
		uint64_t const priority_undetermined = std::numeric_limits<uint64_t>::max() - 1;

		struct opt_unchoke_candidate
		{
			explicit opt_unchoke_candidate(boost::shared_ptr<peer_connection> const* tp)
				: peer(tp)
			{}

			boost::shared_ptr<peer_connection> const* peer;
#ifndef TORRENT_DISABLE_EXTENSIONS
			// this is mutable because comparison functors passed to std::partial_sort
			// are not supposed to modify the elements they are sorting. Here the mutation
			// being applied is idempotent so it should not pose a problem.
			mutable uint64_t ext_priority = priority_undetermined;
#endif
		};

		struct last_optimistic_unchoke_cmp
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			explicit last_optimistic_unchoke_cmp(std::vector<std::shared_ptr<plugin>>& ps)
				: plugins(ps)
			{}

			std::vector<std::shared_ptr<plugin>>& plugins;
#endif

			uint64_t get_ext_priority(opt_unchoke_candidate const& peer) const
			{
#ifndef TORRENT_DISABLE_EXTENSIONS
				if (peer.ext_priority == priority_undetermined)
				{
					peer.ext_priority = std::numeric_limits<uint64_t>::max();
					for (auto& e : plugins)
					{
						uint64_t const priority = e->get_unchoke_priority(peer_connection_handle(*peer.peer));
						peer.ext_priority = (std::min)(priority, peer.ext_priority);
					}
				}
				return peer.ext_priority;
#else
				TORRENT_UNUSED(peer);
				return std::numeric_limits<uint64_t>::max();
#endif
			}

			bool operator()(opt_unchoke_candidate const& l
				, opt_unchoke_candidate const& r) const
			{
				torrent_peer* pil = (*l.peer)->peer_info_struct();
				torrent_peer* pir = (*r.peer)->peer_info_struct();
				if (pil->last_optimistically_unchoked
					!= pir->last_optimistically_unchoked)
				{
					return pil->last_optimistically_unchoked
						< pir->last_optimistically_unchoked;
				}
				else
				{
					return get_ext_priority(l) < get_ext_priority(r);
				}
			}
		};
	}

	void session_impl::recalculate_optimistic_unchoke_slots()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());
		if (m_stats_counters[counters::num_unchoke_slots] == 0) return;

		std::vector<opt_unchoke_candidate> opt_unchoke;

		// collect the currently optimistically unchoked peers here, so we can
		// choke them when we've found new optimistic unchoke candidates.
		std::vector<torrent_peer*> prev_opt_unchoke;

		// TODO: 3 it would probably make sense to have a separate list of peers
		// that are eligible for optimistic unchoke, similar to the torrents
		// perhaps this could even iterate over the pool allocators of
		// torrent_peer objects. It could probably be done in a single pass and
		// collect the n best candidates. maybe just a queue of peers would make
		// even more sense, just pick the next peer in the queue for unchoking. It
		// would be O(1).
		for (auto& i : m_connections)
		{
			peer_connection* p = i.get();
			TORRENT_ASSERT(p);
			torrent_peer* pi = p->peer_info_struct();
			if (!pi) continue;
			if (pi->web_seed) continue;

			if (pi->optimistically_unchoked)
			{
				prev_opt_unchoke.push_back(pi);
			}

			torrent* t = p->associated_torrent().lock().get();
			if (!t) continue;

			// TODO: 3 peers should know whether their torrent is paused or not,
			// instead of having to ask it over and over again
			if (t->is_paused()) continue;

			if (!p->is_connecting()
				&& !p->is_disconnecting()
				&& p->is_peer_interested()
				&& t->free_upload_slots()
				&& (p->is_choked() || pi->optimistically_unchoked)
				&& !p->ignore_unchoke_slots()
				&& t->valid_metadata())
			{
				opt_unchoke.emplace_back(&i);
			}
		}

		// find the peers that has been waiting the longest to be optimistically
		// unchoked

		int num_opt_unchoke = m_settings.get_int(settings_pack::num_optimistic_unchoke_slots);
		int const allowed_unchoke_slots = m_stats_counters[counters::num_unchoke_slots];
		if (num_opt_unchoke == 0) num_opt_unchoke = (std::max)(1, allowed_unchoke_slots / 5);
		if (num_opt_unchoke > int(opt_unchoke.size())) num_opt_unchoke =
			int(opt_unchoke.size());

		// find the n best optimistic unchoke candidates
		std::partial_sort(opt_unchoke.begin()
			, opt_unchoke.begin() + num_opt_unchoke
			, opt_unchoke.end()
#ifndef TORRENT_DISABLE_EXTENSIONS
			, last_optimistic_unchoke_cmp(m_ses_extensions[plugins_optimistic_unchoke_idx])
#else
			, last_optimistic_unchoke_cmp()
#endif
			);

		// unchoke the first num_opt_unchoke peers in the candidate set
		// and make sure that the others are choked
		auto opt_unchoke_end = opt_unchoke.begin()
			+ num_opt_unchoke;

		for (auto i = opt_unchoke.begin(); i != opt_unchoke_end; ++i)
		{
			torrent_peer* pi = (*i->peer)->peer_info_struct();
			peer_connection* p = static_cast<peer_connection*>(pi->connection);
			if (pi->optimistically_unchoked)
			{
#ifndef TORRENT_DISABLE_LOGGING
					p->peer_log(peer_log_alert::info, "OPTIMISTIC UNCHOKE"
						, "already unchoked | session-time: %d"
						, pi->last_optimistically_unchoked);
#endif
				TORRENT_ASSERT(!pi->connection->is_choked());
				// remove this peer from prev_opt_unchoke, to prevent us from
				// choking it later. This peer gets another round of optimistic
				// unchoke
				std::vector<torrent_peer*>::iterator existing =
					std::find(prev_opt_unchoke.begin(), prev_opt_unchoke.end(), pi);
				TORRENT_ASSERT(existing != prev_opt_unchoke.end());
				prev_opt_unchoke.erase(existing);
			}
			else
			{
				TORRENT_ASSERT(p->is_choked());
				std::shared_ptr<torrent> t = p->associated_torrent().lock();
				bool ret = t->unchoke_peer(*p, true);
				TORRENT_ASSERT(ret);
				if (ret)
				{
					pi->optimistically_unchoked = true;
					m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic);
					pi->last_optimistically_unchoked = std::uint16_t(session_time());
#ifndef TORRENT_DISABLE_LOGGING
					p->peer_log(peer_log_alert::info, "OPTIMISTIC UNCHOKE"
						, "session-time: %d", pi->last_optimistically_unchoked);
#endif
				}
			}
		}

		// now, choke all the previous optimistically unchoked peers
		for (torrent_peer* pi : prev_opt_unchoke)
		{
			TORRENT_ASSERT(pi->optimistically_unchoked);
			peer_connection* p = static_cast<peer_connection*>(pi->connection);
			std::shared_ptr<torrent> t = p->associated_torrent().lock();
			pi->optimistically_unchoked = false;
			m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
			t->choke_peer(*p);
		}

		// if we have too many unchoked peers now, we need to trigger the regular
		// choking logic to choke some
		if (m_stats_counters[counters::num_unchoke_slots]
			< m_stats_counters[counters::num_peers_up_unchoked_all])
		{
			m_unchoke_time_scaler = 0;
		}
	}

	void session_impl::try_connect_more_peers()
	{
		if (m_abort) return;

		if (num_connections() >= m_settings.get_int(settings_pack::connections_limit))
			return;

		// this is the maximum number of connections we will
		// attempt this tick
		int max_connections = m_settings.get_int(settings_pack::connection_speed);

		// zero connections speeds are allowed, we just won't make any connections
		if (max_connections <= 0) return;

		// this loop will "hand out" connection_speed to the torrents, in a round
		// robin fashion, so that every torrent is equally likely to connect to a
		// peer

		// boost connections are connections made by torrent connection
		// boost, which are done immediately on a tracker response. These
		// connections needs to be deducted from this second
		if (m_boost_connections > 0)
		{
			if (m_boost_connections > max_connections)
			{
				m_boost_connections -= max_connections;
				max_connections = 0;
			}
			else
			{
				max_connections -= m_boost_connections;
				m_boost_connections = 0;
			}
		}

		// TODO: use a lower limit than m_settings.connections_limit
		// to allocate the to 10% or so of connection slots for incoming
		// connections
		int limit = m_settings.get_int(settings_pack::connections_limit)
			- num_connections();

		// this logic is here to smooth out the number of new connection
		// attempts over time, to prevent connecting a large number of
		// sockets, wait 10 seconds, and then try again
		if (m_settings.get_bool(settings_pack::smooth_connects) && max_connections > (limit+1) / 2)
			max_connections = (limit+1) / 2;

		std::vector<torrent*>& want_peers_download = m_torrent_lists[torrent_want_peers_download];
		std::vector<torrent*>& want_peers_finished = m_torrent_lists[torrent_want_peers_finished];

		// if no torrent want any peers, just return
		if (want_peers_download.empty() && want_peers_finished.empty()) return;

		// if we don't have any connection attempt quota, return
		if (max_connections <= 0) return;

		INVARIANT_CHECK;

		int steps_since_last_connect = 0;
		int num_torrents = int(want_peers_finished.size() + want_peers_download.size());
		for (;;)
		{
			if (m_next_downloading_connect_torrent >= int(want_peers_download.size()))
				m_next_downloading_connect_torrent = 0;

			if (m_next_finished_connect_torrent >= int(want_peers_finished.size()))
				m_next_finished_connect_torrent = 0;

			torrent* t = nullptr;
			// there are prioritized torrents. Pick one of those
			while (!m_prio_torrents.empty())
			{
				t = m_prio_torrents.front().first.lock().get();
				--m_prio_torrents.front().second;
				if (m_prio_torrents.front().second > 0
					&& t != nullptr
					&& t->want_peers()) break;
				m_prio_torrents.pop_front();
				t = nullptr;
			}

			if (t == nullptr)
			{
				if ((m_download_connect_attempts >= m_settings.get_int(
						settings_pack::connect_seed_every_n_download)
					&& want_peers_finished.size())
						|| want_peers_download.empty())
				{
					// pick a finished torrent to give a peer to
					t = want_peers_finished[m_next_finished_connect_torrent];
					TORRENT_ASSERT(t->want_peers_finished());
					m_download_connect_attempts = 0;
					++m_next_finished_connect_torrent;
				}
				else
				{
					// pick a downloading torrent to give a peer to
					t = want_peers_download[m_next_downloading_connect_torrent];
					TORRENT_ASSERT(t->want_peers_download());
					++m_download_connect_attempts;
					++m_next_downloading_connect_torrent;
				}
			}

			TORRENT_ASSERT(t->want_peers());
			TORRENT_ASSERT(!t->is_torrent_paused());

			TORRENT_TRY
			{
				if (t->try_connect_peer())
				{
					--max_connections;
					steps_since_last_connect = 0;
					m_stats_counters.inc_stats_counter(counters::connection_attempts);
				}
			}
			TORRENT_CATCH(std::bad_alloc&)
			{
				// we ran out of memory trying to connect to a peer
				// lower the global limit to the number of peers
				// we already have
				m_settings.set_int(settings_pack::connections_limit, num_connections());
				if (m_settings.get_int(settings_pack::connections_limit) < 2)
					m_settings.set_int(settings_pack::connections_limit, 2);
			}

			++steps_since_last_connect;

			// if there are no more free connection slots, abort
			if (max_connections == 0) return;
			// there are no more torrents that want peers
			if (want_peers_download.empty() && want_peers_finished.empty()) break;
			// if we have gone a whole loop without
			// handing out a single connection, break
			if (steps_since_last_connect > num_torrents + 1) break;
			// maintain the global limit on number of connections
			if (num_connections() >= m_settings.get_int(settings_pack::connections_limit)) break;
		}
	}

	void session_impl::recalculate_unchoke_slots()
	{
		TORRENT_ASSERT(is_single_thread());
		INVARIANT_CHECK;

		time_point const now = aux::time_now();
		time_duration const unchoke_interval = now - m_last_choke;
		m_last_choke = now;

		// build list of all peers that are
		// unchokable.
		// TODO: 3 there should be a pre-calculated list of all peers eligible for
		// unchoking
		std::vector<peer_connection*> peers;
		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::shared_ptr<peer_connection> p = *i;
			TORRENT_ASSERT(p);
			++i;
			torrent* const t = p->associated_torrent().lock().get();
			torrent_peer* const pi = p->peer_info_struct();

			if (p->ignore_unchoke_slots() || t == nullptr || pi == nullptr
				|| pi->web_seed || t->is_paused())
			{
				p->reset_choke_counters();
				continue;
			}

			if (!p->is_peer_interested()
				|| p->is_disconnecting()
				|| p->is_connecting())
			{
				// this peer is not unchokable. So, if it's unchoked
				// already, make sure to choke it.
				if (p->is_choked())
				{
					p->reset_choke_counters();
					continue;
				}
				if (pi && pi->optimistically_unchoked)
				{
					m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
					pi->optimistically_unchoked = false;
					// force a new optimistic unchoke
					m_optimistic_unchoke_time_scaler = 0;
					// TODO: post a message to have this happen
					// immediately instead of waiting for the next tick
				}
				t->choke_peer(*p);
				p->reset_choke_counters();
				continue;
			}

			peers.push_back(p.get());
		}

		// the unchoker wants an estimate of our upload rate capacity
		// (used by bittyrant)
		int max_upload_rate = upload_rate_limit(m_global_class);
		if (m_settings.get_int(settings_pack::choking_algorithm)
			== settings_pack::bittyrant_choker
			&& max_upload_rate == 0)
		{
			// we don't know at what rate we can upload. If we have a
			// measurement of the peak, use that + 10kB/s, otherwise
			// assume 20 kB/s
			max_upload_rate = (std::max)(20000, m_peak_up_rate + 10000);
			if (m_alerts.should_post<performance_alert>())
				m_alerts.emplace_alert<performance_alert>(torrent_handle()
					, performance_alert::bittyrant_with_no_uplimit);
		}

		int const allowed_upload_slots = unchoke_sort(peers, max_upload_rate
			, unchoke_interval, m_settings);

		m_stats_counters.set_value(counters::num_unchoke_slots
			, allowed_upload_slots);

#ifndef TORRENT_DISABLE_LOGGING
		session_log("RECALCULATE UNCHOKE SLOTS: [ peers: %d "
			"eligible-peers: %d"
			" max_upload_rate: %d"
			" allowed-slots: %d ]"
			, int(m_connections.size())
			, int(peers.size())
			, max_upload_rate
			, allowed_upload_slots);
#endif

		int const unchoked_counter_optimistic
			= m_stats_counters[counters::num_peers_up_unchoked_optimistic];
		int const num_opt_unchoke = (unchoked_counter_optimistic == 0)
			? (std::max)(1, allowed_upload_slots / 5) : unchoked_counter_optimistic;

		int unchoke_set_size = allowed_upload_slots - num_opt_unchoke;

		// go through all the peers and unchoke the first ones and choke
		// all the other ones.
		for (std::vector<peer_connection*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_connection* p = *i;
			TORRENT_ASSERT(p);
			TORRENT_ASSERT(!p->ignore_unchoke_slots());

			// this will update the m_uploaded_at_last_unchoke
			p->reset_choke_counters();

			torrent* t = p->associated_torrent().lock().get();
			TORRENT_ASSERT(t);

			if (unchoke_set_size > 0)
			{
				// yes, this peer should be unchoked
				if (p->is_choked())
				{
					if (!t->unchoke_peer(*p))
						continue;
				}

				--unchoke_set_size;

				TORRENT_ASSERT(p->peer_info_struct());
				if (p->peer_info_struct()->optimistically_unchoked)
				{
					// force a new optimistic unchoke
					// since this one just got promoted into the
					// proper unchoke set
					m_optimistic_unchoke_time_scaler = 0;
					p->peer_info_struct()->optimistically_unchoked = false;
					m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
				}
			}
			else
			{
				// no, this peer should be choked
				TORRENT_ASSERT(p->peer_info_struct());
				if (!p->is_choked() && !p->peer_info_struct()->optimistically_unchoked)
					t->choke_peer(*p);
			}
		}
	}

	void session_impl::cork_burst(peer_connection* p)
	{
		TORRENT_ASSERT(is_single_thread());
		if (p->is_corked()) return;
		p->cork_socket();
		m_delayed_uncorks.push_back(p);
	}

	void session_impl::do_delayed_uncork()
	{
		m_stats_counters.inc_stats_counter(counters::on_disk_counter);
		TORRENT_ASSERT(is_single_thread());
		for (std::vector<peer_connection*>::iterator i = m_delayed_uncorks.begin()
			, end(m_delayed_uncorks.end()); i != end; ++i)
		{
			(*i)->uncork_socket();
		}
		m_delayed_uncorks.clear();
	}

	std::shared_ptr<torrent> session_impl::delay_load_torrent(sha1_hash const& info_hash
		, peer_connection* pc)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& e : m_ses_extensions[plugins_all_idx])
		{
			add_torrent_params p;
			if (e->on_unknown_torrent(info_hash, peer_connection_handle(pc->self()), p))
			{
				error_code ec;
				torrent_handle handle = add_torrent(p, ec);

				return handle.native_handle();
			}
		}
#else
		TORRENT_UNUSED(pc);
		TORRENT_UNUSED(info_hash);
#endif
		return std::shared_ptr<torrent>();
	}

	// the return value from this function is valid only as long as the
	// session is locked!
	std::weak_ptr<torrent> session_impl::find_torrent(sha1_hash const& info_hash) const
	{
		TORRENT_ASSERT(is_single_thread());

		torrent_map::const_iterator i = m_torrents.find(info_hash);
#if TORRENT_USE_INVARIANT_CHECKS
		for (torrent_map::const_iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			torrent* p = boost::get_pointer(j->second);
			TORRENT_ASSERT(p);
		}
#endif
		if (i != m_torrents.end()) return i->second;
		return std::weak_ptr<torrent>();
	}

	void session_impl::insert_torrent(sha1_hash const& ih, std::shared_ptr<torrent> const& t
		, std::string uuid)
	{
		m_torrents.insert(std::make_pair(ih, t));
#ifndef TORRENT_NO_DEPRECATE
		//deprecated in 1.2
		if (!uuid.empty()) m_uuids.insert(std::make_pair(uuid, t));
#else
		TORRENT_UNUSED(uuid);
#endif

		TORRENT_ASSERT(m_torrents.size() >= m_torrent_lru.size());
	}

	void session_impl::set_queue_position(torrent* me, int p)
	{
		if (p >= 0 && me->queue_position() == -1)
		{
			for (session_impl::torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t->queue_position() >= p)
				{
					t->set_queue_position_impl(t->queue_position()+1);
					t->state_updated();
				}
				if (t->queue_position() >= p) t->set_queue_position_impl(t->queue_position()+1);
			}
			++m_max_queue_pos;
			me->set_queue_position_impl((std::min)(m_max_queue_pos, p));
		}
		else if (p < 0)
		{
			TORRENT_ASSERT(me->queue_position() >= 0);
			TORRENT_ASSERT(p == -1);
			for (session_impl::torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t == me) continue;
				if (t->queue_position() == -1) continue;
				if (t->queue_position() >= me->queue_position())
				{
					t->set_queue_position_impl(t->queue_position()-1);
					t->state_updated();
				}
			}
			--m_max_queue_pos;
			me->set_queue_position_impl(p);
		}
		else if (p < me->queue_position())
		{
			for (session_impl::torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t == me) continue;
				if (t->queue_position() == -1) continue;
				if (t->queue_position() >= p
					&& t->queue_position() < me->queue_position())
				{
					t->set_queue_position_impl(t->queue_position()+1);
					t->state_updated();
				}
			}
			me->set_queue_position_impl(p);
		}
		else if (p > me->queue_position())
		{
			for (session_impl::torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				int pos = t->queue_position();
				if (t == me) continue;
				if (pos == -1) continue;

				if (pos <= p
						&& pos > me->queue_position()
						&& pos != -1)
				{
					t->set_queue_position_impl(t->queue_position()-1);
					t->state_updated();
				}

			}
			me->set_queue_position_impl((std::min)(m_max_queue_pos, p));
		}

		trigger_auto_manage();
	}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	torrent const* session_impl::find_encrypted_torrent(sha1_hash const& info_hash
		, sha1_hash const& xor_mask)
	{
		sha1_hash obfuscated = info_hash;
		obfuscated ^= xor_mask;

		torrent_map::iterator i = m_obfuscated_torrents.find(obfuscated);
		if (i == m_obfuscated_torrents.end()) return nullptr;
		return i->second.get();
	}
#endif

#ifndef TORRENT_NO_DEPRECATE
	//deprecated in 1.2
	std::weak_ptr<torrent> session_impl::find_torrent(std::string const& uuid) const
	{
		TORRENT_ASSERT(is_single_thread());

		std::map<std::string, std::shared_ptr<torrent> >::const_iterator i
			= m_uuids.find(uuid);
		if (i != m_uuids.end()) return i->second;
		return std::weak_ptr<torrent>();
	}
#endif

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::vector<std::shared_ptr<torrent> > session_impl::find_collection(
		std::string const& collection) const
	{
		std::vector<std::shared_ptr<torrent> > ret;
		for (session_impl::torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			std::shared_ptr<torrent> t = i->second;
			if (!t) continue;
			std::vector<std::string> const& c = t->torrent_file().collections();
			if (std::count(c.begin(), c.end(), collection) == 0) continue;
			ret.push_back(t);
		}
		return ret;
	}
#endif //TORRENT_DISABLE_MUTABLE_TORRENTS

	namespace {

	// returns true if lhs is a better disconnect candidate than rhs
	bool compare_disconnect_torrent(session_impl::torrent_map::value_type const& lhs
		, session_impl::torrent_map::value_type const& rhs)
	{
		// a torrent with 0 peers is never a good disconnect candidate
		// since there's nothing to disconnect
		if ((lhs.second->num_peers() == 0) != (rhs.second->num_peers() == 0))
			return lhs.second->num_peers() != 0;

		// other than that, always prefer to disconnect peers from seeding torrents
		// in order to not harm downloading ones
		if (lhs.second->is_seed() != rhs.second->is_seed())
			return lhs.second->is_seed();

		return lhs.second->num_peers() > rhs.second->num_peers();
	}

	} // anonymous namespace

	std::weak_ptr<torrent> session_impl::find_disconnect_candidate_torrent() const
	{
		aux::session_impl::torrent_map::const_iterator i = std::min_element(m_torrents.begin(), m_torrents.end()
			, &compare_disconnect_torrent);

		TORRENT_ASSERT(i != m_torrents.end());
		if (i == m_torrents.end()) return std::shared_ptr<torrent>();

		return i->second;
	}

#ifndef TORRENT_DISABLE_LOGGING
	TORRENT_FORMAT(2,3)
	void session_impl::session_log(char const* fmt, ...) const
	{
		if (!m_alerts.should_post<log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		session_vlog(fmt, v);
		va_end(v);
	}

	TORRENT_FORMAT(2, 0)
	void session_impl::session_vlog(char const* fmt, va_list& v) const
	{
		if (!m_alerts.should_post<log_alert>()) return;
		m_alerts.emplace_alert<log_alert>(fmt, v);
	}
#endif

	void session_impl::get_torrent_status(std::vector<torrent_status>* ret
		, std::function<bool(torrent_status const&)> const& pred
		, std::uint32_t flags) const
	{
		for (torrent_map::const_iterator i
			= m_torrents.begin(), end(m_torrents.end());
			i != end; ++i)
		{
			if (i->second->is_aborted()) continue;
			torrent_status st;
			i->second->status(&st, flags);
			if (!pred(st)) continue;
			ret->push_back(st);
		}
	}

	void session_impl::refresh_torrent_status(std::vector<torrent_status>* ret
		, std::uint32_t flags) const
	{
		for (std::vector<torrent_status>::iterator i
			= ret->begin(), end(ret->end()); i != end; ++i)
		{
			std::shared_ptr<torrent> t = i->handle.m_torrent.lock();
			if (!t) continue;
			t->status(&*i, flags);
		}
	}

	void session_impl::post_torrent_updates(std::uint32_t flags)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());

		std::vector<torrent*>& state_updates
			= m_torrent_lists[aux::session_impl::torrent_state_updates];

#if TORRENT_USE_ASSERTS
		m_posting_torrent_updates = true;
#endif

		std::vector<torrent_status> status;
		status.reserve(state_updates.size());

		// TODO: it might be a nice feature here to limit the number of torrents
		// to send in a single update. By just posting the first n torrents, they
		// would nicely be round-robined because the torrent lists are always
		// pushed back. Perhaps the status_update_alert could even have a fixed
		// array of n entries rather than a vector, to further improve memory
		// locality.
		for (std::vector<torrent*>::iterator i = state_updates.begin()
			, end(state_updates.end()); i != end; ++i)
		{
			torrent* t = *i;
			TORRENT_ASSERT(t->m_links[aux::session_impl::torrent_state_updates].in_list());
			status.push_back(torrent_status());
			// querying accurate download counters may require
			// the torrent to be loaded. Loading a torrent, and evicting another
			// one will lead to calling state_updated(), which screws with
			// this list while we're working on it, and break things
			t->status(&status.back(), flags);
			t->clear_in_state_update();
		}
		state_updates.clear();

#if TORRENT_USE_ASSERTS
		m_posting_torrent_updates = false;
#endif

		m_alerts.emplace_alert<state_update_alert>(std::move(status));
	}

	void session_impl::post_session_stats()
	{
		m_disk_thread.update_stats_counters(m_stats_counters);

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_dht->update_stats_counters(m_stats_counters);
#endif

		m_stats_counters.set_value(counters::sent_ip_overhead_bytes
			, m_stat.total_transfer(stat::upload_ip_protocol));

		m_stats_counters.set_value(counters::recv_ip_overhead_bytes
			, m_stat.total_transfer(stat::download_ip_protocol));

		m_stats_counters.set_value(counters::limiter_up_queue
			, m_upload_rate.queue_size());
		m_stats_counters.set_value(counters::limiter_down_queue
			, m_download_rate.queue_size());

		m_stats_counters.set_value(counters::limiter_up_bytes
			, m_upload_rate.queued_bytes());
		m_stats_counters.set_value(counters::limiter_down_bytes
			, m_download_rate.queued_bytes());

		m_alerts.emplace_alert<session_stats_alert>(m_stats_counters);
	}

	void session_impl::post_dht_stats()
	{
		std::vector<dht_lookup> requests;
		std::vector<dht_routing_bucket> table;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_dht->dht_status(table, requests);
#endif

		m_alerts.emplace_alert<dht_stats_alert>(std::move(table), std::move(requests));
	}

	std::vector<torrent_handle> session_impl::get_torrents() const
	{
		std::vector<torrent_handle> ret;

		for (torrent_map::const_iterator i
			= m_torrents.begin(), end(m_torrents.end());
			i != end; ++i)
		{
			if (i->second->is_aborted()) continue;
			ret.push_back(torrent_handle(i->second));
		}
		return ret;
	}

	torrent_handle session_impl::find_torrent_handle(sha1_hash const& info_hash)
	{
		return torrent_handle(find_torrent(info_hash));
	}

	void session_impl::async_add_torrent(add_torrent_params* params)
	{
		if (string_begins_no_case("file://", params->url.c_str()) && !params->ti)
		{
			m_disk_thread.async_load_torrent(params
				, std::bind(&session_impl::on_async_load_torrent, this, _1));
			return;
		}

		error_code ec;
		torrent_handle handle = add_torrent(*params, ec);
		delete params;
	}

	void session_impl::on_async_load_torrent(disk_io_job const* j)
	{
		add_torrent_params* params = static_cast<add_torrent_params*>(j->requester);
		error_code ec;
		torrent_handle handle;
		if (j->error.ec)
		{
			ec = j->error.ec;
			m_alerts.emplace_alert<add_torrent_alert>(handle, *params, ec);
		}
		else
		{
			params->url.clear();
			params->ti = std::shared_ptr<torrent_info>(j->buffer.torrent_file);
			handle = add_torrent(*params, ec);
		}

		delete params;
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session_impl::add_extensions_to_torrent(
		std::shared_ptr<torrent> const& torrent_ptr, void* userdata)
	{
		for (auto& e : m_ses_extensions[plugins_all_idx])
		{
			std::shared_ptr<torrent_plugin> tp(e->new_torrent(
				torrent_ptr->get_handle(), userdata));
			if (tp) torrent_ptr->add_extension(std::move(tp));
		}
	}
#endif

	torrent_handle session_impl::add_torrent(add_torrent_params const& p
		, error_code& ec)
	{
		// params is updated by add_torrent_impl()
		add_torrent_params params = p;
		std::shared_ptr<torrent> torrent_ptr;
		bool added;
		boost::tie(torrent_ptr, added) = add_torrent_impl(params, ec);

		torrent_handle const handle(torrent_ptr);
		m_alerts.emplace_alert<add_torrent_alert>(handle, params, ec);

		if (!torrent_ptr) return handle;

		// params.info_hash should have been initialized by add_torrent_impl()
		TORRENT_ASSERT(params.info_hash != sha1_hash(nullptr));

#ifndef TORRENT_DISABLE_DHT
		if (params.ti)
		{
			for (auto const& n : params.ti->nodes())
				add_dht_node_name(n);
		}
#endif

		if (m_alerts.should_post<torrent_added_alert>())
			m_alerts.emplace_alert<torrent_added_alert>(handle);

		// if this was an existing torrent, we can't start it again, or add
		// another set of plugins etc. we're done
		if (!added) return handle;

		torrent_ptr->set_ip_filter(m_ip_filter);
		torrent_ptr->start(params);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : params.extensions)
		{
			std::shared_ptr<torrent_plugin> tp(ext(handle, params.userdata));
			if (tp) torrent_ptr->add_extension(std::move(tp));
		}

		add_extensions_to_torrent(torrent_ptr, params.userdata);
#endif

		sha1_hash next_lsd(nullptr);
		sha1_hash next_dht(nullptr);
		if (m_next_lsd_torrent != m_torrents.end())
			next_lsd = m_next_lsd_torrent->first;
#ifndef TORRENT_DISABLE_DHT
		if (m_next_dht_torrent != m_torrents.end())
			next_dht = m_next_dht_torrent->first;
#endif
		float load_factor = m_torrents.load_factor();

		m_torrents.insert(std::make_pair(params.info_hash, torrent_ptr));

		TORRENT_ASSERT(m_torrents.size() >= m_torrent_lru.size());

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		static char const req2[4] = {'r', 'e', 'q', '2'};
		hasher h(req2);
		h.update(params.info_hash);
		// this is SHA1("req2" + info-hash), used for
		// encrypted hand shakes
		m_obfuscated_torrents.insert(std::make_pair(h.final(), torrent_ptr));
#endif

		if (torrent_ptr->is_pinned() == false)
		{
			evict_torrents_except(torrent_ptr.get());
			bump_torrent(torrent_ptr.get());
		}

		// if this insert made the hash grow, the iterators became invalid
		// we need to reset them
		if (m_torrents.load_factor() < load_factor)
		{
			// this indicates the hash table re-hashed
			if (!next_lsd.is_all_zeros())
				m_next_lsd_torrent = m_torrents.find(next_lsd);
#ifndef TORRENT_DISABLE_DHT
			if (!next_dht.is_all_zeros())
				m_next_dht_torrent = m_torrents.find(next_dht);
#endif
		}

#ifndef TORRENT_NO_DEPRECATE
		//deprecated in 1.2
		if (!params.uuid.empty() || !params.url.empty())
			m_uuids.insert(std::make_pair(params.uuid.empty()
				? params.url : params.uuid, torrent_ptr));
#endif

		// recalculate auto-managed torrents sooner (or put it off)
		// if another torrent will be added within one second from now
		// we want to put it off again anyway. So that while we're adding
		// a boat load of torrents, we postpone the recalculation until
		// we're done adding them all (since it's kind of an expensive operation)
		if (params.flags & add_torrent_params::flag_auto_managed)
		{
			const int max_downloading = settings().get_int(settings_pack::active_downloads);
			const int max_seeds = settings().get_int(settings_pack::active_seeds);
			const int max_active = settings().get_int(settings_pack::active_limit);

			const int num_downloading
			= int(torrent_list(session_interface::torrent_downloading_auto_managed).size());
			const int num_seeds
			= int(torrent_list(session_interface::torrent_seeding_auto_managed).size());
			const int num_active = num_downloading + num_seeds;

			// there's no point in triggering the auto manage logic early if we
			// don't have a reason to believe anything will change. It's kind of
			// expensive.
			if ((num_downloading < max_downloading
				|| num_seeds < max_seeds)
				&& num_active < max_active)
			{
				trigger_auto_manage();
			}
		}

		return handle;
	}

	std::pair<std::shared_ptr<torrent>, bool>
	session_impl::add_torrent_impl(
		add_torrent_params& params
		, error_code& ec)
	{
		TORRENT_ASSERT(!params.save_path.empty());

		using ptr_t = std::shared_ptr<torrent>;

		if (string_begins_no_case("magnet:", params.url.c_str()))
		{
			parse_magnet_uri(params.url, params, ec);
			if (ec) return std::make_pair(ptr_t(), false);
			params.url.clear();
		}

		if (string_begins_no_case("file://", params.url.c_str()) && !params.ti)
		{
			std::string const filename = resolve_file_url(params.url);
			auto t = std::make_shared<torrent_info>(filename, std::ref(ec), 0);
			if (ec) return std::make_pair(ptr_t(), false);
			params.url.clear();
			params.ti = t;
		}

		if (params.ti && !params.ti->is_valid())
		{
			ec = errors::no_metadata;
			return std::make_pair(ptr_t(), false);
		}

		if (params.ti && params.ti->is_valid() && params.ti->num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			return std::make_pair(ptr_t(), false);
		}

#ifndef TORRENT_DISABLE_DHT
		// add params.dht_nodes to the DHT, if enabled
		if (!params.dht_nodes.empty())
		{
			for (std::vector<std::pair<std::string, int> >::const_iterator i = params.dht_nodes.begin()
				, end(params.dht_nodes.end()); i != end; ++i)
			{
				add_dht_node_name(*i);
			}
		}
#endif

		INVARIANT_CHECK;

		if (is_aborted())
		{
			ec = errors::session_is_closing;
			return std::make_pair(ptr_t(), false);
		}

		// figure out the info hash of the torrent and make sure params.info_hash
		// is set correctly
		if (params.ti) params.info_hash = params.ti->info_hash();
#ifndef TORRENT_NO_DEPRECATE
		//deprecated in 1.2
		else if (!params.url.empty())
		{
			// in order to avoid info-hash collisions, for
			// torrents where we don't have an info-hash, but
			// just a URL, set the temporary info-hash to the
			// hash of the URL. This will be changed once we
			// have the actual .torrent file
			params.info_hash = hasher(&params.url[0], int(params.url.size())).final();
		}
#endif

		if (params.info_hash == sha1_hash(nullptr))
		{
			ec = errors::missing_info_hash_in_uri;
			return std::make_pair(ptr_t(), false);
		}

		// is the torrent already active?
		std::shared_ptr<torrent> torrent_ptr = find_torrent(params.info_hash).lock();
#ifndef TORRENT_NO_DEPRECATE
		//deprecated in 1.2
		if (!torrent_ptr && !params.uuid.empty()) torrent_ptr = find_torrent(params.uuid).lock();
		// if we still can't find the torrent, look for it by url
		if (!torrent_ptr && !params.url.empty())
		{
			torrent_map::iterator i = std::find_if(m_torrents.begin(), m_torrents.end()
				, [&params](torrent_map::value_type const& te)
				{ return te.second->url() == params.url; });
			if (i != m_torrents.end())
				torrent_ptr = i->second;
		}
#endif

		if (torrent_ptr)
		{
			if ((params.flags & add_torrent_params::flag_duplicate_is_error) == 0)
			{
#ifndef TORRENT_NO_DEPRECATE
				//deprecated in 1.2
				if (!params.uuid.empty() && torrent_ptr->uuid().empty())
					torrent_ptr->set_uuid(params.uuid);
				if (!params.url.empty() && torrent_ptr->url().empty())
					torrent_ptr->set_url(params.url);
#endif
				return std::make_pair(torrent_ptr, false);
			}

			ec = errors::duplicate_torrent;
			return std::make_pair(ptr_t(), false);
		}

		int queue_pos = ++m_max_queue_pos;

		torrent_ptr = std::make_shared<torrent>(std::ref(*this)
			, 16 * 1024, queue_pos, m_paused
			, boost::cref(params), boost::cref(params.info_hash));

		return std::make_pair(torrent_ptr, true);
	}

	void session_impl::update_outgoing_interfaces()
	{
		INVARIANT_CHECK;
		std::string net_interfaces = m_settings.get_str(settings_pack::outgoing_interfaces);

		// declared in string_util.hpp
		parse_comma_separated_string(net_interfaces, m_outgoing_interfaces);
	}

	tcp::endpoint session_impl::bind_outgoing_socket(socket_type& s, address
		const& remote_address, error_code& ec) const
	{
		tcp::endpoint bind_ep(address_v4(), 0);
		if (m_settings.get_int(settings_pack::outgoing_port) > 0)
		{
#ifdef TORRENT_WINDOWS
			s.set_option(exclusive_address_use(true), ec);
#endif
			s.set_option(tcp::acceptor::reuse_address(true), ec);
			// ignore errors because the underlying socket may not
			// be opened yet. This happens when we're routing through
			// a proxy. In that case, we don't yet know the address of
			// the proxy server, and more importantly, we don't know
			// the address family of its address. This means we can't
			// open the socket yet. The socks abstraction layer defers
			// opening it.
			ec.clear();
			bind_ep.port(next_port());
		}

		if (!m_outgoing_interfaces.empty())
		{
			if (m_interface_index >= m_outgoing_interfaces.size()) m_interface_index = 0;
			std::string const& ifname = m_outgoing_interfaces[m_interface_index++];

			if (ec) return bind_ep;

			bind_ep.address(bind_socket_to_device(m_io_service, s
				, remote_address.is_v4()
					? boost::asio::ip::tcp::v4()
					: boost::asio::ip::tcp::v6()
				, ifname.c_str(), bind_ep.port(), ec));
			return bind_ep;
		}

		// if we're not binding to a specific interface, bind
		// to the same protocol family as the target endpoint
		if (is_any(bind_ep.address()))
		{
#if TORRENT_USE_IPV6
			if (remote_address.is_v6())
				bind_ep.address(address_v6::any());
			else
#endif
				bind_ep.address(address_v4::any());
		}

		s.bind(bind_ep, ec);
		return bind_ep;
	}

	// verify that the given local address satisfies the requirements of
	// the outgoing interfaces. i.e. that one of the allowed outgoing
	// interfaces has this address. For uTP sockets, which are all backed
	// by an unconnected udp socket, we won't be able to tell what local
	// address is used for this peer's packets, in that case, just make
	// sure one of the allowed interfaces exists and maybe that it's the
	// default route. For systems that have SO_BINDTODEVICE, it should be
	// enough to just know that one of the devices exist
	bool session_impl::verify_bound_address(address const& addr, bool utp
		, error_code& ec)
	{
		TORRENT_UNUSED(utp);

		// we have specific outgoing interfaces specified. Make sure the
		// local endpoint for this socket is bound to one of the allowed
		// interfaces. the list can be a mixture of interfaces and IP
		// addresses.
		for (int i = 0; i < int(m_outgoing_interfaces.size()); ++i)
		{
			error_code err;
			address ip = address::from_string(m_outgoing_interfaces[i].c_str(), err);
			if (err) continue;
			if (ip == addr) return true;
		}

		// we didn't find the address as an IP in the interface list. Now,
		// resolve which device (if any) has this IP address.
		std::string device = device_for_address(addr, m_io_service, ec);
		if (ec) return false;

		// if no device was found to have this address, we fail
		if (device.empty()) return false;

		for (int i = 0; i < int(m_outgoing_interfaces.size()); ++i)
		{
			if (m_outgoing_interfaces[i] == device) return true;
		}

		return false;
	}

	void session_impl::remove_torrent(const torrent_handle& h, int options)
	{
		INVARIANT_CHECK;

		std::shared_ptr<torrent> tptr = h.m_torrent.lock();
		if (!tptr) return;

		m_alerts.emplace_alert<torrent_removed_alert>(tptr->get_handle()
			, tptr->info_hash());

		remove_torrent_impl(tptr, options);

		tptr->abort();
		tptr->set_queue_position(-1);
	}

	void session_impl::remove_torrent_impl(std::shared_ptr<torrent> tptr
		, int options)
	{
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.2
		// remove from uuid list
		if (!tptr->uuid().empty())
		{
			std::map<std::string, std::shared_ptr<torrent> >::iterator j
				= m_uuids.find(tptr->uuid());
			if (j != m_uuids.end()) m_uuids.erase(j);
		}
#endif

		torrent_map::iterator i =
			m_torrents.find(tptr->torrent_file().info_hash());

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.2
		// this torrent might be filed under the URL-hash
		if (i == m_torrents.end() && !tptr->url().empty())
		{
			std::string const& url = tptr->url();
			i = m_torrents.find(hasher(url).final());
		}
#endif

		if (i == m_torrents.end()) return;

		torrent& t = *i->second;
		if (options)
		{
			if (!t.delete_files(options))
			{
				if (m_alerts.should_post<torrent_delete_failed_alert>())
					m_alerts.emplace_alert<torrent_delete_failed_alert>(t.get_handle()
						, error_code(), t.torrent_file().info_hash());
			}
		}

		if (m_torrent_lru.size() > 0
			&& (t.prev != nullptr || t.next != nullptr || m_torrent_lru.front() == &t))
			m_torrent_lru.erase(&t);

		TORRENT_ASSERT(t.prev == nullptr && t.next == nullptr);

		tptr->update_gauge();

#if TORRENT_USE_ASSERTS
		sha1_hash i_hash = t.torrent_file().info_hash();
#endif
#ifndef TORRENT_DISABLE_DHT
		if (i == m_next_dht_torrent)
			++m_next_dht_torrent;
#endif
		if (i == m_next_lsd_torrent)
			++m_next_lsd_torrent;

		m_torrents.erase(i);

		TORRENT_ASSERT(m_torrents.size() >= m_torrent_lru.size());

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		static char const req2[4] = {'r', 'e', 'q', '2'};
		hasher h(req2);
		h.update(tptr->info_hash());
		m_obfuscated_torrents.erase(h.final());
#endif

#ifndef TORRENT_DISABLE_DHT
		if (m_next_dht_torrent == m_torrents.end())
			m_next_dht_torrent = m_torrents.begin();
#endif
		if (m_next_lsd_torrent == m_torrents.end())
			m_next_lsd_torrent = m_torrents.begin();

		// this torrent may open up a slot for a queued torrent
		trigger_auto_manage();

		TORRENT_ASSERT(m_torrents.find(i_hash) == m_torrents.end());
	}

#ifndef TORRENT_NO_DEPRECATE
	namespace
	{
		listen_interface_t set_ssl_flag(listen_interface_t in)
		{
			in.ssl = true;
			return in;
		}
	}

	void session_impl::update_ssl_listen()
	{
		INVARIANT_CHECK;

		// this function maps the previous functionality of just setting the ssl
		// listen port in order to enable the ssl listen sockets, to the new
		// mechanism where SSL sockets are specified in listen_interfaces.
		std::vector<listen_interface_t> current_ifaces;
		parse_listen_interfaces(m_settings.get_str(settings_pack::listen_interfaces)
			, current_ifaces);
		// these are the current interfaces we have, first remove all the SSL
		// interfaces
		current_ifaces.erase(std::remove_if(current_ifaces.begin(), current_ifaces.end()
			, std::bind(&listen_interface_t::ssl, _1)), current_ifaces.end());

		int const ssl_listen_port = m_settings.get_int(settings_pack::ssl_listen);

		// setting a port of 0 means to disable listening on SSL, so just update
		// the interface list with the new list, and we're done
		if (ssl_listen_port == 0)
		{
			m_settings.set_str(settings_pack::listen_interfaces
				, print_listen_interfaces(current_ifaces));
			return;
		}

		std::vector<listen_interface_t> new_ifaces;
		std::transform(current_ifaces.begin(), current_ifaces.end()
			, std::back_inserter(new_ifaces), &set_ssl_flag);

		current_ifaces.insert(current_ifaces.end(), new_ifaces.begin(), new_ifaces.end());

		m_settings.set_str(settings_pack::listen_interfaces
			, print_listen_interfaces(current_ifaces));
	}
#endif // TORRENT_NO_DEPRECATE

	void session_impl::update_listen_interfaces()
	{
		INVARIANT_CHECK;

		std::string net_interfaces = m_settings.get_str(settings_pack::listen_interfaces);
		std::vector<listen_interface_t> new_listen_interfaces;

		// declared in string_util.hpp
		parse_listen_interfaces(net_interfaces, new_listen_interfaces);

#ifndef TORRENT_DISABLE_LOGGING
		session_log("update listen interfaces: %s", net_interfaces.c_str());
#endif

		m_listen_interfaces = new_listen_interfaces;
	}

	void session_impl::update_privileged_ports()
	{
		if (m_settings.get_bool(settings_pack::no_connect_privileged_ports))
		{
			m_port_filter.add_rule(0, 1024, port_filter::blocked);

			// Close connections whose endpoint is filtered
			// by the new ip-filter
			for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
				i->second->port_filter_updated();
		}
		else
		{
			m_port_filter.add_rule(0, 1024, 0);
		}
	}

	void session_impl::update_auto_sequential()
	{
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->update_auto_sequential();
	}

	void session_impl::update_max_failcount()
	{
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->update_max_failcount();
		}
	}

	void session_impl::update_proxy()
	{
		// in case we just set a socks proxy, we might have to
		// open the socks incoming connection
		if (!m_socks_listen_socket) open_new_incoming_socks_connection();
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->udp_sock->set_proxy_settings(proxy());
		}
	}

	void session_impl::update_upnp()
	{
		if (m_settings.get_bool(settings_pack::enable_upnp))
			start_upnp();
		else
			stop_upnp();
	}

	void session_impl::update_natpmp()
	{
		if (m_settings.get_bool(settings_pack::enable_natpmp))
			start_natpmp();
		else
			stop_natpmp();
	}

	void session_impl::update_lsd()
	{
		if (m_settings.get_bool(settings_pack::enable_lsd))
			start_lsd();
		else
			stop_lsd();
	}

	void session_impl::update_dht()
	{
#ifndef TORRENT_DISABLE_DHT
		if (m_settings.get_bool(settings_pack::enable_dht))
			start_dht();
		else
			stop_dht();
#endif
	}

	void session_impl::update_peer_fingerprint()
	{
		// ---- generate a peer id ----
		std::string print = m_settings.get_str(settings_pack::peer_fingerprint);
		if (print.size() > 20) print.resize(20);

		// the client's fingerprint
		std::copy(print.begin(), print.begin() + print.length(), m_peer_id.begin());
		if (print.length() < 20)
		{
			url_random(m_peer_id.data() + print.length(), m_peer_id.data() + 20);
		}
	}

	void session_impl::update_dht_bootstrap_nodes()
	{
#ifndef TORRENT_DISABLE_DHT
		std::string const& node_list = m_settings.get_str(settings_pack::dht_bootstrap_nodes);
		std::vector<std::pair<std::string, int> > nodes;
		parse_comma_separated_string_port(node_list, nodes);

		for (int i = 0; i < nodes.size(); ++i)
		{
			add_dht_router(nodes[i]);
		}
#endif
	}

	void session_impl::update_count_slow()
	{
		error_code ec;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->on_inactivity_tick(ec);
		}
	}

	std::uint16_t session_impl::listen_port() const
	{
		// if peer connections are set up to be received over a socks
		// proxy, and it's the same one as we're using for the tracker
		// just tell the tracker the socks5 port we're listening on
		if (m_socks_listen_socket && m_socks_listen_socket->is_open())
			return m_socks_listen_socket->local_endpoint().port();

		// if not, don't tell the tracker anything if we're in force_proxy
		// mode. We don't want to leak our listen port since it can
		// potentially identify us if it is leaked elsewere
		if (m_settings.get_bool(settings_pack::force_proxy)) return 0;
		if (m_listen_sockets.empty()) return 0;
		return m_listen_sockets.front().tcp_external_port;
	}

	// TODO: 2 this function should be removed and users need to deal with the
	// more generic case of having multiple ssl ports
	std::uint16_t session_impl::ssl_listen_port() const
	{
#ifdef TORRENT_USE_OPENSSL
		// if peer connections are set up to be received over a socks
		// proxy, and it's the same one as we're using for the tracker
		// just tell the tracker the socks5 port we're listening on
		if (m_socks_listen_socket && m_socks_listen_socket->is_open())
			return m_socks_listen_port;

		// if not, don't tell the tracker anything if we're in force_proxy
		// mode. We don't want to leak our listen port since it can
		// potentially identify us if it is leaked elsewere
		if (m_settings.get_bool(settings_pack::force_proxy)) return 0;
		for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->ssl) return i->tcp_external_port;
		}
#endif
		return 0;
	}

	void session_impl::announce_lsd(sha1_hash const& ih, int port, bool broadcast)
	{
		// use internal listen port for local peers
		if (m_lsd)
			m_lsd->announce(ih, port, broadcast);
	}

	void session_impl::on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih)
	{
		m_stats_counters.inc_stats_counter(counters::on_lsd_peer_counter);
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = find_torrent(ih).lock();
		if (!t) return;
		// don't add peers from lsd to private torrents
		if (t->torrent_file().priv() || (t->torrent_file().is_i2p()
			&& !m_settings.get_bool(settings_pack::allow_i2p_mixed))) return;

#ifndef TORRENT_DISABLE_LOGGING
		session_log("added peer from local discovery: %s", print_endpoint(peer).c_str());
#endif
		t->add_peer(peer, peer_info::lsd);
		t->do_connect_boost();

		if (m_alerts.should_post<lsd_peer_alert>())
			m_alerts.emplace_alert<lsd_peer_alert>(t->get_handle(), peer);
	}

	// TODO: perhaps this function should not exist when logging is disabled
	void session_impl::on_port_map_log(
		char const* msg, int map_transport)
	{
#ifndef TORRENT_DISABLE_LOGGING
		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);
		// log message
		if (m_alerts.should_post<portmap_log_alert>())
			m_alerts.emplace_alert<portmap_log_alert>(map_transport, msg);
#else
		TORRENT_UNUSED(msg);
		TORRENT_UNUSED(map_transport);
#endif
	}

	namespace {
		bool find_tcp_port_mapping(int transport, int mapping, listen_socket_t const& ls)
		{
			return ls.tcp_port_mapping[transport] == mapping;
		}

		bool find_udp_port_mapping(int transport, int mapping, listen_socket_t const& ls)
		{
			return ls.udp_port_mapping[transport] == mapping;
		}
	}

	// transport is 0 for NAT-PMP and 1 for UPnP
	void session_impl::on_port_mapping(int mapping, address const& ip, int port
		, int const protocol, error_code const& ec, int map_transport)
	{
		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);

		if (ec && m_alerts.should_post<portmap_error_alert>())
		{
			m_alerts.emplace_alert<portmap_error_alert>(mapping
				, map_transport, ec);
		}

		// look through our listen sockets to see if this mapping is for one of
		// them (it could also be a user mapping)

		std::list<listen_socket_t>::iterator ls
			= std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, std::bind(find_tcp_port_mapping, map_transport, mapping, _1));

		bool tcp = true;
		if (ls == m_listen_sockets.end())
		{
			ls = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
				, std::bind(find_udp_port_mapping, map_transport, mapping, _1));
			tcp = false;
		}

		if (ls != m_listen_sockets.end())
		{
			if (ip != address())
			{
				// TODO: 1 report the proper address of the router as the source IP of
				// this vote of our external address, instead of the empty address
				set_external_address(ip, source_router, address());
			}

			ls->external_address = ip;
			if (tcp) ls->tcp_external_port = port;
			else ls->udp_external_port = port;
		}

		if (!ec && m_alerts.should_post<portmap_alert>())
		{
			m_alerts.emplace_alert<portmap_alert>(mapping, port
				, map_transport, protocol == natpmp::udp
				? portmap_alert::udp : portmap_alert::tcp);
		}
	}

#ifndef TORRENT_NO_DEPRECATE
	session_status session_impl::status() const
	{
//		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());

		session_status s;

		s.optimistic_unchoke_counter = m_optimistic_unchoke_time_scaler;
		s.unchoke_counter = m_unchoke_time_scaler;
		s.num_dead_peers = int(m_undead_peers.size());

		s.num_peers = m_stats_counters[counters::num_peers_connected];
		s.num_unchoked = m_stats_counters[counters::num_peers_up_unchoked_all];
		s.allowed_upload_slots = m_stats_counters[counters::num_unchoke_slots];

		s.num_torrents
			= m_stats_counters[counters::num_checking_torrents]
			+ m_stats_counters[counters::num_stopped_torrents]
			+ m_stats_counters[counters::num_queued_seeding_torrents]
			+ m_stats_counters[counters::num_queued_download_torrents]
			+ m_stats_counters[counters::num_upload_only_torrents]
			+ m_stats_counters[counters::num_downloading_torrents]
			+ m_stats_counters[counters::num_seeding_torrents]
			+ m_stats_counters[counters::num_error_torrents];

		s.num_paused_torrents
			= m_stats_counters[counters::num_stopped_torrents]
			+ m_stats_counters[counters::num_error_torrents]
			+ m_stats_counters[counters::num_queued_seeding_torrents]
			+ m_stats_counters[counters::num_queued_download_torrents];

		s.total_redundant_bytes = m_stats_counters[counters::recv_redundant_bytes];
		s.total_failed_bytes = m_stats_counters[counters::recv_failed_bytes];

		s.up_bandwidth_queue = m_stats_counters[counters::limiter_up_queue];
		s.down_bandwidth_queue = m_stats_counters[counters::limiter_down_queue];

		s.up_bandwidth_bytes_queue = m_stats_counters[counters::limiter_up_bytes];
		s.down_bandwidth_bytes_queue = m_stats_counters[counters::limiter_down_bytes];

		s.disk_write_queue = m_stats_counters[counters::num_peers_down_disk];
		s.disk_read_queue = m_stats_counters[counters::num_peers_up_disk];

		s.has_incoming_connections = m_stats_counters[counters::has_incoming_connections] != 0;

		// total
		s.download_rate = m_stat.download_rate();
		s.total_upload = m_stat.total_upload();
		s.upload_rate = m_stat.upload_rate();
		s.total_download = m_stat.total_download();

		// payload
		s.payload_download_rate = m_stat.transfer_rate(stat::download_payload);
		s.total_payload_download = m_stat.total_transfer(stat::download_payload);
		s.payload_upload_rate = m_stat.transfer_rate(stat::upload_payload);
		s.total_payload_upload = m_stat.total_transfer(stat::upload_payload);

		// IP-overhead
		s.ip_overhead_download_rate = m_stat.transfer_rate(stat::download_ip_protocol);
		s.total_ip_overhead_download = m_stat.total_transfer(stat::download_ip_protocol);
		s.ip_overhead_upload_rate = m_stat.transfer_rate(stat::upload_ip_protocol);
		s.total_ip_overhead_upload = m_stat.total_transfer(stat::upload_ip_protocol);

		// tracker
		s.total_tracker_download = m_stats_counters[counters::recv_tracker_bytes];
		s.total_tracker_upload = m_stats_counters[counters::sent_tracker_bytes];

		// dht
		s.total_dht_download = m_stats_counters[counters::dht_bytes_in];
		s.total_dht_upload = m_stats_counters[counters::dht_bytes_out];

		// deprecated
		s.tracker_download_rate = 0;
		s.tracker_upload_rate = 0;
		s.dht_download_rate = 0;
		s.dht_upload_rate = 0;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			m_dht->dht_status(s);
		}
		else
#endif
		{
			s.dht_nodes = 0;
			s.dht_node_cache = 0;
			s.dht_torrents = 0;
			s.dht_global_nodes = 0;
			s.dht_total_allocations = 0;
		}

		s.utp_stats.packet_loss = m_stats_counters[counters::utp_packet_loss];
		s.utp_stats.timeout = m_stats_counters[counters::utp_timeout];
		s.utp_stats.packets_in = m_stats_counters[counters::utp_packets_in];
		s.utp_stats.packets_out = m_stats_counters[counters::utp_packets_out];
		s.utp_stats.fast_retransmit = m_stats_counters[counters::utp_fast_retransmit];
		s.utp_stats.packet_resend = m_stats_counters[counters::utp_packet_resend];
		s.utp_stats.samples_above_target = m_stats_counters[counters::utp_samples_above_target];
		s.utp_stats.samples_below_target = m_stats_counters[counters::utp_samples_below_target];
		s.utp_stats.payload_pkts_in = m_stats_counters[counters::utp_payload_pkts_in];
		s.utp_stats.payload_pkts_out = m_stats_counters[counters::utp_payload_pkts_out];
		s.utp_stats.invalid_pkts_in = m_stats_counters[counters::utp_invalid_pkts_in];
		s.utp_stats.redundant_pkts_in = m_stats_counters[counters::utp_redundant_pkts_in];

		s.utp_stats.num_idle = m_stats_counters[counters::num_utp_idle];
		s.utp_stats.num_syn_sent = m_stats_counters[counters::num_utp_syn_sent];
		s.utp_stats.num_connected = m_stats_counters[counters::num_utp_connected];
		s.utp_stats.num_fin_sent = m_stats_counters[counters::num_utp_fin_sent];
		s.utp_stats.num_close_wait = m_stats_counters[counters::num_utp_close_wait];

		// this loop is potentially expensive. It could be optimized by
		// simply keeping a global counter
		int peerlist_size = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			peerlist_size += i->second->num_known_peers();
		}

		s.peerlist_size = peerlist_size;

		return s;
	}
#endif // TORRENT_NO_DEPRECATE

#ifndef TORRENT_DISABLE_DHT

	void session_impl::start_dht()
	{ start_dht(m_dht_state); }

	namespace {

		void on_bootstrap(alert_manager& alerts)
		{
			if (alerts.should_post<dht_bootstrap_alert>())
				alerts.emplace_alert<dht_bootstrap_alert>();
		}
	}

	void session_impl::start_dht(entry const& startup_state)
	{
		INVARIANT_CHECK;

		stop_dht();

		// postpone starting the DHT if we're still resolving the DHT router
		if (m_outstanding_router_lookups > 0) return;

		m_dht_storage = m_dht_storage_constructor(m_dht_settings);
		m_dht = std::make_shared<dht::dht_tracker>(
			static_cast<dht_observer*>(this)
			, m_io_service
			, std::bind(&session_impl::send_udp_packet, this, false, _1, _2, _3, _4)
			, m_dht_settings
			, m_stats_counters
			, *m_dht_storage
			, startup_state);

		for (auto const& n : m_dht_router_nodes)
		{
			m_dht->add_router_node(n);
		}

		for (auto const& n : m_dht_nodes)
		{
			m_dht->add_node(n);
		}
		m_dht_nodes.clear();

		m_dht->start(startup_state, std::bind(&on_bootstrap, std::ref(m_alerts)));
	}

	void session_impl::stop_dht()
	{
		if (m_dht)
		{
			m_dht->stop();
			m_dht.reset();
		}

		m_dht_storage.reset();
	}

	void session_impl::set_dht_settings(dht_settings const& settings)
	{
		m_dht_settings = settings;
	}

	void session_impl::set_dht_storage(dht::dht_storage_constructor_type sc)
	{
		m_dht_storage_constructor = sc;
	}

#ifndef TORRENT_NO_DEPRECATE
	entry session_impl::dht_state() const
	{
		if (!m_dht) return entry();
		return m_dht->state();
	}

	void session_impl::start_dht_deprecated(entry const& startup_state)
	{
		m_settings.set_bool(settings_pack::enable_dht, true);
		start_dht(startup_state);
	}
#endif

	void session_impl::add_dht_node_name(std::pair<std::string, int> const& node)
	{
		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_name_lookup");
		m_host_resolver.async_resolve(node.first, resolver_interface::abort_on_shutdown
			, std::bind(&session_impl::on_dht_name_lookup
				, this, _1, _2, node.second));
	}

	void session_impl::on_dht_name_lookup(error_code const& e
		, std::vector<address> const& addresses, int port)
	{
		COMPLETE_ASYNC("session_impl::on_dht_name_lookup");

		if (e)
		{
			if (m_alerts.should_post<dht_error_alert>())
				m_alerts.emplace_alert<dht_error_alert>(
					dht_error_alert::hostname_lookup, e);
			return;
		}

		for (std::vector<address>::const_iterator i = addresses.begin()
			, end(addresses.end()); i != end; ++i)
		{
			udp::endpoint ep(*i, port);
			add_dht_node(ep);
		}
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_router_name_lookup");
		++m_outstanding_router_lookups;
		m_host_resolver.async_resolve(node.first, resolver_interface::abort_on_shutdown
			, std::bind(&session_impl::on_dht_router_name_lookup
				, this, _1, _2, node.second));
	}

	void session_impl::on_dht_router_name_lookup(error_code const& e
		, std::vector<address> const& addresses, int port)
	{
		COMPLETE_ASYNC("session_impl::on_dht_router_name_lookup");
		--m_outstanding_router_lookups;

		if (e)
		{
			if (m_alerts.should_post<dht_error_alert>())
				m_alerts.emplace_alert<dht_error_alert>(
					dht_error_alert::hostname_lookup, e);

			if (m_outstanding_router_lookups == 0) update_dht();
			return;
		}


		for (std::vector<address>::const_iterator i = addresses.begin()
			, end(addresses.end()); i != end; ++i)
		{
			// router nodes should be added before the DHT is started (and bootstrapped)
			udp::endpoint ep(*i, port);
			if (m_dht) m_dht->add_router_node(ep);
			m_dht_router_nodes.push_back(ep);
		}

		if (m_outstanding_router_lookups == 0) update_dht();
	}

	// callback for dht_immutable_get
	void session_impl::get_immutable_callback(sha1_hash target
		, dht::item const& i)
	{
		TORRENT_ASSERT(!i.is_mutable());
		m_alerts.emplace_alert<dht_immutable_item_alert>(target, i.value());
	}

	void session_impl::dht_get_immutable_item(sha1_hash const& target)
	{
		if (!m_dht) return;
		m_dht->get_item(target, std::bind(&session_impl::get_immutable_callback
			, this, target, _1));
	}

	// callback for dht_mutable_get
	void session_impl::get_mutable_callback(dht::item const& i
		, bool const authoritative)
	{
		TORRENT_ASSERT(i.is_mutable());
		m_alerts.emplace_alert<dht_mutable_item_alert>(i.pk().bytes
			, i.sig().bytes, i.seq().value
			, i.salt(), i.value(), authoritative);
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	// TODO: 3 use public_key here instead of std::array
	void session_impl::dht_get_mutable_item(std::array<char, 32> key
		, std::string salt)
	{
		if (!m_dht) return;
		m_dht->get_item(dht::public_key(key.data()), std::bind(&session_impl::get_mutable_callback
			, this, _1, _2), salt);
	}

	namespace {

		void on_dht_put_immutable_item(alert_manager& alerts, sha1_hash target, int num)
		{
			if (alerts.should_post<dht_put_alert>())
				alerts.emplace_alert<dht_put_alert>(target, num);
		}

		void on_dht_put_mutable_item(alert_manager& alerts, dht::item const& i, int num)
		{
			dht::signature sig = i.sig();
			dht::public_key pk = i.pk();
			dht::sequence_number seq = i.seq();
			std::string salt = i.salt();

			if (alerts.should_post<dht_put_alert>())
			{
				alerts.emplace_alert<dht_put_alert>(pk.bytes, sig.bytes, salt
					, seq.value, num);
			}
		}

		void put_mutable_callback(dht::item& i
			, std::function<void(entry&, std::array<char, 64>&
				, std::uint64_t&, std::string const&)> cb)
		{
			entry value = i.value();
			dht::signature sig = i.sig();
			dht::public_key pk = i.pk();
			dht::sequence_number seq = i.seq();
			std::string salt = i.salt();
			cb(value, sig.bytes, seq.value, salt);
			i.assign(std::move(value), salt, seq, pk, sig);
		}

		void on_dht_get_peers(alert_manager& alerts, sha1_hash info_hash, std::vector<tcp::endpoint> const& peers)
		{
			if (alerts.should_post<dht_get_peers_reply_alert>())
				alerts.emplace_alert<dht_get_peers_reply_alert>(info_hash, peers);
		}

		void on_direct_response(alert_manager& alerts, void* userdata, dht::msg const& msg)
		{
			if (msg.message.type() == bdecode_node::none_t)
				alerts.emplace_alert<dht_direct_response_alert>(userdata, msg.addr);
			else
				alerts.emplace_alert<dht_direct_response_alert>(userdata, msg.addr, msg.message);
		}

	} // anonymous namespace

	void session_impl::dht_put_immutable_item(entry const& data, sha1_hash target)
	{
		if (!m_dht) return;
		m_dht->put_item(data, std::bind(&on_dht_put_immutable_item, std::ref(m_alerts)
			, target, _1));
	}

	void session_impl::dht_put_mutable_item(std::array<char, 32> key
		, std::function<void(entry&, std::array<char,64>&
		, std::uint64_t&, std::string const&)> cb
		, std::string salt)
	{
		if (!m_dht) return;
		m_dht->put_item(dht::public_key(key.data())
			, std::bind(&on_dht_put_mutable_item, std::ref(m_alerts), _1, _2)
			, std::bind(&put_mutable_callback, _1, cb), salt);
	}

	void session_impl::dht_get_peers(sha1_hash const& info_hash)
	{
		if (!m_dht) return;
		m_dht->get_peers(info_hash, std::bind(&on_dht_get_peers, std::ref(m_alerts), info_hash, _1));
	}

	void session_impl::dht_announce(sha1_hash const& info_hash, int port, int flags)
	{
		if (!m_dht) return;
		m_dht->announce(info_hash, port, flags, std::bind(&on_dht_get_peers, std::ref(m_alerts), info_hash, _1));
	}

	void session_impl::dht_direct_request(udp::endpoint ep, entry& e, void* userdata)
	{
		if (!m_dht) return;
		m_dht->direct_request(ep, e, std::bind(&on_direct_response, std::ref(m_alerts), userdata, _1));
	}

#endif

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	void session_impl::add_obfuscated_hash(sha1_hash const& obfuscated
		, std::weak_ptr<torrent> const& t)
	{
		m_obfuscated_torrents.insert(std::make_pair(obfuscated, t.lock()));
	}
#endif // !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

	bool session_impl::is_listening() const
	{
		return !m_listen_sockets.empty();
	}

	session_impl::~session_impl()
	{
		// this is not allowed to be the network thread!
//		TORRENT_ASSERT(is_not_thread());

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());

#if defined TORRENT_ASIO_DEBUGGING
		FILE* f = fopen("wakeups.log", "w+");
		if (f != nullptr)
		{
			time_point m = min_time();
			if (!_wakeups.empty()) m = _wakeups[0].timestamp;
			time_point prev = m;
			std::uint64_t prev_csw = 0;
			if (!_wakeups.empty()) prev_csw = _wakeups[0].context_switches;
			std::fprintf(f, "abs. time\trel. time\tctx switch\tidle-wakeup\toperation\n");
			for (int i = 0; i < _wakeups.size(); ++i)
			{
				wakeup_t const& w = _wakeups[i];
				bool idle_wakeup = w.context_switches > prev_csw;
				std::fprintf(f, "%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%c\t%s\n"
					, total_microseconds(w.timestamp - m)
					, total_microseconds(w.timestamp - prev)
					, w.context_switches
					, idle_wakeup ? '*' : '.'
					, w.operation);
				prev = w.timestamp;
				prev_csw = w.context_switches;
			}
			fclose(f);
		}
#endif

		// clear the torrent LRU. We do this to avoid having the torrent
		// destructor assert because it's still linked into the lru list
#if TORRENT_USE_ASSERTS
		list_node<torrent>* i = m_torrent_lru.get_all();
		// clear the prev and next pointers in all torrents
		// to avoid the assert when destructing them
		while (i)
		{
			list_node<torrent>* tmp = i;
			i = i->next;
			tmp->next = nullptr;
			tmp->prev = nullptr;
		}
#endif

	}

#ifndef TORRENT_NO_DEPRECATE
	int session_impl::max_connections() const
	{
		return m_settings.get_int(settings_pack::connections_limit);
	}

	int session_impl::max_uploads() const
	{
		return m_settings.get_int(settings_pack::unchoke_slots_limit);
	}

	void session_impl::set_local_download_rate_limit(int bytes_per_second)
	{
		settings_pack p;
		p.set_int(settings_pack::local_download_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_local_upload_rate_limit(int bytes_per_second)
	{
		settings_pack p;
		p.set_int(settings_pack::local_upload_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_download_rate_limit_depr(int bytes_per_second)
	{
		settings_pack p;
		p.set_int(settings_pack::download_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_upload_rate_limit_depr(int bytes_per_second)
	{
		settings_pack p;
		p.set_int(settings_pack::upload_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_max_connections(int limit)
	{
		settings_pack p;
		p.set_int(settings_pack::connections_limit, limit);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_max_uploads(int limit)
	{
		settings_pack p;
		p.set_int(settings_pack::unchoke_slots_limit, limit);
		apply_settings_pack_impl(p);
	}

	int session_impl::local_upload_rate_limit() const
	{
		return upload_rate_limit(m_local_peer_class);
	}

	int session_impl::local_download_rate_limit() const
	{
		return download_rate_limit(m_local_peer_class);
	}

	int session_impl::upload_rate_limit_depr() const
	{
		return upload_rate_limit(m_global_class);
	}

	int session_impl::download_rate_limit_depr() const
	{
		return download_rate_limit(m_global_class);
	}
#endif


	namespace {
		template <typename Socket>
		void set_tos(Socket& s, int v, error_code& ec)
		{
#if TORRENT_USE_IPV6
			if (s.local_endpoint(ec).address().is_v6())
				s.set_option(traffic_class(v), ec);
			else if (!ec)
#endif
				s.set_option(type_of_service(v), ec);
		}
	}

	// TODO: 2 this should be factored into the udp socket, so we only have the
	// code once
	void session_impl::update_peer_tos()
	{
		int const tos = m_settings.get_int(settings_pack::peer_tos);
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			error_code ec;
			set_tos(*i->sock, tos, ec);

#ifndef TORRENT_DISABLE_LOGGING
			error_code err;
			session_log(">>> SET_TOS [ tcp (%s %d) tos: %x e: %s ]"
				, i->sock->local_endpoint(err).address().to_string().c_str()
				, i->sock->local_endpoint(err).port(), tos, ec.message().c_str());
#endif
			ec.clear();
			set_tos(*i->udp_sock, tos, ec);

#ifndef TORRENT_DISABLE_LOGGING
			session_log(">>> SET_TOS [ udp (%s %d) tos: %x e: %s ]"
				, i->udp_sock->local_endpoint(err).address().to_string().c_str()
				, i->udp_sock->local_port()
				, tos, ec.message().c_str());
#endif
		}
	}

	void session_impl::update_user_agent()
	{
		// replace all occurrences of '\n' with ' '.
		std::string agent = m_settings.get_str(settings_pack::user_agent);
		std::string::iterator i = agent.begin();
		while ((i = std::find(i, agent.end(), '\n'))
			!= agent.end())
			*i = ' ';
		m_settings.set_str(settings_pack::user_agent, agent);
	}

	void session_impl::update_unchoke_limit()
	{
		int const allowed_upload_slots = get_int_setting(settings_pack::unchoke_slots_limit);

		m_stats_counters.set_value(counters::num_unchoke_slots
			, allowed_upload_slots);

		if (m_settings.get_int(settings_pack::num_optimistic_unchoke_slots)
			>= allowed_upload_slots / 2)
		{
			if (m_alerts.should_post<performance_alert>())
				m_alerts.emplace_alert<performance_alert>(torrent_handle()
					, performance_alert::too_many_optimistic_unchoke_slots);
		}
	}

	void session_impl::update_connection_speed()
	{
		if (m_settings.get_int(settings_pack::connection_speed) < 0)
			m_settings.set_int(settings_pack::connection_speed, 200);
	}

	void session_impl::update_queued_disk_bytes()
	{
		std::uint64_t cache_size = m_settings.get_int(settings_pack::cache_size);
		if (m_settings.get_int(settings_pack::max_queued_disk_bytes) / 16 / 1024
			> cache_size / 2
			&& cache_size > 5
			&& m_alerts.should_post<performance_alert>())
		{
			m_alerts.emplace_alert<performance_alert>(torrent_handle()
				, performance_alert::too_high_disk_queue_limit);
		}
	}

	void session_impl::update_alert_queue_size()
	{
		m_alerts.set_alert_queue_size_limit(m_settings.get_int(settings_pack::alert_queue_size));
	}

	bool session_impl::preemptive_unchoke() const
	{
		return m_stats_counters[counters::num_peers_up_unchoked]
			< m_stats_counters[counters::num_unchoke_slots]
			|| m_settings.get_int(settings_pack::unchoke_slots_limit) < 0;
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::update_dht_upload_rate_limit()
	{
#ifndef TORRENT_DISABLE_DHT
		m_dht_settings.upload_rate_limit
			= m_settings.get_int(settings_pack::dht_upload_rate_limit);
#endif
	}
#endif

	void session_impl::update_disk_threads()
	{
		if (m_settings.get_int(settings_pack::aio_threads) < 0)
			m_settings.set_int(settings_pack::aio_threads, 0);

#if !TORRENT_USE_PREAD && !TORRENT_USE_PREADV
		// if we don't have pread() nor preadv() there's no way
		// to perform concurrent file operations on the same file
		// handle, so we must limit the disk thread to a single one

		if (m_settings.get_int(settings_pack::aio_threads) > 1)
			m_settings.set_int(settings_pack::aio_threads, 1);
#endif

		m_disk_thread.set_num_threads(m_settings.get_int(settings_pack::aio_threads));
	}

	void session_impl::update_cache_buffer_chunk_size()
	{
		if (m_settings.get_int(settings_pack::cache_buffer_chunk_size) <= 0)
			m_settings.set_int(settings_pack::cache_buffer_chunk_size, 1);
	}

	void session_impl::update_report_web_seed_downloads()
	{
		// if this flag changed, update all web seed connections
		bool report = m_settings.get_bool(settings_pack::report_web_seed_downloads);
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			int type = (*i)->type();
			if (type == peer_connection::url_seed_connection
				|| type == peer_connection::http_seed_connection)
				(*i)->ignore_stats(!report);
		}
	}

	void session_impl::trigger_auto_manage()
	{
		if (m_pending_auto_manage || m_abort) return;

		// we recalculated auto-managed torrents less than a second ago,
		// put it off one second.
		if (time_now() - m_last_auto_manage < seconds(1))
		{
			m_auto_manage_time_scaler = 0;
			return;
		}
		m_pending_auto_manage = true;
		m_need_auto_manage = true;

		m_io_service.post(std::bind(&session_impl::on_trigger_auto_manage, this));
	}

	void session_impl::on_trigger_auto_manage()
	{
		TORRENT_ASSERT(m_pending_auto_manage);
		if (!m_need_auto_manage || m_abort)
		{
			m_pending_auto_manage = false;
			return;
		}
		// don't clear m_pending_auto_manage until after we've
		// recalculated the auto managed torrents. The auto-managed
		// logic may trigger another auto-managed event otherwise
		recalculate_auto_managed_torrents();
		m_pending_auto_manage = false;
	}

	void session_impl::update_socket_buffer_size()
	{
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			error_code ec;
			set_socket_buffer_size(*i->udp_sock, m_settings, ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (ec)
			{
				error_code err;
				session_log("socket buffer size [ udp %s %d]: (%d) %s"
				, i->udp_sock->local_endpoint(err).address().to_string(err).c_str()
				, i->udp_sock->local_port(), ec.value(), ec.message().c_str());
			}
#endif
			ec.clear();
			set_socket_buffer_size(*i->sock, m_settings, ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (ec)
			{
				error_code err;
				session_log("socket buffer size [ udp %s %d]: (%d) %s"
				, i->sock->local_endpoint(err).address().to_string(err).c_str()
				, i->sock->local_endpoint(err).port(), ec.value(), ec.message().c_str());
			}
#endif
		}
	}

	void session_impl::update_dht_announce_interval()
	{
#ifndef TORRENT_DISABLE_DHT
		if (!m_dht)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("not starting DHT announce timer: m_dht == nullptr");
#endif
			return;
		}

		m_dht_interval_update_torrents = int(m_torrents.size());

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("not starting DHT announce timer: m_abort set");
#endif
			return;
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_announce");
		error_code ec;
		int delay = (std::max)(m_settings.get_int(settings_pack::dht_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait(
			std::bind(&session_impl::on_dht_announce, this, _1));
#endif
	}

	void session_impl::update_anonymous_mode()
	{
		if (!m_settings.get_bool(settings_pack::anonymous_mode)) return;

		m_settings.set_str(settings_pack::user_agent, "");
		url_random(m_peer_id.data(), m_peer_id.data() + 20);
	}

	void session_impl::update_force_proxy()
	{
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->udp_sock->set_force_proxy(m_settings.get_bool(settings_pack::force_proxy));

			// close the TCP listen sockets
			if (i->sock)
			{
				error_code ec;
				i->sock->close(ec);
				i->sock.reset();
			}
		}

		if (!m_settings.get_bool(settings_pack::force_proxy)) return;

		// enable force_proxy mode. We don't want to accept any incoming
		// connections, except through a proxy.
		stop_lsd();
		stop_upnp();
		stop_natpmp();
#ifndef TORRENT_DISABLE_DHT
		stop_dht();
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::update_local_download_rate()
	{
		if (m_settings.get_int(settings_pack::local_download_rate_limit) < 0)
			m_settings.set_int(settings_pack::local_download_rate_limit, 0);
		set_download_rate_limit(m_local_peer_class
			, m_settings.get_int(settings_pack::local_download_rate_limit));
	}

	void session_impl::update_local_upload_rate()
	{
		if (m_settings.get_int(settings_pack::local_upload_rate_limit) < 0)
			m_settings.set_int(settings_pack::local_upload_rate_limit, 0);
		set_upload_rate_limit(m_local_peer_class
			, m_settings.get_int(settings_pack::local_upload_rate_limit));
	}
#endif

	void session_impl::update_download_rate()
	{
		if (m_settings.get_int(settings_pack::download_rate_limit) < 0)
			m_settings.set_int(settings_pack::download_rate_limit, 0);
		set_download_rate_limit(m_global_class
			, m_settings.get_int(settings_pack::download_rate_limit));
	}

	void session_impl::update_upload_rate()
	{
		if (m_settings.get_int(settings_pack::upload_rate_limit) < 0)
			m_settings.set_int(settings_pack::upload_rate_limit, 0);
		set_upload_rate_limit(m_global_class
			, m_settings.get_int(settings_pack::upload_rate_limit));
	}

	void session_impl::update_connections_limit()
	{
		int limit = m_settings.get_int(settings_pack::connections_limit);

		if (limit <= 0)
			limit = (std::numeric_limits<int>::max)();

		limit = (std::max)(5, (std::min)(limit
				, max_open_files() - 20 - m_settings.get_int(settings_pack::file_pool_size)));

		m_settings.set_int(settings_pack::connections_limit, limit);

		if (num_connections() > m_settings.get_int(settings_pack::connections_limit)
			&& !m_torrents.empty())
		{
			// if we have more connections that we're allowed, disconnect
			// peers from the torrents so that they are all as even as possible

			int to_disconnect = num_connections() - m_settings.get_int(settings_pack::connections_limit);

			int last_average = 0;
			int average = int(m_settings.get_int(settings_pack::connections_limit) / m_torrents.size());

			// the number of slots that are unused by torrents
			int extra = m_settings.get_int(settings_pack::connections_limit) % m_torrents.size();

			// run 3 iterations of this, then we're probably close enough
			for (int iter = 0; iter < 4; ++iter)
			{
				// the number of torrents that are above average
				int num_above = 0;
				for (torrent_map::iterator i = m_torrents.begin()
					, end(m_torrents.end()); i != end; ++i)
				{
					int num = i->second->num_peers();
					if (num <= last_average) continue;
					if (num > average) ++num_above;
					if (num < average) extra += average - num;
				}

				// distribute extra among the torrents that are above average
				if (num_above == 0) num_above = 1;
				last_average = average;
				average += extra / num_above;
				if (extra == 0) break;
				// save the remainder for the next iteration
				extra = extra % num_above;
			}

			for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				int num = i->second->num_peers();
				if (num <= average) continue;

				// distribute the remainder
				int my_average = average;
				if (extra > 0)
				{
					++my_average;
					--extra;
				}

				int disconnect = (std::min)(to_disconnect, num - my_average);
				to_disconnect -= disconnect;
				i->second->disconnect_peers(disconnect
					, error_code(errors::too_many_connections, get_libtorrent_category()));
			}
		}
	}

	void session_impl::update_alert_mask()
	{
		m_alerts.set_alert_mask(m_settings.get_int(settings_pack::alert_mask));
	}

	void session_impl::pop_alerts(std::vector<alert*>* alerts)
	{
		m_alerts.get_all(*alerts);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::update_rate_limit_utp()
	{
		if (m_settings.get_bool(settings_pack::rate_limit_utp))
		{
			// allow the global or local peer class to limit uTP peers
			m_peer_class_type_filter.add(peer_class_type_filter::utp_socket
				, m_local_peer_class);
			m_peer_class_type_filter.add(peer_class_type_filter::utp_socket
				, m_global_class);
			m_peer_class_type_filter.add(peer_class_type_filter::ssl_utp_socket
				, m_local_peer_class);
			m_peer_class_type_filter.add(peer_class_type_filter::ssl_utp_socket
				, m_global_class);
		}
		else
		{
			// don't add the global or local peer class to limit uTP peers
			m_peer_class_type_filter.remove(peer_class_type_filter::utp_socket
				, m_local_peer_class);
			m_peer_class_type_filter.remove(peer_class_type_filter::utp_socket
				, m_global_class);
			m_peer_class_type_filter.remove(peer_class_type_filter::ssl_utp_socket
				, m_local_peer_class);
			m_peer_class_type_filter.remove(peer_class_type_filter::ssl_utp_socket
				, m_global_class);
		}
	}

	void session_impl::update_ignore_rate_limits_on_local_network()
	{
		init_peer_class_filter(
			m_settings.get_bool(settings_pack::ignore_limits_on_local_network));
	}

	// this function is called on the user's thread
	// not the network thread
	void session_impl::pop_alerts()
	{
		// if we don't have any alerts in our local cache, we have to ask
		// the alert_manager for more. It will swap our vector with its and
		// destruct eny left-over alerts in there.
		if (m_alert_pointer_pos >= m_alert_pointers.size())
		{
			pop_alerts(&m_alert_pointers);
			m_alert_pointer_pos = 0;
		}
	}

	alert const* session_impl::pop_alert()
	{
		if (m_alert_pointer_pos >= m_alert_pointers.size())
		{
			pop_alerts();
			if (m_alert_pointers.empty())
				return nullptr;
		}

		if (m_alert_pointers.empty()) return nullptr;

		// clone here to be backwards compatible, to make the client delete the
		// alert object
		return m_alert_pointers[m_alert_pointer_pos++];
	}

#endif

	alert* session_impl::wait_for_alert(time_duration max_wait)
	{
		return m_alerts.wait_for_alert(max_wait);
	}

#ifndef TORRENT_NO_DEPRECATE
	size_t session_impl::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		m_settings.set_int(settings_pack::alert_queue_size, int(queue_size_limit_));
		return m_alerts.set_alert_queue_size_limit(int(queue_size_limit_));
	}
#endif

	void session_impl::start_lsd()
	{
		INVARIANT_CHECK;

		if (m_lsd) return;

		m_lsd = std::make_shared<lsd>(std::ref(m_io_service)
			, std::bind(&session_impl::on_lsd_peer, this, _1, _2)
#ifndef TORRENT_DISABLE_LOGGING
			, std::bind(&session_impl::on_lsd_log, this, _1)
#endif
			);
		error_code ec;
		m_lsd->start(ec);
		if (ec && m_alerts.should_post<lsd_error_alert>())
			m_alerts.emplace_alert<lsd_error_alert>(ec);
	}

#ifndef TORRENT_DISABLE_LOGGING
	void session_impl::on_lsd_log(char const* log)
	{
		if (!m_alerts.should_post<log_alert>()) return;
		m_alerts.emplace_alert<log_alert>(log);
	}
#endif

	natpmp* session_impl::start_natpmp()
	{
		INVARIANT_CHECK;

		if (m_natpmp) return m_natpmp.get();

		// the natpmp constructor may fail and call the callbacks
		// into the session_impl.
		m_natpmp = std::make_shared<natpmp>(std::ref(m_io_service)
			, std::bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, _4, _5, 0)
			, std::bind(&session_impl::on_port_map_log
				, this, _1, 0));
		m_natpmp->start();

		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			remap_ports(remap_natpmp, *i);
		}
		return m_natpmp.get();
	}

	upnp* session_impl::start_upnp()
	{
		INVARIANT_CHECK;

		if (m_upnp) return m_upnp.get();

		// the upnp constructor may fail and call the callbacks
		m_upnp = std::make_shared<upnp>(std::ref(m_io_service)
			, m_settings.get_str(settings_pack::user_agent)
			, std::bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, _4, _5, 1)
			, std::bind(&session_impl::on_port_map_log
				, this, _1, 1)
			, m_settings.get_bool(settings_pack::upnp_ignore_nonrouters));
		m_upnp->start();

		m_upnp->discover_device();

		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			remap_ports(remap_upnp, *i);
		}
		return m_upnp.get();
	}

	int session_impl::add_port_mapping(int t, int external_port
		, int local_port)
	{
		int ret = 0;
		if (m_upnp) ret = m_upnp->add_mapping(static_cast<upnp::protocol_type>(t), external_port
			, local_port);
		if (m_natpmp) ret = m_natpmp->add_mapping(static_cast<natpmp::protocol_type>(t), external_port
			, local_port);
		return ret;
	}

	void session_impl::delete_port_mapping(int handle)
	{
		if (m_upnp) m_upnp->delete_mapping(handle);
		if (m_natpmp) m_natpmp->delete_mapping(handle);
	}

	void session_impl::stop_lsd()
	{
		if (m_lsd)
			m_lsd->close();
		m_lsd.reset();
	}

	void session_impl::stop_natpmp()
	{
		if (!m_natpmp) return;

		m_natpmp->close();
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->tcp_port_mapping[0] = -1;
			i->udp_port_mapping[0] = -1;
		}

		m_natpmp.reset();
	}

	void session_impl::stop_upnp()
	{
		if (!m_upnp) return;

		m_upnp->close();
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->tcp_port_mapping[1] = -1;
			i->udp_port_mapping[1] = -1;
		}
		m_upnp.reset();
	}

	external_ip const& session_impl::external_address() const
	{
		return m_external_ip;
	}

	// this is the DHT observer version. DHT is the implied source
	void session_impl::set_external_address(address const& ip
		, address const& source)
	{
		set_external_address(ip, source_dht, source);
	}

	address session_impl::external_address(udp proto)
	{
#if !TORRENT_USE_IPV6
		TORRENT_UNUSED(proto);
#endif

		address addr;
#if TORRENT_USE_IPV6
		if (proto == udp::v6())
			addr = address_v6();
		else
#endif
			addr = address_v4();
		return m_external_ip.external_address(addr);
	}

	void session_impl::get_peers(sha1_hash const& ih)
	{
		if (!m_alerts.should_post<dht_get_peers_alert>()) return;
		m_alerts.emplace_alert<dht_get_peers_alert>(ih);
	}

	void session_impl::announce(sha1_hash const& ih, address const& addr
		, int port)
	{
		if (!m_alerts.should_post<dht_announce_alert>()) return;
		m_alerts.emplace_alert<dht_announce_alert>(addr, port, ih);
	}

	void session_impl::outgoing_get_peers(sha1_hash const& target
		, sha1_hash const& sent_target, udp::endpoint const& ep)
	{
		if (!m_alerts.should_post<dht_outgoing_get_peers_alert>()) return;
		m_alerts.emplace_alert<dht_outgoing_get_peers_alert>(target, sent_target, ep);
	}

#ifndef TORRENT_DISABLE_LOGGING
	TORRENT_FORMAT(3,4)
	void session_impl::log(libtorrent::dht::dht_logger::module_t m, char const* fmt, ...)
	{
		if (!m_alerts.should_post<dht_log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		m_alerts.emplace_alert<dht_log_alert>(
			static_cast<dht_log_alert::dht_module_t>(m), fmt, v);
		va_end(v);
	}

	void session_impl::log_packet(message_direction_t dir, char const* pkt, int len
		, udp::endpoint node)
	{
		if (!m_alerts.should_post<dht_pkt_alert>()) return;

		dht_pkt_alert::direction_t d = dir == dht_logger::incoming_message
			? dht_pkt_alert::incoming : dht_pkt_alert::outgoing;

		m_alerts.emplace_alert<dht_pkt_alert>(pkt, len, d, node);
	}
#endif

	bool session_impl::on_dht_request(string_view query
		, dht::msg const& request, entry& response)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_ses_extensions[plugins_dht_request_idx])
		{
			if (ext->on_dht_request(query
				, request.addr, request.message, response))
				return true;
		}
#else
		TORRENT_UNUSED(query);
		TORRENT_UNUSED(request);
		TORRENT_UNUSED(response);
#endif
		return false;
	}

	void session_impl::set_external_address(address const& ip
		, int source_type, address const& source)
	{
#ifndef TORRENT_DISABLE_LOGGING
		session_log(": set_external_address(%s, %d, %s)", print_address(ip).c_str()
			, source_type, print_address(source).c_str());
#endif

		if (!m_external_ip.cast_vote(ip, source_type, source)) return;

#ifndef TORRENT_DISABLE_LOGGING
		session_log("  external IP updated");
#endif

		if (m_alerts.should_post<external_ip_alert>())
			m_alerts.emplace_alert<external_ip_alert>(ip);

		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->new_external_ip();
		}

		// since we have a new external IP now, we need to
		// restart the DHT with a new node ID

#ifndef TORRENT_DISABLE_DHT
		if (m_dht) m_dht->update_node_id();
#endif
	}

	// decrement the refcount of the block in the disk cache
	// since the network thread doesn't need it anymore
	void session_impl::reclaim_block(block_cache_reference ref)
	{
		m_disk_thread.reclaim_block(ref);
	}

	disk_buffer_holder session_impl::allocate_disk_buffer(char const* category)
	{
		return m_disk_thread.allocate_disk_buffer(category);
	}

	void session_impl::free_disk_buffer(char* buf)
	{
		m_disk_thread.free_disk_buffer(buf);
	}

	disk_buffer_holder session_impl::allocate_disk_buffer(bool& exceeded
		, boost::shared_ptr<disk_observer> o
		, char const* category)
	{
		return m_disk_thread.allocate_disk_buffer(exceeded, o, category);
	}

	char* session_impl::allocate_buffer()
	{
		TORRENT_ASSERT(is_single_thread());

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		int num_bytes = send_buffer_size();
		return static_cast<char*>(malloc(num_bytes));
#else
		return static_cast<char*>(m_send_buffers.malloc());
#endif
	}

	void session_impl::free_buffer(char* buf)
	{
		TORRENT_ASSERT(is_single_thread());

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		free(buf);
#else
		m_send_buffers.free(buf);
#endif
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void session_impl::check_invariant() const
	{
		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(m_torrents.size() >= m_torrent_lru.size());

		if (m_settings.get_int(settings_pack::unchoke_slots_limit) < 0
			&& m_settings.get_int(settings_pack::choking_algorithm) == settings_pack::fixed_slots_choker)
			TORRENT_ASSERT(m_stats_counters[counters::num_unchoke_slots] == (std::numeric_limits<int>::max)());

		for (int l = 0; l < num_torrent_lists; ++l)
		{
			std::vector<torrent*> const& list = m_torrent_lists[l];
			for (std::vector<torrent*>::const_iterator i = list.begin()
				, end(list.end()); i != end; ++i)
			{
				TORRENT_ASSERT((*i)->m_links[l].in_list());
			}
		}

		std::unordered_set<torrent*> unique_torrents;
		for (list_iterator<torrent> i = m_torrent_lru.iterate(); i.get(); i.next())
		{
			torrent* t = i.get();
			TORRENT_ASSERT(t->is_loaded());
			TORRENT_ASSERT(unique_torrents.count(t) == 0);
			unique_torrents.insert(t);
		}
		TORRENT_ASSERT(unique_torrents.size() == m_torrent_lru.size());

		int torrent_state_gauges[counters::num_error_torrents - counters::num_checking_torrents + 1];
		memset(torrent_state_gauges, 0, sizeof(torrent_state_gauges));

#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS

		std::unordered_set<int> unique;
#endif

		int num_active_downloading = 0;
		int num_active_finished = 0;
		int total_downloaders = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			std::shared_ptr<torrent> t = i->second;
			if (t->want_peers_download()) ++num_active_downloading;
			if (t->want_peers_finished()) ++num_active_finished;
			TORRENT_ASSERT(!(t->want_peers_download() && t->want_peers_finished()));

			++torrent_state_gauges[t->current_stats_state() - counters::num_checking_torrents];

			int pos = t->queue_position();
			if (pos < 0)
			{
				TORRENT_ASSERT(pos == -1);
				continue;
			}
			++total_downloaders;

#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
			unique.insert(t->queue_position());
#endif
		}

		for (int i = 0, j = counters::num_checking_torrents;
			j < counters::num_error_torrents + 1; ++i, ++j)
		{
			TORRENT_ASSERT(torrent_state_gauges[i] == m_stats_counters[j]);
		}

#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_ASSERT(int(unique.size()) == total_downloaders);
#endif
		TORRENT_ASSERT(num_active_downloading == m_torrent_lists[torrent_want_peers_download].size());
		TORRENT_ASSERT(num_active_finished == m_torrent_lists[torrent_want_peers_finished].size());

		std::unordered_set<peer_connection*> unique_peers;
		TORRENT_ASSERT(m_settings.get_int(settings_pack::connections_limit) > 0);

		int unchokes = 0;
		int unchokes_all = 0;
		int num_optimistic = 0;
		int disk_queue[2] = {0, 0};
		for (connection_map::const_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			std::shared_ptr<torrent> t = (*i)->associated_torrent().lock();
			TORRENT_ASSERT(unique_peers.find(i->get()) == unique_peers.end());
			unique_peers.insert(i->get());

			if ((*i)->m_channel_state[0] & peer_info::bw_disk) ++disk_queue[0];
			if ((*i)->m_channel_state[1] & peer_info::bw_disk) ++disk_queue[1];

			peer_connection* p = i->get();
			TORRENT_ASSERT(!p->is_disconnecting());
			if (p->ignore_unchoke_slots())
			{
				if (!p->is_choked()) ++unchokes_all;
				continue;
			}
			if (!p->is_choked())
			{
				++unchokes;
				++unchokes_all;
			}

			if (p->peer_info_struct()
				&& p->peer_info_struct()->optimistically_unchoked)
			{
				++num_optimistic;
				TORRENT_ASSERT(!p->is_choked());
			}
		}

		for (std::vector<boost::shared_ptr<peer_connection> >::const_iterator i
			= m_undead_peers.begin(); i != m_undead_peers.end(); ++i)
		{
			peer_connection* p = i->get();
			if (p->ignore_unchoke_slots())
			{
				if (!p->is_choked()) ++unchokes_all;
				continue;
			}
			if (!p->is_choked())
			{
				++unchokes_all;
				++unchokes;
			}

			if (p->peer_info_struct()
				&& p->peer_info_struct()->optimistically_unchoked)
			{
				++num_optimistic;
				TORRENT_ASSERT(!p->is_choked());
			}
		}

		TORRENT_ASSERT(disk_queue[peer_connection::download_channel]
			== m_stats_counters[counters::num_peers_down_disk]);
		TORRENT_ASSERT(disk_queue[peer_connection::upload_channel]
			== m_stats_counters[counters::num_peers_up_disk]);

		if (m_settings.get_int(settings_pack::num_optimistic_unchoke_slots))
		{
			TORRENT_ASSERT(num_optimistic <= m_settings.get_int(
				settings_pack::num_optimistic_unchoke_slots));
		}

		int const unchoked_counter_all = m_stats_counters[counters::num_peers_up_unchoked_all];
		int const unchoked_counter = m_stats_counters[counters::num_peers_up_unchoked];
		int const unchoked_counter_optimistic
			= m_stats_counters[counters::num_peers_up_unchoked_optimistic];

		TORRENT_ASSERT_VAL(unchoked_counter_all == unchokes_all, unchokes_all);
		TORRENT_ASSERT_VAL(unchoked_counter == unchokes, unchokes);
		TORRENT_ASSERT_VAL(unchoked_counter_optimistic == num_optimistic, num_optimistic);

		for (torrent_map::const_iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			TORRENT_ASSERT(boost::get_pointer(j->second));
		}
	}
#endif // TORRENT_USE_INVARIANT_CHECKS

#ifndef TORRENT_DISABLE_LOGGING
		tracker_logger::tracker_logger(session_interface& ses): m_ses(ses) {}
		void tracker_logger::tracker_warning(tracker_request const&
			, std::string const& str)
		{
			debug_log("*** tracker warning: %s", str.c_str());
		}

		void tracker_logger::tracker_response(tracker_request const&
			, libtorrent::address const& tracker_ip
			, std::list<address> const& tracker_ips
			, struct tracker_response const& resp)
		{
			TORRENT_UNUSED(tracker_ips);
			debug_log("TRACKER RESPONSE\n"
				"interval: %d\n"
				"external ip: %s\n"
				"we connected to: %s\n"
				"peers:"
				, resp.interval
				, print_address(resp.external_ip).c_str()
				, print_address(tracker_ip).c_str());

			for (std::vector<peer_entry>::const_iterator i = resp.peers.begin();
			i != resp.peers.end(); ++i)
			{
				debug_log("  %16s %5d %s %s", i->hostname.c_str(), i->port
					, i->pid.is_all_zeros() ? "" : to_hex(i->pid).c_str()
					, identify_client(i->pid).c_str());
			}
			for (std::vector<ipv4_peer_entry>::const_iterator i = resp.peers4.begin();
				i != resp.peers4.end(); ++i)
			{
				debug_log("  %s:%d", print_address(address_v4(i->ip)).c_str(), i->port);
			}
#if TORRENT_USE_IPV6
			for (std::vector<ipv6_peer_entry>::const_iterator i = resp.peers6.begin();
				i != resp.peers6.end(); ++i)
			{
				debug_log("  [%s]:%d", print_address(address_v6(i->ip)).c_str(), i->port);
			}
#endif
		}

		void tracker_logger::tracker_request_timed_out(
			tracker_request const&)
		{
			debug_log("*** tracker timed out");
		}

		void tracker_logger::tracker_request_error(tracker_request const&
			, int response_code, error_code const& ec, const std::string& str
			, int retry_interval)
		{
			TORRENT_UNUSED(retry_interval);
			debug_log("*** tracker error: %d: %s %s"
				, response_code, ec.message().c_str(), str.c_str());
		}

		void tracker_logger::debug_log(const char* fmt, ...) const
		{
			va_list v;
			va_start(v, fmt);
			m_ses.session_vlog(fmt, v);
			va_end(v);
		}
#endif // TORRENT_DISABLE_LOGGING
}}
