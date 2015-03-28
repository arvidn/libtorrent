/*

Copyright (c) 2006-2014, Arvid Norberg, Magnus Jonsson
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

#include <ctime>
#include <algorithm>
#include <cctype>
#include <algorithm>

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
#if TORRENT_HAS_BOOST_UNORDERED
#include <boost/unordered_set.hpp>
#else
#include <set>
#endif
#endif // TORRENT_DEBUG && !TORRENT_DISABLE_INVARIANT_CHECKS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/bind.hpp>
#include <boost/function_equal.hpp>
#include <boost/make_shared.hpp>

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#ifndef TORRENT_DISABLE_DHT
#include "libtorrent/kademlia/dht_tracker.hpp"
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
#include "libtorrent/extensions.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/choker.hpp"

#ifndef TORRENT_WINDOWS
#include <sys/resource.h>
#endif

#if defined TORRENT_LOGGING

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

#endif // TORRENT_LOGGING

#ifdef TORRENT_USE_GCRYPT

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
			if (e != 0) fprintf(stderr, "libcrypt ERROR: %s\n", gcry_strerror(e));
			e = gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
			if (e != 0) fprintf(stderr, "initialization finished error: %s\n", gcry_strerror(e));
		}
	} gcrypt_global_constructor;
}

#endif // TORRENT_USE_GCRYPT

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
	mutex _async_ops_mutex;
#endif

socket_job::~socket_job() {}

void network_thread_pool::process_job(socket_job const& j, bool post)
{
	if (j.type == socket_job::write_job)
	{
		TORRENT_ASSERT(j.peer->m_socket_is_writing);
		j.peer->get_socket()->async_write_some(
			*j.vec, j.peer->make_write_handler(boost::bind(
				&peer_connection::on_send_data, j.peer, _1, _2)));
	}
	else
	{
		if (j.recv_buf)
		{
			j.peer->get_socket()->async_read_some(asio::buffer(j.recv_buf, j.buf_size)
				, j.peer->make_read_handler(boost::bind(
				&peer_connection::on_receive_data, j.peer, _1, _2)));
		}
		else
		{
			j.peer->get_socket()->async_read_some(j.read_vec
				, j.peer->make_read_handler(boost::bind(
				&peer_connection::on_receive_data, j.peer, _1, _2)));
		}
	}
}

// TODO: 2 find a better place for this function
proxy_settings::proxy_settings(aux::session_settings const& sett)
{
	hostname = sett.get_str(settings_pack::proxy_hostname);
	username = sett.get_str(settings_pack::proxy_username);
	password = sett.get_str(settings_pack::proxy_password);
	type = sett.get_int(settings_pack::proxy_type);
	port = sett.get_int(settings_pack::proxy_port);
	proxy_hostnames = sett.get_bool(settings_pack::proxy_hostnames);
	proxy_peer_connections = sett.get_bool(
		settings_pack::proxy_peer_connections);
}

namespace aux {

	void session_impl::init_peer_class_filter(bool unlimited_local)
	{
		// set the default peer_class_filter to use the local peer class
		// for peers on local networks
		boost::uint32_t lfilter = 1 << m_local_peer_class;
		boost::uint32_t gfilter = 1 << m_global_class;

		struct class_mapping
		{
			char const* first;
			char const* last;
			boost::uint32_t filter;
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

#if defined TORRENT_USE_OPENSSL && BOOST_VERSION >= 104700 && OPENSSL_VERSION_NUMBER >= 0x90812f
	// when running bittorrent over SSL, the SNI (server name indication)
	// extension is used to know which torrent the incoming connection is
	// trying to connect to. The 40 first bytes in the name is expected to
	// be the hex encoded info-hash
	int servername_callback(SSL *s, int *ad, void *arg)
	{
		session_impl* ses = (session_impl*)arg;
		const char* servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);
	
		if (!servername || strlen(servername) < 40)
			return SSL_TLSEXT_ERR_ALERT_FATAL;

		sha1_hash info_hash;
		bool valid = from_hex(servername, 40, (char*)&info_hash[0]);

		// the server name is not a valid hex-encoded info-hash
		if (!valid)
			return SSL_TLSEXT_ERR_ALERT_FATAL;

		// see if there is a torrent with this info-hash
		boost::shared_ptr<torrent> t = ses->find_torrent(info_hash).lock();

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
#endif

	session_impl::session_impl()
		:
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		m_send_buffers(send_buffer_size())
		,
#endif
		m_io_service()
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_ctx(m_io_service, asio::ssl::context::sslv23)
#endif
		, m_alerts(m_settings.get_int(settings_pack::alert_queue_size), alert::all_categories)
		, m_disk_thread(m_io_service, this, m_stats_counters
			, (uncork_interface*)this)
		, m_download_rate(peer_connection::download_channel)
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		, m_upload_rate(peer_connection::upload_channel, true)
#else
		, m_upload_rate(peer_connection::upload_channel)
#endif
		, m_tracker_manager(m_udp_socket, m_stats_counters, m_host_resolver
			, m_ip_filter, m_settings
#if defined TORRENT_LOGGING || TORRENT_USE_ASSERTS
			, *this
#endif
			)
		, m_num_save_resume(0)
		, m_work(io_service::work(m_io_service))
		, m_max_queue_pos(-1)
		, m_key(0)
		, m_listen_port_retries(10)
#if TORRENT_USE_I2P
		, m_i2p_conn(m_io_service)
#endif
		, m_socks_listen_port(0)
		, m_interface_index(0)
		, m_unchoke_time_scaler(0)
		, m_auto_manage_time_scaler(0)
		, m_optimistic_unchoke_time_scaler(0)
		, m_disconnect_time_scaler(90)
		, m_auto_scrape_time_scaler(180)
		, m_next_explicit_cache_torrent(0)
		, m_cache_rotation_timer(0)
		, m_next_suggest_torrent(0)
		, m_suggest_timer(0)
		, m_peak_up_rate(0)
		, m_peak_down_rate(0)
		, m_created(clock_type::now())
		, m_last_tick(m_created)
		, m_last_second_tick(m_created - milliseconds(900))
		, m_last_choke(m_created)
#ifndef TORRENT_NO_DEPRECATE
		, m_next_rss_update(min_time())
#endif
#ifndef TORRENT_DISABLE_DHT
		, m_dht_announce_timer(m_io_service)
		, m_dht_interval_update_torrents(0)
#endif
		, m_external_udp_port(0)
		, m_udp_socket(m_io_service)
		, m_utp_socket_manager(m_settings, m_udp_socket, m_stats_counters, NULL
			, boost::bind(&session_impl::incoming_connection, this, _1))
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_udp_socket(m_io_service)
		, m_ssl_utp_socket_manager(m_settings, m_ssl_udp_socket, m_stats_counters
			, &m_ssl_ctx
			, boost::bind(&session_impl::on_incoming_utp_ssl, this, _1))
#endif
		, m_boost_connections(0)
		, m_timer(m_io_service)
		, m_lsd_announce_timer(m_io_service)
		, m_host_resolver(m_io_service)
		, m_download_connect_attempts(0)
		, m_tick_residual(0)
		, m_deferred_submit_disk_jobs(false)
		, m_pending_auto_manage(false)
		, m_need_auto_manage(false)
		, m_abort(false)
		, m_paused(false)
#if TORRENT_USE_ASSERTS && defined BOOST_HAS_PTHREADS
		, m_network_thread(0)
#endif
	{
#if TORRENT_USE_ASSERTS
		m_posting_torrent_updates = false;
#endif
		m_udp_socket.set_rate_limit(m_settings.get_int(settings_pack::dht_upload_rate_limit));

		m_udp_socket.subscribe(&m_utp_socket_manager);
		m_udp_socket.subscribe(this);
		m_udp_socket.subscribe(&m_tracker_manager);

#ifdef TORRENT_USE_OPENSSL
		m_ssl_udp_socket.subscribe(&m_ssl_utp_socket_manager);
		m_ssl_udp_socket.subscribe(this);
#endif

#ifdef TORRENT_REQUEST_LOGGING
		char log_filename[200];
#ifdef TORRENT_WINDOWS
		const int pid = GetCurrentProcessId();
#else
		const int pid = getpid();
#endif
		snprintf(log_filename, sizeof(log_filename), "requests-%d.log", pid);
		m_request_log = fopen(log_filename, "w+");
		if (m_request_log == 0)
		{
			fprintf(stderr, "failed to open request log file: (%d) %s\n", errno, strerror(errno));
		}
#endif

		error_code ec;
		m_listen_interface = tcp::endpoint(address_v4::any(), 0);
		TORRENT_ASSERT_VAL(!ec, ec);
	}

	void session_impl::start_session(settings_pack const& pack)
	{
		m_alerts.set_alert_mask(pack.get_int(settings_pack::alert_mask));

#if defined TORRENT_LOGGING
		session_log("start session");
#endif

		error_code ec;
#ifdef TORRENT_USE_OPENSSL
		m_ssl_ctx.set_verify_mode(asio::ssl::context::verify_none, ec);
#if BOOST_VERSION >= 104700
#if OPENSSL_VERSION_NUMBER >= 0x90812f
		SSL_CTX_set_tlsext_servername_callback(m_ssl_ctx.native_handle(), servername_callback);
		SSL_CTX_set_tlsext_servername_arg(m_ssl_ctx.native_handle(), this);
#endif // OPENSSL_VERSION_NUMBER
#endif // BOOST_VERSION
#endif

#ifndef TORRENT_DISABLE_DHT
		m_next_dht_torrent = m_torrents.begin();
#endif
		m_next_lsd_torrent = m_torrents.begin();
		m_next_downloading_connect_torrent = 0;
		m_next_finished_connect_torrent = 0;
		m_next_scrape_torrent = 0;

		m_tcp_mapping[0] = -1;
		m_tcp_mapping[1] = -1;
		m_udp_mapping[0] = -1;
		m_udp_mapping[1] = -1;
#ifdef TORRENT_USE_OPENSSL
		m_ssl_tcp_mapping[0] = -1;
		m_ssl_tcp_mapping[1] = -1;
		m_ssl_udp_mapping[0] = -1;
		m_ssl_udp_mapping[1] = -1;
#endif

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

		// TODO: there's no rule here to make uTP connections not have the global or
		// local rate limits apply to it. This used to be the default.

#if defined TORRENT_LOGGING

		session_log("libtorrent configuration: %s\n"
			"libtorrent version: %s\n"
			"libtorrent revision: %s\n\n"
		  	, TORRENT_CFG_STRING
			, LIBTORRENT_VERSION
			, LIBTORRENT_REVISION);

#endif // TORRENT_LOGGING

#if TORRENT_USE_RLIMIT
		// ---- auto-cap max connections ----

		struct rlimit rl;
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		{
#if defined TORRENT_LOGGING
			session_log(" max number of open files: %d", rl.rlim_cur);
#endif
			// deduct some margin for epoll/kqueue, log files,
			// futexes, shared objects etc.
			rl.rlim_cur -= 20;

			// 80% of the available file descriptors should go to connections
			m_settings.set_int(settings_pack::connections_limit, (std::min)(
				m_settings.get_int(settings_pack::connections_limit)
				, int(rl.rlim_cur * 8 / 10)));
			// 20% goes towards regular files (see disk_io_thread)
#if defined TORRENT_LOGGING
			session_log("   max connections: %d", m_settings.get_int(settings_pack::connections_limit));
			session_log("   max files: %d", int(rl.rlim_cur * 2 / 10));
#endif
		}
#endif // TORRENT_USE_RLIMIT


#if defined TORRENT_LOGGING
		session_log(" generated peer ID: %s", m_peer_id.to_string().c_str());
#endif

		settings_pack* copy = new settings_pack(pack);
		m_io_service.post(boost::bind(&session_impl::apply_settings_pack, this, copy));
		// call update_* after settings set initialized
		m_io_service.post(boost::bind(&session_impl::init_settings, this));

#if defined TORRENT_LOGGING
		session_log(" spawning network thread");
#endif
		m_thread.reset(new thread(boost::bind(&session_impl::main_thread, this)));
	}

	void session_impl::init_settings()
	{
#ifndef TORRENT_NO_DEPRECATE
		update_local_download_rate();
		update_local_upload_rate();
#endif
		update_download_rate();
		update_upload_rate();
		update_connections_limit();
		update_unchoke_limit();
		update_disk_threads();
		update_network_threads();
		update_upnp();
		update_natpmp();
		update_lsd();
		update_dht();
		update_peer_fingerprint();

		if (m_listen_sockets.empty())
		{
			update_listen_interfaces();
			open_listen_port();
		}
	}

	void session_impl::async_resolve(std::string const& host, int flags
		, session_interface::callback_t const& h)
	{
		m_host_resolver.async_resolve(host, flags, h);
	}

	void session_impl::queue_async_resume_data(boost::shared_ptr<torrent> const& t)
	{
		INVARIANT_CHECK;

		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);

		if (m_num_save_resume + m_alerts.num_queued_resume() >= loaded_limit
			&& m_user_load_torrent
			&& loaded_limit > 0)
		{
			TORRENT_ASSERT(t);
			// do loaded torrents first, otherwise they'll just be
			// evicted and have to be loaded again
			if (t->is_loaded())
				m_save_resume_queue.push_front(t);
			else
				m_save_resume_queue.push_back(t);
			return;
		}

		if (t->do_async_save_resume_data())
			++m_num_save_resume;
	}

	// this is called whenever a save_resume_data comes back
	// from the disk thread
	void session_impl::done_async_resume()
	{
		TORRENT_ASSERT(m_num_save_resume > 0);
		--m_num_save_resume;
	}

	// this is called when one or all save resume alerts are
	// popped off the alert queue
	void session_impl::async_resume_dispatched(int num_popped_resume)
	{
		INVARIANT_CHECK;

		int num_queued_resume = m_alerts.num_queued_resume();

		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);
		while (!m_save_resume_queue.empty()
			&& (m_num_save_resume + num_queued_resume < loaded_limit
			|| loaded_limit == 0))
		{
			boost::shared_ptr<torrent> t = m_save_resume_queue.front();
			m_save_resume_queue.erase(m_save_resume_queue.begin());
			if (t->do_async_save_resume_data())
				++m_num_save_resume;
		}
	}

	void session_impl::init()
	{
#if defined TORRENT_LOGGING
		session_log(" *** session thread init");
#endif

		// this is where we should set up all async operations. This
		// is called from within the network thread as opposed to the
		// constructor which is called from the main thread

#if defined TORRENT_ASIO_DEBUGGING
		async_inc_threads();
		add_outstanding_async("session_impl::on_tick");
#endif
		error_code ec;
		m_io_service.post(boost::bind(&session_impl::on_tick, this, ec));

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_lsd_announce");
#endif
		int delay = (std::max)(m_settings.get_int(settings_pack::local_service_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait(
			boost::bind(&session_impl::on_lsd_announce, this, _1));
		TORRENT_ASSERT(!ec);

#ifndef TORRENT_DISABLE_DHT
		update_dht_announce_interval();
#endif

#if defined TORRENT_LOGGING
		session_log(" done starting session");
#endif
	}

	void session_impl::save_state(entry* eptr, boost::uint32_t flags) const
	{
		TORRENT_ASSERT(is_single_thread());

		entry& e = *eptr;

		entry::dictionary_type& sett = e["settings"].dict();
		save_settings_to_dict(m_settings, sett);

#ifndef TORRENT_DISABLE_DHT
		if (flags & session::save_dht_settings)
		{
			entry::dictionary_type& dht_sett = e["dht"].dict();
		
			dht_sett["max_peers_reply"] = m_dht_settings.max_peers_reply;
			dht_sett["search_branching"] = m_dht_settings.search_branching;
			dht_sett["max_fail_count"] = m_dht_settings.max_fail_count;
			dht_sett["max_torrents"] = m_dht_settings.max_torrents;
			dht_sett["max_dht_items"] = m_dht_settings.max_dht_items;
			dht_sett["max_torrent_search_reply"] = m_dht_settings.max_torrent_search_reply;
			dht_sett["restrict_routing_ips"] = m_dht_settings.restrict_routing_ips;
			dht_sett["extended_routing_table"] = m_dht_settings.extended_routing_table;
		}

		if (m_dht && (flags & session::save_dht_state))
		{
			e["dht state"] = m_dht->state();
		}
#endif

#ifndef TORRENT_NO_DEPRECATE
		if (flags & session::save_feeds)
		{
			entry::list_type& feeds = e["feeds"].list();
			for (std::vector<boost::shared_ptr<feed> >::const_iterator i =
				m_feeds.begin(), end(m_feeds.end()); i != end; ++i)
			{
				feeds.push_back(entry());
				(*i)->save_state(feeds.back());
			}
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::const_iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->save_state(*eptr);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif
	}

	proxy_settings session_impl::proxy() const
	{
		return proxy_settings(m_settings);
	}
	
	void session_impl::load_state(bdecode_node const* e)
	{
		TORRENT_ASSERT(is_single_thread());

		bdecode_node settings;
		if (e->type() != bdecode_node::dict_t) return;

#ifndef TORRENT_DISABLE_DHT
		// load from the old settings names
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
			val = settings.dict_find_int("max_torrent_search_reply");
			if (val) m_dht_settings.max_torrent_search_reply = val.int_value();
			val = settings.dict_find_int("restrict_routing_ips");
			if (val) m_dht_settings.restrict_routing_ips = val.int_value();
			val = settings.dict_find_int("extended_routing_table");
			if (val) m_dht_settings.extended_routing_table = val.int_value();
		}
#endif

#ifndef TORRENT_NO_DEPRECATE
		settings = e->dict_find_dict("proxy");
		if (settings)
		{
			bdecode_node val;
			val = settings.dict_find_int("port");
			if (val) m_settings.set_int(settings_pack::proxy_port, val.int_value());
			val = settings.dict_find_int("type");
			if (val) m_settings.set_int(settings_pack::proxy_type, val.int_value());
			val = settings.dict_find_int("proxy_hostnames");
			if (val) m_settings.set_bool(settings_pack::proxy_hostnames, val.int_value());
			val = settings.dict_find_int("proxy_peer_connections");
			if (val) m_settings.set_bool(settings_pack::proxy_peer_connections, val.int_value());
			val = settings.dict_find_string("hostname");
			if (val) m_settings.set_str(settings_pack::proxy_hostname, val.string_value());
			val = settings.dict_find_string("password");
			if (val) m_settings.set_str(settings_pack::proxy_password, val.string_value());
			val = settings.dict_find_string("username");
			if (val) m_settings.set_str(settings_pack::proxy_username, val.string_value());
		}

		settings = e->dict_find_dict("encryption");
		if (settings)
		{
			bdecode_node val;
			val = settings.dict_find_int("prefer_rc4");
			if (val) m_settings.set_bool(settings_pack::prefer_rc4, val.int_value());
			val = settings.dict_find_int("out_enc_policy");
			if (val) m_settings.set_int(settings_pack::out_enc_policy, val.int_value());
			val = settings.dict_find_int("in_enc_policy");
			if (val) m_settings.set_int(settings_pack::in_enc_policy, val.int_value());
			val = settings.dict_find_int("allowed_enc_level");
			if (val) m_settings.set_int(settings_pack::allowed_enc_level, val.int_value());
		}
#endif
		
		settings = e->dict_find_dict("settings");
		if (settings)
		{
			settings_pack* pack = load_pack_from_dict(settings);
			apply_settings_pack(pack);
		}

		// in case we just set a socks proxy, we might have to
		// open the socks incoming connection
		if (!m_socks_listen_socket) open_new_incoming_socks_connection();
		m_udp_socket.set_proxy_settings(proxy());

#ifndef TORRENT_DISABLE_DHT
		settings = e->dict_find_dict("dht state");
		if (settings)
		{
			m_dht_state = settings;
		}
#endif

#ifndef TORRENT_NO_DEPRECATE
		settings = e->dict_find_list("feeds");
		if (settings)
		{
			m_feeds.reserve(settings.list_size());
			for (int i = 0; i < settings.list_size(); ++i)
			{
				if (settings.list_at(i).type() != bdecode_node::dict_t) continue;
				boost::shared_ptr<feed> f(new_feed(*this, feed_settings()));
				f->load_state(settings.list_at(i));
				f->update_feed();
				m_feeds.push_back(f);
			}
			update_rss_feeds();
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->load_state(*e);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif
	}

#ifndef TORRENT_DISABLE_EXTENSIONS

	typedef boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext_function_t;

	struct session_plugin_wrapper : plugin
	{
		session_plugin_wrapper(ext_function_t const& f) : m_f(f) {}

		virtual boost::shared_ptr<torrent_plugin> new_torrent(torrent* t, void* user)
		{ return m_f(t, user); }
		ext_function_t m_f;
	};

	void session_impl::add_extension(ext_function_t ext)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT_VAL(ext, ext);

		boost::shared_ptr<plugin> p(new session_plugin_wrapper(ext));

		m_ses_extensions.push_back(p);
	}

	void session_impl::add_ses_extension(boost::shared_ptr<plugin> ext)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT_VAL(ext, ext);

		m_ses_extensions.push_back(ext);
		m_alerts.add_extension(ext);
		ext->added(this);
	}
#endif

#ifndef TORRENT_NO_DEPRECATE
	feed_handle session_impl::add_feed(feed_settings const& sett)
	{
		TORRENT_ASSERT(is_single_thread());

		// look for duplicates. If we already have a feed with this
		// URL, return a handle to the existing one
		for (std::vector<boost::shared_ptr<feed> >::const_iterator i
			= m_feeds.begin(), end(m_feeds.end()); i != end; ++i)
		{
			if (sett.url != (*i)->m_settings.url) continue;
			return feed_handle(*i);
		}

		boost::shared_ptr<feed> f(new_feed(*this, sett));
		m_feeds.push_back(f);
		update_rss_feeds();
		return feed_handle(f);
	}

	void session_impl::remove_feed(feed_handle h)
	{
		TORRENT_ASSERT(is_single_thread());

		boost::shared_ptr<feed> f = h.m_feed_ptr.lock();
		if (!f) return;

		std::vector<boost::shared_ptr<feed> >::iterator i
			= std::find(m_feeds.begin(), m_feeds.end(), f);

		if (i == m_feeds.end()) return;

		m_feeds.erase(i);
	}

	void session_impl::get_feeds(std::vector<feed_handle>* ret) const
	{
		TORRENT_ASSERT(is_single_thread());

		ret->clear();
		ret->reserve(m_feeds.size());
		for (std::vector<boost::shared_ptr<feed> >::const_iterator i = m_feeds.begin()
			, end(m_feeds.end()); i != end; ++i)
			ret->push_back(feed_handle(*i));
	}
#endif

	void session_impl::pause()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_paused) return;
#if defined TORRENT_LOGGING
		session_log(" *** session paused ***");
#endif
		m_paused = true;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent& t = *i->second;
			t.do_pause();
		}
	}

	void session_impl::resume()
	{
		TORRENT_ASSERT(is_single_thread());

		if (!m_paused) return;
		m_paused = false;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent& t = *i->second;
			t.do_resume();
			if (t.should_check_files()) t.start_checking();
		}
	}
	
	void session_impl::abort()
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;
#if defined TORRENT_LOGGING
		session_log(" *** ABORT CALLED ***");
#endif

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

		for (std::set<boost::shared_ptr<socket_type> >::iterator i = m_incoming_sockets.begin()
			, end(m_incoming_sockets.end()); i != end; ++i)
		{
			(*i)->close(ec);
			TORRENT_ASSERT(!ec);
		}
		m_incoming_sockets.clear();

		// close the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->sock->close(ec);
			TORRENT_ASSERT(!ec);
		}
		m_listen_sockets.clear();
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

#if defined TORRENT_LOGGING
		session_log(" aborting all torrents (%d)", m_torrents.size());
#endif
		// abort all torrents
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->abort();
		}
		m_torrents.clear();

#if defined TORRENT_LOGGING
		session_log(" aborting all tracker requests");
#endif
		m_tracker_manager.abort_all_requests();

#if defined TORRENT_LOGGING
		session_log(" aborting all connections (%d)", m_connections.size());
#endif
		// abort all connections
		while (!m_connections.empty())
		{
#if TORRENT_USE_ASSERTS
			int conn = m_connections.size();
#endif
			(*m_connections.begin())->disconnect(errors::stopping_torrent, op_bittorrent);
			TORRENT_ASSERT_VAL(conn == int(m_connections.size()) + 1, conn);
		}

		m_download_rate.close();
		m_upload_rate.close();

		// #error closing the udp socket here means that
		// the uTP connections cannot be closed gracefully
		m_udp_socket.close();
		m_external_udp_port = 0;
#ifdef TORRENT_USE_OPENSSL
		m_ssl_udp_socket.close();
#endif

		m_undead_peers.clear();

		// it's OK to detach the threads here. The disk_io_thread
		// has an internal counter and won't release the network
		// thread until they're all dead (via m_work).
		m_disk_thread.set_num_threads(0, false);
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
			i->second->ip_filter_updated();
	}

	void session_impl::set_ip_filter(ip_filter const& f)
	{
		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->port_filter_updated();
	}

	ip_filter& session_impl::get_ip_filter()
	{
		return m_ip_filter;
	}

	port_filter const& session_impl::get_port_filter() const
	{
		return m_port_filter;
	}

	template <class Socket>
	void static set_socket_buffer_size(Socket& s, session_settings const& sett, error_code& ec)
	{
		int snd_size = sett.get_int(settings_pack::send_socket_buffer_size);
		if (snd_size)
		{
			stream_socket::send_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != snd_size)
			{
				stream_socket::send_buffer_size option(snd_size);
				s.set_option(option, ec);
				if (ec)
				{
					// restore previous value
					s.set_option(prev_option, ec);
					return;
				}
			}
		}
		int recv_size = sett.get_int(settings_pack::recv_socket_buffer_size);
		if (recv_size)
		{
			stream_socket::receive_buffer_size prev_option;
			s.get_option(prev_option, ec);
			if (!ec && prev_option.value() != recv_size)
			{
				stream_socket::receive_buffer_size option(recv_size);
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

	int session_impl::create_peer_class(char const* name)
	{
		return m_classes.new_peer_class(name);
	}

	void session_impl::delete_peer_class(int cid)
	{
		// if you hit this assert, you're deleting a non-existent peer class
		TORRENT_ASSERT(m_classes.at(cid));
		if (m_classes.at(cid) == 0) return;
		m_classes.decref(cid);
	}

	peer_class_info session_impl::get_peer_class(int cid)
	{
		peer_class_info ret;
		peer_class* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT(pc);
		if (pc == 0)
		{
#ifdef TORRENT_DEBUG
			// make it obvious that the return value is undefined
			ret.upload_limit = random();
			ret.download_limit = random();
			ret.label.resize(20);
			url_random(&ret.label[0], &ret.label[0] + 20);
			ret.ignore_unchoke_slots = false;
#endif
			return ret;
		}

		pc->get_info(&ret);
		return ret;
	}

	void session_impl::queue_tracker_request(tracker_request& req
		, boost::weak_ptr<request_callback> c)
	{
		req.listen_port = listen_port();
		if (m_key) req.key = m_key;

#ifdef TORRENT_USE_OPENSSL
		// SSL torrents use the SSL listen port
		if (req.ssl_ctx) req.listen_port = ssl_listen_port();
		req.ssl_ctx = &m_ssl_ctx;
#endif
#if TORRENT_USE_I2P
		req.i2pconn = &m_i2p_conn;
#endif

		if (is_any(req.bind_ip)) req.bind_ip = m_listen_interface.address();
		m_tracker_manager.queue_request(get_io_service(), req, c);
	}

	void session_impl::set_peer_class(int cid, peer_class_info const& pci)
	{
		peer_class* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT(pc);
		if (pc == 0) return;

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
		boost::uint32_t peer_class_mask = m_peer_class_filter.access(a);

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

			if (m_classes.at(i) == 0) continue;
			s->add_class(m_classes, i);
		}
	}

	bool session_impl::ignore_unchoke_slots_set(peer_class_set const& set) const
	{
		int num = set.num_classes();
		for (int i = 0; i < num; ++i)
		{
			peer_class const* pc = m_classes.at(set.class_at(i));
			if (pc == 0) continue;
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
		// its prev and next links will be NULL, even though
		// it's already in the list. Cover this case by also
		// checking to see if it's the first item
		if (t->next != NULL || t->prev != NULL || m_torrent_lru.front() == t)
		{
#ifdef TORRENT_DEBUG
			torrent* i = (torrent*)m_torrent_lru.front();
			while (i != NULL && i != t) i = (torrent*)i->next;
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

		TORRENT_ASSERT(t->next != NULL || t->prev != NULL || m_torrent_lru.front() == t);

#if defined TORRENT_DEBUG && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		torrent* i = (torrent*)m_torrent_lru.front();
		while (i != NULL && i != t) i = (torrent*)i->next;
		TORRENT_ASSERT(i == t);
#endif
		
		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);

		// 0 means unlimited, never evict enything
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

		// 0 means unlimited, never evict enything
		if (loaded_limit == 0) return;

		// if the torrent we're ignoring (i.e. making room for), allow
		// one more torrent in the list.
		if (ignore->next != NULL || ignore->prev != NULL || m_torrent_lru.front() == ignore)
		{
#ifdef TORRENT_DEBUG
			torrent* i = (torrent*)m_torrent_lru.front();
			while (i != NULL && i != ignore) i = (torrent*)i->next;
			TORRENT_ASSERT(i == ignore);
#endif
			++loaded_limit;
		}

		while (m_torrent_lru.size() >= loaded_limit)
		{
			// we're at the limit of loaded torrents. Find the least important
			// torrent and unload it. This is done with an LRU.
			torrent* i = (torrent*)m_torrent_lru.front();

			if (i == ignore)
			{
				i = (torrent*)i->next;
				if (i == NULL) break;
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
		TORRENT_ASSERT(t->next == NULL && t->prev == NULL && m_torrent_lru.front() != t);

		// now, load t into RAM
		std::vector<char> buffer;
		error_code ec;
		m_user_load_torrent(t->info_hash(), buffer, ec);
		if (ec)
		{
			t->set_error(ec, torrent::error_file_metadata);
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
		m_io_service.post(boost::bind(&session_impl::submit_disk_jobs, this));
	}

	void session_impl::submit_disk_jobs()
	{
		TORRENT_ASSERT(m_deferred_submit_disk_jobs);
		m_deferred_submit_disk_jobs = false;
		if (m_abort) return;
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
			if (pc == 0) continue;
			bandwidth_channel* chan = &pc->channel[channel];
			// no need to include channels that don't have any bandwidth limits
			if (chan->throttle() == 0) continue;
			dst[num_copied] = chan;
			++num_copied;
			if (num_copied == max) break;
		}
		return num_copied;
	}

	bool session_impl::use_quota_overhead(bandwidth_channel* ch, int channel, int amount)
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
			if (p == 0) continue;
			bandwidth_channel* ch = &p->channel[peer_connection::download_channel];
			if (use_quota_overhead(ch, peer_connection::download_channel, amount_down))
				ret |= 1 << peer_connection::download_channel;
			ch = &p->channel[peer_connection::upload_channel];
			if (use_quota_overhead(ch, peer_connection::upload_channel, amount_up))
				ret |= 1 << peer_connection::upload_channel;
		}
		return ret;
	}

	// session_impl is responsible for deleting 'pack', but it
	// will pass it on to the disk io thread, which will take
	// over ownership of it
	void session_impl::apply_settings_pack(settings_pack* pack)
	{
		bool reopen_listen_port =
			(pack->has_val(settings_pack::ssl_listen)
				&& pack->get_int(settings_pack::ssl_listen)
					!= m_settings.get_int(settings_pack::ssl_listen))
			|| (pack->has_val(settings_pack::listen_interfaces)
				&& pack->get_str(settings_pack::listen_interfaces)
					!= m_settings.get_str(settings_pack::listen_interfaces));

		apply_pack(pack, m_settings, this);
		m_disk_thread.set_settings(pack);
		delete pack;

		if (reopen_listen_port)
		{
			error_code ec;
			open_listen_port();
		}
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::set_settings(libtorrent::session_settings const& s)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());
		settings_pack* p = load_pack_from_struct(m_settings, s);
		apply_settings_pack(p);
	}

	libtorrent::session_settings session_impl::deprecated_settings() const
	{
		libtorrent::session_settings ret;

		load_struct_from_settings(m_settings, ret);
		return ret;
	}
#endif

	tcp::endpoint session_impl::get_ipv6_interface() const
	{
		return m_ipv6_interface;
	}

	tcp::endpoint session_impl::get_ipv4_interface() const
	{
		return m_ipv4_interface;
	}

	enum { listen_no_system_port = 0x02 };

	listen_socket_t session_impl::setup_listener(std::string const& device
		, bool ipv4, int port, int& retries, int flags, error_code& ec)
	{
		listen_socket_t ret;
		ret.ssl = flags & open_ssl_socket;
		int last_op = 0;
		listen_failed_alert::socket_type_t sock_type = (flags & open_ssl_socket)
			? listen_failed_alert::tcp_ssl : listen_failed_alert::tcp;
		ret.sock.reset(new socket_acceptor(m_io_service));
		ret.sock->open(ipv4 ? tcp::v4() : tcp::v6(), ec);
		last_op = listen_failed_alert::open;
		if (ec)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(device, last_op, ec, sock_type));

#if defined TORRENT_LOGGING
			session_log("failed to open socket: %s: %s"
				, device.c_str(), ec.message().c_str());
#endif
			return ret;
		}

		// SO_REUSEADDR on windows is a bit special. It actually allows
		// two active sockets to bind to the same port. That means we
		// may end up binding to the same socket as some other random
		// application. Don't do it!
#ifndef TORRENT_WINDOWS
		error_code err; // ignore errors here
		ret.sock->set_option(socket_acceptor::reuse_address(true), err);
#endif

#if TORRENT_USE_IPV6
		if (!ipv4)
		{
			error_code err; // ignore errors here
#ifdef IPV6_V6ONLY
			ret.sock->set_option(v6only(true), err);
#endif
#ifdef TORRENT_WINDOWS

#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
			// enable Teredo on windows
			ret.sock->set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), err);
#endif
		}
#endif // TORRENT_USE_IPV6

		address bind_ip = bind_to_device(m_io_service, *ret.sock, ipv4
			, device.c_str(), port, ec);

		if (ec == error_code(boost::system::errc::no_such_device, generic_category()))
			return ret;

		while (ec && retries > 0)
		{
#if defined TORRENT_LOGGING
			session_log("failed to bind to interface [%s] \"%s\": %s"
				, device.c_str(), bind_ip.to_string(ec).c_str()
				, ec.message().c_str());
#endif
			ec.clear();
			TORRENT_ASSERT_VAL(!ec, ec);
			--retries;
			port += 1;
			bind_ip = bind_to_device(m_io_service, *ret.sock, ipv4
				, device.c_str(), port, ec);
			last_op = listen_failed_alert::bind;
		}
		if (ec && !(flags & listen_no_system_port))
		{
			// instead of giving up, trying
			// let the OS pick a port
			port = 0;
			ec.clear();
			bind_ip = bind_to_device(m_io_service, *ret.sock, ipv4
				, device.c_str(), port, ec);
			last_op = listen_failed_alert::bind;
		}
		if (ec)
		{
			// not even that worked, give up
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(device, last_op, ec, sock_type));
#if defined TORRENT_LOGGING
			session_log("cannot bind to interface \"%s\": %s"
				, device.c_str(), ec.message().c_str());
#endif
			return ret;
		}
		ret.external_port = ret.sock->local_endpoint(ec).port();
		TORRENT_ASSERT(ret.external_port == port || port == 0);
		last_op = listen_failed_alert::get_peer_name;
		if (!ec)
		{
			ret.sock->listen(m_settings.get_int(settings_pack::listen_queue_size), ec);
			last_op = listen_failed_alert::listen;
		}
		if (ec)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(device, last_op, ec, sock_type));
#if defined TORRENT_LOGGING
			session_log("cannot listen on interface \"%s\": %s"
				, device.c_str(), ec.message().c_str());
#endif
			return ret;
		}

		// if we asked the system to listen on port 0, which
		// socket did it end up choosing?
		if (port == 0)
		{
			port = ret.sock->local_endpoint(ec).port();
			last_op = listen_failed_alert::get_peer_name;
			if (ec)
			{
				if (m_alerts.should_post<listen_failed_alert>())
					m_alerts.post_alert(listen_failed_alert(device, last_op, ec, sock_type));
#if defined TORRENT_LOGGING
				session_log("failed to get peer name \"%s\": %s"
					, device.c_str(), ec.message().c_str());
#endif
				return ret;
			}
		}

		if (m_alerts.should_post<listen_succeeded_alert>())
			m_alerts.post_alert(listen_succeeded_alert(tcp::endpoint(bind_ip, port)
				, (flags & open_ssl_socket) ? listen_succeeded_alert::tcp_ssl
				: listen_succeeded_alert::tcp));

#if defined TORRENT_LOGGING
		session_log(" listening on: %s external port: %d"
			, print_endpoint(tcp::endpoint(bind_ip, port)).c_str(), ret.external_port);
#endif
		return ret;
	}
	
	void session_impl::open_listen_port()
	{
#if defined TORRENT_LOGGING
		session_log("open listen port");
#endif

		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(!m_abort);
		int flags = m_settings.get_bool(settings_pack::listen_system_port_fallback) ? 0 : listen_no_system_port;
		error_code ec;

		// reset the retry counter
		m_listen_port_retries = m_settings.get_int(settings_pack::max_retry_port_bind);

retry:

		// close the open listen sockets
		// close the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
			i->sock->close(ec);
		m_listen_sockets.clear();
		m_stats_counters.set_value(counters::has_incoming_connections, 0);
		ec.clear();

		if (m_abort) return;

		m_ipv6_interface = tcp::endpoint();
		m_ipv4_interface = tcp::endpoint();

		// TODO: instead of having a special case for this, just make the
		// default listen interfaces be "0.0.0.0:6881,[::1]:6881" and use
		// the generic path. That would even allow for not listening at all.
		if (m_listen_interfaces.empty())
		{
			// this means we should open two listen sockets
			// one for IPv4 and one for IPv6
		
			listen_socket_t s = setup_listener("0.0.0.0", true
				, m_listen_interface.port()
				, m_listen_port_retries, flags, ec);

			if (s.sock)
			{
				// update the listen_interface member with the
				// actual port we ended up listening on, so that the other
				// sockets can be bound to the same one
				m_listen_interface.port(s.external_port);

				TORRENT_ASSERT(!m_abort);
				m_listen_sockets.push_back(s);
			}

#ifdef TORRENT_USE_OPENSSL
			if (m_settings.get_int(settings_pack::ssl_listen))
			{
				int retries = 10;
				listen_socket_t s = setup_listener("0.0.0.0", true
					, m_settings.get_int(settings_pack::ssl_listen)
					, retries, flags | open_ssl_socket, ec);

				if (s.sock)
				{
					TORRENT_ASSERT(!m_abort);
					m_listen_sockets.push_back(s);
				}
			}
#endif

#if TORRENT_USE_IPV6
			// only try to open the IPv6 port if IPv6 is installed
			if (supports_ipv6())
			{
				listen_socket_t s = setup_listener("::1", false, m_listen_interface.port()
					, m_listen_port_retries, flags, ec);

				if (s.sock)
				{
					TORRENT_ASSERT(!m_abort);
					m_listen_sockets.push_back(s);
				}

#ifdef TORRENT_USE_OPENSSL
				if (m_settings.get_int(settings_pack::ssl_listen))
				{
					s.ssl = true;
					int retries = 10;
					listen_socket_t s = setup_listener("::1", false
						, m_settings.get_int(settings_pack::ssl_listen)
						, retries, flags | open_ssl_socket, ec);

					if (s.sock)
					{
						TORRENT_ASSERT(!m_abort);
						m_listen_sockets.push_back(s);
					}
				}
#endif // TORRENT_USE_OPENSSL
			}
#endif // TORRENT_USE_IPV6

			// set our main IPv4 and IPv6 interfaces
			// used to send to the tracker
			std::vector<ip_interface> ifs = enum_net_interfaces(m_io_service, ec);
			for (std::vector<ip_interface>::const_iterator i = ifs.begin()
					, end(ifs.end()); i != end; ++i)
			{
				address const& addr = i->interface_address;
				if (addr.is_v6() && !is_local(addr) && !is_loopback(addr))
					m_ipv6_interface = tcp::endpoint(addr, m_listen_interface.port());
				else if (addr.is_v4() && !is_local(addr) && !is_loopback(addr))
					m_ipv4_interface = tcp::endpoint(addr, m_listen_interface.port());
			}
		}
		else
		{
			// TODO: 2 the udp socket(s) should be using the same generic
			// mechanism and not be restricted to a single one
			// we should open a one listen socket for each entry in the
			// listen_interfaces list
			for (int i = 0; i < m_listen_interfaces.size(); ++i)
			{
				std::string const& device = m_listen_interfaces[i].first;
				int port = m_listen_interfaces[i].second;

				int num_device_fails = 0;
				
#if TORRENT_USE_IPV6
				const int first_family = 0;
#else
				const int first_family = 1;
#endif
				for (int address_family = first_family; address_family < 2; ++address_family)
				{
					error_code err;
					address test_family = address::from_string(device.c_str(), err);
					if (!err && test_family.is_v4() != address_family)
						continue;

					listen_socket_t s = setup_listener(device, address_family, port
						, m_listen_port_retries, flags, ec);

					if (ec == error_code(boost::system::errc::no_such_device, generic_category()))
					{
						++num_device_fails;
						continue;
					}

					if (s.sock)
					{
						TORRENT_ASSERT(!m_abort);
						m_listen_sockets.push_back(s);

						tcp::endpoint bind_ep = s.sock->local_endpoint(ec);
#if TORRENT_USE_IPV6
						if (bind_ep.address().is_v6())
							m_ipv6_interface = bind_ep;
						else
#endif
							m_ipv4_interface = bind_ep;
					}

#ifdef TORRENT_USE_OPENSSL
					if (m_settings.get_int(settings_pack::ssl_listen))
					{
						int retries = 10;

						listen_socket_t s = setup_listener(device, address_family
							, m_settings.get_int(settings_pack::ssl_listen)
							, m_listen_port_retries, flags | open_ssl_socket, ec);

						if (s.sock)
						{
							TORRENT_ASSERT(!m_abort);
							m_listen_sockets.push_back(s);
						}
					}
#endif
				}

				if (num_device_fails == 2)
				{
					// only report this if both IPv4 and IPv6 fails for a device
					if (m_alerts.should_post<listen_failed_alert>())
						m_alerts.post_alert(listen_failed_alert(device
							, listen_failed_alert::bind
							, error_code(boost::system::errc::no_such_device, generic_category())
							, listen_failed_alert::tcp));
				}
			}
		}

		if (m_listen_sockets.empty() && ec)
		{
#if defined TORRENT_LOGGING
			session_log("cannot bind TCP listen socket to interface \"%s\": %s"
				, print_endpoint(m_listen_interface).c_str(), ec.message().c_str());
#endif
			if (m_listen_port_retries > 0)
			{
				m_listen_interface.port(m_listen_interface.port() + 1);
				--m_listen_port_retries;
				goto retry;
			}
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(print_endpoint(m_listen_interface)
					, listen_failed_alert::bind, ec, listen_failed_alert::udp));
			return;
		}

#ifdef TORRENT_USE_OPENSSL
		int ssl_port = m_settings.get_int(settings_pack::ssl_listen);

		// if ssl port is 0, we don't want to listen on an SSL port
		if (ssl_port != 0)
		{
			udp::endpoint ssl_bind_if(m_listen_interface.address(), ssl_port);

			// TODO: 2 use bind_to_device in udp_socket
			m_ssl_udp_socket.bind(ssl_bind_if, ec);
			if (ec)
			{
#if defined TORRENT_LOGGING
				session_log("SSL: cannot bind to UDP interface \"%s\": %s"
					, print_endpoint(m_listen_interface).c_str(), ec.message().c_str());
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					error_code err;
					m_alerts.post_alert(listen_failed_alert(print_endpoint(ssl_bind_if)
							, listen_failed_alert::bind, ec, listen_failed_alert::utp_ssl));
				}
				ec.clear();
			}
			else
			{
				if (m_alerts.should_post<listen_succeeded_alert>())
					m_alerts.post_alert(listen_succeeded_alert(
							tcp::endpoint(ssl_bind_if.address(), ssl_bind_if.port())
							, listen_succeeded_alert::utp_ssl));
			}
		}
#endif // TORRENT_USE_OPENSSL

		// TODO: 2 use bind_to_device in udp_socket
		m_udp_socket.bind(udp::endpoint(m_listen_interface.address(), m_listen_interface.port()), ec);
		if (ec)
		{
#if defined TORRENT_LOGGING
			session_log("cannot bind to UDP interface \"%s\": %s"
				, print_endpoint(m_listen_interface).c_str(), ec.message().c_str());
#endif
			if (m_listen_port_retries > 0)
			{
				m_listen_interface.port(m_listen_interface.port() + 1);
				--m_listen_port_retries;
				goto retry;
			}
			if (m_alerts.should_post<listen_failed_alert>())
			{
				error_code err;
				m_alerts.post_alert(listen_failed_alert(print_endpoint(m_listen_interface)
					, listen_failed_alert::bind, ec, listen_failed_alert::udp));
			}
			return;
		}
		else
		{
			m_external_udp_port = m_udp_socket.local_port();
			maybe_update_udp_mapping(0, m_listen_interface.port(), m_listen_interface.port());
			maybe_update_udp_mapping(1, m_listen_interface.port(), m_listen_interface.port());
			if (m_alerts.should_post<listen_succeeded_alert>())
				m_alerts.post_alert(listen_succeeded_alert(m_listen_interface, listen_succeeded_alert::udp));
		}

		if (m_settings.get_int(settings_pack::peer_tos) != 0)
		{
			update_peer_tos();
		}

		ec.clear();

		set_socket_buffer_size(m_udp_socket, m_settings, ec);
		if (ec)
		{
			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.post_alert(udp_error_alert(udp::endpoint(), ec));
		}

		// initiate accepting on the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
			async_accept(i->sock, i->ssl);

		open_new_incoming_socks_connection();
#if TORRENT_USE_I2P
		open_new_incoming_i2p_connection();
#endif

		if (!m_listen_sockets.empty())
		{
			tcp::endpoint local = m_listen_sockets.front().sock->local_endpoint(ec);
			if (!ec) remap_tcp_ports(3, local.port(), ssl_listen_port());
		}
	}

	void session_impl::remap_tcp_ports(boost::uint32_t mask, int tcp_port, int ssl_port)
	{
		if ((mask & 1) && m_natpmp)
		{
			if (m_tcp_mapping[0] != -1) m_natpmp->delete_mapping(m_tcp_mapping[0]);
			m_tcp_mapping[0] = m_natpmp->add_mapping(natpmp::tcp, tcp_port, tcp_port);
#ifdef TORRENT_USE_OPENSSL
			if (m_ssl_tcp_mapping[0] != -1) m_natpmp->delete_mapping(m_ssl_tcp_mapping[0]);
			if (ssl_port > 0) m_ssl_tcp_mapping[0] = m_natpmp->add_mapping(natpmp::tcp
				, ssl_port, ssl_port);
#endif
		}
		if ((mask & 2) && m_upnp)
		{
			if (m_tcp_mapping[1] != -1) m_upnp->delete_mapping(m_tcp_mapping[1]);
			m_tcp_mapping[1] = m_upnp->add_mapping(upnp::tcp, tcp_port, tcp_port);
#ifdef TORRENT_USE_OPENSSL
			if (m_ssl_tcp_mapping[1] != -1) m_upnp->delete_mapping(m_ssl_tcp_mapping[1]);
			if (ssl_port > 0) m_ssl_tcp_mapping[1] = m_upnp->add_mapping(upnp::tcp
				, ssl_port, ssl_port);
#endif
		}
	}

	void session_impl::open_new_incoming_socks_connection()
	{
		int proxy_type = m_settings.get_int(settings_pack::proxy_type);

		if (proxy_type != settings_pack::socks5
			&& proxy_type != settings_pack::socks5_pw
			&& proxy_type != settings_pack::socks4)
			return;
		
		if (m_socks_listen_socket) return;

		m_socks_listen_socket = boost::shared_ptr<socket_type>(new socket_type(m_io_service));
		bool ret = instantiate_connection(m_io_service, proxy()
			, *m_socks_listen_socket);
		TORRENT_ASSERT_VAL(ret, ret);

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_socks_accept");
#endif
		socks5_stream& s = *m_socks_listen_socket->get<socks5_stream>();
		s.set_command(2); // 2 means BIND (as opposed to CONNECT)
		m_socks_listen_port = m_listen_interface.port();
		if (m_socks_listen_port == 0) m_socks_listen_port = 2000 + random() % 60000;
		s.async_connect(tcp::endpoint(address_v4::any(), m_socks_listen_port)
			, boost::bind(&session_impl::on_socks_accept, this, m_socks_listen_socket, _1));
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
			, boost::bind(&session_impl::on_i2p_open, this, _1));
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
				m_alerts.post_alert(i2p_alert(ec));

#if defined TORRENT_LOGGING
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

		m_i2p_listen_socket = boost::shared_ptr<socket_type>(new socket_type(m_io_service));
		bool ret = instantiate_connection(m_io_service, m_i2p_conn.proxy()
			, *m_i2p_listen_socket);
		TORRENT_ASSERT_VAL(ret, ret);

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_i2p_accept");
#endif
		i2p_stream& s = *m_i2p_listen_socket->get<i2p_stream>();
		s.set_command(i2p_stream::cmd_accept);
		s.set_session_id(m_i2p_conn.session_id());
		s.async_connect(tcp::endpoint(address_v4::any(), m_listen_interface.port())
			, boost::bind(&session_impl::on_i2p_accept, this, m_i2p_listen_socket, _1));
	}

	void session_impl::on_i2p_accept(boost::shared_ptr<socket_type> const& s
		, error_code const& e)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_i2p_accept");
#endif
		m_i2p_listen_socket.reset();
		if (e == asio::error::operation_aborted) return;
		if (e)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert("i2p", listen_failed_alert::accept
						, e, listen_failed_alert::i2p));
#if defined TORRENT_LOGGING
			session_log("cannot bind to port %d: %s"
				, m_listen_interface.port(), e.message().c_str());
#endif
			return;
		}
		open_new_incoming_i2p_connection();
		incoming_connection(s);
	}
#endif

	bool session_impl::incoming_packet(error_code const& ec
		, udp::endpoint const& ep, char const* buf, int size)
	{
		m_stats_counters.inc_stats_counter(counters::on_udp_counter);

		if (ec)
		{
			// don't bubble up operation aborted errors to the user
			if (ec != asio::error::operation_aborted
				&& m_alerts.should_post<udp_error_alert>())
				m_alerts.post_alert(udp_error_alert(ep, ec));

#if defined TORRENT_LOGGING
			session_log("UDP socket error: (%d) %s", ec.value(), ec.message().c_str());
#endif
		}
		return false;
	}

	void session_impl::async_accept(boost::shared_ptr<socket_acceptor> const& listener, bool ssl)
	{
		TORRENT_ASSERT(!m_abort);
		shared_ptr<socket_type> c(new socket_type(m_io_service));
		stream_socket* str = 0;

#ifdef TORRENT_USE_OPENSSL
		if (ssl)
		{
			// accept connections initializing the SSL connection to
			// use the generic m_ssl_ctx context. However, since it has
			// the servername callback set on it, we will switch away from
			// this context into a specific torrent once we start handshaking
			c->instantiate<ssl_stream<stream_socket> >(m_io_service, &m_ssl_ctx);
			str = &c->get<ssl_stream<stream_socket> >()->next_layer();
		}
		else
#endif
		{
			c->instantiate<stream_socket>(m_io_service);
			str = c->get<stream_socket>();
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_accept_connection");
#endif

#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASSERT(ssl == is_ssl(*c));
#endif

		listener->async_accept(*str
			, boost::bind(&session_impl::on_accept_connection, this, c
			, boost::weak_ptr<socket_acceptor>(listener), _1, ssl));
	}

	void session_impl::on_accept_connection(shared_ptr<socket_type> const& s
		, weak_ptr<socket_acceptor> listen_socket, error_code const& e, bool ssl)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_accept_connection");
#endif
		m_stats_counters.inc_stats_counter(counters::on_accept_counter);
		TORRENT_ASSERT(is_single_thread());
		boost::shared_ptr<socket_acceptor> listener = listen_socket.lock();
		if (!listener) return;
		
		if (e == asio::error::operation_aborted) return;

		if (m_abort) return;

		error_code ec;
		if (e)
		{
			tcp::endpoint ep = listener->local_endpoint(ec);
#if defined TORRENT_LOGGING
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
					torrent_map::iterator i = std::max_element(m_torrents.begin()
						, m_torrents.end(), boost::bind(&torrent::num_peers
							, boost::bind(&torrent_map::value_type::second, _1)));

					if (m_alerts.should_post<performance_alert>())
						m_alerts.post_alert(performance_alert(
							torrent_handle(), performance_alert::too_few_file_descriptors));

					if (i != m_torrents.end())
					{
						i->second->disconnect_peers(1, e);
					}

					m_settings.set_int(settings_pack::connections_limit, m_connections.size());
				}
				// try again, but still alert the user of the problem
				async_accept(listener, ssl);
			}
			if (m_alerts.should_post<listen_failed_alert>())
			{
				error_code err;
				m_alerts.post_alert(listen_failed_alert(print_endpoint(ep), listen_failed_alert::accept, e
					, ssl ? listen_failed_alert::tcp_ssl : listen_failed_alert::tcp));
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
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("session_impl::ssl_handshake");
#endif
			s->get<ssl_stream<stream_socket> >()->async_accept_handshake(
				boost::bind(&session_impl::ssl_handshake, this, _1, s));
			m_incoming_sockets.insert(s);
		}
		else
#endif
		{
			incoming_connection(s);
		}
	}

#ifdef TORRENT_USE_OPENSSL

	void session_impl::on_incoming_utp_ssl(boost::shared_ptr<socket_type> const& s)
	{
		TORRENT_ASSERT(is_ssl(*s));

		// for SSL connections, incoming_connection() is called
		// after the handshake is done
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::ssl_handshake");
#endif
		s->get<ssl_stream<utp_stream> >()->async_accept_handshake(
			boost::bind(&session_impl::ssl_handshake, this, _1, s));
		m_incoming_sockets.insert(s);
	}

	// to test SSL connections, one can use this openssl command template:
	// 
	// openssl s_client -cert <client-cert>.pem -key <client-private-key>.pem
	//   -CAfile <torrent-cert>.pem  -debug -connect 127.0.0.1:4433 -tls1
	//   -servername <hex-encoded-info-hash>

	void session_impl::ssl_handshake(error_code const& ec, boost::shared_ptr<socket_type> s)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::ssl_handshake");
#endif
		TORRENT_ASSERT(is_ssl(*s));

		m_incoming_sockets.erase(s);

		error_code e;
		tcp::endpoint endp = s->remote_endpoint(e);
		if (e) return;

#if defined TORRENT_LOGGING
		session_log(" *** peer SSL handshake done [ ip: %s ec: %s socket: %s ]"
			, print_endpoint(endp).c_str(), ec.message().c_str(), s->type_name());
#endif

		if (ec)
		{
			if (m_alerts.should_post<peer_error_alert>())
			{
				m_alerts.post_alert(peer_error_alert(torrent_handle(), endp
					, peer_id(), op_ssl_handshake, ec));
			}
			return;
		}

		incoming_connection(s);
	}

#endif // TORRENT_USE_OPENSSL

	void session_impl::incoming_connection(boost::shared_ptr<socket_type> const& s)
	{
		TORRENT_ASSERT(is_single_thread());

#ifdef TORRENT_USE_OPENSSL
		// add the current time to the PRNG, to add more unpredictability
		boost::uint64_t now = clock_type::now().time_since_epoch().count();
		// assume 12 bits of entropy (i.e. about 8 milliseconds)
		RAND_add(&now, 8, 1.5);
#endif

		if (m_paused)
		{
#if defined TORRENT_LOGGING
			session_log(" <== INCOMING CONNECTION [ ignored, paused ]");
#endif
			return;
		}

		error_code ec;
		// we got a connection request!
		tcp::endpoint endp = s->remote_endpoint(ec);

		if (ec)
		{
#if defined TORRENT_LOGGING
			session_log("%s <== INCOMING CONNECTION FAILED, could "
				"not retrieve remote endpoint "
				, print_endpoint(endp).c_str(), ec.message().c_str());
#endif
			return;
		}

#if defined TORRENT_LOGGING
		session_log(" <== INCOMING CONNECTION %s type: %s"
			, print_endpoint(endp).c_str(), s->type_name());
#endif

		if (!m_settings.get_bool(settings_pack::enable_incoming_utp)
			&& is_utp(*s))
		{
#if defined TORRENT_LOGGING
			session_log("    rejected uTP connection");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.post_alert(peer_blocked_alert(torrent_handle()
					, endp.address(), peer_blocked_alert::utp_disabled));
			return;
		}

		if (!m_settings.get_bool(settings_pack::enable_incoming_tcp)
			&& s->get<stream_socket>())
		{
#if defined TORRENT_LOGGING
			session_log("    rejected TCP connection");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.post_alert(peer_blocked_alert(torrent_handle()
					, endp.address(), peer_blocked_alert::tcp_disabled));
			return;
		}

		// if there are outgoing interfaces specified, verify this
		// peer is correctly bound to on of them
		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
		{
			error_code ec;
			tcp::endpoint local = s->local_endpoint(ec);
			if (ec)
			{
#if defined TORRENT_LOGGING
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
#if defined TORRENT_LOGGING
					session_log("    rejected connection, not allowed local interface: (%d) %s"
						, ec.value(), ec.message().c_str());
#endif
					return;
				}

#if defined TORRENT_LOGGING
				session_log("    rejected connection, not allowed local interface: %s"
					, local.address().to_string(ec).c_str());
#endif
				if (m_alerts.should_post<peer_blocked_alert>())
					m_alerts.post_alert(peer_blocked_alert(torrent_handle()
						, endp.address(), peer_blocked_alert::invalid_local_interface));
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
			&& (m_ip_filter.access(endp.address()) & ip_filter::blocked))
		{
#if defined TORRENT_LOGGING
			session_log("filtered blocked ip");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.post_alert(peer_blocked_alert(torrent_handle()
					, endp.address(), peer_blocked_alert::ip_filter));
			return;
		}

		// check if we have any active torrents
		// if we don't reject the connection
		if (m_torrents.empty())
		{
#if defined TORRENT_LOGGING
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
			if (m_classes.at(pc) == NULL) continue;
			int f = m_classes.at(pc)->connection_limit_factor;
			if (connection_limit_factor < f) connection_limit_factor = f;
		}
		if (connection_limit_factor == 0) connection_limit_factor = 100;

		boost::uint64_t limit = m_settings.get_int(settings_pack::connections_limit);
		limit = limit * 100 / connection_limit_factor;

		// don't allow more connections than the max setting
		// weighed by the peer class' setting
		bool reject = num_connections() >= limit + m_settings.get_int(settings_pack::connections_slack);

		if (reject)
		{
			if (m_alerts.should_post<peer_disconnected_alert>())
			{
				m_alerts.post_alert(
					peer_disconnected_alert(torrent_handle(), endp, peer_id()
						, op_bittorrent, s->type()
						, error_code(errors::too_many_connections, get_libtorrent_category())
						, close_no_reason));
			}
#if defined TORRENT_LOGGING
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
				if (i->second->allows_peers())
				{
					has_active_torrent = true;
					break;
				}
			}
			if (!has_active_torrent)
			{
#if defined TORRENT_LOGGING
				session_log(" There are no _active_ torrents, disconnect");
#endif
			  	return;
			}
		}

		m_stats_counters.inc_stats_counter(counters::incoming_connections);

		if (m_alerts.should_post<incoming_connection_alert>())
			m_alerts.post_alert(incoming_connection_alert(s->type(), endp));

		setup_socket_buffers(*s);

		peer_connection_args pack;
		pack.ses = this;
		pack.sett = &m_settings;
		pack.stats_counters = &m_stats_counters;
		pack.allocator = this;
		pack.disk_thread = &m_disk_thread;
		pack.ios = &m_io_service;
		pack.tor = boost::weak_ptr<torrent>();
		pack.s = s;
		pack.endp = endp;
		pack.peerinfo = 0;

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

	void session_impl::on_socks_accept(boost::shared_ptr<socket_type> const& s
		, error_code const& e)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_socks_accept");
#endif
		m_socks_listen_socket.reset();
		if (e == asio::error::operation_aborted) return;
		if (e)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert("socks5", listen_failed_alert::accept, e
						, listen_failed_alert::socks5));
			return;
		}
		open_new_incoming_socks_connection();
		incoming_connection(s);
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

// too expensive
//		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
//		for (aux::session_impl::torrent_map::const_iterator i = m_torrents.begin()
//			, end(m_torrents.end()); i != end; ++i)
//			TORRENT_ASSERT(!i->second->has_peer((peer_connection*)p));
#endif

#if defined TORRENT_LOGGING
		session_log(" CLOSING CONNECTION %s : %s"
			, print_endpoint(p->remote()).c_str(), ec.message().c_str());
#endif

		TORRENT_ASSERT(p->is_disconnecting());

		TORRENT_ASSERT(sp.use_count() > 0);

		connection_map::iterator i = m_connections.find(sp);
		// make sure the next disk peer round-robin cursor stays valid
		if (i != m_connections.end()) m_connections.erase(i);
	}

	// implements alert_dispatcher
	bool session_impl::post_alert(alert* a)
	{
		if (!m_alerts.should_post(a)) return false;
		m_alerts.post_alert_ptr(a);
		return true;
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
#if defined TORRENT_LOGGING
		session_log(" *** BINDING OUTGOING CONNECTION [ port: %d ]", port);
#endif
		return port;
	}

	// used to cache the current time
	// every 100 ms. This is cheaper
	// than a system call and can be
	// used where more accurate time
	// is not necessary
	extern time_point g_current_time;

	initialize_timer::initialize_timer()
	{
		g_current_time = clock_type::now();
	}

	int session_impl::rate_limit(peer_class_t c, int channel) const
	{
		TORRENT_ASSERT(channel >= 0 && channel <= 1);
		if (channel < 0 || channel > 1) return 0;

		peer_class const* pc = m_classes.at(c);
		if (pc == 0) return 0;
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
		if (pc == 0) return;
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
		return std::find_if(m_connections.begin(), m_connections.end()
			, boost::bind(&boost::shared_ptr<peer_connection>::get, _1) == p)
			!= m_connections.end();
	}

	bool session_impl::any_torrent_has_peer(peer_connection const* p) const
	{
		for (aux::session_impl::torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			if (i->second->has_peer(p)) return true;
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
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_tick");
#endif
		m_stats_counters.inc_stats_counter(counters::on_tick_counter);

		TORRENT_ASSERT(is_single_thread());

		// submit all disk jobs when we leave this function
		deferred_submit_jobs();

		time_point now = clock_type::now();
		aux::g_current_time = now;
// too expensive
//		INVARIANT_CHECK;

		// we have to keep ticking the utp socket manager
		// until they're all closed
		if (m_abort)
		{
			if (m_utp_socket_manager.num_sockets() == 0)
				return;
#if defined TORRENT_ASIO_DEBUGGING
			fprintf(stderr, "uTP sockets left: %d\n", m_utp_socket_manager.num_sockets());
#endif
		}

		if (e == asio::error::operation_aborted) return;

		if (e)
		{
#if defined TORRENT_LOGGING
			session_log("*** TICK TIMER FAILED %s", e.message().c_str());
#endif
			::abort();
			return;
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_tick");
#endif
		error_code ec;
		m_timer.expires_at(now + milliseconds(m_settings.get_int(settings_pack::tick_interval)), ec);
		m_timer.async_wait(bind(&session_impl::on_tick, this, _1));

		m_download_rate.update_quotas(now - m_last_tick);
		m_upload_rate.update_quotas(now - m_last_tick);

		m_last_tick = now;

		m_utp_socket_manager.tick(now);

		// only tick the following once per second
		if (now - m_last_second_tick < seconds(1)) return;

#ifndef TORRENT_DISABLE_DHT
		if (m_dht
			&& m_dht_interval_update_torrents < 40
			&& m_dht_interval_update_torrents != int(m_torrents.size()))
			update_dht_announce_interval();
#endif

		// remove undead peers that only have this list as their reference keeping them alive
		std::vector<boost::shared_ptr<peer_connection> >::iterator i = std::remove_if(
			m_undead_peers.begin(), m_undead_peers.end()
			, boost::bind(&boost::shared_ptr<peer_connection>::unique, _1));
		m_undead_peers.erase(i, m_undead_peers.end());

		int tick_interval_ms = int(total_milliseconds(now - m_last_second_tick));
		m_last_second_tick = now;
		m_tick_residual += tick_interval_ms - 1000;

		boost::int64_t session_time = total_seconds(now - m_created);
		if (session_time > 65000)
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
		for (ses_extension_list_t::const_iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_tick();
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		// don't do any of the following while we're shutting down
		if (m_abort) return;

#ifndef TORRENT_NO_DEPRECATE
		// --------------------------------------------------------------
		// RSS feeds
		// --------------------------------------------------------------
		if (now > m_next_rss_update)
			update_rss_feeds();
#endif

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
							boost::uint64_t rate = stat_rate[i];
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
			if (m_last_tick - p->connected_time()
				> seconds(m_settings.get_int(settings_pack::handshake_timeout)))
				p->disconnect(errors::timed_out, op_bittorrent);
		}

		// --------------------------------------------------------------
		// second_tick every torrent (that wants it)
		// --------------------------------------------------------------

#if TORRENT_DEBUG_STREAMING > 0
		printf("\033[2J\033[0;0H");
#endif

		std::vector<torrent*>& want_tick = m_torrent_lists[torrent_want_tick];
		for (int i = 0; i < int(want_tick.size()); ++i)
		{
			torrent& t = *want_tick[i];
			TORRENT_ASSERT(t.want_tick());
			TORRENT_ASSERT(!t.is_aborted());

			t.second_tick(tick_interval_ms, m_tick_residual / 1000);

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
				m_alerts.post_alert(performance_alert(torrent_handle()
					, performance_alert::download_limit_too_low));
			}

			if (up_limit > 0
				&& m_stat.upload_ip_overhead() >= up_limit
				&& m_alerts.should_post<performance_alert>())
			{
				m_alerts.post_alert(performance_alert(torrent_handle()
					, performance_alert::upload_limit_too_low));
			}
		}

		m_peak_up_rate = (std::max)(m_stat.upload_rate(), m_peak_up_rate);
		m_peak_down_rate = (std::max)(m_stat.download_rate(), m_peak_down_rate);
	
		m_stat.second_tick(tick_interval_ms);

		// --------------------------------------------------------------
		// scrape paused torrents that are auto managed
		// (unless the session is paused)
		// --------------------------------------------------------------
		if (!is_paused())
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

					t.scrape_tracker();

					++m_next_scrape_torrent;
					if (m_next_scrape_torrent >= int(want_scrape.size()))
						m_next_scrape_torrent = 0;

				}
			}
		}

		// --------------------------------------------------------------
		// refresh torrent suggestions
		// --------------------------------------------------------------
		--m_suggest_timer;
		if (m_settings.get_int(settings_pack::suggest_mode) != settings_pack::no_piece_suggestions
			&& m_suggest_timer <= 0)
		{
			INVARIANT_CHECK;
			m_suggest_timer = 10;

			torrent_map::iterator least_recently_refreshed = m_torrents.begin();
			if (m_next_suggest_torrent >= int(m_torrents.size()))
				m_next_suggest_torrent = 0;

			std::advance(least_recently_refreshed, m_next_suggest_torrent);

			if (least_recently_refreshed != m_torrents.end())
				least_recently_refreshed->second->refresh_suggest_pieces();
			++m_next_suggest_torrent;
		}

		// --------------------------------------------------------------
		// refresh explicit disk read cache
		// --------------------------------------------------------------
		--m_cache_rotation_timer;
		if (m_settings.get_bool(settings_pack::explicit_read_cache)
			&& m_cache_rotation_timer <= 0)
		{
			INVARIANT_CHECK;
			m_cache_rotation_timer = m_settings.get_int(settings_pack::explicit_cache_interval);

			torrent_map::iterator least_recently_refreshed = m_torrents.begin();
			if (m_next_explicit_cache_torrent >= int(m_torrents.size()))
				m_next_explicit_cache_torrent = 0;

			std::advance(least_recently_refreshed, m_next_explicit_cache_torrent);

			// how many blocks does this torrent get?
			int cache_size = (std::max)(0, m_settings.get_int(settings_pack::cache_size) * 9 / 10);

			if (m_connections.empty())
			{
				// if we don't have any connections at all, split the
				// cache evenly across all torrents
				cache_size = cache_size / (std::max)(int(m_torrents.size()), 1);
			}
			else
			{
				cache_size = cache_size * least_recently_refreshed->second->num_peers()
					/ m_connections.size();
			}

			if (least_recently_refreshed != m_torrents.end())
				least_recently_refreshed->second->refresh_explicit_cache(cache_size);
			++m_next_explicit_cache_torrent;
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
						, boost::bind(&torrent::num_peers, boost::bind(&torrent_map::value_type::second, _1))
						< boost::bind(&torrent::num_peers, boost::bind(&torrent_map::value_type::second, _2)));
			
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
						boost::shared_ptr<torrent> t = i->second;

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

	// returns the index of the first set bit.
	int log2(boost::uint32_t v)
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

		return MultiplyDeBruijnBitPosition[boost::uint32_t(v * 0x07C4ACDDU) >> 27];
	}

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

#ifndef TORRENT_NO_DEPRECATE
	void session_impl::update_rss_feeds()
	{
		time_t now_posix = time(0);
		time_point min_update = max_time();
		time_point now = aux::time_now();
		for (std::vector<boost::shared_ptr<feed> >::iterator i
			= m_feeds.begin(), end(m_feeds.end()); i != end; ++i)
		{
			feed& f = **i;
			int delta = f.next_update(now_posix);
			if (delta <= 0)
				delta = f.update_feed();
			TORRENT_ASSERT(delta >= 0);
			time_point next_update = now + seconds(delta);
			if (next_update < min_update) min_update = next_update;
		}
		m_next_rss_update = min_update;
	}
#endif

	void session_impl::prioritize_connections(boost::weak_ptr<torrent> t)
	{
		m_prio_torrents.push_back(std::make_pair(t, 10));
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::add_dht_node(udp::endpoint n)
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_dht) m_dht->add_node(n);
	}

	bool session_impl::has_dht() const
	{
		return m_dht.get();
	}

	void session_impl::prioritize_dht(boost::weak_ptr<torrent> t)
	{
		TORRENT_ASSERT(!m_abort);
		if (m_abort) return;

		TORRENT_ASSERT(m_dht);
		m_dht_torrents.push_back(t);
#if defined TORRENT_LOGGING
		boost::shared_ptr<torrent> tor = t.lock();
		if (tor)
			session_log("prioritizing DHT announce: \"%s\"", tor->name().c_str());
#endif
		// trigger a DHT announce right away if we just added a new torrent and
		// there's no back-log. in the timer handler, as long as there are more
		// high priority torrents to be announced to the DHT, it will keep the
		// timer interval short until all torrents have been announced.
		if (m_dht_torrents.size() == 1)
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("session_impl::on_dht_announce");
#endif
			error_code ec;
			m_dht_announce_timer.expires_from_now(seconds(0), ec);
			m_dht_announce_timer.async_wait(
				bind(&session_impl::on_dht_announce, this, _1));
		}
	}

	void session_impl::on_dht_announce(error_code const& e)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_dht_announce");
#endif
		TORRENT_ASSERT(is_single_thread());
		if (e)
		{
#if defined TORRENT_LOGGING
			session_log("aborting DHT announce timer (%d): %s"
				, e.value(), e.message().c_str());
#endif
			return;
		}

		if (m_abort)
		{
#if defined TORRENT_LOGGING
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

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_dht_announce");
#endif
		error_code ec;
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait(
			bind(&session_impl::on_dht_announce, this, _1));

		if (!m_dht_torrents.empty())
		{
			boost::shared_ptr<torrent> t;
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
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_lsd_announce");
#endif
		m_stats_counters.inc_stats_counter(counters::on_lsd_counter);
		TORRENT_ASSERT(is_single_thread());
		if (e) return;

		if (m_abort) return;

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_lsd_announce");
#endif
		// announce on local network every 5 minutes
		int delay = (std::max)(m_settings.get_int(settings_pack::local_service_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		error_code ec;
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait(
			bind(&session_impl::on_lsd_announce, this, _1));

		if (m_torrents.empty()) return;

		if (m_next_lsd_torrent == m_torrents.end())
			m_next_lsd_torrent = m_torrents.begin();
		m_next_lsd_torrent->second->lsd_announce();
		++m_next_lsd_torrent;
		if (m_next_lsd_torrent == m_torrents.end())
			m_next_lsd_torrent = m_torrents.begin();
	}

	void session_impl::auto_manage_torrents(std::vector<torrent*>& list
		, int& checking_limit, int& dht_limit, int& tracker_limit
		, int& lsd_limit, int& hard_limit, int type_limit)
	{
		for (std::vector<torrent*>::iterator i = list.begin()
			, end(list.end()); i != end; ++i)
		{
			torrent* t = *i;

			if (t->state() == torrent_status::checking_files)
			{
				if (checking_limit <= 0) t->pause();
				else
				{
					t->resume();
					t->start_checking();
					--checking_limit;
				}
				continue;
			}

			--dht_limit;
			--lsd_limit;
			--tracker_limit;
			t->set_announce_to_dht(dht_limit >= 0);
			t->set_announce_to_trackers(tracker_limit >= 0);
			t->set_announce_to_lsd(lsd_limit >= 0);
   
			if (!t->is_paused() && t->is_inactive()
				&& hard_limit > 0)
			{
				// the hard limit takes inactive torrents into account, but the
				// download and seed limits don't.
				continue;
			}

			if (type_limit > 0 && hard_limit > 0)
			{
				--hard_limit;
				--type_limit;
#if defined TORRENT_LOGGING
				if (!t->allows_peers())
					t->log_to_all_peers("AUTO MANAGER STARTING TORRENT");
#endif
				t->set_allow_peers(true);
			}
			else
			{
#if defined TORRENT_LOGGING
				if (t->allows_peers())
					t->log_to_all_peers("AUTO MANAGER PAUSING TORRENT");
#endif
				// use graceful pause for auto-managed torrents
				t->set_allow_peers(false, true);
			}
		}
	}

	void session_impl::recalculate_auto_managed_torrents()
	{
		INVARIANT_CHECK;

		m_need_auto_manage = false;

		if (is_paused()) return;

		// these vectors are filled with auto managed torrents

		// TODO: these vectors could be copied from m_torrent_lists,
		// if we would maintain them. That way the first pass over
		// all torrents could be avoided. It would be especially
		// efficient if most torrents are not auto-managed
		// whenever we receive a scrape response (or anything
		// that may change the rank of a torrent) that one torrent
		// could re-sort itself in a list that's kept sorted at all
		// times. That way, this pass over all torrents could be
		// avoided alltogether.
		std::vector<torrent*> checking;
		std::vector<torrent*> downloaders;
		downloaders.reserve(m_torrents.size());
		std::vector<torrent*> seeds;
		seeds.reserve(m_torrents.size());

		// these counters are set to the number of torrents
		// of each kind we're allowed to have active
		int num_downloaders = settings().get_int(settings_pack::active_downloads);
		int num_seeds = settings().get_int(settings_pack::active_seeds);
		int checking_limit = 1;
		int dht_limit = settings().get_int(settings_pack::active_dht_limit);
		int tracker_limit = settings().get_int(settings_pack::active_tracker_limit);
		int lsd_limit = settings().get_int(settings_pack::active_lsd_limit);
		int hard_limit = settings().get_int(settings_pack::active_limit);

		if (num_downloaders == -1)
			num_downloaders = (std::numeric_limits<int>::max)();
		if (num_seeds == -1)
			num_seeds = (std::numeric_limits<int>::max)();
		if (hard_limit == -1)
			hard_limit = (std::numeric_limits<int>::max)();
		if (dht_limit == -1)
			dht_limit = (std::numeric_limits<int>::max)();
		if (lsd_limit == -1)
			lsd_limit = (std::numeric_limits<int>::max)();
		if (tracker_limit == -1)
			tracker_limit = (std::numeric_limits<int>::max)();
            
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent* t = i->second.get();
			TORRENT_ASSERT(t);

			if (t->state() == torrent_status::checking_files
				&& !t->has_error()
				&& (t->is_auto_managed() || t->allows_peers()))
			{
				checking.push_back(t);
				continue;
			}

			if (t->is_auto_managed() && !t->has_error())
			{
				TORRENT_ASSERT(t->m_resume_data_loaded || !t->valid_metadata());
				// this torrent is auto managed, add it to
				// the list (depending on if it's a seed or not)
				if (t->is_finished())
					seeds.push_back(t);
				else
					downloaders.push_back(t);
			}
			else if (!t->is_paused())
			{
				if (t->state() == torrent_status::checking_files)
				{
					if (checking_limit > 0) --checking_limit;
					continue;
				}
				TORRENT_ASSERT(t->m_resume_data_loaded || !t->valid_metadata());
				--hard_limit;
			}
		}

		bool handled_by_extension = false;

#ifndef TORRENT_DISABLE_EXTENSIONS
		// TODO: 0 allow extensions to sort torrents for queuing
#endif

		if (!handled_by_extension)
		{
			std::sort(checking.begin(), checking.end()
				, boost::bind(&torrent::sequence_number, _1) < boost::bind(&torrent::sequence_number, _2));

			std::sort(downloaders.begin(), downloaders.end()
				, boost::bind(&torrent::sequence_number, _1) < boost::bind(&torrent::sequence_number, _2));

			std::sort(seeds.begin(), seeds.end()
				, boost::bind(&torrent::seed_rank, _1, boost::ref(m_settings))
				> boost::bind(&torrent::seed_rank, _2, boost::ref(m_settings)));
		}

		auto_manage_torrents(checking, checking_limit, dht_limit, tracker_limit, lsd_limit
			, hard_limit, num_downloaders);

		if (settings().get_bool(settings_pack::auto_manage_prefer_seeds))
		{
			auto_manage_torrents(seeds, checking_limit, dht_limit, tracker_limit, lsd_limit
				, hard_limit, num_seeds);
			auto_manage_torrents(downloaders, checking_limit, dht_limit, tracker_limit, lsd_limit
				, hard_limit, num_downloaders);
		}
		else
		{
			auto_manage_torrents(downloaders, checking_limit, dht_limit, tracker_limit, lsd_limit
				, hard_limit, num_downloaders);
			auto_manage_torrents(seeds, checking_limit, dht_limit, tracker_limit, lsd_limit
				, hard_limit, num_seeds);
		}
	}

	void session_impl::recalculate_optimistic_unchoke_slots()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_stats_counters[counters::num_unchoke_slots] == 0) return;
	
		std::vector<torrent_peer*> opt_unchoke;

		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			peer_connection* p = i->get();
			TORRENT_ASSERT(p);
			torrent_peer* pi = p->peer_info_struct();
			if (!pi) continue;
			if (pi->web_seed) continue;
			torrent* t = p->associated_torrent().lock().get();
			if (!t) continue;
			if (t->is_paused()) continue;

			if (pi->optimistically_unchoked)
			{
				TORRENT_ASSERT(!p->is_choked());
				opt_unchoke.push_back(pi);
			}

			if (!p->is_connecting()
				&& !p->is_disconnecting()
				&& p->is_peer_interested()
				&& t->free_upload_slots()
				&& p->is_choked()
				&& !p->ignore_unchoke_slots()
				&& t->valid_metadata())
			{
				opt_unchoke.push_back(pi);
			}
		}

		// find the peers that has been waiting the longest to be optimistically
		// unchoked

		// avoid having a bias towards peers that happen to be sorted first
		std::random_shuffle(opt_unchoke.begin(), opt_unchoke.end());

		// sort all candidates based on when they were last optimistically
		// unchoked.
		std::sort(opt_unchoke.begin(), opt_unchoke.end()
			, boost::bind(&torrent_peer::last_optimistically_unchoked, _1)
			< boost::bind(&torrent_peer::last_optimistically_unchoked, _2));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_optimistic_unchoke(opt_unchoke))
				break;
		}
#endif

		int num_opt_unchoke = m_settings.get_int(settings_pack::num_optimistic_unchoke_slots);
		int allowed_unchoke_slots = m_stats_counters[counters::num_unchoke_slots];
		if (num_opt_unchoke == 0) num_opt_unchoke = (std::max)(1, allowed_unchoke_slots / 5);

		// unchoke the first num_opt_unchoke peers in the candidate set
		// and make sure that the others are choked
		for (std::vector<torrent_peer*>::iterator i = opt_unchoke.begin()
			, end(opt_unchoke.end()); i != end; ++i)
		{
			torrent_peer* pi = *i;
			if (num_opt_unchoke > 0)
			{
				--num_opt_unchoke;
				if (!pi->optimistically_unchoked)
				{
					peer_connection* p = static_cast<peer_connection*>(pi->connection);
					torrent* t = p->associated_torrent().lock().get();
					bool ret = t->unchoke_peer(*p, true);
					if (ret)
					{
						pi->optimistically_unchoked = true;
						m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic);
						pi->last_optimistically_unchoked = boost::uint16_t(session_time());
					}
					else
					{
						// we failed to unchoke it, increment the count again
						++num_opt_unchoke;
					}
				}
			}
			else
			{
				if (pi->optimistically_unchoked)
				{
					peer_connection* p = static_cast<peer_connection*>(pi->connection);
					torrent* t = p->associated_torrent().lock().get();
					pi->optimistically_unchoked = false;
					m_stats_counters.inc_stats_counter(counters::num_peers_up_unchoked_optimistic, -1);
					t->choke_peer(*p);
				}	
			}
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

			torrent* t = NULL;
			// there are prioritized torrents. Pick one of those
			while (!m_prio_torrents.empty())
			{
				t = m_prio_torrents.front().first.lock().get();
				--m_prio_torrents.front().second;
				if (m_prio_torrents.front().second > 0
					&& t != NULL
					&& t->want_peers()) break;
				m_prio_torrents.pop_front();
				t = NULL;
			}
			
			if (t == NULL)
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
			TORRENT_ASSERT(t->allows_peers());

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

		time_point now = aux::time_now();
		time_duration unchoke_interval = now - m_last_choke;
		m_last_choke = now;

		// build list of all peers that are
		// unchokable.
		std::vector<peer_connection*> peers;
		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::shared_ptr<peer_connection> p = *i;
			TORRENT_ASSERT(p);
			++i;
			torrent* t = p->associated_torrent().lock().get();
			torrent_peer* pi = p->peer_info_struct();

			if (p->ignore_unchoke_slots() || t == 0 || pi == 0
				|| pi->web_seed || t->is_paused())
				continue;

			if (!p->is_peer_interested()
				|| p->is_disconnecting()
				|| p->is_connecting())
			{
				// this peer is not unchokable. So, if it's unchoked
				// already, make sure to choke it.
				if (p->is_choked()) continue;
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
				m_alerts.post_alert(performance_alert(torrent_handle()
					, performance_alert::bittyrant_with_no_uplimit));
		}

		int allowed_upload_slots = unchoke_sort(peers, max_upload_rate
			, unchoke_interval, m_settings);
		m_stats_counters.set_value(counters::num_unchoke_slots
			, allowed_upload_slots);

		int num_opt_unchoke = m_settings.get_int(settings_pack::num_optimistic_unchoke_slots);
		if (num_opt_unchoke == 0) num_opt_unchoke = (std::max)(1, allowed_upload_slots / 5);

		// reserve some upload slots for optimistic unchokes
		int unchoke_set_size = allowed_upload_slots;

		// go through all the peers and unchoke the first ones and choke
		// all the other ones.
		for (std::vector<peer_connection*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_connection* p = *i;
			TORRENT_ASSERT(p);
			TORRENT_ASSERT(!p->ignore_unchoke_slots());

			// this will update the m_uploaded_at_last_unchoke
			// TODO: this should be called for all peers!
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

#if defined _MSC_VER && defined TORRENT_DEBUG
	static void straight_to_debugger(unsigned int, _EXCEPTION_POINTERS*)
	{ throw; }
#endif

	void session_impl::main_thread()
	{
#if defined _MSC_VER && defined TORRENT_DEBUG
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
		::_set_se_translator(straight_to_debugger);
#endif
		// this is a debug facility
		// see single_threaded in debug.hpp
		thread_started();

		TORRENT_ASSERT(is_single_thread());

		// initialize async operations
		init();

		bool stop_loop = false;
		while (!stop_loop)
		{
			error_code ec;
			m_io_service.run(ec);
			if (ec)
			{
#ifdef TORRENT_DEBUG
				fprintf(stderr, "%s\n", ec.message().c_str());
				std::string err = ec.message();
#endif
				TORRENT_ASSERT(false);
			}
			m_io_service.reset();

			stop_loop = m_abort;
		}

#if defined TORRENT_LOGGING
		session_log(" locking mutex");
#endif

/*
#ifdef TORRENT_DEBUG
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			TORRENT_ASSERT(i->second->num_peers() == 0);
		}
#endif
*/
#if defined TORRENT_LOGGING
		session_log(" cleaning up torrents");
#endif

		// clear the torrent LRU (probably not strictly necessary)
		list_node* i = m_torrent_lru.get_all();
#if TORRENT_USE_ASSERTS
		// clear the prev and next pointers in all torrents
		// to avoid the assert when destructing them
		while (i)
		{
			list_node* tmp = i;
			i = i->next;
			tmp->next = NULL;
			tmp->prev= NULL;
		}
#endif
		m_torrents.clear();

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());

#if TORRENT_USE_ASSERTS && defined BOOST_HAS_PTHREADS
		m_network_thread = 0;
#endif
	}

	boost::shared_ptr<torrent> session_impl::delay_load_torrent(sha1_hash const& info_hash
		, peer_connection* pc)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			add_torrent_params p;
			if ((*i)->on_unknown_torrent(info_hash, pc, p))
			{
				error_code ec;
				torrent_handle handle = add_torrent(p, ec);

				return handle.native_handle();
			}
		}
#endif
		return boost::shared_ptr<torrent>();
	}

	// the return value from this function is valid only as long as the
	// session is locked!
	boost::weak_ptr<torrent> session_impl::find_torrent(sha1_hash const& info_hash) const
	{
		TORRENT_ASSERT(is_single_thread());

		torrent_map::const_iterator i = m_torrents.find(info_hash);
#if defined TORRENT_DEBUG && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		for (torrent_map::const_iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			torrent* p = boost::get_pointer(j->second);
			TORRENT_ASSERT(p);
		}
#endif
		if (i != m_torrents.end()) return i->second;
		return boost::weak_ptr<torrent>();
	}

	void session_impl::insert_torrent(sha1_hash const& ih, boost::shared_ptr<torrent> const& t
		, std::string uuid)
	{
		m_torrents.insert(std::make_pair(ih, t));
		if (!uuid.empty()) m_uuids.insert(std::make_pair(uuid, t));

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
		if (i == m_obfuscated_torrents.end()) return NULL;
		return i->second.get();
	}
#endif

	boost::weak_ptr<torrent> session_impl::find_torrent(std::string const& uuid) const
	{
		TORRENT_ASSERT(is_single_thread());

		std::map<std::string, boost::shared_ptr<torrent> >::const_iterator i
			= m_uuids.find(uuid);
		if (i != m_uuids.end()) return i->second;
		return boost::weak_ptr<torrent>();
	}

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::vector<boost::shared_ptr<torrent> > session_impl::find_collection(
		std::string const& collection) const
	{
		std::vector<boost::shared_ptr<torrent> > ret;
		for (session_impl::torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent> t = i->second;
			if (!t) continue;
			std::vector<std::string> const& c = t->torrent_file().collections();
			if (std::count(c.begin(), c.end(), collection) == 0) continue;
			ret.push_back(t);
		}
		return ret;
	}
#endif //TORRENT_DISABLE_MUTABLE_TORRENTS

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

 	boost::weak_ptr<torrent> session_impl::find_disconnect_candidate_torrent() const
 	{
		aux::session_impl::torrent_map::const_iterator i = std::min_element(m_torrents.begin(), m_torrents.end()
			, boost::bind(&compare_disconnect_torrent, _1, _2));

		TORRENT_ASSERT(i != m_torrents.end());
		if (i == m_torrents.end()) return boost::shared_ptr<torrent>();

		return i->second;
	}

#if defined TORRENT_LOGGING
	void session_impl::session_log(char const* fmt, ...) const
	{
		if (!m_alerts.should_post<log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		session_vlog(fmt, v);
		va_end(v);
	}
	
	void session_impl::session_vlog(char const* fmt, va_list& v) const
	{
		if (!m_alerts.should_post<log_alert>()) return;

		char buf[1024];
		vsnprintf(buf, sizeof(buf), fmt, v);

		m_alerts.post_alert(log_alert(buf));
	}

	void session_impl::log_all_torrents(peer_connection* p)
	{
		for (session_impl::torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			p->peer_log("   %s", to_hex(i->second->torrent_file().info_hash().to_string()).c_str());
		}
	}
#endif

	void session_impl::get_torrent_status(std::vector<torrent_status>* ret
		, boost::function<bool(torrent_status const&)> const& pred
		, boost::uint32_t flags) const
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
		, boost::uint32_t flags) const
	{
		for (std::vector<torrent_status>::iterator i
			= ret->begin(), end(ret->end()); i != end; ++i)
		{
			boost::shared_ptr<torrent> t = i->handle.m_torrent.lock();
			if (!t) continue;
			t->status(&*i, flags);
		}
	}
	
	void session_impl::post_torrent_updates(boost::uint32_t flags)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_single_thread());

		std::auto_ptr<state_update_alert> alert(new state_update_alert());
		std::vector<torrent*>& state_updates
			= m_torrent_lists[aux::session_impl::torrent_state_updates];

		alert->status.reserve(state_updates.size());

#if TORRENT_USE_ASSERTS
		m_posting_torrent_updates = true;
#endif

		// TODO: it might be a nice feature here to limit the number of torrents
		// to send in a single update. By just posting the first n torrents, they
		// would nicely be round-robined because the torrent lists are always
		// pushed back
		for (std::vector<torrent*>::iterator i = state_updates.begin()
			, end(state_updates.end()); i != end; ++i)
		{
			torrent* t = *i;
			TORRENT_ASSERT(t->m_links[aux::session_impl::torrent_state_updates].in_list());
			alert->status.push_back(torrent_status());
			// querying accurate download counters may require
			// the torrent to be loaded. Loading a torrent, and evicting another
			// one will lead to calling state_updated(), which screws with
			// this list while we're working on it, and break things
			t->status(&alert->status.back(), flags);
			t->clear_in_state_update();
		}
		state_updates.clear();

#if TORRENT_USE_ASSERTS
		m_posting_torrent_updates = false;
#endif

		m_alerts.post_alert_ptr(alert.release());
	}

	void session_impl::post_session_stats()
	{
		std::auto_ptr<session_stats_alert> alert(new session_stats_alert());
		std::vector<boost::uint64_t>& values = alert->values;
		values.resize(counters::num_counters, 0);

		m_disk_thread.update_stats_counters(m_stats_counters);

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

		for (int i = 0; i < counters::num_counters; ++i)
			values[i] = m_stats_counters[i];

		alert->timestamp = total_microseconds(clock_type::now() - m_created);

		m_alerts.post_alert_ptr(alert.release());
	}

	void session_impl::post_dht_stats()
	{
		std::auto_ptr<dht_stats_alert> alert(new dht_stats_alert());
	
#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_dht->dht_status(alert->routing_table, alert->active_requests);
#endif

		m_alerts.post_alert_ptr(alert.release());
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
				, boost::bind(&session_impl::on_async_load_torrent, this, _1));
			return;
		}

		error_code ec;
		torrent_handle handle = add_torrent(*params, ec);
		delete params;
	}

	void session_impl::on_async_load_torrent(disk_io_job const* j)
	{
		add_torrent_params* params = (add_torrent_params*)j->requester;
		error_code ec;
		torrent_handle handle;
		if (j->error.ec)
		{
			ec = j->error.ec;
			m_alerts.post_alert(add_torrent_alert(handle, *params, ec));
		}
		else
		{
			params->url.clear();
			params->ti = boost::shared_ptr<torrent_info>((torrent_info*)j->buffer);
			handle = add_torrent(*params, ec);
		}

		delete params;
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session_impl::add_extensions_to_torrent(
		boost::shared_ptr<torrent> const& torrent_ptr, void* userdata)
	{
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)->new_torrent(torrent_ptr.get(), userdata));
			if (tp) torrent_ptr->add_extension(tp);
		}
	}
#endif

	torrent_handle session_impl::add_torrent(add_torrent_params const& p
		, error_code& ec)
	{
		torrent_handle h = add_torrent_impl(p, ec);
		m_alerts.post_alert(add_torrent_alert(h, p, ec));
		return h;
	}

	torrent_handle session_impl::add_torrent_impl(add_torrent_params const& p
		, error_code& ec)
	{
		TORRENT_ASSERT(!p.save_path.empty());

#ifndef TORRENT_NO_DEPRECATE
		p.update_flags();
#endif

		add_torrent_params params = p;
		if (string_begins_no_case("magnet:", params.url.c_str()))
		{
			parse_magnet_uri(params.url, params, ec);
			if (ec) return torrent_handle();
			params.url.clear();
		}

		if (string_begins_no_case("file://", params.url.c_str()) && !params.ti)
		{
			std::string filename = resolve_file_url(params.url);
			boost::shared_ptr<torrent_info> t = boost::make_shared<torrent_info>(filename, boost::ref(ec), 0);
			if (ec) return torrent_handle();
			params.url.clear();
			params.ti = t;
		}

		if (params.ti && params.ti->is_valid() && params.ti->num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			return torrent_handle();
		}

#ifndef TORRENT_DISABLE_DHT	
		// add p.dht_nodes to the DHT, if enabled
		if (m_dht && !p.dht_nodes.empty())
		{
			for (std::vector<std::pair<std::string, int> >::const_iterator i = p.dht_nodes.begin()
				, end(p.dht_nodes.end()); i != end; ++i)
				m_dht->add_node(*i);
		}
#endif

		INVARIANT_CHECK;

		if (is_aborted())
		{
			ec = errors::session_is_closing;
			return torrent_handle();
		}
		
		// figure out the info hash of the torrent
		sha1_hash const* ih = 0;
		sha1_hash tmp;
		if (params.ti) ih = &params.ti->info_hash();
		else if (!params.url.empty())
		{
			// in order to avoid info-hash collisions, for
			// torrents where we don't have an info-hash, but
			// just a URL, set the temporary info-hash to the
			// hash of the URL. This will be changed once we
			// have the actual .torrent file
			tmp = hasher(&params.url[0], params.url.size()).final();
			ih = &tmp;
		}
		else ih = &params.info_hash;

		// we don't have a torrent file. If the user provided
		// resume data, there may be some metadata in there
		if ((!params.ti || !params.ti->is_valid())
			&& !params.resume_data.empty())
		{
			int pos;
			error_code ec;
			bdecode_node tmp;
			bdecode_node info;
#if defined TORRENT_LOGGING
			session_log("adding magnet link with resume data");
#endif
			if (bdecode(&params.resume_data[0], &params.resume_data[0]
					+ params.resume_data.size(), tmp, ec, &pos) == 0
				&& tmp.type() == bdecode_node::dict_t
				&& (info = tmp.dict_find_dict("info")))
			{
#if defined TORRENT_LOGGING
				session_log("found metadata in resume data");
#endif
				// verify the info-hash of the metadata stored in the resume file matches
				// the torrent we're loading

				std::pair<char const*, int> buf = info.data_section();
				sha1_hash resume_ih = hasher(buf.first, buf.second).final();

				// if url is set, the info_hash is not actually the info-hash of the
				// torrent, but the hash of the URL, until we have the full torrent
				// only require the info-hash to match if we actually passed in one
				if (resume_ih == params.info_hash
					|| !params.url.empty()
					|| params.info_hash.is_all_zeros())
				{
#if defined TORRENT_LOGGING
					session_log("info-hash matched");
#endif
					params.ti = boost::make_shared<torrent_info>(resume_ih);

					if (params.ti->parse_info_section(info, ec, 0))
					{
#if defined TORRENT_LOGGING
						session_log("successfully loaded metadata from resume file");
#endif
						// make the info-hash be the one in the resume file
						params.info_hash = resume_ih;
						ih = &params.info_hash;
					}
					else
					{
#if defined TORRENT_LOGGING
						session_log("failed to load metadata from resume file: %s"
								, ec.message().c_str());
#endif
					}
				}
#if defined TORRENT_LOGGING
				else
				{
					session_log("metadata info-hash failed");
				}
#endif
			}
#if defined TORRENT_LOGGING
			else
			{
				session_log("no metadata found");
			}
#endif
		}

		// is the torrent already active?
		boost::shared_ptr<torrent> torrent_ptr = find_torrent(*ih).lock();
		if (!torrent_ptr && !params.uuid.empty()) torrent_ptr = find_torrent(params.uuid).lock();
		// if we still can't find the torrent, look for it by url
		if (!torrent_ptr && !params.url.empty())
		{
			torrent_map::iterator i = std::find_if(m_torrents.begin()
				, m_torrents.end(), boost::bind(&torrent::url, boost::bind(&std::pair<const sha1_hash
					, boost::shared_ptr<torrent> >::second, _1)) == params.url);
			if (i != m_torrents.end())
				torrent_ptr = i->second;
		}

		if (torrent_ptr)
		{
			if ((params.flags & add_torrent_params::flag_duplicate_is_error) == 0)
			{
				if (!params.uuid.empty() && torrent_ptr->uuid().empty())
					torrent_ptr->set_uuid(params.uuid);
				if (!params.url.empty() && torrent_ptr->url().empty())
					torrent_ptr->set_url(params.url);
				if (!params.source_feed_url.empty() && torrent_ptr->source_feed_url().empty())
					torrent_ptr->set_source_feed_url(params.source_feed_url);
				return torrent_handle(torrent_ptr);
			}

			ec = errors::duplicate_torrent;
			return torrent_handle();
		}

		int queue_pos = ++m_max_queue_pos;

		torrent_ptr.reset(new torrent(*this
			, 16 * 1024, queue_pos, params, *ih));
		torrent_ptr->start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::vector<boost::function<
			boost::shared_ptr<torrent_plugin>(torrent*, void*)> >
			torrent_plugins_t;

		for (torrent_plugins_t::const_iterator i = params.extensions.begin()
			, end(params.extensions.end()); i != end; ++i)
		{
			torrent_ptr->add_extension((*i)(torrent_ptr.get(),
				params.userdata));
		}

		add_extensions_to_torrent(torrent_ptr, params.userdata);
#endif

#ifndef TORRENT_DISABLE_DHT
		if (m_dht && params.ti)
		{
			torrent_info::nodes_t const& nodes = params.ti->nodes();
			std::for_each(nodes.begin(), nodes.end(), boost::bind(
				(void(dht::dht_tracker::*)(std::pair<std::string, int> const&))
				&dht::dht_tracker::add_node
				, boost::ref(m_dht), _1));
		}
#endif

#if TORRENT_HAS_BOOST_UNORDERED
		sha1_hash next_lsd(0);
		sha1_hash next_dht(0);
		if (m_next_lsd_torrent != m_torrents.end())
			next_lsd = m_next_lsd_torrent->first;
#ifndef TORRENT_DISABLE_DHT
		if (m_next_dht_torrent != m_torrents.end())
			next_dht = m_next_dht_torrent->first;
#endif
		float load_factor = m_torrents.load_factor();
#endif // TORRENT_HAS_BOOST_UNORDERED

		m_torrents.insert(std::make_pair(*ih, torrent_ptr));

		TORRENT_ASSERT(m_torrents.size() >= m_torrent_lru.size());

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		hasher h;
		h.update("req2", 4);
		h.update((char*)&(*ih)[0], 20);
		// this is SHA1("req2" + info-hash), used for
		// encrypted hand shakes
		m_obfuscated_torrents.insert(std::make_pair(h.final(), torrent_ptr));
#endif

		if (torrent_ptr->is_pinned() == false)
		{
			evict_torrents_except(torrent_ptr.get());
			bump_torrent(torrent_ptr.get());
		}

#if TORRENT_HAS_BOOST_UNORDERED
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
#endif // TORRENT_HAS_BOOST_UNORDERED
		if (!params.uuid.empty() || !params.url.empty())
			m_uuids.insert(std::make_pair(params.uuid.empty()
				? params.url : params.uuid, torrent_ptr));

		if (m_alerts.should_post<torrent_added_alert>())
			m_alerts.post_alert(torrent_added_alert(torrent_ptr->get_handle()));

		// recalculate auto-managed torrents sooner (or put it off)
		// if another torrent will be added within one second from now
		// we want to put it off again anyway. So that while we're adding
		// a boat load of torrents, we postpone the recalculation until
		// we're done adding them all (since it's kind of an expensive operation)
		if (params.flags & add_torrent_params::flag_auto_managed)
			trigger_auto_manage();

		return torrent_handle(torrent_ptr);
	}

	void session_impl::update_outgoing_interfaces()
	{
		INVARIANT_CHECK;
		std::string net_interfaces = m_settings.get_str(settings_pack::outgoing_interfaces);

		// declared in string_util.hpp
		parse_comma_separated_string(net_interfaces, m_net_interfaces);
	}

	tcp::endpoint session_impl::bind_outgoing_socket(socket_type& s, address
		const& remote_address, error_code& ec) const
	{
		tcp::endpoint bind_ep(address_v4(), 0);
		if (m_settings.get_int(settings_pack::outgoing_port) > 0)
		{
			s.set_option(socket_acceptor::reuse_address(true), ec);
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

		if (!m_net_interfaces.empty())
		{
			if (m_interface_index >= m_net_interfaces.size()) m_interface_index = 0;
			std::string const& ifname = m_net_interfaces[m_interface_index++];

			if (ec) return bind_ep;

			bind_ep.address(bind_to_device(m_io_service, s, remote_address.is_v4()
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
		// we have specific outgoing interfaces specified. Make sure the
		// local endpoint for this socket is bound to one of the allowed
		// interfaces. the list can be a mixture of interfaces and IP
		// addresses. first look for the address 
		for (int i = 0; i < int(m_net_interfaces.size()); ++i)
		{
			error_code err;
			address ip = address::from_string(m_net_interfaces[i].c_str(), err);
			if (err) continue;
			if (ip == addr) return true;
		}

		// we didn't find the address as an IP in the interface list. Now,
		// resolve which device (if any) has this IP address.
		std::string device = device_for_address(addr, m_io_service, ec);
		if (ec) return false;

		// if no device was found to have this address, we fail
		if (device.empty()) return false;

		for (int i = 0; i < int(m_net_interfaces.size()); ++i)
		{
			if (m_net_interfaces[i] == device) return true;
		}

		return false;
	}

	void session_impl::remove_torrent(const torrent_handle& h, int options)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> tptr = h.m_torrent.lock();
		if (!tptr) return;

		m_alerts.post_alert(torrent_removed_alert(tptr->get_handle(), tptr->info_hash()));

		remove_torrent_impl(tptr, options);

		tptr->abort();
		tptr->set_queue_position(-1);
	}

	void session_impl::remove_torrent_impl(boost::shared_ptr<torrent> tptr, int options)
	{
		// remove from uuid list
		if (!tptr->uuid().empty())
		{
			std::map<std::string, boost::shared_ptr<torrent> >::iterator j
				= m_uuids.find(tptr->uuid());
			if (j != m_uuids.end()) m_uuids.erase(j);
		}

		torrent_map::iterator i =
			m_torrents.find(tptr->torrent_file().info_hash());

		// this torrent might be filed under the URL-hash
		if (i == m_torrents.end() && !tptr->url().empty())
		{
			std::string const& url = tptr->url();
			sha1_hash urlhash = hasher(&url[0], url.size()).final();
			i = m_torrents.find(urlhash);
		}

		if (i == m_torrents.end()) return;

		torrent& t = *i->second;
		if (options & session::delete_files)
		{
			if (!t.delete_files())
			{
				if (m_alerts.should_post<torrent_delete_failed_alert>())
					m_alerts.post_alert(torrent_delete_failed_alert(t.get_handle()
						, error_code(), t.torrent_file().info_hash()));
			}
		}

		if (m_torrent_lru.size() > 0
			&& (t.prev != NULL || t.next != NULL || m_torrent_lru.front() == &t))
			m_torrent_lru.erase(&t);

		TORRENT_ASSERT(t.prev == NULL && t.next == NULL);

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
		hasher h;
		h.update("req2", 4);
		h.update((char*)&tptr->info_hash()[0], 20);
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

	void session_impl::update_listen_interfaces()
	{
		INVARIANT_CHECK;

		std::string net_interfaces = m_settings.get_str(settings_pack::listen_interfaces);
		std::vector<std::pair<std::string, int> > new_listen_interfaces;
		
		// declared in string_util.hpp
		parse_comma_separated_string_port(net_interfaces, new_listen_interfaces);

#if defined TORRENT_LOGGING
		session_log("update listen interfaces: %s", net_interfaces.c_str());
#endif

		// if the interface is the same and the socket is open
		// don't do anything
		if (new_listen_interfaces == m_listen_interfaces
			&& !m_listen_sockets.empty())
			return;

		m_listen_interfaces = new_listen_interfaces;

		// for backwards compatibility. Some components still only supports
		// a single listen interface
		m_listen_interface.address(address_v4::any());
		m_listen_interface.port(0);
		if (m_listen_interfaces.size() > 0)
		{
			error_code ec;
			m_listen_interface.port(m_listen_interfaces[0].second);
			char const* device_name = m_listen_interfaces[0].first.c_str();

			// if the first character is [, skip it since it may be an
			// IPv6 address
			m_listen_interface.address(address::from_string(
				device_name[0] == '[' ? device_name + 1 : device_name, ec));
			if (ec)
			{
#if defined TORRENT_LOGGING
				session_log("failed to treat %s as an IP address [ %s ]"
					, device_name, ec.message().c_str());
#endif
				// it may have been a device name.
				std::vector<ip_interface> ifs = enum_net_interfaces(m_io_service, ec);
				
#if defined TORRENT_LOGGING
				if (ec)
					session_log("failed to enumerate interfaces [ %s ]"
						, ec.message().c_str());
#endif

				bool found = false;
				for (int i = 0; i < int(ifs.size()); ++i)
				{
					// we're looking for a specific interface, and its address
					// (which must be of the same family as the address we're
					// connecting to)
					if (strcmp(ifs[i].name, device_name) != 0) continue;
					m_listen_interface.address(ifs[i].interface_address);
#if defined TORRENT_LOGGING
					session_log("binding to %s"
						, m_listen_interface.address().to_string(ec).c_str());
#endif
					found = true;
					break;
				}

				if (!found)
				{
#if defined TORRENT_LOGGING
					session_log("failed to find device %s", device_name);
#endif
					// effectively disable whatever socket decides to bind to this
					m_listen_interface.address(address_v4::loopback());
				}
			}
		}
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
				i->second->ip_filter_updated();
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
		m_udp_socket.set_proxy_settings(proxy());

#ifdef TORRENT_USE_OPENSSL
		m_ssl_udp_socket.set_proxy_settings(proxy());
#endif
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
			url_random((char*)&m_peer_id[print.length()], (char*)&m_peer_id[0] + 20);
		}
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

	address session_impl::listen_address() const
	{
		for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			if (i->external_address != address()) return i->external_address;
		}
		return address();
	}

	boost::uint16_t session_impl::listen_port() const
	{
		// if peer connections are set up to be received over a socks
		// proxy, and it's the same one as we're using for the tracker
		// just tell the tracker the socks5 port we're listening on
		if (m_socks_listen_socket && m_socks_listen_socket->is_open())
			return m_socks_listen_port;

		// if not, don't tell the tracker anything if we're in force_proxy
		// mode. We don't want to leak our listen port since it can
		// potentially identify us if it is leaked elsewere
		if (m_settings.get_bool(settings_pack::force_proxy)) return 0;
		if (m_listen_sockets.empty()) return 0;
		return m_listen_sockets.front().external_port;
	}

	boost::uint16_t session_impl::ssl_listen_port() const
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
			if (i->ssl) return i->external_port;
		}

		if (m_ssl_udp_socket.is_open())
			return m_ssl_udp_socket.local_port();
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

		boost::shared_ptr<torrent> t = find_torrent(ih).lock();
		if (!t) return;
		// don't add peers from lsd to private torrents
		if (t->torrent_file().priv() || (t->torrent_file().is_i2p()
			&& !m_settings.get_bool(settings_pack::allow_i2p_mixed))) return;

#if defined TORRENT_LOGGING
		session_log("added peer from local discovery: %s", print_endpoint(peer).c_str());
#endif
		t->add_peer(peer, peer_info::lsd);
		t->do_connect_boost();

		if (m_alerts.should_post<lsd_peer_alert>())
			m_alerts.post_alert(lsd_peer_alert(t->get_handle(), peer));
	}

	void session_impl::on_port_map_log(
		char const* msg, int map_transport)
	{
		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);
		// log message
		if (m_alerts.should_post<portmap_log_alert>())
			m_alerts.post_alert(portmap_log_alert(map_transport, msg));
	}

	void session_impl::on_port_mapping(int mapping, address const& ip, int port
		, error_code const& ec, int map_transport)
	{
		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);

		if (mapping == m_udp_mapping[map_transport] && port != 0)
		{
			m_external_udp_port = port;
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
			return;
		}

		if (mapping == m_tcp_mapping[map_transport] && port != 0)
		{
			if (ip != address())
			{
				// TODO: 1 report the proper address of the router as the source IP of
				// this understanding of our external address, instead of the empty address
				set_external_address(ip, source_router, address());
			}

			if (!m_listen_sockets.empty()) {
				m_listen_sockets.front().external_address = ip;
				m_listen_sockets.front().external_port = port;
			}
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
			return;
		}

		if (ec)
		{
			if (m_alerts.should_post<portmap_error_alert>())
				m_alerts.post_alert(portmap_error_alert(mapping
					, map_transport, ec));
		}
		else
		{
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
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

		s.has_incoming_connections = m_stats_counters[counters::has_incoming_connections];

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

	void on_bootstrap(alert_manager& alerts)
	{
		if (alerts.should_post<dht_bootstrap_alert>())
			alerts.post_alert(dht_bootstrap_alert());
	}

	void session_impl::start_dht(entry const& startup_state)
	{
		INVARIANT_CHECK;

		stop_dht();
		m_dht = boost::make_shared<dht::dht_tracker>(boost::ref(*this)
			, boost::ref(m_udp_socket), boost::cref(m_dht_settings)
			, boost::ref(m_stats_counters), &startup_state);

		for (std::list<udp::endpoint>::iterator i = m_dht_router_nodes.begin()
			, end(m_dht_router_nodes.end()); i != end; ++i)
		{
			m_dht->add_router_node(*i);
		}

		m_dht->start(startup_state, boost::bind(&on_bootstrap, boost::ref(m_alerts)));

		m_udp_socket.subscribe(m_dht.get());
	}

	void session_impl::stop_dht()
	{
		if (!m_dht) return;
		m_udp_socket.unsubscribe(m_dht.get());
		m_dht->stop();
		m_dht.reset();
	}

	void session_impl::set_dht_settings(dht_settings const& settings)
	{
		m_dht_settings = settings;
	}

#ifndef TORRENT_NO_DEPRECATE
	entry session_impl::dht_state() const
	{
		if (!m_dht) return entry();
		return m_dht->state();
	}
#endif

	void session_impl::add_dht_node_name(std::pair<std::string, int> const& node)
	{
		if (m_dht) m_dht->add_node(node);
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_dht_router_name_lookup");
#endif
		m_host_resolver.async_resolve(node.first, resolver_interface::abort_on_shutdown
			, boost::bind(&session_impl::on_dht_router_name_lookup
				, this, _1, _2, node.second));
	}

	void session_impl::on_dht_router_name_lookup(error_code const& e
		, std::vector<address> const& addresses, int port)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("session_impl::on_dht_router_name_lookup");
#endif
		if (e)
		{
			if (m_alerts.should_post<dht_error_alert>())
				m_alerts.post_alert(dht_error_alert(
					dht_error_alert::hostname_lookup, e));
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
	}

	// callback for dht_immutable_get
	void session_impl::get_immutable_callback(sha1_hash target
		, dht::item const& i)
	{
		TORRENT_ASSERT(!i.is_mutable());
		m_alerts.post_alert(dht_immutable_item_alert(target, i.value()));
	}

	void session_impl::dht_get_immutable_item(sha1_hash const& target)
	{
		if (!m_dht) return;
		m_dht->get_item(target, boost::bind(&session_impl::get_immutable_callback
			, this, target, _1));
	}

	// callback for dht_mutable_get
	void session_impl::get_mutable_callback(dht::item const& i)
	{
		TORRENT_ASSERT(i.is_mutable());
		m_alerts.post_alert(dht_mutable_item_alert(i.pk(), i.sig(), i.seq()
			, i.salt(), i.value()));
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	void session_impl::dht_get_mutable_item(boost::array<char, 32> key
		, std::string salt)
	{
		if (!m_dht) return;
		m_dht->get_item(key.data(), boost::bind(&session_impl::get_mutable_callback
			, this, _1), salt);
	}

	void on_dht_put(alert_manager& alerts, sha1_hash target)
	{
		if (alerts.should_post<dht_put_alert>())
			alerts.post_alert(dht_put_alert(target));
	}

	void session_impl::dht_put_item(entry data, sha1_hash target)
	{
		if (!m_dht) return;
		m_dht->put_item(data, boost::bind(&on_dht_put, boost::ref(m_alerts)
			, target));
	}

	void put_mutable_callback(alert_manager& alerts, dht::item& i
		, boost::function<void(entry&, boost::array<char,64>&
			, boost::uint64_t&, std::string const&)> cb)
	{
		entry value = i.value();
		boost::array<char, 64> sig = i.sig();
		boost::array<char, 32> pk = i.pk();
		boost::uint64_t seq = i.seq();
		std::string salt = i.salt();
		cb(value, sig, seq, salt);
		i.assign(value, salt, seq, pk.data(), sig.data());

		if (alerts.should_post<dht_put_alert>())
			alerts.post_alert(dht_put_alert(pk, sig, salt, seq));
	}

	void session_impl::dht_put_mutable_item(boost::array<char, 32> key
		, boost::function<void(entry&, boost::array<char,64>&
			, boost::uint64_t&, std::string const&)> cb
		, std::string salt)
	{
		if (!m_dht) return;
		m_dht->put_item(key.data(), boost::bind(&put_mutable_callback
			, boost::ref(m_alerts), _1, cb), salt);
	}

#endif

	void session_impl::maybe_update_udp_mapping(int nat, int local_port, int external_port)
	{
		int local, external, protocol;
		if (nat == 0 && m_natpmp)
		{
			if (m_udp_mapping[nat] != -1)
			{
				if (m_natpmp->get_mapping(m_udp_mapping[nat], local, external, protocol))
				{
					// we already have a mapping. If it's the same, don't do anything
					if (local == local_port && external == external_port && protocol == natpmp::udp)
						return;
				}
				m_natpmp->delete_mapping(m_udp_mapping[nat]);
			}
			m_udp_mapping[nat] = m_natpmp->add_mapping(natpmp::udp
				, local_port, external_port);
			return;
		}
		else if (nat == 1 && m_upnp)
		{
			if (m_udp_mapping[nat] != -1)
			{
				if (m_upnp->get_mapping(m_udp_mapping[nat], local, external, protocol))
				{
					// we already have a mapping. If it's the same, don't do anything
					if (local == local_port && external == external_port && protocol == natpmp::udp)
						return;
				}
				m_upnp->delete_mapping(m_udp_mapping[nat]);
			}
			m_udp_mapping[nat] = m_upnp->add_mapping(upnp::udp
				, local_port, external_port);
			return;
		}
	}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	void session_impl::add_obfuscated_hash(sha1_hash const& obfuscated
		, boost::weak_ptr<torrent> const& t)
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
		TORRENT_ASSERT(is_not_thread());

		m_io_service.post(boost::bind(&session_impl::abort, this));

		// now it's OK for the network thread to exit
		m_work.reset();

#if defined TORRENT_ASIO_DEBUGGING
		int counter = 0;
		while (log_async())
		{
			sleep(1000);
			++counter;
			printf("\x1b[2J\x1b[0;0H\x1b[33m==== Waiting to shut down: %d ==== \x1b[0m\n\n"
				, counter);
		}
		async_dec_threads();

		fprintf(stderr, "\n\nEXPECTS NO MORE ASYNC OPS\n\n\n");

//		m_io_service.post(boost::bind(&io_service::stop, &m_io_service));
#endif

		if (m_thread) m_thread->join();

		m_udp_socket.unsubscribe(this);
		m_udp_socket.unsubscribe(&m_utp_socket_manager);
		m_udp_socket.unsubscribe(&m_tracker_manager);

#ifdef TORRENT_USE_OPENSSL
		m_ssl_udp_socket.unsubscribe(this);
		m_ssl_udp_socket.unsubscribe(&m_utp_socket_manager);
#endif

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());

#ifdef TORRENT_REQUEST_LOGGING
		if (m_request_log) fclose(m_request_log);
#endif

#if defined TORRENT_ASIO_DEBUGGING
		FILE* f = fopen("wakeups.log", "w+");
		if (f != NULL)
		{
			time_point m = min_time();
			if (_wakeups.size() > 0) m = _wakeups[0].timestamp;
			time_point prev = m;
			boost::uint64_t prev_csw = 0;
			if (_wakeups.size() > 0) prev_csw = _wakeups[0].context_switches;
			fprintf(f, "abs. time\trel. time\tctx switch\tidle-wakeup\toperation\n");
			for (int i = 0; i < _wakeups.size(); ++i)
			{
				wakeup_t const& w = _wakeups[i];
				bool idle_wakeup = w.context_switches > prev_csw;
				fprintf(f, "%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%c\t%s\n"
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
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::local_download_rate_limit, bytes_per_second);
		apply_settings_pack(p);
	}

	void session_impl::set_local_upload_rate_limit(int bytes_per_second)
	{
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::local_upload_rate_limit, bytes_per_second);
		apply_settings_pack(p);
	}

	void session_impl::set_download_rate_limit(int bytes_per_second)
	{
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::download_rate_limit, bytes_per_second);
		apply_settings_pack(p);
	}

	void session_impl::set_upload_rate_limit(int bytes_per_second)
	{
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::upload_rate_limit, bytes_per_second);
		apply_settings_pack(p);
	}

	void session_impl::set_max_connections(int limit)
	{
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::connections_limit, limit);
		apply_settings_pack(p);
	}

	void session_impl::set_max_uploads(int limit)
	{
		settings_pack* p = new settings_pack;
		p->set_int(settings_pack::unchoke_slots_limit, limit);
		apply_settings_pack(p);
	}

	int session_impl::local_upload_rate_limit() const
	{
		return upload_rate_limit(m_local_peer_class);
	}

	int session_impl::local_download_rate_limit() const
	{
		return download_rate_limit(m_local_peer_class);
	}

	int session_impl::upload_rate_limit() const
	{
		return upload_rate_limit(m_global_class);
	}

	int session_impl::download_rate_limit() const
	{
		return download_rate_limit(m_global_class);
	}
#endif

	void session_impl::update_peer_tos()
	{
		error_code ec;

#if TORRENT_USE_IPV6 && defined IPV6_TCLASS
		if (m_udp_socket.local_endpoint(ec).address().is_v6())
			m_udp_socket.set_option(traffic_class(m_settings.get_int(settings_pack::peer_tos)), ec);
		else
#endif
			m_udp_socket.set_option(type_of_service(m_settings.get_int(settings_pack::peer_tos)), ec);

#ifdef TORRENT_USE_OPENSSL
#if TORRENT_USE_IPV6 && defined IPV6_TCLASS
		if (m_ssl_udp_socket.local_endpoint(ec).address().is_v6())
			m_ssl_udp_socket.set_option(traffic_class(m_settings.get_int(settings_pack::peer_tos)), ec);
		else
#endif
			m_ssl_udp_socket.set_option(type_of_service(m_settings.get_int(settings_pack::peer_tos)), ec);
#endif

#if defined TORRENT_LOGGING
		session_log(">>> SET_TOS [ udp_socket tos: %x e: %s ]"
			, m_settings.get_int(settings_pack::peer_tos)
			, ec.message().c_str());
#endif
	}

	void session_impl::update_user_agent()
	{
		// replace all occurances of '\n' with ' '.
		std::string agent = m_settings.get_str(settings_pack::user_agent);
		std::string::iterator i = agent.begin();
		while ((i = std::find(i, agent.end(), '\n'))
			!= agent.end())
			*i = ' ';
		m_settings.set_str(settings_pack::user_agent, agent);
	}

	void session_impl::update_unchoke_limit()
	{
		int unchoke_limit = m_settings.get_int(settings_pack::unchoke_slots_limit);

		int allowed_upload_slots = unchoke_limit;
		
		if (allowed_upload_slots < 0)
			allowed_upload_slots = (std::numeric_limits<int>::max)();

		m_stats_counters.set_value(counters::num_unchoke_slots
			, allowed_upload_slots);

		if (m_settings.get_int(settings_pack::num_optimistic_unchoke_slots)
			>= allowed_upload_slots / 2)
		{
			if (m_alerts.should_post<performance_alert>())
				m_alerts.post_alert(performance_alert(torrent_handle()
					, performance_alert::too_many_optimistic_unchoke_slots));
		}
	}

	void session_impl::update_connection_speed()
	{
		if (m_settings.get_int(settings_pack::connection_speed) < 0)
			m_settings.set_int(settings_pack::connection_speed, 200);
	}

	void session_impl::update_queued_disk_bytes()
	{
		boost::uint64_t cache_size = m_settings.get_int(settings_pack::cache_size);
		if (m_settings.get_int(settings_pack::max_queued_disk_bytes) / 16 / 1024
			> cache_size / 2
			&& cache_size > 5
			&& m_alerts.should_post<performance_alert>())
		{
			m_alerts.post_alert(performance_alert(torrent_handle()
				, performance_alert::too_high_disk_queue_limit));
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

	void session_impl::update_dht_upload_rate_limit()
	{
		m_udp_socket.set_rate_limit(m_settings.get_int(settings_pack::dht_upload_rate_limit));
	}

	void session_impl::update_disk_threads()
	{
		if (m_settings.get_int(settings_pack::aio_threads) < 1)
			m_settings.set_int(settings_pack::aio_threads, 1);

#if !TORRENT_USE_PREAD && !TORRENT_USE_PREADV
		// if we don't have pread() nor preadv() there's no way
		// to perform concurrent file operations on the same file
		// handle, so we must limit the disk thread to a single one

		if (m_settings.get_int(settings_pack::aio_threads) > 1)
			m_settings.set_int(settings_pack::aio_threads, 1);
#endif

		m_disk_thread.set_num_threads(m_settings.get_int(settings_pack::aio_threads));
	}

	void session_impl::update_network_threads()
	{
		int num_threads = m_settings.get_int(settings_pack::network_threads);
		int num_pools = num_threads > 0 ? num_threads : 1;
		while (num_pools > m_net_thread_pool.size())
		{
			m_net_thread_pool.push_back(boost::make_shared<network_thread_pool>());
			m_net_thread_pool.back()->set_num_threads(1);
		}

		while (num_pools < m_net_thread_pool.size())
		{
			m_net_thread_pool.erase(m_net_thread_pool.end() - 1);
		}

		if (num_threads == 0 && m_net_thread_pool.size() > 0)
		{
			m_net_thread_pool[0]->set_num_threads(0);
		}
	}

	void session_impl::post_socket_job(socket_job& j)
	{
		uintptr_t idx = 0;
		if (m_net_thread_pool.size() > 1)
		{
			// each peer needs to be pinned to a specific thread
			// since reading and writing simultaneously on the same
			// socket from different threads is not supported by asio.
			// as long as a specific socket is consistently used from
			// the same thread, it's safe
			idx = uintptr_t(j.peer.get());
			idx ^= idx >> 8;
			idx %= m_net_thread_pool.size();
		}
		m_net_thread_pool[idx]->post_job(j);
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

		m_pending_auto_manage = true;
		m_need_auto_manage = true;

		// if we haven't started yet, don't actually trigger this
		if (!m_thread) return;

		m_io_service.post(boost::bind(&session_impl::on_trigger_auto_manage, this));
	}

	void session_impl::on_trigger_auto_manage()
	{
		assert(m_pending_auto_manage);
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
		error_code ec;
		set_socket_buffer_size(m_udp_socket, m_settings, ec);
		if (ec)
		{
			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.post_alert(udp_error_alert(udp::endpoint(), ec));
		}

#ifdef TORRENT_USE_OPENSSL
		set_socket_buffer_size(m_ssl_udp_socket, m_settings, ec);
		if (ec)
		{
			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.post_alert(udp_error_alert(udp::endpoint(), ec));
		}
#endif
	}

	void session_impl::update_dht_announce_interval()
	{
#ifndef TORRENT_DISABLE_DHT
		if (!m_dht)
		{
#if defined TORRENT_LOGGING
			session_log("not starting DHT announce timer: m_dht == NULL");
#endif
			return;
		}

		m_dht_interval_update_torrents = m_torrents.size();

		// if we haven't started yet, don't actually trigger this
		if (!m_thread)
		{
#if defined TORRENT_LOGGING
			session_log("not starting DHT announce timer: thread not running yet");
#endif
			return;
		}

		if (m_abort)
		{
#if defined TORRENT_LOGGING
			session_log("not starting DHT announce timer: m_abort set");
#endif
			return;
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("session_impl::on_dht_announce");
#endif
		error_code ec;
		int delay = (std::max)(m_settings.get_int(settings_pack::dht_announce_interval)
			/ (std::max)(int(m_torrents.size()), 1), 1);
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait(
			boost::bind(&session_impl::on_dht_announce, this, _1));
#endif
	}

	void session_impl::update_anonymous_mode()
	{
		if (!m_settings.get_bool(settings_pack::anonymous_mode)) return;

		m_settings.set_str(settings_pack::user_agent, "");
		url_random((char*)&m_peer_id[0], (char*)&m_peer_id[0] + 20);
	}

	void session_impl::update_force_proxy()
	{
		m_udp_socket.set_force_proxy(m_settings.get_bool(settings_pack::force_proxy));
#ifdef TORRENT_USE_OPENSSL
		m_ssl_udp_socket.set_force_proxy(m_settings.get_bool(settings_pack::force_proxy));
#endif

		if (!m_settings.get_bool(settings_pack::force_proxy)) return;

		// if we haven't started yet, don't actually trigger this
		if (!m_thread) return;

		// enable force_proxy mode. We don't want to accept any incoming
		// connections, except through a proxy.
		stop_lsd();
		stop_upnp();
		stop_natpmp();
#ifndef TORRENT_DISABLE_DHT
		stop_dht();
#endif
		// close the listen sockets
		error_code ec;
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
			i->sock->close(ec);
		m_listen_sockets.clear();
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
		if (m_settings.get_int(settings_pack::connections_limit) <= 0)
		{
			m_settings.set_int(settings_pack::connections_limit, (std::numeric_limits<int>::max)());
#if TORRENT_USE_RLIMIT
			rlimit l;
			if (getrlimit(RLIMIT_NOFILE, &l) == 0
				&& l.rlim_cur != RLIM_INFINITY)
			{
				m_settings.set_int(settings_pack::connections_limit
					, l.rlim_cur - m_settings.get_int(settings_pack::file_pool_size));
				if (m_settings.get_int(settings_pack::connections_limit) < 5)
					m_settings.set_int(settings_pack::connections_limit, 5);
			}
#endif
		}

		if (num_connections() > m_settings.get_int(settings_pack::connections_limit)
			&& !m_torrents.empty())
		{
			// if we have more connections that we're allowed, disconnect
			// peers from the torrents so that they are all as even as possible

			int to_disconnect = num_connections() - m_settings.get_int(settings_pack::connections_limit);

			int last_average = 0;
			int average = m_settings.get_int(settings_pack::connections_limit) / m_torrents.size();
	
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
		init_peer_class_filter(m_settings.get_bool(settings_pack::ignore_limits_on_local_network));
	}
#endif

	void session_impl::update_alert_mask()
	{
		m_alerts.set_alert_mask(m_settings.get_int(settings_pack::alert_mask));
	}

	void session_impl::set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		m_alerts.set_dispatch_function(fun);
	}

	// this function is called on the user's thread
	// not the network thread
	std::auto_ptr<alert> session_impl::pop_alert()
	{
		int num_resume = 0;
		std::auto_ptr<alert> ret = m_alerts.get(num_resume);
		if (num_resume > 0)
		{
			// we can only issue more resume data jobs from
			// the network thread
			m_io_service.post(boost::bind(&session_impl::async_resume_dispatched
				, this, num_resume));
		}
		return ret;
	}
	
	// this function is called on the user's thread
	// not the network thread
	void session_impl::pop_alerts(std::deque<alert*>* alerts)
	{
		int num_resume = 0;
		m_alerts.get_all(alerts, num_resume);
		// we can only issue more resume data jobs from
		// the network thread
		m_io_service.post(boost::bind(&session_impl::async_resume_dispatched
			, this, num_resume));
	}

	alert const* session_impl::wait_for_alert(time_duration max_wait)
	{
		return m_alerts.wait_for_alert(max_wait);
	}

#ifndef TORRENT_NO_DEPRECATE
	size_t session_impl::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		m_settings.set_int(settings_pack::alert_queue_size, queue_size_limit_);
		return m_alerts.set_alert_queue_size_limit(queue_size_limit_);
	}
#endif

	void session_impl::start_lsd()
	{
		INVARIANT_CHECK;

		if (m_lsd) return;

		m_lsd = boost::make_shared<lsd>(boost::ref(m_io_service)
			, boost::bind(&session_impl::on_lsd_peer, this, _1, _2)
#if defined TORRENT_LOGGING
			, boost::bind(&session_impl::on_lsd_log, this, _1)
#endif
			);
		error_code ec;
		m_lsd->start(ec);
		if (ec && m_alerts.should_post<lsd_error_alert>())
			m_alerts.post_alert(lsd_error_alert(ec));
	}
	
#if defined TORRENT_LOGGING
	void session_impl::on_lsd_log(char const* log)
	{
		if (!m_alerts.should_post<log_alert>()) return;
		m_alerts.post_alert(log_alert(log));
	}
#endif

	natpmp* session_impl::start_natpmp()
	{
		INVARIANT_CHECK;

		if (m_natpmp) return m_natpmp.get();

		// the natpmp constructor may fail and call the callbacks
		// into the session_impl.
		m_natpmp = boost::make_shared<natpmp>(boost::ref(m_io_service)
			, boost::bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, _4, 0)
			, boost::bind(&session_impl::on_port_map_log
				, this, _1, 0));
		m_natpmp->start();

		int ssl_port = ssl_listen_port();

		if (m_listen_interface.port() > 0)
		{
			remap_tcp_ports(1, m_listen_interface.port(), ssl_port);
		}
		if (m_udp_socket.is_open())
		{
			m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
				, m_listen_interface.port(), m_listen_interface.port());
		}
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl_udp_socket.is_open() && ssl_port > 0)
		{
			m_ssl_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
				, ssl_port, ssl_port);
		}
#endif
		return m_natpmp.get();
	}

	upnp* session_impl::start_upnp()
	{
		INVARIANT_CHECK;

		if (m_upnp) return m_upnp.get();

		// the upnp constructor may fail and call the callbacks
		m_upnp = boost::make_shared<upnp>(boost::ref(m_io_service)
			, m_listen_interface.address()
			, m_settings.get_str(settings_pack::user_agent)
			, boost::bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, _4, 1)
			, boost::bind(&session_impl::on_port_map_log
				, this, _1, 1)
			, m_settings.get_bool(settings_pack::upnp_ignore_nonrouters));
		m_upnp->start();

		int ssl_port = ssl_listen_port();

		m_upnp->discover_device();
		if (m_listen_interface.port() > 0 || ssl_port > 0)
		{
			remap_tcp_ports(2, m_listen_interface.port(), ssl_port);
		}
		if (m_udp_socket.is_open())
		{
			m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
				, m_listen_interface.port(), m_listen_interface.port());
		}
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl_udp_socket.is_open() && ssl_port > 0)
		{
			m_ssl_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
				, ssl_port, ssl_port);
		}
#endif
		return m_upnp.get();
	}

	int session_impl::add_port_mapping(int t, int external_port
		, int local_port)
	{
		int ret = 0;
		if (m_upnp) ret = m_upnp->add_mapping((upnp::protocol_type)t, external_port
			, local_port);
		if (m_natpmp) ret = m_natpmp->add_mapping((natpmp::protocol_type)t, external_port
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
		if (m_natpmp)
		{
			m_natpmp->close();
			m_udp_mapping[0] = -1;
			m_tcp_mapping[0] = -1;
#ifdef TORRENT_USE_OPENSSL
			m_ssl_tcp_mapping[0] = -1;
			m_ssl_udp_mapping[0] = -1;
#endif
		}
		m_natpmp.reset();
	}
	
	void session_impl::stop_upnp()
	{
		if (m_upnp)
		{
			m_upnp->close();
			m_udp_mapping[1] = -1;
			m_tcp_mapping[1] = -1;
#ifdef TORRENT_USE_OPENSSL
			m_ssl_tcp_mapping[1] = -1;
			m_ssl_udp_mapping[1] = -1;
#endif
		}
		m_upnp.reset();
	}

	external_ip const& session_impl::external_address() const
	{ return m_external_ip; }

	// this is the DHT observer version. DHT is the implied source
	void session_impl::set_external_address(address const& ip
		, address const& source)
	{
		set_external_address(ip, source_dht, source);
	}

	void session_impl::set_external_address(address const& ip
		, int source_type, address const& source)
	{
#if defined TORRENT_LOGGING
		session_log(": set_external_address(%s, %d, %s)", print_address(ip).c_str()
			, source_type, print_address(source).c_str());
#endif

		if (!m_external_ip.cast_vote(ip, source_type, source)) return;

#if defined TORRENT_LOGGING
		session_log("  external IP updated");
#endif

		if (m_alerts.should_post<external_ip_alert>())
			m_alerts.post_alert(external_ip_alert(ip));

		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->new_external_ip();
		}

		// since we have a new external IP now, we need to
		// restart the DHT with a new node ID
#ifndef TORRENT_DISABLE_DHT
		// TODO: 1 we only need to do this if our global IPv4 address has changed
		// since the DHT (currently) only supports IPv4. Since restarting the DHT
		// is kind of expensive, it would be nice to not do it unnecessarily
		if (m_dht)
		{
			entry s = m_dht->state();
			int cur_state = 0;
			int prev_state = 0;
			entry* nodes1 = s.find_key("nodes");
			if (nodes1 && nodes1->type() == entry::list_t) cur_state = nodes1->list().size();
			entry* nodes2 = m_dht_state.find_key("nodes");
			if (nodes2 && nodes2->type() == entry::list_t) prev_state = nodes2->list().size();
			if (cur_state > prev_state) m_dht_state = s;
			start_dht(m_dht_state);
		}
#endif
	}

	// decrement the refcount of the block in the disk cache
	// since the network thread doesn't need it anymore
	void session_impl::reclaim_block(block_cache_reference ref)
	{
		m_disk_thread.reclaim_block(ref);
	}

	char* session_impl::allocate_disk_buffer(char const* category)
	{
		return m_disk_thread.allocate_disk_buffer(category);
	}

	char* session_impl::async_allocate_disk_buffer(char const* category
		, boost::function<void(char*)> const& handler)
	{
		return m_disk_thread.async_allocate_disk_buffer(category, handler);
	}

	void session_impl::free_disk_buffer(char* buf)
	{
		m_disk_thread.free_disk_buffer(buf);
	}
	
	char* session_impl::allocate_disk_buffer(bool& exceeded
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
		return (char*)malloc(num_bytes);
#else
		return (char*)m_send_buffers.malloc();
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

		int loaded_limit = m_settings.get_int(settings_pack::active_loaded_limit);
		TORRENT_ASSERT(m_num_save_resume <= loaded_limit);
//		if (m_num_save_resume < loaded_limit)
//			TORRENT_ASSERT(m_save_resume_queue.empty());

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

#if TORRENT_HAS_BOOST_UNORDERED
		boost::unordered_set<torrent*> unique_torrents;
#else
		std::set<torrent*> unique_torrents;
#endif
		for (list_iterator i = m_torrent_lru.iterate(); i.get(); i.next())
		{
			torrent* t = (torrent*)i.get();
			TORRENT_ASSERT(t->is_loaded());
			TORRENT_ASSERT(unique_torrents.count(t) == 0);
			unique_torrents.insert(t);
		}
		TORRENT_ASSERT(unique_torrents.size() == m_torrent_lru.size());

		int torrent_state_gauges[counters::num_error_torrents - counters::num_checking_torrents + 1];
		memset(torrent_state_gauges, 0, sizeof(torrent_state_gauges));
	
#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS

#if TORRENT_HAS_BOOST_UNORDERED
		boost::unordered_set<int> unique;
#else
		std::set<int> unique;
#endif
#endif

		int num_active_downloading = 0;
		int num_active_finished = 0;
		int total_downloaders = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent> t = i->second;
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

#if TORRENT_HAS_BOOST_UNORDERED
		boost::unordered_set<peer_connection*> unique_peers;
#else
		std::set<peer_connection*> unique_peers;
#endif
		TORRENT_ASSERT(m_settings.get_int(settings_pack::connections_limit) > 0);

		int unchokes = 0;
		int unchokes_all = 0;
		int num_optimistic = 0;
		int disk_queue[2] = {0, 0};
		for (connection_map::const_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			boost::shared_ptr<torrent> t = (*i)->associated_torrent().lock();
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

		int unchoked_counter_all = m_stats_counters[counters::num_peers_up_unchoked_all];
		int unchoked_counter = m_stats_counters[counters::num_peers_up_unchoked];
		int unchoked_counter_optimistic
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
#endif

#if defined TORRENT_LOGGING
		tracker_logger::tracker_logger(session_interface& ses): m_ses(ses) {}
		void tracker_logger::tracker_warning(tracker_request const& req
			, std::string const& str)
		{
			debug_log("*** tracker warning: %s", str.c_str());
		}

		void tracker_logger::tracker_response(tracker_request const&
			, libtorrent::address const& tracker_ip
			, std::list<address> const& ip_list
			, struct tracker_response const& resp)
		{
#if defined TORRENT_LOGGING 
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
					, i->pid.is_all_zeros()?"":to_hex(i->pid.to_string()).c_str()
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
#endif
		}

		void tracker_logger::tracker_request_timed_out(
			tracker_request const&)
		{
			debug_log("*** tracker timed out");
		}

		void tracker_logger::tracker_request_error(tracker_request const& r
			, int response_code, error_code const& ec, const std::string& str
			, int retry_interval)
		{
			debug_log("*** tracker error: %d: %s %s"
				, response_code, ec.message().c_str(), str.c_str());
		}
		
		void tracker_logger::debug_log(const char* fmt, ...) const
		{
			va_list v;	
			va_start(v, fmt);
	
			char usr[1024];
			vsnprintf(usr, sizeof(usr), fmt, v);
			va_end(v);
			m_ses.session_log("%s", usr);
		}
#endif
}}

