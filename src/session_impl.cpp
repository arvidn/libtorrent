/*

Copyright (c) 2006-2018, Arvid Norberg, Magnus Jonsson
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
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <functional>
#include <type_traits>
#include <numeric> // for accumulate

#if TORRENT_USE_INVARIANT_CHECKS
#include <unordered_set>
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/v6_only.hpp>
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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#ifndef TORRENT_DISABLE_DHT
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/types.hpp"
#include "libtorrent/kademlia/node_entry.hpp"
#endif
#include "libtorrent/enum_net.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/lsd.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/peer_info.hpp"
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
#include "libtorrent/aux_/scope_end.hpp"
#include "libtorrent/aux_/set_socket_buffer.hpp"
#include "libtorrent/aux_/generate_peer_id.hpp"
#include "libtorrent/aux_/ffs.hpp"

#ifndef TORRENT_DISABLE_LOGGING

#include "libtorrent/socket_io.hpp"

// for logging stat layout
#include "libtorrent/stat.hpp"

#include <cstdarg> // for va_list

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

#if GCRYPT_VERSION_NUMBER < 0x010600
extern "C" {
GCRY_THREAD_OPTION_PTHREAD_IMPL;
}
#endif

namespace {

	// libgcrypt requires this to initialize the library
	struct gcrypt_setup
	{
		gcrypt_setup()
		{
			gcry_check_version(nullptr);
#if GCRYPT_VERSION_NUMBER < 0x010600
			gcry_error_t e = gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
			if (e != 0) std::fprintf(stderr, "libcrypt ERROR: %s\n", gcry_strerror(e));
			e = gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
			if (e != 0) std::fprintf(stderr, "initialization finished error: %s\n", gcry_strerror(e));
#endif
		}
	} gcrypt_global_constructor;
}

#endif // TORRENT_USE_LIBGCRYPT

#ifdef TORRENT_USE_OPENSSL

#include <openssl/crypto.h>
#include <openssl/rand.h>

// by openssl changelog at https://www.openssl.org/news/changelog.html
// Changes between 1.0.2h and 1.1.0  [25 Aug 2016]
// - Most global cleanup functions are no longer required because they are handled
//   via auto-deinit. Affected function CRYPTO_cleanup_all_ex_data()
#if !defined(OPENSSL_API_COMPAT) || OPENSSL_API_COMPAT < 0x10100000L
namespace {

	// openssl requires this to clean up internal
	// structures it allocates
	struct openssl_cleanup
	{
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
		~openssl_cleanup() { CRYPTO_cleanup_all_ex_data(); }
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
	} openssl_global_destructor;
}
#endif

#endif // TORRENT_USE_OPENSSL

#ifdef TORRENT_WINDOWS
// for ERROR_SEM_TIMEOUT
#include <winerror.h>
#endif

using namespace std::placeholders;

#ifdef BOOST_NO_EXCEPTIONS
namespace boost {

	void throw_exception(std::exception const& e) { std::abort(); }
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

	constexpr listen_socket_flags_t listen_socket_t::accept_incoming;
	constexpr listen_socket_flags_t listen_socket_t::local_network;
	constexpr listen_socket_flags_t listen_socket_t::was_expanded;
	constexpr listen_socket_flags_t listen_socket_t::proxy;

	constexpr ip_source_t session_interface::source_dht;
	constexpr ip_source_t session_interface::source_peer;
	constexpr ip_source_t session_interface::source_tracker;
	constexpr ip_source_t session_interface::source_router;

	std::vector<std::shared_ptr<listen_socket_t>>::iterator partition_listen_sockets(
		std::vector<listen_endpoint_t>& eps
		, std::vector<std::shared_ptr<listen_socket_t>>& sockets)
	{
		return std::partition(sockets.begin(), sockets.end()
			, [&eps](std::shared_ptr<listen_socket_t> const& sock)
		{
			auto match = std::find_if(eps.begin(), eps.end()
				, [&sock](listen_endpoint_t const& ep)
			{
				return ep.ssl == sock->ssl
					&& ep.port == sock->original_port
					&& ep.device == sock->device
					&& ep.flags == sock->flags
					&& ep.addr == sock->local_endpoint.address();
			});

			if (match != eps.end())
			{
				// remove the matched endpoint so that another socket can't match it
				// this also signals to the caller that it doesn't need to create a
				// socket for the endpoint
				eps.erase(match);
				return true;
			}
			else
			{
				return false;
			}
		});
	}

	// To comply with BEP 45 multi homed clients must run separate DHT nodes
	// on each interface they use to talk to the DHT. This is enforced
	// by prohibiting creating a listen socket on [::] and 0.0.0.0. Instead the list of
	// interfaces is enumerated and sockets are created for each of them.
	void expand_unspecified_address(span<ip_interface const> const ifs
		, span<ip_route const> const routes
		, std::vector<listen_endpoint_t>& eps)
	{
		auto unspecified_begin = std::partition(eps.begin(), eps.end()
			, [](listen_endpoint_t const& ep) { return !ep.addr.is_unspecified(); });
		std::vector<listen_endpoint_t> unspecified_eps(unspecified_begin, eps.end());
		eps.erase(unspecified_begin, eps.end());
		for (auto const& uep : unspecified_eps)
		{
			bool const v4 = uep.addr.is_v4();
			for (auto const& ipface : ifs)
			{
				if (!ipface.preferred)
					continue;
				if (ipface.interface_address.is_v4() != v4)
					continue;
				if (!uep.device.empty() && uep.device != ipface.name)
					continue;
				if (std::any_of(eps.begin(), eps.end(), [&](listen_endpoint_t const& e)
				{
					// ignore device name because we don't want to create
					// duplicates if the user explicitly configured an address
					// without a device name
					return e.addr == ipface.interface_address
						&& e.port == uep.port
						&& e.ssl == uep.ssl;
				}))
				{
					continue;
				}

				// record whether the device has a gateway associated with it
				// (which indicates it can be used to reach the internet)
				// if the IP address tell us it's loopback or link-local, don't
				// bother looking for the gateway
				bool const local = ipface.interface_address.is_loopback()
					|| is_link_local(ipface.interface_address)
					|| (!is_global(ipface.interface_address)
						&& !has_default_route(ipface.name, family(ipface.interface_address), routes));

				eps.emplace_back(ipface.interface_address, uep.port, uep.device
					, uep.ssl, uep.flags | listen_socket_t::was_expanded
					| (local ? listen_socket_t::local_network : listen_socket_flags_t{}));
			}
		}
	}

	void expand_devices(span<ip_interface const> const ifs
		, std::vector<listen_endpoint_t>& eps)
	{
		for (auto& ep : eps)
		{
			auto const iface = ep.device.empty()
				? std::find_if(ifs.begin(), ifs.end(), [&](ip_interface const& ipface)
					{
						return match_addr_mask(ipface.interface_address, ep.addr, ipface.netmask);
					})
				: std::find_if(ifs.begin(), ifs.end(), [&](ip_interface const& ipface)
					{
						return ipface.name == ep.device
							&& match_addr_mask(ipface.interface_address, ep.addr, ipface.netmask);
					});

			if (iface == ifs.end())
			{
				// we can't find which device this is for, just assume we can't
				// reach anything on it
				ep.netmask = build_netmask(0, ep.addr.is_v4() ? AF_INET : AF_INET6);
				continue;
			}

			ep.netmask = iface->netmask;
			ep.device = iface->name;
		}
	}

	bool listen_socket_t::can_route(address const& addr) const
	{
		// if this is a proxy, we assume it can reach everything
		if (flags & proxy) return true;

		if (is_v4(local_endpoint) != addr.is_v4()) return false;

		if (local_endpoint.address().is_v6()
			&& local_endpoint.address().to_v6().scope_id() != addr.to_v6().scope_id())
			return false;

		if (local_endpoint.address() == addr) return true;
		if (local_endpoint.address().is_unspecified()) return true;
		if (match_addr_mask(addr, local_endpoint.address(), netmask)) return true;
		return !(flags & local_network);
	}

	void session_impl::init_peer_class_filter(bool unlimited_local)
	{
		// set the default peer_class_filter to use the local peer class
		// for peers on local networks
		std::uint32_t lfilter = 1 << static_cast<std::uint32_t>(m_local_peer_class);
		std::uint32_t gfilter = 1 << static_cast<std::uint32_t>(m_global_class);

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
			{"172.16.0.0", "172.31.255.255", lfilter},
			{"192.168.0.0", "192.168.255.255", lfilter},
			// link-local
			{"169.254.0.0", "169.254.255.255", lfilter},
			// loop-back
			{"127.0.0.0", "127.255.255.255", lfilter},
		};

		static const class_mapping v6_classes[] =
		{
			// everything
			{"::0", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", gfilter},
			// local networks
			{"fc00::", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", lfilter},
			// link-local
			{"fe80::", "febf::ffff:ffff:ffff:ffff:ffff:ffff:ffff", lfilter},
			// loop-back
			{"::1", "::1", lfilter},
		};

		class_mapping const* p = v4_classes;
		int len = sizeof(v4_classes) / sizeof(v4_classes[0]);
		if (!unlimited_local) len = 1;
		for (int i = 0; i < len; ++i)
		{
			error_code ec;
			address_v4 begin = make_address_v4(p[i].first, ec);
			address_v4 end = make_address_v4(p[i].last, ec);
			if (ec) continue;
			m_peer_class_filter.add_rule(begin, end, p[i].filter);
		}
		p = v6_classes;
		len = sizeof(v6_classes) / sizeof(v6_classes[0]);
		if (!unlimited_local) len = 1;
		for (int i = 0; i < len; ++i)
		{
			error_code ec;
			address_v6 begin = make_address_v6(p[i].first, ec);
			address_v6 end = make_address_v6(p[i].last, ec);
			if (ec) continue;
			m_peer_class_filter.add_rule(begin, end, p[i].filter);
		}
	}

#if defined TORRENT_USE_OPENSSL && OPENSSL_VERSION_NUMBER >= 0x90812f
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
	namespace {
	// when running bittorrent over SSL, the SNI (server name indication)
	// extension is used to know which torrent the incoming connection is
	// trying to connect to. The 40 first bytes in the name is expected to
	// be the hex encoded info-hash
	int servername_callback(SSL* s, int*, void* arg)
	{
		auto* ses = reinterpret_cast<session_impl*>(arg);
		const char* servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);

		if (!servername || std::strlen(servername) < 40)
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
		SSL_set_verify(s, SSL_CTX_get_verify_mode(torrent_context)
			, SSL_CTX_get_verify_callback(torrent_context));

		return SSL_TLSEXT_ERR_OK;
	}
	} // anonymous namespace
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
#endif

	session_impl::session_impl(io_service& ios, settings_pack const& pack)
		: m_settings(pack)
		, m_io_service(ios)
#ifdef TORRENT_USE_OPENSSL
#if BOOST_VERSION >= 106400
		, m_ssl_ctx(ssl::context::tls_client)
		, m_peer_ssl_ctx(ssl::context::tls)
#else
		, m_ssl_ctx(ssl::context::tlsv12_client)
		, m_peer_ssl_ctx(ssl::context::tlsv12)
#endif
#endif
		, m_alerts(m_settings.get_int(settings_pack::alert_queue_size)
			, alert_category_t{static_cast<unsigned int>(m_settings.get_int(settings_pack::alert_mask))})
		, m_disk_thread(m_io_service, m_settings, m_stats_counters)
		, m_download_rate(peer_connection::download_channel)
		, m_upload_rate(peer_connection::upload_channel)
		, m_host_resolver(m_io_service)
		, m_tracker_manager(
			std::bind(&session_impl::send_udp_packet_listen, this, _1, _2, _3, _4, _5)
			, std::bind(&session_impl::send_udp_packet_hostname_listen, this, _1, _2, _3, _4, _5, _6)
			, m_stats_counters
			, m_host_resolver
			, m_settings
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, *this
#endif
			)
		, m_work(new io_service::work(m_io_service))
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
			std::bind(&session_impl::send_udp_packet, this, _1, _2, _3, _4, _5)
			, std::bind(&session_impl::incoming_connection, this, _1)
			, m_io_service
			, m_settings, m_stats_counters, nullptr)
#ifdef TORRENT_USE_OPENSSL
		, m_ssl_utp_socket_manager(
			std::bind(&session_impl::send_udp_packet, this, _1, _2, _3, _4, _5)
			, std::bind(&session_impl::on_incoming_utp_ssl, this, _1)
			, m_io_service
			, m_settings, m_stats_counters
			, &m_peer_ssl_ctx)
#endif
		, m_timer(m_io_service)
		, m_lsd_announce_timer(m_io_service)
		, m_close_file_timer(m_io_service)
	{
	}

	template <typename Fun, typename... Args>
	void session_impl::wrap(Fun f, Args&&... a)
#ifndef BOOST_NO_EXCEPTIONS
	try
#endif
	{
		(this->*f)(std::forward<Args>(a)...);
	}
#ifndef BOOST_NO_EXCEPTIONS
	catch (system_error const& e) {
		alerts().emplace_alert<session_error_alert>(e.code(), e.what());
		pause();
	} catch (std::exception const& e) {
		alerts().emplace_alert<session_error_alert>(error_code(), e.what());
		pause();
	} catch (...) {
		alerts().emplace_alert<session_error_alert>(error_code(), "unknown error");
		pause();
	}
#endif

	// This function is called by the creating thread, not in the message loop's
	// io_service thread.
	// TODO: 2 is there a reason not to move all of this into init()? and just
	// post it to the io_service?
	void session_impl::start_session()
	{
#ifndef TORRENT_DISABLE_LOGGING
		session_log("start session");
#endif

#ifdef TORRENT_USE_OPENSSL
		error_code ec;
		m_ssl_ctx.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
		m_ssl_ctx.set_default_verify_paths(ec);
		m_peer_ssl_ctx.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
#if OPENSSL_VERSION_NUMBER >= 0x90812f
		aux::openssl_set_tlsext_servername_callback(m_peer_ssl_ctx.native_handle()
			, servername_callback);
		aux::openssl_set_tlsext_servername_arg(m_peer_ssl_ctx.native_handle(), this);
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

		session_log("version: %s revision: %s"
			, LIBTORRENT_VERSION, LIBTORRENT_REVISION);

#endif // TORRENT_DISABLE_LOGGING

		// ---- auto-cap max connections ----
		int const max_files = max_open_files();
		// deduct some margin for epoll/kqueue, log files,
		// futexes, shared objects etc.
		// 80% of the available file descriptors should go to connections
		m_settings.set_int(settings_pack::connections_limit, std::min(
			m_settings.get_int(settings_pack::connections_limit)
			, std::max(5, (max_files - 20) * 8 / 10)));
		// 20% goes towards regular files (see disk_io_thread)
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log("max-connections: %d max-files: %d"
				, m_settings.get_int(settings_pack::connections_limit)
				, max_files);
		}
#endif

		m_io_service.post([this] { this->wrap(&session_impl::init); });
	}

	void session_impl::init()
	{
		// this is a debug facility
		// see single_threaded in debug.hpp
		thread_started();

		TORRENT_ASSERT(is_single_thread());

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** session thread init");
#endif

		// this is where we should set up all async operations. This
		// is called from within the network thread as opposed to the
		// constructor which is called from the main thread

#if defined TORRENT_ASIO_DEBUGGING
		async_inc_threads();
		add_outstanding_async("session_impl::on_tick");
#endif
		m_io_service.post([this]{ this->wrap(&session_impl::on_tick, error_code()); });

		int const lsd_announce_interval
			= m_settings.get_int(settings_pack::local_service_announce_interval);
		int const delay = std::max(lsd_announce_interval
			/ std::max(static_cast<int>(m_torrents.size()), 1), 1);
		error_code ec;
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		ADD_OUTSTANDING_ASYNC("session_impl::on_lsd_announce");
		m_lsd_announce_timer.async_wait([this](error_code const& e) {
			this->wrap(&session_impl::on_lsd_announce, e); } );
		TORRENT_ASSERT(!ec);

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" done starting session");
#endif

		// this applies unchoke settings from m_settings
		recalculate_unchoke_slots();

		// apply all m_settings to this session
		run_all_updates(*this);
		reopen_listen_sockets(false);

#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif
	}

	// TODO: 2 the ip filter should probably be saved here too
	void session_impl::save_state(entry* eptr, save_state_flags_t const flags) const
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
			e["dht"] = dht::save_dht_settings(m_dht_settings);
		}

		if (m_dht && (flags & session::save_dht_state))
		{
			e["dht state"] = dht::save_dht_state(m_dht->state());
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto const& ext : m_ses_extensions[plugins_all_idx])
		{
			ext->save_state(*eptr);
		}
#endif
	}

	proxy_settings session_impl::proxy() const
	{
		return proxy_settings(m_settings);
	}

	void session_impl::load_state(bdecode_node const* e
		, save_state_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());

		bdecode_node settings;
		if (e->type() != bdecode_node::dict_t) return;

#ifndef TORRENT_DISABLE_DHT
		bool need_update_dht = false;
		if (flags & session_handle::save_dht_settings)
		{
			settings = e->dict_find_dict("dht");
			if (settings)
			{
				static_cast<dht::dht_settings&>(m_dht_settings) = dht::read_dht_settings(settings);
			}
		}

		if (flags & session_handle::save_dht_state)
		{
			settings = e->dict_find_dict("dht state");
			if (settings)
			{
				m_dht_state = dht::read_dht_state(settings);
				need_update_dht = true;
			}
		}
#endif

#if TORRENT_ABI_VERSION == 1
		bool need_update_proxy = false;
		if (flags & session_handle::save_proxy)
		{
			settings = e->dict_find_dict("proxy");
			if (settings)
			{
				m_settings.bulk_set([&settings](session_settings_single_thread& s)
				{
					bdecode_node val;
					val = settings.dict_find_int("port");
					if (val) s.set_int(settings_pack::proxy_port, int(val.int_value()));
					val = settings.dict_find_int("type");
					if (val) s.set_int(settings_pack::proxy_type, int(val.int_value()));
					val = settings.dict_find_int("proxy_hostnames");
					if (val) s.set_bool(settings_pack::proxy_hostnames, val.int_value() != 0);
					val = settings.dict_find_int("proxy_peer_connections");
					if (val) s.set_bool(settings_pack::proxy_peer_connections, val.int_value() != 0);
					val = settings.dict_find_string("hostname");
					if (val) s.set_str(settings_pack::proxy_hostname, val.string_value().to_string());
					val = settings.dict_find_string("password");
					if (val) s.set_str(settings_pack::proxy_password, val.string_value().to_string());
					val = settings.dict_find_string("username");
					if (val) s.set_str(settings_pack::proxy_username, val.string_value().to_string());
				});
				need_update_proxy = true;
			}
		}

		settings = e->dict_find_dict("encryption");
		if (settings)
		{
			m_settings.bulk_set([&settings](session_settings_single_thread& s)
			{
				bdecode_node val;
				val = settings.dict_find_int("prefer_rc4");
				if (val) s.set_bool(settings_pack::prefer_rc4, val.int_value() != 0);
				val = settings.dict_find_int("out_enc_policy");
				if (val) s.set_int(settings_pack::out_enc_policy, int(val.int_value()));
				val = settings.dict_find_int("in_enc_policy");
				if (val) s.set_int(settings_pack::in_enc_policy, int(val.int_value()));
				val = settings.dict_find_int("allowed_enc_level");
				if (val) s.set_int(settings_pack::allowed_enc_level, int(val.int_value()));
			});
		}
#endif

		if (flags & session_handle::save_settings)
		{
			settings = e->dict_find_dict("settings");
			if (settings)
			{
				// apply_settings_pack will update dht and proxy
				settings_pack pack = load_pack_from_dict(settings);

				// these settings are not loaded from state
				// they are set by the client software, not configured by users
				pack.clear(settings_pack::user_agent);
				pack.clear(settings_pack::peer_fingerprint);

				apply_settings_pack_impl(pack);
#ifndef TORRENT_DISABLE_DHT
				need_update_dht = false;
#endif
#if TORRENT_ABI_VERSION == 1
				need_update_proxy = false;
#endif
			}
		}

#ifndef TORRENT_DISABLE_DHT
		if (need_update_dht) start_dht();
#endif
#if TORRENT_ABI_VERSION == 1
		if (need_update_proxy) update_proxy();
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_ses_extensions[plugins_all_idx])
		{
			ext->load_state(*e);
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
		// this is called during startup of the session, from the thread creating
		// it, not its own thread
//		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT_VAL(ext, ext);

		feature_flags_t const features = ext->implemented_features();

		m_ses_extensions[plugins_all_idx].push_back(ext);

		if (features & plugin::optimistic_unchoke_feature)
			m_ses_extensions[plugins_optimistic_unchoke_idx].push_back(ext);
		if (features & plugin::tick_feature)
			m_ses_extensions[plugins_tick_idx].push_back(ext);
		if (features & plugin::dht_request_feature)
			m_ses_extensions[plugins_dht_request_idx].push_back(ext);
		if (features & plugin::alert_feature)
			m_alerts.add_extension(ext);
		session_handle h(shared_from_this());
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

	void session_impl::abort() noexcept
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_abort) return;
#ifndef TORRENT_DISABLE_LOGGING
		session_log(" *** ABORT CALLED ***");
#endif

		// at this point we cannot call the notify function anymore, since the
		// session will become invalid.
		m_alerts.set_notify_function({});

		// this will cancel requests that are not critical for shutting down
		// cleanly. i.e. essentially tracker hostname lookups that we're not
		// about to send event=stopped to
		m_host_resolver.abort();

		m_close_file_timer.cancel();

		// abort the main thread
		m_abort = true;
		error_code ec;

#if TORRENT_USE_I2P
		m_i2p_conn.close(ec);
#endif
		stop_ip_notifier();
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
		for (auto const& te : m_torrents)
		{
			te.second->abort();
		}
		m_torrents.clear();
		m_stats_counters.set_value(counters::num_peers_up_unchoked_all, 0);
		m_stats_counters.set_value(counters::num_peers_up_unchoked, 0);
		m_stats_counters.set_value(counters::num_peers_up_unchoked_optimistic, 0);

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" aborting all tracker requests");
#endif
		m_tracker_manager.abort_all_requests();

#ifndef TORRENT_DISABLE_LOGGING
		session_log(" aborting all connections (%d)", int(m_connections.size()));
#endif
		// abort all connections
		for (auto i = m_connections.begin(); i != m_connections.end();)
		{
			peer_connection* p = (*i).get();
			++i;
			p->disconnect(errors::stopping_torrent, operation_t::bittorrent);
		}

		// close the listen sockets
		for (auto const& l : m_listen_sockets)
		{
			if (l->sock)
			{
				l->sock->close(ec);
				TORRENT_ASSERT(!ec);
			}

			// TODO: 3 closing the udp sockets here means that
			// the uTP connections cannot be closed gracefully
			if (l->udp_sock)
			{
				l->udp_sock->sock.close();
			}
		}

		// we need to give all the sockets an opportunity to actually have their handlers
		// called and cancelled before we continue the shutdown. This is a bit
		// complicated, if there are no "undead" peers, it's safe to resume the
		// shutdown, but if there are, we have to wait for them to be cleared out
		// first. In session_impl::on_tick() we check them periodically. If we're
		// shutting down and we remove the last one, we'll initiate
		// shutdown_stage2 from there.
		if (m_undead_peers.empty())
		{
			m_io_service.post(make_handler([this] { abort_stage2(); }
				, m_abort_handler_storage, *this));
		}
	}

	void session_impl::abort_stage2() noexcept
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

	void session_impl::insert_peer(std::shared_ptr<peer_connection> const& c)
	{
		TORRENT_ASSERT(!c->m_in_constructor);

		// removing a peer may not throw an exception, so prepare for this
		// connection to be added to the undead peers now.
		m_undead_peers.reserve(m_undead_peers.size() + m_connections.size() + 1);
		m_connections.insert(c);

		TORRENT_ASSERT_VAL(m_undead_peers.capacity() >= m_connections.size()
			, m_undead_peers.capacity());
	}

	void session_impl::set_port_filter(port_filter const& f)
	{
		m_port_filter = f;
		if (m_settings.get_bool(settings_pack::no_connect_privileged_ports))
			m_port_filter.add_rule(0, 1024, port_filter::blocked);
		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (auto const& t : m_torrents)
			t.second->port_filter_updated();
	}

	void session_impl::set_ip_filter(std::shared_ptr<ip_filter> const& f)
	{
		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (auto& i : m_torrents)
			i.second->set_ip_filter(m_ip_filter);
	}

	void session_impl::ban_ip(address addr)
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ip_filter) m_ip_filter = std::make_shared<ip_filter>();
		m_ip_filter->add_rule(addr, addr, ip_filter::blocked);
		for (auto& i : m_torrents)
			i.second->set_ip_filter(m_ip_filter);
	}

	ip_filter const& session_impl::get_ip_filter()
	{
		TORRENT_ASSERT(is_single_thread());
		if (!m_ip_filter) m_ip_filter = std::make_shared<ip_filter>();
		return *m_ip_filter;
	}

	port_filter const& session_impl::get_port_filter() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_port_filter;
	}

	peer_class_t session_impl::create_peer_class(char const* name)
	{
		TORRENT_ASSERT(is_single_thread());
		return m_classes.new_peer_class(name);
	}

	void session_impl::delete_peer_class(peer_class_t const cid)
	{
		TORRENT_ASSERT(is_single_thread());
		// if you hit this assert, you're deleting a non-existent peer class
		TORRENT_ASSERT_PRECOND(m_classes.at(cid));
		if (m_classes.at(cid) == nullptr) return;
		m_classes.decref(cid);
	}

	peer_class_info session_impl::get_peer_class(peer_class_t const cid) const
	{
		peer_class_info ret{};
		peer_class const* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT_PRECOND(pc);
		if (pc == nullptr)
		{
#if TORRENT_USE_INVARIANT_CHECKS
			// make it obvious that the return value is undefined
			ret.upload_limit = 0xf0f0f0f;
			ret.download_limit = 0xf0f0f0f;
			ret.label.resize(20);
			url_random(span<char>(ret.label));
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

namespace {

	std::uint16_t make_announce_port(std::uint16_t const p)
	{ return p == 0 ? 1 : p; }
}

	void session_impl::queue_tracker_request(tracker_request&& req
		, std::weak_ptr<request_callback> c)
	{
		req.listen_port = 0;
#if TORRENT_USE_I2P
		if (!m_settings.get_str(settings_pack::i2p_hostname).empty())
		{
			req.i2pconn = &m_i2p_conn;
		}
#endif

#ifdef TORRENT_USE_OPENSSL
		bool const use_ssl = req.ssl_ctx != nullptr && req.ssl_ctx != &m_ssl_ctx;
		if (!use_ssl) req.ssl_ctx = &m_ssl_ctx;
#endif

		if (req.outgoing_socket)
		{
			auto ls = req.outgoing_socket.get();

			req.listen_port =
#if TORRENT_USE_I2P
				(req.kind == tracker_request::i2p) ? 1 :
#endif
#ifdef TORRENT_USE_OPENSSL
			// SSL torrents use the SSL listen port
				use_ssl ? make_announce_port(ssl_listen_port(ls)) :
#endif
				make_announce_port(listen_port(ls));
			m_tracker_manager.queue_request(get_io_service(), std::move(req)
				, m_settings, c);
		}
		else
		{
			for (auto& ls : m_listen_sockets)
			{
				if (!(ls->flags & listen_socket_t::accept_incoming)) continue;
#ifdef TORRENT_USE_OPENSSL
				if ((ls->ssl == transport::ssl) != use_ssl) continue;
#endif
				tracker_request socket_req(req);
				socket_req.listen_port =
#if TORRENT_USE_I2P
					(req.kind == tracker_request::i2p) ? 1 :
#endif
#ifdef TORRENT_USE_OPENSSL
				// SSL torrents use the SSL listen port
					use_ssl ? make_announce_port(ssl_listen_port(ls.get())) :
#endif
					make_announce_port(listen_port(ls.get()));

				socket_req.outgoing_socket = ls;
				m_tracker_manager.queue_request(get_io_service()
					, std::move(socket_req), m_settings, c);
			}
		}
	}

	void session_impl::set_peer_class(peer_class_t const cid, peer_class_info const& pci)
	{
		peer_class* pc = m_classes.at(cid);
		// if you hit this assert, you're passing in an invalid cid
		TORRENT_ASSERT_PRECOND(pc);
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

	void session_impl::set_peer_classes(peer_class_set* s, address const& a, int const st)
	{
		std::uint32_t peer_class_mask = m_peer_class_filter.access(a);

		using sock_t = peer_class_type_filter::socket_type_t;
		// assign peer class based on socket type
		static const sock_t mapping[] = {
			sock_t::tcp_socket, sock_t::tcp_socket
			, sock_t::tcp_socket, sock_t::tcp_socket
			, sock_t::utp_socket, sock_t::i2p_socket
			, sock_t::ssl_tcp_socket, sock_t::ssl_tcp_socket
			, sock_t::ssl_tcp_socket, sock_t::ssl_utp_socket
		};
		sock_t const socket_type = mapping[st];
		// filter peer classes based on type
		peer_class_mask = m_peer_class_type_filter.apply(socket_type, peer_class_mask);

		for (peer_class_t i{0}; peer_class_mask; peer_class_mask >>= 1, ++i)
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

	void session_impl::deferred_submit_jobs()
	{
		if (m_deferred_submit_disk_jobs) return;
		m_deferred_submit_disk_jobs = true;
		m_io_service.post([this] { this->wrap(&session_impl::submit_disk_jobs); } );
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
		, int channel, bandwidth_channel** dst, int const max)
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

	int session_impl::use_quota_overhead(peer_class_set& set, int const amount_down, int const amount_up)
	{
		int ret = 0;
		int const num = set.num_classes();
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
	void session_impl::apply_settings_pack(std::shared_ptr<settings_pack> pack)
	{
		INVARIANT_CHECK;
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

	void session_impl::apply_settings_pack_impl(settings_pack const& pack)
	{
		bool const reopen_listen_port =
#if TORRENT_ABI_VERSION == 1
			(pack.has_val(settings_pack::ssl_listen)
				&& pack.get_int(settings_pack::ssl_listen)
					!= m_settings.get_int(settings_pack::ssl_listen))
			||
#endif
			(pack.has_val(settings_pack::listen_interfaces)
				&& pack.get_str(settings_pack::listen_interfaces)
					!= m_settings.get_str(settings_pack::listen_interfaces))
			|| (pack.has_val(settings_pack::proxy_type)
				&& pack.get_int(settings_pack::proxy_type)
					!= m_settings.get_int(settings_pack::proxy_type))
			;

#ifndef TORRENT_DISABLE_LOGGING
		session_log("applying settings pack, reopen_listen_port=%s"
			, reopen_listen_port ? "true" : "false");
#endif

		apply_pack(&pack, m_settings, this);
		m_disk_thread.settings_updated();

		if (!reopen_listen_port)
		{
			// no need to call this if reopen_listen_port is true
			// since the apply_pack will do it
			update_listen_interfaces();
		}

		if (reopen_listen_port)
		{
			reopen_listen_sockets();
		}
	}

	std::shared_ptr<listen_socket_t> session_impl::setup_listener(
		listen_endpoint_t const& lep, error_code& ec)
	{
		int retries = m_settings.get_int(settings_pack::max_retry_port_bind);
		tcp::endpoint bind_ep(lep.addr, std::uint16_t(lep.port));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log("attempting to open listen socket to: %s on device: %s %s%s%s%s%s"
				, print_endpoint(bind_ep).c_str(), lep.device.c_str()
				, (lep.ssl == transport::ssl) ? "ssl " : ""
				, (lep.flags & listen_socket_t::local_network) ? "local-network " : ""
				, (lep.flags & listen_socket_t::accept_incoming) ? "accept-incoming " : "no-incoming "
				, (lep.flags & listen_socket_t::was_expanded) ? "expanded-ip " : ""
				, (lep.flags & listen_socket_t::proxy) ? "proxy " : "");
		}
#endif

		auto ret = std::make_shared<listen_socket_t>();
		ret->ssl = lep.ssl;
		ret->original_port = bind_ep.port();
		ret->flags = lep.flags;
		ret->netmask = lep.netmask;
		operation_t last_op = operation_t::unknown;
		socket_type_t const sock_type
			= (lep.ssl == transport::ssl)
			? socket_type_t::tcp_ssl
			: socket_type_t::tcp;

		// if we're in force-proxy mode, don't open TCP listen sockets. We cannot
		// accept connections on our local machine in this case.
		// TODO: 3 the logic in this if-block should be factored out into a
		// separate function. At least most of it
		if (ret->flags & listen_socket_t::accept_incoming)
		{
			ret->sock = std::make_shared<tcp::acceptor>(m_io_service);
			ret->sock->open(bind_ep.protocol(), ec);
			last_op = operation_t::sock_open;
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("failed to open socket: %s"
						, ec.message().c_str());
				}
#endif

				if (m_alerts.should_post<listen_failed_alert>())
					m_alerts.emplace_alert<listen_failed_alert>(lep.device, bind_ep, last_op
						, ec, sock_type);
				return ret;
			}

#ifdef TORRENT_WINDOWS
			{
				// this is best-effort. ignore errors
				error_code err;
				ret->sock->set_option(exclusive_address_use(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err && should_log())
				{
					session_log("failed enable exclusive address use on listen socket: %s"
						, err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
			}
#else

			{
				// this is best-effort. ignore errors
				error_code err;
				ret->sock->set_option(tcp::acceptor::reuse_address(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err && should_log())
				{
					session_log("failed enable reuse-address on listen socket: %s"
						, err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
			}
#endif // TORRENT_WINDOWS

			if (is_v6(bind_ep))
			{
				error_code err; // ignore errors here
				ret->sock->set_option(boost::asio::ip::v6_only(true), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err && should_log())
				{
					session_log("failed enable v6 only on listen socket: %s"
						, err.message().c_str());
				}
#endif // LOGGING

#ifdef TORRENT_WINDOWS
				// enable Teredo on windows
				ret->sock->set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), err);
#ifndef TORRENT_DISABLE_LOGGING
				if (err && should_log())
				{
					session_log("failed enable IPv6 unrestricted protection level on "
						"listen socket: %s", err.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
#endif // TORRENT_WINDOWS
			}

			if (!lep.device.empty())
			{
				// we have an actual device we're interested in listening on, if we
				// have SO_BINDTODEVICE functionality, use it now.
#if TORRENT_HAS_BINDTODEVICE
				bind_device(*ret->sock, lep.device.c_str(), ec);
#ifndef TORRENT_DISABLE_LOGGING
				if (ec && should_log())
				{
					session_log("bind to device failed (device: %s): %s"
						, lep.device.c_str(), ec.message().c_str());
				}
#endif // TORRENT_DISABLE_LOGGING
				ec.clear();
#endif // TORRENT_HAS_BINDTODEVICE
			}

			ret->sock->bind(bind_ep, ec);
			last_op = operation_t::sock_bind;

			while (ec == error_code(error::address_in_use) && retries > 0)
			{
				TORRENT_ASSERT_VAL(ec, ec);
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("failed to bind listen socket to: %s on device: %s :"
						" [%s] (%d) %s (retries: %d)"
						, print_endpoint(bind_ep).c_str()
						, lep.device.c_str()
						, ec.category().name(), ec.value(), ec.message().c_str()
						, retries);
				}
#endif
				ec.clear();
				--retries;
				bind_ep.port(bind_ep.port() + 1);
				ret->sock->bind(bind_ep, ec);
			}

			if (ec == error_code(error::address_in_use)
				&& m_settings.get_bool(settings_pack::listen_system_port_fallback)
				&& bind_ep.port() != 0)
			{
				// instead of giving up, try let the OS pick a port
				bind_ep.port(0);
				ec.clear();
				ret->sock->bind(bind_ep, ec);
				last_op = operation_t::sock_bind;
			}

			if (ec)
			{
				// not even that worked, give up

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("failed to bind listen socket to: %s on device: %s :"
						" [%s] (%d) %s (giving up)"
						, print_endpoint(bind_ep).c_str()
						, lep.device.c_str()
						, ec.category().name(), ec.value(), ec.message().c_str());
				}
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(lep.device, bind_ep
						, last_op, ec, sock_type);
				}
				ret->sock.reset();
				return ret;
			}
			ret->local_endpoint = ret->sock->local_endpoint(ec);
			last_op = operation_t::getname;
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("get_sockname failed on listen socket: %s"
						, ec.message().c_str());
				}
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(lep.device, bind_ep
						, last_op, ec, sock_type);
				}
				return ret;
			}

			TORRENT_ASSERT(ret->local_endpoint.port() == bind_ep.port()
				|| bind_ep.port() == 0);

			if (bind_ep.port() == 0) bind_ep = ret->local_endpoint;

			ret->sock->listen(m_settings.get_int(settings_pack::listen_queue_size), ec);
			last_op = operation_t::sock_listen;

			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("cannot listen on interface \"%s\": %s"
						, lep.device.c_str(), ec.message().c_str());
				}
#endif
				if (m_alerts.should_post<listen_failed_alert>())
				{
					m_alerts.emplace_alert<listen_failed_alert>(lep.device, bind_ep
						, last_op, ec, sock_type);
				}
				return ret;
			}
		} // accept incoming

		socket_type_t const udp_sock_type
			= (lep.ssl == transport::ssl)
			? socket_type_t::utp_ssl
			: socket_type_t::udp;
		udp::endpoint udp_bind_ep(bind_ep.address(), bind_ep.port());

		ret->udp_sock = std::make_shared<session_udp_socket>(m_io_service, ret);
		ret->udp_sock->sock.open(udp_bind_ep.protocol(), ec);
		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("failed to open UDP socket: %s: %s"
					, lep.device.c_str(), ec.message().c_str());
			}
#endif

			last_op = operation_t::sock_open;
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.emplace_alert<listen_failed_alert>(lep.device
					, bind_ep, last_op, ec, udp_sock_type);

			return ret;
		}

#if TORRENT_HAS_BINDTODEVICE
		if (!lep.device.empty())
		{
			bind_device(ret->udp_sock->sock, lep.device.c_str(), ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (ec && should_log())
			{
				session_log("bind to device failed (device: %s): %s"
					, lep.device.c_str(), ec.message().c_str());
			}
#endif // TORRENT_DISABLE_LOGGING
			ec.clear();
		}
#endif
		ret->udp_sock->sock.bind(udp_bind_ep, ec);

		while (ec == error_code(error::address_in_use) && retries > 0)
		{
			TORRENT_ASSERT_VAL(ec, ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("failed to bind udp socket to: %s on device: %s :"
					" [%s] (%d) %s (retries: %d)"
					, print_endpoint(bind_ep).c_str()
					, lep.device.c_str()
					, ec.category().name(), ec.value(), ec.message().c_str()
					, retries);
			}
#endif
			ec.clear();
			--retries;
			udp_bind_ep.port(udp_bind_ep.port() + 1);
			ret->udp_sock->sock.bind(udp_bind_ep, ec);
		}

		if (ec == error_code(error::address_in_use)
			&& m_settings.get_bool(settings_pack::listen_system_port_fallback)
			&& udp_bind_ep.port() != 0)
		{
			// instead of giving up, try let the OS pick a port
			udp_bind_ep.port(0);
			ec.clear();
			ret->udp_sock->sock.bind(udp_bind_ep, ec);
		}

		last_op = operation_t::sock_bind;
		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("failed to bind UDP socket: %s: %s"
					, lep.device.c_str(), ec.message().c_str());
			}
#endif

			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.emplace_alert<listen_failed_alert>(lep.device
					, bind_ep, last_op, ec, udp_sock_type);

			return ret;
		}

		// if we did not open a TCP listen socket, ret->local_endpoint was never
		// initialized, so do that now, based on the UDP socket
		if (!(ret->flags & listen_socket_t::accept_incoming))
		{
			auto const udp_ep = ret->udp_sock->local_endpoint();
			ret->local_endpoint = tcp::endpoint(udp_ep.address(), udp_ep.port());
		}

		ret->device = lep.device;

		error_code err;
		set_socket_buffer_size(ret->udp_sock->sock, m_settings, err);
		if (err)
		{
			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.emplace_alert<udp_error_alert>(ret->udp_sock->sock.local_endpoint(ec)
					, operation_t::alloc_recvbuf, err);
		}

		// this call is necessary here because, unless the settings actually
		// change after the session is up and listening, at no other point
		// set_proxy_settings is called with the correct proxy configuration,
		// internally, this method handle the SOCKS5's connection logic
		ret->udp_sock->sock.set_proxy_settings(proxy(), m_alerts);

		ADD_OUTSTANDING_ASYNC("session_impl::on_udp_packet");
		ret->udp_sock->sock.async_read(aux::make_handler(std::bind(&session_impl::on_udp_packet
			, this, ret->udp_sock, ret, ret->ssl, _1)
			, ret->udp_handler_storage, *this));

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log(" listening on: %s TCP port: %d UDP port: %d"
				, bind_ep.address().to_string().c_str()
				, ret->tcp_external_port(), ret->udp_external_port());
		}
#endif
		return ret;
	}

	void session_impl::on_exception(std::exception const& e)
	{
		TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
		session_log("FATAL SESSION ERROR [%s]", e.what());
#endif
		this->abort();
	}

	void session_impl::on_error(error_code const& ec)
	{
		TORRENT_UNUSED(ec);
#ifndef TORRENT_DISABLE_LOGGING
		session_log("FATAL SESSION ERROR (%s : %d) [%s]"
			, ec.category().name(), ec.value(), ec.message().c_str());
#endif
		this->abort();
	}

	void session_impl::on_ip_change(error_code const& ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (!ec)
			session_log("received ip change from internal ip_notifier");
		else
			session_log("received error on_ip_change: %d, %s", ec.value(), ec.message().c_str());
#endif
		if (ec || m_abort || !m_ip_notifier) return;
		m_ip_notifier->async_wait([this] (error_code const& e)
			{ this->wrap(&session_impl::on_ip_change, e); });
		reopen_network_sockets({});
	}

	// TODO: could this function be merged with expand_unspecified_addresses?
	// right now both listen_endpoint_t and listen_interface_t are almost
	// identical, maybe the latter could be removed too
	void interface_to_endpoints(listen_interface_t const& iface
		, listen_socket_flags_t flags
		, span<ip_interface const> const ifs
		, std::vector<listen_endpoint_t>& eps)
	{
		flags |= iface.local ? listen_socket_t::local_network : listen_socket_flags_t{};
		transport const ssl = iface.ssl ? transport::ssl : transport::plaintext;

		// First, check to see if it's an IP address
		error_code err;
		address const adr = make_address(iface.device.c_str(), err);
		if (!err)
		{
			eps.emplace_back(adr, iface.port, std::string{}, ssl, flags);
		}
		else
		{
			flags |= listen_socket_t::was_expanded;

			// this is the case where device names a network device. We need to
			// enumerate all IPs associated with this device
			for (auto const& ipface : ifs)
			{
				// we're looking for a specific interface, and its address
				// (which must be of the same family as the address we're
				// connecting to)
				if (iface.device != ipface.name) continue;

				// record whether the device has a gateway associated with it
				// (which indicates it can be used to reach the internet)
				// if the IP address tell us it's loopback or link-local, don't
				// bother looking for the gateway
				bool const local = iface.local
					|| ipface.interface_address.is_loopback()
					|| is_link_local(ipface.interface_address);

				eps.emplace_back(ipface.interface_address, iface.port, iface.device
					, ssl, flags | (local ? listen_socket_t::local_network : listen_socket_flags_t{}));
			}
		}
	}

	void session_impl::reopen_listen_sockets(bool const map_ports)
	{
#ifndef TORRENT_DISABLE_LOGGING
		session_log("reopen listen sockets");
#endif

		TORRENT_ASSERT(is_single_thread());

		TORRENT_ASSERT(!m_abort);

		error_code ec;

		if (m_abort) return;

		// first build a list of endpoints we should be listening on
		// we need to remove any unneeded sockets first to avoid the possibility
		// of a new socket failing to bind due to a conflict with a stale socket
		std::vector<listen_endpoint_t> eps;

		if (m_settings.get_int(settings_pack::proxy_type) != settings_pack::none)
		{
			// we will be able to accept incoming connections over UDP. so use
			// one of the ports the user specified to use a consistent port
			// across sessions. If the user did not specify any ports, pick one
			// at random
			int const port = m_listen_interfaces.empty()
				? int(random(63000) + 2000)
				: m_listen_interfaces.front().port;
			listen_endpoint_t ep(address_v4::any(), port, {}
				, transport::plaintext, listen_socket_t::proxy);
			eps.emplace_back(ep);
		}
		else
		{

			listen_socket_flags_t const flags
				= (m_settings.get_int(settings_pack::proxy_type) != settings_pack::none)
				? listen_socket_flags_t{}
				: listen_socket_t::accept_incoming;

			std::vector<ip_interface> const ifs = enum_net_interfaces(m_io_service, ec);
			if (ec && m_alerts.should_post<listen_failed_alert>())
			{
				m_alerts.emplace_alert<listen_failed_alert>(""
					, operation_t::enum_if, ec, socket_type_t::tcp);
			}
			auto const routes = enum_routes(m_io_service, ec);
			if (ec && m_alerts.should_post<listen_failed_alert>())
			{
				m_alerts.emplace_alert<listen_failed_alert>(""
					, operation_t::enum_route, ec, socket_type_t::tcp);
			}

			// expand device names and populate eps
			for (auto const& iface : m_listen_interfaces)
			{
#ifndef TORRENT_USE_OPENSSL
				if (iface.ssl)
				{
#ifndef TORRENT_DISABLE_LOGGING
					session_log("attempted to listen ssl with no library support on device: \"%s\""
						, iface.device.c_str());
#endif
					if (m_alerts.should_post<listen_failed_alert>())
					{
						m_alerts.emplace_alert<listen_failed_alert>(iface.device
							, operation_t::sock_open
							, boost::asio::error::operation_not_supported
							, socket_type_t::tcp_ssl);
					}
					continue;
				}
#endif

				// now we have a device to bind to. This device may actually just be an
				// IP address or a device name. In case it's a device name, we want to
				// (potentially) end up binding a socket for each IP address associated
				// with that device.
				interface_to_endpoints(iface, flags, ifs, eps);
			}

			if (eps.empty())
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("no listen sockets");
#endif
			}

			expand_unspecified_address(ifs, routes, eps);
			expand_devices(ifs, eps);
		}

		auto remove_iter = partition_listen_sockets(eps, m_listen_sockets);

		while (remove_iter != m_listen_sockets.end())
		{
#ifndef TORRENT_DISABLE_DHT
			if (m_dht)
				m_dht->delete_socket(*remove_iter);
#endif

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("closing listen socket for %s on device \"%s\""
					, print_endpoint((*remove_iter)->local_endpoint).c_str()
					, (*remove_iter)->device.c_str());
			}
#endif
			if ((*remove_iter)->sock) (*remove_iter)->sock->close(ec);
			if ((*remove_iter)->udp_sock) (*remove_iter)->udp_sock->sock.close();
			if ((*remove_iter)->natpmp_mapper) (*remove_iter)->natpmp_mapper->close();
			if ((*remove_iter)->upnp_mapper) (*remove_iter)->upnp_mapper->close();
			if ((*remove_iter)->lsd) (*remove_iter)->lsd->close();
			remove_iter = m_listen_sockets.erase(remove_iter);
		}

		// all sockets in there stayed the same. Only sockets after this point are
		// new and should post alerts
		int const existing_sockets = int(m_listen_sockets.size());

		m_stats_counters.set_value(counters::has_incoming_connections
			, std::any_of(m_listen_sockets.begin(), m_listen_sockets.end()
				, [](std::shared_ptr<listen_socket_t> const& l)
				{ return l->incoming_connection; }));

		// open new sockets on any endpoints that didn't match with
		// an existing socket
		for (auto const& ep : eps)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try
#endif
		{
			std::shared_ptr<listen_socket_t> s = setup_listener(ep, ec);

			if (!ec && (s->sock || s->udp_sock))
			{
				m_listen_sockets.emplace_back(s);

#ifndef TORRENT_DISABLE_DHT
				if (m_dht
					&& s->ssl != transport::ssl
					&& !(s->flags & listen_socket_t::local_network))
				{
					m_dht->new_socket(m_listen_sockets.back());
				}
#endif

				TORRENT_ASSERT(bool(s->flags & listen_socket_t::accept_incoming) == bool(s->sock));
				if (s->sock) async_accept(s->sock, s->ssl);
			}
		}
#ifndef BOOST_NO_EXCEPTIONS
		catch (std::exception const& e)
		{
			TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("setup_listener(%s) device: %s failed: %s"
					, print_endpoint(ep.addr, ep.port).c_str()
					, ep.device.c_str()
					, e.what());
			}
#endif // TORRENT_DISABLE_LOGGING
		}
#endif // BOOST_NO_EXCEPTIONS
		}

		if (m_listen_sockets.empty())
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("giving up on binding listen sockets");
#endif
			return;
		}

		auto const new_sockets = span<std::shared_ptr<listen_socket_t>>(
			m_listen_sockets).subspan(existing_sockets);

		// now, send out listen_succeeded_alert for the listen sockets we are
		// listening on
		if (m_alerts.should_post<listen_succeeded_alert>())
		{
			for (auto const& l : new_sockets)
			{
				error_code err;
				if (l->sock)
				{
					tcp::endpoint const tcp_ep = l->sock->local_endpoint(err);
					if (!err)
					{
						socket_type_t const socket_type
							= l->ssl == transport::ssl
							? socket_type_t::tcp_ssl
							: socket_type_t::tcp;

						m_alerts.emplace_alert<listen_succeeded_alert>(
							tcp_ep, socket_type);
					}
				}

				if (l->udp_sock)
				{
					udp::endpoint const udp_ep = l->udp_sock->sock.local_endpoint(err);
					if (!err && l->udp_sock->sock.is_open())
					{
						socket_type_t const socket_type
							= l->ssl == transport::ssl
							? socket_type_t::utp_ssl
							: socket_type_t::udp;

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

		if (m_settings.get_bool(settings_pack::enable_natpmp))
		{
			for (auto const& s : new_sockets)
				start_natpmp(*s);
		}

		if (m_settings.get_bool(settings_pack::enable_upnp))
		{
			for (auto const& s : new_sockets)
				start_upnp(*s);
		}

		if (map_ports)
		{
			for (auto const& s : m_listen_sockets)
				remap_ports(remap_natpmp_and_upnp, *s);
		}
		else
		{
			// new sockets need to map ports even if the caller did not request
			// re-mapping
			for (auto const& s : new_sockets)
				remap_ports(remap_natpmp_and_upnp, *s);
		}

		update_lsd();

#if TORRENT_USE_I2P
		open_new_incoming_i2p_connection();
#endif

		// trackers that were not reachable, may have become reachable now.
		// so clear the "disabled" flags to let them be tried one more time
		// TODO: it would probably be better to do this by having a
		// listen-socket "version" number that gets bumped. And instead of
		// setting a bool to disable a tracker, we set the version number that
		// it was disabled at. This change would affect the ABI in 1.2, so
		// should be done in 2.0 or later
		for (auto& t : m_torrents)
			t.second->enable_all_trackers();
	}

	void session_impl::reopen_network_sockets(reopen_network_flags_t const options)
	{
		reopen_listen_sockets(bool(options & session_handle::reopen_map_ports));
	}

	namespace {
		template <typename MapProtocol, typename ProtoType, typename EndpointType>
		void map_port(MapProtocol& m, ProtoType protocol, EndpointType const& ep
			, port_mapping_t& map_handle)
		{
			if (map_handle != port_mapping_t{-1}) m.delete_mapping(map_handle);
			map_handle = port_mapping_t{-1};

			address const addr = ep.address();
			// with IPv4 the interface might be behind NAT so we can't skip them
			// based on the scope of the local address
			if (addr.is_v6() && is_local(addr))
				return;

			// only update this mapping if we actually have a socket listening
			if (ep != EndpointType())
				map_handle = m.add_mapping(protocol, ep.port(), ep);
		}
	}

	void session_impl::remap_ports(remap_port_mask_t const mask
		, listen_socket_t& s)
	{
		tcp::endpoint const tcp_ep = s.sock ? s.sock->local_endpoint() : tcp::endpoint();
		udp::endpoint const udp_ep = s.udp_sock ? s.udp_sock->sock.local_endpoint() : udp::endpoint();

		if ((mask & remap_natpmp) && s.natpmp_mapper)
		{
			map_port(*s.natpmp_mapper, portmap_protocol::tcp, tcp_ep
				, s.tcp_port_mapping[portmap_transport::natpmp].mapping);
			map_port(*s.natpmp_mapper, portmap_protocol::udp, make_tcp(udp_ep)
				, s.udp_port_mapping[portmap_transport::natpmp].mapping);
		}
		if ((mask & remap_upnp) && s.upnp_mapper)
		{
			map_port(*s.upnp_mapper, portmap_protocol::tcp, tcp_ep
				, s.tcp_port_mapping[portmap_transport::upnp].mapping);
			map_port(*s.upnp_mapper, portmap_protocol::udp, make_tcp(udp_ep)
				, s.udp_port_mapping[portmap_transport::upnp].mapping);
		}
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

#ifndef TORRENT_DISABLE_DHT
	int session_impl::external_udp_port(address const& local_address) const
	{
		auto ls = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, [&](std::shared_ptr<listen_socket_t> const& e)
		{
			return e->local_endpoint.address() == local_address;
		});

		if (ls != m_listen_sockets.end())
			return (*ls)->udp_external_port();
		else
			return -1;
	}
#endif

#if TORRENT_USE_I2P

	proxy_settings session_impl::i2p_proxy() const
	{
		proxy_settings ret;

		ret.hostname = m_settings.get_str(settings_pack::i2p_hostname);
		ret.type = settings_pack::i2p_proxy;
		ret.port = std::uint16_t(m_settings.get_int(settings_pack::i2p_port));
		return ret;
	}

	void session_impl::on_i2p_open(error_code const& ec)
	{
		if (ec)
		{
			if (m_alerts.should_post<i2p_alert>())
				m_alerts.emplace_alert<i2p_alert>(ec);

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
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

		m_i2p_listen_socket = std::make_shared<socket_type>(m_io_service);
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
					, operation_t::sock_accept
					, e, socket_type_t::i2p);
			}
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
				session_log("i2p SAM connection failure: %s", e.message().c_str());
#endif
			return;
		}
		open_new_incoming_i2p_connection();
		incoming_connection(s);
	}
#endif

	void session_impl::send_udp_packet_hostname(std::weak_ptr<utp_socket_interface> sock
		, char const* hostname
		, int const port
		, span<char const> p
		, error_code& ec
		, udp_send_flags_t const flags)
	{
		auto si = sock.lock();
		if (!si)
		{
			ec = boost::asio::error::bad_descriptor;
			return;
		}

		auto s = std::static_pointer_cast<aux::listen_socket_t>(si)->udp_sock;

		s->sock.send_hostname(hostname, port, p, ec, flags);

		if ((ec == error::would_block || ec == error::try_again)
			&& !s->write_blocked)
		{
			s->write_blocked = true;
			ADD_OUTSTANDING_ASYNC("session_impl::on_udp_writeable");
			s->sock.async_write(std::bind(&session_impl::on_udp_writeable
				, this, s, _1));
		}
	}

	void session_impl::send_udp_packet(std::weak_ptr<utp_socket_interface> sock
		, udp::endpoint const& ep
		, span<char const> p
		, error_code& ec
		, udp_send_flags_t const flags)
	{
		auto si = sock.lock();
		if (!si)
		{
			ec = boost::asio::error::bad_descriptor;
			return;
		}

		auto s = std::static_pointer_cast<aux::listen_socket_t>(si)->udp_sock;

		TORRENT_ASSERT(s->sock.is_closed() || s->sock.local_endpoint().protocol() == ep.protocol());

		s->sock.send(ep, p, ec, flags);

		if ((ec == error::would_block || ec == error::try_again) && !s->write_blocked)
		{
			s->write_blocked = true;
			ADD_OUTSTANDING_ASYNC("session_impl::on_udp_writeable");
			s->sock.async_write(std::bind(&session_impl::on_udp_writeable
				, this, s, _1));
		}
	}

	void session_impl::on_udp_writeable(std::weak_ptr<session_udp_socket> sock, error_code const& ec)
	{
		COMPLETE_ASYNC("session_impl::on_udp_writeable");
		if (ec) return;

		auto s = sock.lock();
		if (!s) return;

		s->write_blocked = false;

#ifdef TORRENT_USE_OPENSSL
		auto i = std::find_if(
			m_listen_sockets.begin(), m_listen_sockets.end()
			, [&s] (std::shared_ptr<listen_socket_t> const& ls) { return ls->udp_sock == s; });
#endif

		// notify the utp socket manager it can start sending on the socket again
		struct utp_socket_manager& mgr =
#ifdef TORRENT_USE_OPENSSL
			(i != m_listen_sockets.end() && (*i)->ssl == transport::ssl) ? m_ssl_utp_socket_manager :
#endif
			m_utp_socket_manager;

		mgr.writable();
	}


	void session_impl::on_udp_packet(std::weak_ptr<session_udp_socket> socket
		, std::weak_ptr<listen_socket_t> ls, transport const ssl, error_code const& ec)
	{
		COMPLETE_ASYNC("session_impl::on_udp_packet");
		if (ec)
		{
			std::shared_ptr<session_udp_socket> s = socket.lock();
			udp::endpoint ep;
			if (s) ep = s->local_endpoint();

			// don't bubble up operation aborted errors to the user
			if (ec != boost::asio::error::operation_aborted
				&& ec != boost::asio::error::bad_descriptor
				&& m_alerts.should_post<udp_error_alert>())
			{
				m_alerts.emplace_alert<udp_error_alert>(ep
					, operation_t::sock_read, ec);
			}

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("UDP error: %s (%d) %s"
					, print_endpoint(ep).c_str(), ec.value(), ec.message().c_str());
			}
#endif
			return;
		}

		m_stats_counters.inc_stats_counter(counters::on_udp_counter);

		std::shared_ptr<session_udp_socket> s = socket.lock();
		if (!s) return;

		struct utp_socket_manager& mgr =
#ifdef TORRENT_USE_OPENSSL
			ssl == transport::ssl ? m_ssl_utp_socket_manager :
#endif
			m_utp_socket_manager;

		auto listen_socket = ls.lock();
		if (listen_socket)
			listen_socket->incoming_connection = true;

		for (;;)
		{
			aux::array<udp_socket::packet, 50> p;
			error_code err;
			int const num_packets = s->sock.read(p, err);

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

				// give the uTP socket manager first dibs on the packet. Presumably
				// the majority of packets are uTP packets.
				if (!mgr.incoming_packet(ls, packet.from, buf))
				{
					// if it wasn't a uTP packet, try the other users of the UDP
					// socket
					bool handled = false;
#ifndef TORRENT_DISABLE_DHT
					if (m_dht && buf.size() > 20
						&& buf.front() == 'd'
						&& buf.back() == 'e'
						&& listen_socket)
					{
						handled = m_dht->incoming_packet(listen_socket, packet.from, buf);
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
				udp::endpoint const ep = s->local_endpoint();

				if (err != boost::asio::error::operation_aborted
					&& m_alerts.should_post<udp_error_alert>())
					m_alerts.emplace_alert<udp_error_alert>(ep
						, operation_t::sock_read, err);

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("UDP error: %s (%d) %s"
						, print_endpoint(ep).c_str(), ec.value(), ec.message().c_str());
				}
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
		s->sock.async_read(make_handler(std::bind(&session_impl::on_udp_packet
			, this, std::move(socket), std::move(ls), ssl, _1), s->udp_handler_storage
				, *this));
	}

	void session_impl::async_accept(std::shared_ptr<tcp::acceptor> const& listener
		, transport const ssl)
	{
		TORRENT_ASSERT(!m_abort);
		std::shared_ptr<socket_type> c = std::make_shared<socket_type>(m_io_service);
		tcp::socket* str = nullptr;

#ifdef TORRENT_USE_OPENSSL
		if (ssl == transport::ssl)
		{
			// accept connections initializing the SSL connection to use the peer
			// ssl context. Since it has the servername callback set on it, we will
			// switch away from this context into a specific torrent once we start
			// handshaking
			c->instantiate<ssl_stream<tcp::socket>>(m_io_service, &m_peer_ssl_ctx);
			str = &c->get<ssl_stream<tcp::socket>>()->next_layer();
		}
		else
#endif
		{
			c->instantiate<tcp::socket>(m_io_service);
			str = c->get<tcp::socket>();
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_accept_connection");

#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASSERT((ssl == transport::ssl) == is_ssl(*c));
#endif

		std::weak_ptr<tcp::acceptor> ls(listener);
		m_stats_counters.inc_stats_counter(counters::num_outstanding_accept);
		listener->async_accept(*str, [this, c, ls, ssl] (error_code const& ec)
			{ return this->wrap(&session_impl::on_accept_connection, c, ls, ec, ssl); });
	}

	void session_impl::on_accept_connection(std::shared_ptr<socket_type> const& s
		, std::weak_ptr<tcp::acceptor> listen_socket, error_code const& e
		, transport const ssl)
	{
		COMPLETE_ASYNC("session_impl::on_accept_connection");
		m_stats_counters.inc_stats_counter(counters::on_accept_counter);
		m_stats_counters.inc_stats_counter(counters::num_outstanding_accept, -1);

		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<tcp::acceptor> listener = listen_socket.lock();
		if (!listener) return;

		if (e == boost::asio::error::operation_aborted) return;

		if (m_abort) return;

		error_code ec;
		if (e)
		{
			tcp::endpoint const ep = listener->local_endpoint(ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("error accepting connection on '%s': %s"
					, print_endpoint(ep).c_str(), e.message().c_str());
			}
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
				// elsewhere.
				if (m_settings.get_int(settings_pack::connections_limit) > 10)
				{
					// now, disconnect a random peer
					auto const i = std::max_element(m_torrents.begin(), m_torrents.end()
						, [](torrent_map::value_type const& lhs, torrent_map::value_type const& rhs)
						{ return lhs.second->num_peers() < rhs.second->num_peers(); });

					if (m_alerts.should_post<performance_alert>())
						m_alerts.emplace_alert<performance_alert>(
							torrent_handle(), performance_alert::too_few_file_descriptors);

					if (i != m_torrents.end())
					{
						i->second->disconnect_peers(1, e);
					}

					m_settings.set_int(settings_pack::connections_limit
						, std::max(10, int(m_connections.size())));
				}
				// try again, but still alert the user of the problem
				async_accept(listener, ssl);
			}
			if (m_alerts.should_post<listen_failed_alert>())
			{
				error_code err;
				m_alerts.emplace_alert<listen_failed_alert>(ep.address().to_string(err)
					, ep, operation_t::sock_accept, e
					, ssl == transport::ssl ? socket_type_t::tcp_ssl : socket_type_t::tcp);
			}
			return;
		}
		async_accept(listener, ssl);

		// don't accept any connections from our local sockets if we're using a
		// proxy
		if (m_settings.get_int(settings_pack::proxy_type) != settings_pack::none)
			return;

		auto listen = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, [&listener](std::shared_ptr<listen_socket_t> const& l)
		{ return l->sock == listener; });
		if (listen != m_listen_sockets.end())
			(*listen)->incoming_connection = true;

#ifdef TORRENT_USE_OPENSSL
		if (ssl == transport::ssl)
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
		if (should_log())
		{
			session_log(" *** peer SSL handshake done [ ip: %s ec: %s socket: %s ]"
				, print_endpoint(endp).c_str(), ec.message().c_str(), s->type_name());
		}
#endif

		if (ec)
		{
			if (m_alerts.should_post<peer_error_alert>())
			{
				m_alerts.emplace_alert<peer_error_alert>(torrent_handle(), endp
					, peer_id(), operation_t::ssl_handshake, ec);
			}
			return;
		}

		incoming_connection(s);
	}

#endif // TORRENT_USE_OPENSSL

	void session_impl::incoming_connection(std::shared_ptr<socket_type> const& s)
	{
		TORRENT_ASSERT(is_single_thread());

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
			if (should_log())
			{
				session_log(" <== INCOMING CONNECTION [ rejected, could "
					"not retrieve remote endpoint: %s ]"
					, print_error(ec).c_str());
			}
#endif
			return;
		}

		if (!m_settings.get_bool(settings_pack::enable_incoming_utp)
			&& is_utp(*s))
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("<== INCOMING CONNECTION [ rejected uTP connection ]");
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
			session_log("<== INCOMING CONNECTION [ rejected TCP connection ]");
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
					, endp, peer_blocked_alert::tcp_disabled);
			return;
		}

		// if there are outgoing interfaces specified, verify this
		// peer is correctly bound to one of them
		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
		{
			tcp::endpoint local = s->local_endpoint(ec);
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log("<== INCOMING CONNECTION [ rejected connection: %s ]"
						, print_error(ec).c_str());
				}
#endif
				return;
			}

			if (!verify_incoming_interface(local.address()))
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					error_code err;
					session_log("<== INCOMING CONNECTION [ rejected, local interface has incoming connections disabled: %s ]"
						, local.address().to_string(err).c_str());
				}
#endif
				if (m_alerts.should_post<peer_blocked_alert>())
					m_alerts.emplace_alert<peer_blocked_alert>(torrent_handle()
						, endp, peer_blocked_alert::invalid_local_interface);
				return;
			}
			if (!verify_bound_address(local.address(), is_utp(*s), ec))
			{
				if (ec)
				{
#ifndef TORRENT_DISABLE_LOGGING
					if (should_log())
					{
						session_log("<== INCOMING CONNECTION [ rejected, not allowed local interface: %s ]"
							, print_error(ec).c_str());
					}
#endif
					return;
				}

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					error_code err;
					session_log("<== INCOMING CONNECTION [ rejected, not allowed local interface: %s ]"
						, local.address().to_string(err).c_str());
				}
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
			session_log("<== INCOMING CONNECTION [ filtered blocked ip ]");
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
			session_log("<== INCOMING CONNECTION [ rejected, there are no torrents ]");
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
			peer_class_t pc = pcs.class_at(i);
			if (m_classes.at(pc) == nullptr) continue;
			int f = m_classes.at(pc)->connection_limit_factor;
			if (connection_limit_factor < f) connection_limit_factor = f;
		}
		if (connection_limit_factor == 0) connection_limit_factor = 100;

		std::int64_t limit = m_settings.get_int(settings_pack::connections_limit);
		limit = limit * 100 / connection_limit_factor;

		// don't allow more connections than the max setting
		// weighed by the peer class' setting
		bool reject = num_connections() >= limit + m_settings.get_int(settings_pack::connections_slack);

		if (reject)
		{
			if (m_alerts.should_post<peer_disconnected_alert>())
			{
				m_alerts.emplace_alert<peer_disconnected_alert>(torrent_handle(), endp, peer_id()
						, operation_t::bittorrent, s->type()
						, error_code(errors::too_many_connections)
						, close_reason_t::none);
			}
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("<== INCOMING CONNECTION [ connections limit exceeded, conns: %d, limit: %d, slack: %d ]"
					, num_connections(), m_settings.get_int(settings_pack::connections_limit)
					, m_settings.get_int(settings_pack::connections_slack));
			}
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
			bool has_active_torrent = std::any_of(m_torrents.begin(), m_torrents.end()
				, [](std::pair<sha1_hash, std::shared_ptr<torrent>> const& i)
				{ return !i.second->is_torrent_paused(); });
			if (!has_active_torrent)
			{
#ifndef TORRENT_DISABLE_LOGGING
				session_log("<== INCOMING CONNECTION [ rejected, no active torrents ]");
#endif
				return;
			}
		}

		m_stats_counters.inc_stats_counter(counters::incoming_connections);

		if (m_alerts.should_post<incoming_connection_alert>())
			m_alerts.emplace_alert<incoming_connection_alert>(s->type(), endp);

		peer_connection_args pack{
			this
			, &m_settings
			, &m_stats_counters
			, &m_disk_thread
			, &m_io_service
			, std::weak_ptr<torrent>()
			, s
			, endp
			, nullptr
			, aux::generate_peer_id(m_settings)
		};

		std::shared_ptr<peer_connection> c
			= std::make_shared<bt_peer_connection>(std::move(pack));

		if (!c->is_disconnecting())
		{
			// in case we've exceeded the limit, let this peer know that
			// as soon as it's received the handshake, it needs to either
			// disconnect or pick another peer to disconnect
			if (num_connections() >= limit)
				c->peer_exceeds_limit();

			TORRENT_ASSERT(!c->m_in_constructor);
			// removing a peer may not throw an exception, so prepare for this
			// connection to be added to the undead peers now.
			m_undead_peers.reserve(m_undead_peers.size() + m_connections.size() + 1);
			m_connections.insert(c);
			c->start();
		}
	}

	void session_impl::close_connection(peer_connection* p) noexcept
	{
		TORRENT_ASSERT(is_single_thread());
		std::shared_ptr<peer_connection> sp(p->self());

		TORRENT_ASSERT(p->is_disconnecting());

		auto const i = m_connections.find(sp);
		// make sure the next disk peer round-robin cursor stays valid
		if (i != m_connections.end())
		{
			m_connections.erase(i);

			TORRENT_ASSERT(std::find(m_undead_peers.begin()
				, m_undead_peers.end(), sp) == m_undead_peers.end());

			// someone else is holding a reference, it's important that
			// it's destructed from the network thread. Make sure the
			// last reference is held by the network thread.
			TORRENT_ASSERT_VAL(m_undead_peers.capacity() > m_undead_peers.size()
				, m_undead_peers.capacity());
			if (sp.use_count() > 2)
				m_undead_peers.push_back(sp);
		}
	}

#if TORRENT_ABI_VERSION == 1
	peer_id session_impl::deprecated_get_peer_id() const
	{
		return aux::generate_peer_id(m_settings);
	}
#endif

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
		else limit = std::min(limit, std::numeric_limits<int>::max() - 1);
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
			, [p] (std::shared_ptr<peer_connection> const& pr)
			{ return pr.get() == p; });
	}

	bool session_impl::any_torrent_has_peer(peer_connection const* p) const
	{
		for (auto& pe : m_torrents)
			if (pe.second->has_peer(p)) return true;
		return false;
	}

	bool session_impl::verify_queue_position(torrent const* t, queue_position_t const pos)
	{
		return m_download_queue.end_index() > pos && m_download_queue[pos] == t;
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
		// one TCP/IP packet header for the packet
		// sent or received, and one for the ACK
		// The IPv4 header is 20 bytes
		// and IPv6 header is 40 bytes
		int const header = (ipv6 ? 40 : 20) + 20;
		int const mtu = 1500;
		int const packet_size = mtu - header;
		int const overhead = std::max(1, (bytes + packet_size - 1) / packet_size) * header;
		m_stats_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, overhead);
		m_stats_counters.inc_stats_counter(counters::recv_ip_overhead_bytes
			, overhead);

		m_stat.trancieve_ip_packet(bytes, ipv6);
	}

	void session_impl::sent_syn(bool ipv6)
	{
		int const overhead = ipv6 ? 60 : 40;
		m_stats_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, overhead);

		m_stat.sent_syn(ipv6);
	}

	void session_impl::received_synack(bool ipv6)
	{
		int const overhead = ipv6 ? 60 : 40;
		m_stats_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, overhead);
		m_stats_counters.inc_stats_counter(counters::recv_ip_overhead_bytes
			, overhead);

		m_stat.received_synack(ipv6);
	}

	void session_impl::on_tick(error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_tick");
		m_stats_counters.inc_stats_counter(counters::on_tick_counter);

		TORRENT_ASSERT(is_single_thread());

		// submit all disk jobs when we leave this function
		deferred_submit_jobs();

		time_point const now = aux::time_now();

		// remove undead peers that only have this list as their reference keeping them alive
		if (!m_undead_peers.empty())
		{
			auto const remove_it = std::remove_if(m_undead_peers.begin(), m_undead_peers.end()
				, [](std::shared_ptr<peer_connection>& ptr) { return ptr.use_count() == 1; });
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
		// we also have to keep updating the aux time while
		// there are outstanding announces
		if (m_abort)
		{
			if (m_utp_socket_manager.num_sockets() == 0
#ifdef TORRENT_USE_OPENSSL
				&& m_ssl_utp_socket_manager.num_sockets() == 0
#endif
				&& m_undead_peers.empty()
				&& m_tracker_manager.empty())
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
			if (should_log())
				session_log("*** TICK TIMER FAILED %s", e.message().c_str());
#endif
			std::abort();
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_tick");
		error_code ec;
		m_timer.expires_at(now + milliseconds(m_settings.get_int(settings_pack::tick_interval)), ec);
		m_timer.async_wait(aux::make_handler([this](error_code const& err)
		{ this->wrap(&session_impl::on_tick, err); }, m_tick_handler_storage, *this));

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

		m_utp_socket_manager.decay();
#ifdef TORRENT_USE_OPENSSL
		m_ssl_utp_socket_manager.decay();
#endif

		int const tick_interval_ms = aux::numeric_cast<int>(total_milliseconds(now - m_last_second_tick));
		m_last_second_tick = now;

		std::int32_t const stime = session_time();
		if (stime > 65000)
		{
			// we're getting close to the point where our timestamps
			// in torrent_peer are wrapping. We need to step all counters back
			// four hours. This means that any timestamp that refers to a time
			// more than 18.2 - 4 = 14.2 hours ago, will be incremented to refer to
			// 14.2 hours ago.

			m_created += hours(4);

			constexpr int four_hours = 60 * 60 * 4;
			for (auto& i : m_torrents)
			{
				i.second->step_session_time(four_hours);
			}
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : m_ses_extensions[plugins_tick_idx])
		{
			ext->on_tick();
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
					for (auto const& i : m_connections)
					{
						peer_connection& p = *i;
						if (p.in_handshake()) continue;
						int protocol = 0;
						if (is_utp(*p.get_socket())) protocol = 1;

						if (p.download_queue().size() + p.request_queue().size() > 0)
							++num_peers[protocol][peer_connection::download_channel];
						if (!p.upload_queue().empty())
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
							std::int64_t rate = stat_rate[i];
							tcp_channel[i].throttle(std::max(int(rate * num_peers[0][i] / total_peers), lower_limit[i]));
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
			m_auto_manage_time_scaler = settings().get_int(settings_pack::auto_manage_interval);
			recalculate_auto_managed_torrents();
		}

		// --------------------------------------------------------------
		// check for incoming connections that might have timed out
		// --------------------------------------------------------------

		for (auto i = m_connections.begin(); i != m_connections.end();)
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
				p->disconnect(errors::timed_out, operation_t::bittorrent);
		}

		// --------------------------------------------------------------
		// second_tick every torrent (that wants it)
		// --------------------------------------------------------------

#if TORRENT_DEBUG_STREAMING > 0
		std::printf("\033[2J\033[0;0H");
#endif

		aux::vector<torrent*>& want_tick = m_torrent_lists[torrent_want_tick];
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
			int const up_limit = upload_rate_limit(m_global_class);
			int const down_limit = download_rate_limit(m_global_class);

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

#if TORRENT_ABI_VERSION == 1
		m_peak_up_rate = std::max(m_stat.upload_rate(), m_peak_up_rate);
#endif

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
				aux::vector<torrent*>& want_scrape = m_torrent_lists[torrent_want_scrape];
				m_auto_scrape_time_scaler = m_settings.get_int(settings_pack::auto_scrape_interval)
					/ std::max(1, int(want_scrape.size()));
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
					auto const i = std::max_element(m_torrents.begin(), m_torrents.end()
						, [] (torrent_map::value_type const& lhs, torrent_map::value_type const& rhs)
						{ return lhs.second->num_peers() < rhs.second->num_peers(); });

					TORRENT_ASSERT(i != m_torrents.end());
					int const peers_to_disconnect = std::min(std::max(
						i->second->num_peers() * m_settings.get_int(settings_pack::peer_turnover) / 100, 1)
						, i->second->num_connect_candidates());
					i->second->disconnect_peers(peers_to_disconnect
						, error_code(errors::optimistic_disconnect));
				}
				else
				{
					// if we haven't reached the global max. see if any torrent
					// has reached its local limit
					for (auto const& pt : m_torrents)
					{
						std::shared_ptr<torrent> t = pt.second;

						// ths disconnect logic is disabled for torrents with
						// too low connection limit
						if (t->num_peers() < t->max_connections()
							* m_settings.get_int(settings_pack::peer_turnover_cutoff) / 100
							|| t->max_connections() < 6)
							continue;

						int const peers_to_disconnect = std::min(std::max(t->num_peers()
							* m_settings.get_int(settings_pack::peer_turnover) / 100, 1)
							, t->num_connect_candidates());
						t->disconnect_peers(peers_to_disconnect, errors::optimistic_disconnect);
					}
				}
			}
		}
	}

	void session_impl::received_buffer(int s)
	{
		int index = std::min(aux::log2p1(std::uint32_t(s >> 3)), 17);
		m_stats_counters.inc_stats_counter(counters::socket_recv_size3 + index);
	}

	void session_impl::sent_buffer(int s)
	{
		int index = std::min(aux::log2p1(std::uint32_t(s >> 3)), 17);
		m_stats_counters.inc_stats_counter(counters::socket_send_size3 + index);
	}

	void session_impl::prioritize_connections(std::weak_ptr<torrent> t)
	{
		m_prio_torrents.emplace_back(t, 10);
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::add_dht_node(udp::endpoint const& n)
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
		if (tor && should_log())
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
			m_dht_announce_timer.async_wait([this](error_code const& err) {
				this->wrap(&session_impl::on_dht_announce, err); });
		}
	}

	void session_impl::on_dht_announce(error_code const& e)
	{
		COMPLETE_ASYNC("session_impl::on_dht_announce");
		TORRENT_ASSERT(is_single_thread());
		if (e)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				session_log("aborting DHT announce timer (%d): %s"
					, e.value(), e.message().c_str());
			}
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
		int delay = std::max(m_settings.get_int(settings_pack::dht_announce_interval)
			/ std::max(int(m_torrents.size()), 1), 1);

		if (!m_dht_torrents.empty())
		{
			// we have prioritized torrents that need
			// an initial DHT announce. Don't wait too long
			// until we announce those.
			delay = std::min(4, delay);
		}

		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_announce");
		error_code ec;
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait([this](error_code const& err)
			{ this->wrap(&session_impl::on_dht_announce, err); });

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
		int const delay = std::max(m_settings.get_int(settings_pack::local_service_announce_interval)
			/ std::max(int(m_torrents.size()), 1), 1);
		error_code ec;
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait([this](error_code const& err) {
			this->wrap(&session_impl::on_lsd_announce, err); });

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
		for (auto& t : list)
		{
			TORRENT_ASSERT(t->state() == torrent_status::checking_files);
			TORRENT_ASSERT(t->is_auto_managed());
			if (limit <= 0)
			{
				t->pause();
			}
			else
			{
				t->resume();
				if (!t->should_check_files()) continue;
				t->start_checking();
				--limit;
			}
		}
	}

	void session_impl::auto_manage_torrents(std::vector<torrent*>& list
		, int& dht_limit, int& tracker_limit
		, int& lsd_limit, int& hard_limit, int type_limit)
	{
		for (auto& t : list)
		{
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
			t->set_paused(true, torrent_handle::graceful_pause
				| torrent_handle::clear_disk_cache);
			t->set_announce_to_dht(false);
			t->set_announce_to_trackers(false);
			t->set_announce_to_lsd(false);
		}
	}

	int session_impl::get_int_setting(int n) const
	{
		int const v = settings().get_int(n);
		if (v < 0) return std::numeric_limits<int>::max();
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
				std::min(checking_limit, int(checking.size())), checking.end()
				, [](torrent const* lhs, torrent const* rhs)
				{ return lhs->sequence_number() < rhs->sequence_number(); });

			std::partial_sort(downloaders.begin(), downloaders.begin() +
				std::min(hard_limit, int(downloaders.size())), downloaders.end()
				, [](torrent const* lhs, torrent const* rhs)
				{ return lhs->sequence_number() < rhs->sequence_number(); });

			std::partial_sort(seeds.begin(), seeds.begin() +
				std::min(hard_limit, int(seeds.size())), seeds.end()
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
#ifndef TORRENT_DISABLE_EXTENSIONS
		uint64_t const priority_undetermined = std::numeric_limits<uint64_t>::max() - 1;
#endif

		struct opt_unchoke_candidate
		{
			explicit opt_unchoke_candidate(std::shared_ptr<peer_connection> const* tp)
				: peer(tp)
			{}

			std::shared_ptr<peer_connection> const* peer;
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
						peer.ext_priority = std::min(priority, peer.ext_priority);
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
				torrent_peer const* pil = (*l.peer)->peer_info_struct();
				torrent_peer const* pir = (*r.peer)->peer_info_struct();
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

		// if we unchoke everyone, skip this logic
		if (settings().get_int(settings_pack::choking_algorithm) == settings_pack::fixed_slots_choker
			&& settings().get_int(settings_pack::unchoke_slots_limit) < 0)
			return;

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

			torrent const* t = p->associated_torrent().lock().get();
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
		int const allowed_unchoke_slots = int(m_stats_counters[counters::num_unchoke_slots]);
		if (num_opt_unchoke == 0) num_opt_unchoke = std::max(1, allowed_unchoke_slots / 5);
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
				auto const existing =
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
			auto* p = static_cast<peer_connection*>(pi->connection);
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

		// this loop will "hand out" connection_speed to the torrents, in a round
		// robin fashion, so that every torrent is equally likely to connect to a
		// peer

		// boost connections are connections made by torrent connection
		// boost, which are done immediately on a tracker response. These
		// connections needs to be deducted from the regular connection attempt
		// quota for this tick
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

		// zero connections speeds are allowed, we just won't make any connections
		if (max_connections <= 0) return;

		// TODO: use a lower limit than m_settings.connections_limit
		// to allocate the to 10% or so of connection slots for incoming
		// connections
		// cap this at max - 1, since we may add one below
		int const limit = std::min(m_settings.get_int(settings_pack::connections_limit)
			- num_connections(), std::numeric_limits<int>::max() - 1);

		// this logic is here to smooth out the number of new connection
		// attempts over time, to prevent connecting a large number of
		// sockets, wait 10 seconds, and then try again
		if (m_settings.get_bool(settings_pack::smooth_connects) && max_connections > (limit+1) / 2)
			max_connections = (limit + 1) / 2;

		aux::vector<torrent*>& want_peers_download = m_torrent_lists[torrent_want_peers_download];
		aux::vector<torrent*>& want_peers_finished = m_torrent_lists[torrent_want_peers_finished];

		// if no torrent want any peers, just return
		if (want_peers_download.empty() && want_peers_finished.empty()) return;

		// if we don't have any connection attempt quota, return
		if (max_connections <= 0) return;

		int steps_since_last_connect = 0;
		int const num_torrents = int(want_peers_finished.size() + want_peers_download.size());
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
					&& !want_peers_finished.empty())
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

			if (t->try_connect_peer())
			{
				--max_connections;
				steps_since_last_connect = 0;
				m_stats_counters.inc_stats_counter(counters::connection_attempts);
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

		time_point const now = aux::time_now();
		time_duration const unchoke_interval = now - m_last_choke;
		m_last_choke = now;

		// if we unchoke everyone, skip this logic
		if (settings().get_int(settings_pack::choking_algorithm) == settings_pack::fixed_slots_choker
			&& settings().get_int(settings_pack::unchoke_slots_limit) < 0)
		{
			m_stats_counters.set_value(counters::num_unchoke_slots, std::numeric_limits<int>::max());
			return;
		}

		// build list of all peers that are
		// unchokable.
		// TODO: 3 there should be a pre-calculated list of all peers eligible for
		// unchoking
		std::vector<peer_connection*> peers;
		for (auto i = m_connections.begin(); i != m_connections.end();)
		{
			std::shared_ptr<peer_connection> p = *i;
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

#if TORRENT_ABI_VERSION == 1
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
			max_upload_rate = std::max(20000, m_peak_up_rate + 10000);
			if (m_alerts.should_post<performance_alert>())
				m_alerts.emplace_alert<performance_alert>(torrent_handle()
					, performance_alert::bittyrant_with_no_uplimit);
		}
#else
		int const max_upload_rate = 0;
#endif

		int const allowed_upload_slots = unchoke_sort(peers, max_upload_rate
			, unchoke_interval, m_settings);

		m_stats_counters.set_value(counters::num_unchoke_slots
			, allowed_upload_slots);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log("RECALCULATE UNCHOKE SLOTS: [ peers: %d "
				"eligible-peers: %d"
				" allowed-slots: %d ]"
				, int(m_connections.size())
				, int(peers.size())
				, allowed_upload_slots);
		}
#endif

		int const unchoked_counter_optimistic
			= int(m_stats_counters[counters::num_peers_up_unchoked_optimistic]);
		int const num_opt_unchoke = (unchoked_counter_optimistic == 0)
			? std::max(1, allowed_upload_slots / 5) : unchoked_counter_optimistic;

		int unchoke_set_size = allowed_upload_slots - num_opt_unchoke;

		// go through all the peers and unchoke the first ones and choke
		// all the other ones.
		for (auto p : peers)
		{
			TORRENT_ASSERT(p != nullptr);
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
				torrent_handle handle = add_torrent(std::move(p), ec);

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

		auto const i = m_torrents.find(info_hash);
#if TORRENT_USE_INVARIANT_CHECKS
		for (auto const& te : m_torrents)
		{
			TORRENT_ASSERT(te.second);
		}
#endif
		if (i != m_torrents.end()) return i->second;
		return std::weak_ptr<torrent>();
	}

	void session_impl::insert_torrent(sha1_hash const& ih, std::shared_ptr<torrent> const& t
#if TORRENT_ABI_VERSION == 1
		, std::string const uuid
#endif
		)
	{
		sha1_hash const next_lsd = m_next_lsd_torrent != m_torrents.end()
			? m_next_lsd_torrent->first : sha1_hash();
#ifndef TORRENT_DISABLE_DHT
		sha1_hash const next_dht = m_next_dht_torrent != m_torrents.end()
			? m_next_dht_torrent->first : sha1_hash();
#endif

		float const load_factor = m_torrents.load_factor();

		m_torrents.emplace(ih, t);

#if !defined TORRENT_DISABLE_ENCRYPTION
		static char const req2[4] = {'r', 'e', 'q', '2'};
		hasher h(req2);
		h.update(ih);
		// this is SHA1("req2" + info-hash), used for
		// encrypted hand shakes
		m_obfuscated_torrents.emplace(h.final(), t);
#endif

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

#if TORRENT_ABI_VERSION == 1
		//deprecated in 1.2
		if (!uuid.empty()) m_uuids.insert(std::make_pair(uuid, t));
#endif

		t->added();
	}

	void session_impl::set_queue_position(torrent* me, queue_position_t p)
	{
		queue_position_t const current_pos = me->queue_position();
		if (current_pos == p) return;

		if (p >= queue_position_t{0} && current_pos == no_pos)
		{
			// we're inserting the torrent into the download queue
			queue_position_t const last = m_download_queue.end_index();
			if (p >= last)
			{
				m_download_queue.push_back(me);
				me->set_queue_position_impl(last);
			}
			else
			{
				m_download_queue.insert(m_download_queue.begin() + static_cast<int>(p), me);
				for (queue_position_t i = p; i < m_download_queue.end_index(); ++i)
				{
					m_download_queue[i]->set_queue_position_impl(i);
				}
			}
		}
		else if (p < queue_position_t{})
		{
			// we're removing the torrent from the download queue
			TORRENT_ASSERT(current_pos >= queue_position_t{0});
			TORRENT_ASSERT(p == no_pos);
			TORRENT_ASSERT(m_download_queue[current_pos] == me);
			m_download_queue.erase(m_download_queue.begin() + static_cast<int>(current_pos));
			me->set_queue_position_impl(no_pos);
			for (queue_position_t i = current_pos; i < m_download_queue.end_index(); ++i)
			{
				m_download_queue[i]->set_queue_position_impl(i);
			}
		}
		else if (p < current_pos)
		{
			// we're moving the torrent up the queue
			torrent* tmp = me;
			for (queue_position_t i = p; i <= current_pos; ++i)
			{
				std::swap(m_download_queue[i], tmp);
				m_download_queue[i]->set_queue_position_impl(i);
			}
			TORRENT_ASSERT(tmp == me);
		}
		else if (p > current_pos)
		{
			// we're moving the torrent down the queue
			p = std::min(p, prev(m_download_queue.end_index()));
			for (queue_position_t i = current_pos; i < p; ++i)
			{
				m_download_queue[i] = m_download_queue[next(i)];
				m_download_queue[i]->set_queue_position_impl(i);
			}
			m_download_queue[p] = me;
			me->set_queue_position_impl(p);
		}

		trigger_auto_manage();
	}

#if !defined TORRENT_DISABLE_ENCRYPTION
	torrent const* session_impl::find_encrypted_torrent(sha1_hash const& info_hash
		, sha1_hash const& xor_mask)
	{
		sha1_hash obfuscated = info_hash;
		obfuscated ^= xor_mask;

		auto const i = m_obfuscated_torrents.find(obfuscated);
		if (i == m_obfuscated_torrents.end()) return nullptr;
		return i->second.get();
	}
#endif

#if TORRENT_ABI_VERSION == 1
	//deprecated in 1.2
	std::weak_ptr<torrent> session_impl::find_torrent(std::string const& uuid) const
	{
		TORRENT_ASSERT(is_single_thread());

		auto const i = m_uuids.find(uuid);
		if (i != m_uuids.end()) return i->second;
		return std::weak_ptr<torrent>();
	}
#endif

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::vector<std::shared_ptr<torrent>> session_impl::find_collection(
		std::string const& collection) const
	{
		std::vector<std::shared_ptr<torrent>> ret;
		for (auto const& tp : m_torrents)
		{
			std::shared_ptr<torrent> t = tp.second;
			if (!t) continue;
			std::vector<std::string> const& c = t->torrent_file().collections();
			if (std::find(c.begin(), c.end(), collection) == c.end()) continue;
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
		auto const i = std::min_element(m_torrents.begin(), m_torrents.end()
			, &compare_disconnect_torrent);

		TORRENT_ASSERT(i != m_torrents.end());
		if (i == m_torrents.end()) return std::shared_ptr<torrent>();

		return i->second;
	}

#ifndef TORRENT_DISABLE_LOGGING
	bool session_impl::should_log() const
	{
		return m_alerts.should_post<log_alert>();
	}

	TORRENT_FORMAT(2,3)
	void session_impl::session_log(char const* fmt, ...) const noexcept try
	{
		if (!m_alerts.should_post<log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		m_alerts.emplace_alert<log_alert>(fmt, v);
		va_end(v);
	}
	catch (std::exception const&) {}
#endif

	void session_impl::get_torrent_status(std::vector<torrent_status>* ret
		, std::function<bool(torrent_status const&)> const& pred
		, status_flags_t const flags) const
	{
		for (auto const& t : m_torrents)
		{
			if (t.second->is_aborted()) continue;
			torrent_status st;
			t.second->status(&st, flags);
			if (!pred(st)) continue;
			ret->push_back(std::move(st));
		}
	}

	void session_impl::refresh_torrent_status(std::vector<torrent_status>* ret
		, status_flags_t const flags) const
	{
		for (auto& st : *ret)
		{
			auto t = st.handle.m_torrent.lock();
			if (!t) continue;
			t->status(&st, flags);
		}
	}

	void session_impl::post_torrent_updates(status_flags_t const flags)
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
		for (auto& t : state_updates)
		{
			TORRENT_ASSERT(t->m_links[aux::session_impl::torrent_state_updates].in_list());
			status.emplace_back();
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
		if (!m_posted_stats_header)
		{
			m_posted_stats_header = true;
			m_alerts.emplace_alert<session_stats_header_alert>();
		}
		m_disk_thread.update_stats_counters(m_stats_counters);

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_dht->update_stats_counters(m_stats_counters);
#endif

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

		for (auto const& i : m_torrents)
		{
			if (i.second->is_aborted()) continue;
			ret.push_back(torrent_handle(i.second));
		}
		return ret;
	}

	torrent_handle session_impl::find_torrent_handle(sha1_hash const& info_hash)
	{
		return torrent_handle(find_torrent(info_hash));
	}

	void session_impl::async_add_torrent(add_torrent_params* params)
	{
		std::unique_ptr<add_torrent_params> holder(params);

#if TORRENT_ABI_VERSION == 1
		if (!params->ti && string_begins_no_case("file://", params->url.c_str()))
		{
			if (!m_torrent_load_thread)
				m_torrent_load_thread.reset(new work_thread_t());

			m_torrent_load_thread->ios.post([params, this]
			{
				std::string const torrent_file_path = resolve_file_url(params->url);
				params->url.clear();

				std::unique_ptr<add_torrent_params> holder2(params);
				error_code ec;
				params->ti = std::make_shared<torrent_info>(torrent_file_path, ec);
				this->m_io_service.post(std::bind(&session_impl::on_async_load_torrent
					, this, params, ec));
				holder2.release();
			});
			holder.release();
			return;
		}
#endif

		error_code ec;
		add_torrent(std::move(*params), ec);
	}

#if TORRENT_ABI_VERSION == 1
	void session_impl::on_async_load_torrent(add_torrent_params* params, error_code ec)
	{
		std::unique_ptr<add_torrent_params> holder(params);

		if (ec)
		{
			m_alerts.emplace_alert<add_torrent_alert>(torrent_handle()
				, *params, ec);
			return;
		}
		TORRENT_ASSERT(params->ti->is_valid());
		TORRENT_ASSERT(params->ti->num_files() > 0);
		params->url.clear();
		add_torrent(std::move(*params), ec);
	}
#endif

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

	torrent_handle session_impl::add_torrent(add_torrent_params&& params
		, error_code& ec)
	{
		// params is updated by add_torrent_impl()
		std::shared_ptr<torrent> torrent_ptr;

		// in case there's an error, make sure to abort the torrent before leaving
		// the scope
		auto abort_torrent = aux::scope_end([&]{ if (torrent_ptr) torrent_ptr->abort(); });

		bool added;
		// TODO: 3 perhaps params could be moved into the torrent object, instead
		// of it being copied by the torrent constructor
		std::tie(torrent_ptr, added) = add_torrent_impl(params, ec);

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

#if TORRENT_ABI_VERSION == 1
		if (m_alerts.should_post<torrent_added_alert>())
			m_alerts.emplace_alert<torrent_added_alert>(handle);
#endif

		// if this was an existing torrent, we can't start it again, or add
		// another set of plugins etc. we're done
		if (!added)
		{
			abort_torrent.disarm();
			return handle;
		}

		torrent_ptr->set_ip_filter(m_ip_filter);
		torrent_ptr->start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : params.extensions)
		{
			std::shared_ptr<torrent_plugin> tp(ext(handle, params.userdata));
			if (tp) torrent_ptr->add_extension(std::move(tp));
		}

		add_extensions_to_torrent(torrent_ptr, params.userdata);
#endif

		insert_torrent(params.info_hash, torrent_ptr
#if TORRENT_ABI_VERSION == 1
			//deprecated in 1.2
			, params.uuid.empty()
				? params.url.empty() ? std::string()
				: params.url
				: params.uuid
#endif
		);

		// once we successfully add the torrent, we can disarm the abort action
		abort_torrent.disarm();

		// recalculate auto-managed torrents sooner (or put it off)
		// if another torrent will be added within one second from now
		// we want to put it off again anyway. So that while we're adding
		// a boat load of torrents, we postpone the recalculation until
		// we're done adding them all (since it's kind of an expensive operation)
		if (params.flags & torrent_flags::auto_managed)
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
	session_impl::add_torrent_impl(add_torrent_params& params, error_code& ec)
	{
		TORRENT_ASSERT(!params.save_path.empty());

		using ptr_t = std::shared_ptr<torrent>;

#if TORRENT_ABI_VERSION == 1
		if (string_begins_no_case("magnet:", params.url.c_str()))
		{
			parse_magnet_uri(params.url, params, ec);
			if (ec) return std::make_pair(ptr_t(), false);
			params.url.clear();
		}

		if (!params.ti && string_begins_no_case("file://", params.url.c_str()))
		{
			std::string const torrent_file_path = resolve_file_url(params.url);
			params.url.clear();
			auto t = std::make_shared<torrent_info>(torrent_file_path, std::ref(ec), 0);
			if (ec) return std::make_pair(ptr_t(), false);
			params.ti = t;
		}
#endif

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

		if (params.ti
			&& !params.info_hash.is_all_zeros()
			&& params.info_hash != params.ti->info_hash())
		{
			ec = errors::mismatching_info_hash;
			return std::make_pair(ptr_t(), false);
		}

#ifndef TORRENT_DISABLE_DHT
		// add params.dht_nodes to the DHT, if enabled
		for (auto const& n : params.dht_nodes)
			add_dht_node_name(n);
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
#if TORRENT_ABI_VERSION == 1
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

		if (params.info_hash.is_all_zeros())
		{
			ec = errors::missing_info_hash_in_uri;
			return std::make_pair(ptr_t(), false);
		}

		// is the torrent already active?
		std::shared_ptr<torrent> torrent_ptr = find_torrent(params.info_hash).lock();
#if TORRENT_ABI_VERSION == 1
		//deprecated in 1.2
		if (!torrent_ptr && !params.uuid.empty()) torrent_ptr = find_torrent(params.uuid).lock();
		// if we still can't find the torrent, look for it by url
		if (!torrent_ptr && !params.url.empty())
		{
			auto const i = std::find_if(m_torrents.begin(), m_torrents.end()
				, [&params](torrent_map::value_type const& te)
				{ return te.second->url() == params.url; });
			if (i != m_torrents.end())
				torrent_ptr = i->second;
		}
#endif

		if (torrent_ptr)
		{
			if (!(params.flags & torrent_flags::duplicate_is_error))
			{
#if TORRENT_ABI_VERSION == 1
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

		// make sure we have enough memory in the torrent lists up-front,
		// since when torrents changes states, we cannot allocate memory that
		// might fail.
		size_t const num_torrents = m_torrents.size();
		for (auto& l : m_torrent_lists)
		{
			l.reserve(num_torrents + 1);
		}

		torrent_ptr = std::make_shared<torrent>(*this, m_paused, params);
		torrent_ptr->set_queue_position(m_download_queue.end_index());

		return std::make_pair(torrent_ptr, true);
	}

	void session_impl::update_outgoing_interfaces()
	{
		std::string const net_interfaces = m_settings.get_str(settings_pack::outgoing_interfaces);

		// declared in string_util.hpp
		parse_comma_separated_string(net_interfaces, m_outgoing_interfaces);

#ifndef TORRENT_DISABLE_LOGGING
		if (!net_interfaces.empty() && m_outgoing_interfaces.empty())
		{
			session_log("ERROR: failed to parse outgoing interface list: %s"
				, net_interfaces.c_str());
		}
#endif
	}

	tcp::endpoint session_impl::bind_outgoing_socket(socket_type& s
		, address const& remote_address, error_code& ec) const
	{
		tcp::endpoint bind_ep(address_v4(), 0);
		if (m_settings.get_int(settings_pack::outgoing_port) > 0)
		{
#ifdef TORRENT_WINDOWS
			s.set_option(exclusive_address_use(true), ec);
#else
			s.set_option(tcp::acceptor::reuse_address(true), ec);
#endif
			// ignore errors because the underlying socket may not
			// be opened yet. This happens when we're routing through
			// a proxy. In that case, we don't yet know the address of
			// the proxy server, and more importantly, we don't know
			// the address family of its address. This means we can't
			// open the socket yet. The socks abstraction layer defers
			// opening it.
			ec.clear();
			bind_ep.port(std::uint16_t(next_port()));
		}

		if (is_utp(s))
		{
			// TODO: factor out this logic into a separate function for unit
			// testing

			utp_socket_impl* impl = nullptr;
			transport ssl = transport::plaintext;
#ifdef TORRENT_USE_OPENSSL
			if (s.get<ssl_stream<utp_stream>>() != nullptr)
			{
				impl = s.get<ssl_stream<utp_stream>>()->next_layer().get_impl();
				ssl = transport::ssl;
			}
			else
#endif
				impl = s.get<utp_stream>()->get_impl();

			std::vector<std::shared_ptr<listen_socket_t>> with_gateways;
			std::shared_ptr<listen_socket_t> match;
			for (auto& ls : m_listen_sockets)
			{
				if (is_v4(ls->local_endpoint) != remote_address.is_v4()) continue;
				if (ls->ssl != ssl) continue;
				if (!(ls->flags & listen_socket_t::local_network))
					with_gateways.push_back(ls);

				if (match_addr_mask(ls->local_endpoint.address(), remote_address, ls->netmask))
				{
					// is this better than the previous match?
					match = ls;
				}
			}
			if (!match && !with_gateways.empty())
				match = with_gateways[random(std::uint32_t(with_gateways.size() - 1))];

			if (match)
			{
				utp_init_socket(impl, match);
				return match->local_endpoint;
			}
			ec.assign(boost::system::errc::not_supported, generic_category());
			return {};
		}

		if (!m_outgoing_interfaces.empty())
		{
			if (m_interface_index >= m_outgoing_interfaces.size()) m_interface_index = 0;
			std::string const& ifname = m_outgoing_interfaces[m_interface_index++];

			bind_ep.address(bind_socket_to_device(m_io_service, s
				, remote_address.is_v4() ? tcp::v4() : tcp::v6()
				, ifname.c_str(), bind_ep.port(), ec));
			return bind_ep;
		}

		// if we're not binding to a specific interface, bind
		// to the same protocol family as the target endpoint
		if (is_any(bind_ep.address()))
		{
			if (remote_address.is_v6())
				bind_ep.address(address_v6::any());
			else
				bind_ep.address(address_v4::any());
		}

		s.bind(bind_ep, ec);
		return bind_ep;
	}

	// verify that ``addr``s interface allows incoming connections
	bool session_impl::verify_incoming_interface(address const& addr)
	{
		auto const iter = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, [&addr](std::shared_ptr<listen_socket_t> const& s)
			{ return s->local_endpoint.address() == addr; });
		return iter == m_listen_sockets.end()
			? false
			: bool((*iter)->flags & listen_socket_t::accept_incoming);
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
		for (auto const& s : m_outgoing_interfaces)
		{
			error_code err;
			address const ip = make_address(s.c_str(), err);
			if (err) continue;
			if (ip == addr) return true;
		}

		// we didn't find the address as an IP in the interface list. Now,
		// resolve which device (if any) has this IP address.
		std::string const device = device_for_address(addr, m_io_service, ec);
		if (ec) return false;

		// if no device was found to have this address, we fail
		if (device.empty()) return false;

		return std::any_of(m_outgoing_interfaces.begin(), m_outgoing_interfaces.end()
			, [&device](std::string const& s) { return s == device; });
	}

	bool session_impl::has_lsd() const
	{
		return std::any_of(m_listen_sockets.begin(), m_listen_sockets.end()
			, [](std::shared_ptr<listen_socket_t> const& s) { return bool(s->lsd); });
	}

	void session_impl::remove_torrent(const torrent_handle& h
		, remove_flags_t const options)
	{
		INVARIANT_CHECK;

		std::shared_ptr<torrent> tptr = h.m_torrent.lock();
		if (!tptr) return;

		m_alerts.emplace_alert<torrent_removed_alert>(tptr->get_handle()
			, tptr->info_hash());

		remove_torrent_impl(tptr, options);

		tptr->abort();
	}

	void session_impl::remove_torrent_impl(std::shared_ptr<torrent> tptr
		, remove_flags_t const options)
	{
#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		// remove from uuid list
		if (!tptr->uuid().empty())
		{
			auto const j = m_uuids.find(tptr->uuid());
			if (j != m_uuids.end()) m_uuids.erase(j);
		}
#endif

		auto i = m_torrents.find(tptr->torrent_file().info_hash());

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		// this torrent might be filed under the URL-hash
		if (i == m_torrents.end() && !tptr->url().empty())
		{
			i = m_torrents.find(hasher(tptr->url()).final());
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
		tptr->removed();

#if !defined TORRENT_DISABLE_ENCRYPTION
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

#if TORRENT_ABI_VERSION == 1

	void session_impl::update_ssl_listen()
	{
		INVARIANT_CHECK;

		// this function maps the previous functionality of just setting the ssl
		// listen port in order to enable the ssl listen sockets, to the new
		// mechanism where SSL sockets are specified in listen_interfaces.
		std::vector<std::string> ignore;
		auto current_ifaces = parse_listen_interfaces(
			m_settings.get_str(settings_pack::listen_interfaces), ignore);
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
			, std::back_inserter(new_ifaces), [](listen_interface_t in)
			{ in.ssl = true; return in; });

		current_ifaces.insert(current_ifaces.end(), new_ifaces.begin(), new_ifaces.end());

		m_settings.set_str(settings_pack::listen_interfaces
			, print_listen_interfaces(current_ifaces));
	}
#endif // TORRENT_ABI_VERSION

	void session_impl::update_listen_interfaces()
	{
		INVARIANT_CHECK;

		std::string const net_interfaces = m_settings.get_str(settings_pack::listen_interfaces);
		std::vector<std::string> err;
		m_listen_interfaces = parse_listen_interfaces(net_interfaces, err);

		for (auto const& e : err)
		{
			m_alerts.emplace_alert<listen_failed_alert>(e, lt::address{}, 0
				, operation_t::parse_address, errors::invalid_port, lt::socket_type_t::tcp);
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log("update listen interfaces: %s", net_interfaces.c_str());
			session_log("parsed listen interfaces count: %d, ifaces: %s"
				, int(m_listen_interfaces.size())
				, print_listen_interfaces(m_listen_interfaces).c_str());
		}
#endif
	}

	void session_impl::update_privileged_ports()
	{
		if (m_settings.get_bool(settings_pack::no_connect_privileged_ports))
		{
			m_port_filter.add_rule(0, 1024, port_filter::blocked);

			// Close connections whose endpoint is filtered
			// by the new ip-filter
			for (auto const& t : m_torrents)
				t.second->port_filter_updated();
		}
		else
		{
			m_port_filter.add_rule(0, 1024, 0);
		}
	}

	void session_impl::update_auto_sequential()
	{
		for (auto& i : m_torrents)
			i.second->update_auto_sequential();
	}

	void session_impl::update_max_failcount()
	{
		for (auto& i : m_torrents)
			i.second->update_max_failcount();
	}

	void session_impl::update_resolver_cache_timeout()
	{
		int const timeout = m_settings.get_int(settings_pack::resolver_cache_timeout);
		m_host_resolver.set_cache_timeout(seconds(timeout));
	}

	void session_impl::update_proxy()
	{
		for (auto& i : m_listen_sockets)
			i->udp_sock->sock.set_proxy_settings(proxy(), m_alerts);
	}

	void session_impl::update_ip_notifier()
	{
		if (m_settings.get_bool(settings_pack::enable_ip_notifier))
			start_ip_notifier();
		else
			stop_ip_notifier();
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
		{
			if (!m_settings.get_str(settings_pack::dht_bootstrap_nodes).empty()
				&& m_dht_router_nodes.empty())
			{
				// if we have bootstrap nodes configured, make sure we initiate host
				// name lookups. once these complete, the DHT will be started.
				// they are tracked by m_outstanding_router_lookups
				update_dht_bootstrap_nodes();
			}
			else
			{
				start_dht();
			}
		}
		else
			stop_dht();
#endif
	}

	void session_impl::update_dht_bootstrap_nodes()
	{
#ifndef TORRENT_DISABLE_DHT
		if (!m_settings.get_bool(settings_pack::enable_dht)) return;

		std::string const& node_list = m_settings.get_str(settings_pack::dht_bootstrap_nodes);
		std::vector<std::pair<std::string, int>> nodes;
		parse_comma_separated_string_port(node_list, nodes);

#ifndef TORRENT_DISABLE_LOGGING
		if (!node_list.empty() && nodes.empty())
		{
			session_log("ERROR: failed to parse DHT bootstrap list: %s", node_list.c_str());
		}
#endif
		for (auto const& n : nodes)
			add_dht_router(n);
#endif
	}

	void session_impl::update_dht_settings()
	{
#ifndef TORRENT_DISABLE_DHT
		bool const prefer_verified_nodes = m_settings.get_bool(
			settings_pack::dht_prefer_verified_node_ids);

		m_dht_settings.prefer_verified_node_ids = prefer_verified_nodes;
#endif
	}

	void session_impl::update_count_slow()
	{
		error_code ec;
		for (auto const& tp : m_torrents)
		{
			tp.second->on_inactivity_tick(ec);
		}
	}

	// TODO: 2 this function should be removed and users need to deal with the
	// more generic case of having multiple listen ports
	std::uint16_t session_impl::listen_port() const
	{
		return listen_port(nullptr);
	}

	std::uint16_t session_impl::listen_port(listen_socket_t* sock) const
	{
		if (m_listen_sockets.empty()) return 0;
		if (sock)
		{
			// if we're using a proxy, we won't be able to accept any TCP
			// connections. We may be able to accept uTP connections though, so
			// announce the UDP port instead
			if (sock->flags & listen_socket_t::proxy)
				return std::uint16_t(sock->udp_external_port());

			if (!(sock->flags & listen_socket_t::accept_incoming))
				return 0;

			return std::uint16_t(sock->tcp_external_port());
		}

#ifdef TORRENT_USE_OPENSSL
		for (auto const& s : m_listen_sockets)
		{
			if (!(s->flags & listen_socket_t::accept_incoming)) continue;
			if (s->ssl == transport::plaintext)
				return std::uint16_t(s->tcp_external_port());
		}
		return 0;
#else
		sock = m_listen_sockets.front().get();
		if (!(sock->flags & listen_socket_t::accept_incoming)) return 0;
		return std::uint16_t(sock->tcp_external_port());
#endif
	}

	// TODO: 2 this function should be removed and users need to deal with the
	// more generic case of having multiple ssl ports
	std::uint16_t session_impl::ssl_listen_port() const
	{
		return ssl_listen_port(nullptr);
	}

	std::uint16_t session_impl::ssl_listen_port(listen_socket_t* sock) const
	{
#ifdef TORRENT_USE_OPENSSL
		if (sock)
		{
			if (!(sock->flags & listen_socket_t::accept_incoming)) return 0;
			return std::uint16_t(sock->tcp_external_port());
		}

		if (m_settings.get_int(settings_pack::proxy_type) != settings_pack::none)
			return 0;

		for (auto const& s : m_listen_sockets)
		{
			if (!(s->flags & listen_socket_t::accept_incoming)) continue;
			if (s->ssl == transport::ssl)
				return std::uint16_t(s->tcp_external_port());
		}
#else
		TORRENT_UNUSED(sock);
#endif
		return 0;
	}

	int session_impl::get_listen_port(transport const ssl, aux::listen_socket_handle const& s)
	{
		auto socket = s.get();
		if (socket->ssl != ssl)
		{
			auto alt_socket = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
				, [&](std::shared_ptr<listen_socket_t> const& e)
			{
				return e->ssl == ssl
					&& e->external_address.external_address()
						== socket->external_address.external_address();
			});
			if (alt_socket != m_listen_sockets.end())
				socket = alt_socket->get();
		}
		return socket->udp_external_port();
	}

	int session_impl::listen_port(transport const ssl, address const& local_addr)
	{
		auto socket = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, [&](std::shared_ptr<listen_socket_t> const& e)
		{
			if (!(e->flags & listen_socket_t::accept_incoming)) return false;
			auto const& listen_addr = e->external_address.external_address();
			return e->ssl == ssl
				&& (listen_addr == local_addr
					|| (listen_addr.is_v4() == local_addr.is_v4() && listen_addr.is_unspecified()));
		});
		if (socket != m_listen_sockets.end())
			return (*socket)->tcp_external_port();
		return 0;
	}

	void session_impl::announce_lsd(sha1_hash const& ih, int port)
	{
		// use internal listen port for local peers
		for (auto const& s : m_listen_sockets)
		{
			if (s->lsd) s->lsd->announce(ih, port);
		}
	}

	void session_impl::on_lsd_peer(tcp::endpoint const& peer, sha1_hash const& ih)
	{
		m_stats_counters.inc_stats_counter(counters::on_lsd_peer_counter);
		TORRENT_ASSERT(is_single_thread());

		INVARIANT_CHECK;

		std::shared_ptr<torrent> t = find_torrent(ih).lock();
		if (!t) return;
		// don't add peers from lsd to private torrents
		if (t->torrent_file().priv() || (t->torrent_file().is_i2p()
			&& !m_settings.get_bool(settings_pack::allow_i2p_mixed))) return;

		t->add_peer(peer, peer_info::lsd);
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			error_code ec;
			t->debug_log("lsd add_peer() [ %s ]"
				, peer.address().to_string(ec).c_str());
		}
#endif

		t->do_connect_boost();

		if (m_alerts.should_post<lsd_peer_alert>())
			m_alerts.emplace_alert<lsd_peer_alert>(t->get_handle(), peer);
	}

	void session_impl::start_natpmp(aux::listen_socket_t& s)
	{
		// don't create mappings for local IPv6 addresses
		// they can't be reached from outside of the local network anyways
		if (is_v6(s.local_endpoint) && is_local(s.local_endpoint.address()))
			return;

		if (!s.natpmp_mapper
			&& !(s.flags & listen_socket_t::local_network)
			&& !(s.flags & listen_socket_t::proxy))
		{
			// the natpmp constructor may fail and call the callbacks
			// into the session_impl.
			s.natpmp_mapper = std::make_shared<natpmp>(m_io_service, *this);
			ip_interface ip;
			ip.interface_address = s.local_endpoint.address();
			ip.netmask = s.netmask;
			std::strncpy(ip.name, s.device.c_str(), sizeof(ip.name) - 1);
			ip.name[sizeof(ip.name) - 1] = '\0';
			s.natpmp_mapper->start(ip);
		}
	}

	namespace {
		bool find_tcp_port_mapping(portmap_transport const transport
			, port_mapping_t mapping, std::shared_ptr<listen_socket_t> const& ls)
		{
			return ls->tcp_port_mapping[transport].mapping == mapping;
		}

		bool find_udp_port_mapping(portmap_transport const transport
			, port_mapping_t mapping, std::shared_ptr<listen_socket_t> const& ls)
		{
			return ls->udp_port_mapping[transport].mapping == mapping;
		}
	}

	void session_impl::on_port_mapping(port_mapping_t const mapping
		, address const& ip, int port
		, portmap_protocol const proto, error_code const& ec
		, portmap_transport const transport)
	{
		TORRENT_ASSERT(is_single_thread());

		// NOTE: don't assume that if ec != 0, the rest of the logic
		// is not necessary, the ports still need to be set, in other
		// words, don't early return without careful review of the
		// remaining logic
		if (ec && m_alerts.should_post<portmap_error_alert>())
		{
			m_alerts.emplace_alert<portmap_error_alert>(mapping
				, transport, ec);
		}

		// look through our listen sockets to see if this mapping is for one of
		// them (it could also be a user mapping)

		auto ls
			= std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, std::bind(find_tcp_port_mapping, transport, mapping, _1));

		bool tcp = true;
		if (ls == m_listen_sockets.end())
		{
			ls = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
				, std::bind(find_udp_port_mapping, transport, mapping, _1));
			tcp = false;
		}

		if (ls != m_listen_sockets.end())
		{
			if (!ec && ip != address())
			{
				// TODO: 1 report the proper address of the router as the source IP of
				// this vote of our external address, instead of the empty address
				(*ls)->external_address.cast_vote(ip, source_router, address());
			}

			if (tcp) (*ls)->tcp_port_mapping[transport].port = port;
			else (*ls)->udp_port_mapping[transport].port = port;
		}

		if (!ec && m_alerts.should_post<portmap_alert>())
		{
			m_alerts.emplace_alert<portmap_alert>(mapping, port
				, transport, proto);
		}
	}

#if TORRENT_ABI_VERSION == 1
	session_status session_impl::status() const
	{
//		INVARIANT_CHECK;
		TORRENT_ASSERT(is_single_thread());

		session_status s;

		s.optimistic_unchoke_counter = m_optimistic_unchoke_time_scaler;
		s.unchoke_counter = m_unchoke_time_scaler;
		s.num_dead_peers = int(m_undead_peers.size());

		s.num_peers = int(m_stats_counters[counters::num_peers_connected]);
		s.num_unchoked = int(m_stats_counters[counters::num_peers_up_unchoked_all]);
		s.allowed_upload_slots = int(m_stats_counters[counters::num_unchoke_slots]);

		s.num_torrents
			= int(m_stats_counters[counters::num_checking_torrents]
			+ m_stats_counters[counters::num_stopped_torrents]
			+ m_stats_counters[counters::num_queued_seeding_torrents]
			+ m_stats_counters[counters::num_queued_download_torrents]
			+ m_stats_counters[counters::num_upload_only_torrents]
			+ m_stats_counters[counters::num_downloading_torrents]
			+ m_stats_counters[counters::num_seeding_torrents]
			+ m_stats_counters[counters::num_error_torrents]);

		s.num_paused_torrents
			= int(m_stats_counters[counters::num_stopped_torrents]
			+ m_stats_counters[counters::num_error_torrents]
			+ m_stats_counters[counters::num_queued_seeding_torrents]
			+ m_stats_counters[counters::num_queued_download_torrents]);

		s.total_redundant_bytes = m_stats_counters[counters::recv_redundant_bytes];
		s.total_failed_bytes = m_stats_counters[counters::recv_failed_bytes];

		s.up_bandwidth_queue = int(m_stats_counters[counters::limiter_up_queue]);
		s.down_bandwidth_queue = int(m_stats_counters[counters::limiter_down_queue]);

		s.up_bandwidth_bytes_queue = int(m_stats_counters[counters::limiter_up_bytes]);
		s.down_bandwidth_bytes_queue = int(m_stats_counters[counters::limiter_down_bytes]);

		s.disk_write_queue = int(m_stats_counters[counters::num_peers_down_disk]);
		s.disk_read_queue = int(m_stats_counters[counters::num_peers_up_disk]);

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
		s.total_ip_overhead_download = m_stats_counters[counters::recv_ip_overhead_bytes];
		s.ip_overhead_upload_rate = m_stat.transfer_rate(stat::upload_ip_protocol);
		s.total_ip_overhead_upload = m_stats_counters[counters::sent_ip_overhead_bytes];

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

		s.utp_stats.packet_loss = std::uint64_t(m_stats_counters[counters::utp_packet_loss]);
		s.utp_stats.timeout = std::uint64_t(m_stats_counters[counters::utp_timeout]);
		s.utp_stats.packets_in = std::uint64_t(m_stats_counters[counters::utp_packets_in]);
		s.utp_stats.packets_out = std::uint64_t(m_stats_counters[counters::utp_packets_out]);
		s.utp_stats.fast_retransmit = std::uint64_t(m_stats_counters[counters::utp_fast_retransmit]);
		s.utp_stats.packet_resend = std::uint64_t(m_stats_counters[counters::utp_packet_resend]);
		s.utp_stats.samples_above_target = std::uint64_t(m_stats_counters[counters::utp_samples_above_target]);
		s.utp_stats.samples_below_target = std::uint64_t(m_stats_counters[counters::utp_samples_below_target]);
		s.utp_stats.payload_pkts_in = std::uint64_t(m_stats_counters[counters::utp_payload_pkts_in]);
		s.utp_stats.payload_pkts_out = std::uint64_t(m_stats_counters[counters::utp_payload_pkts_out]);
		s.utp_stats.invalid_pkts_in = std::uint64_t(m_stats_counters[counters::utp_invalid_pkts_in]);
		s.utp_stats.redundant_pkts_in = std::uint64_t(m_stats_counters[counters::utp_redundant_pkts_in]);

		s.utp_stats.num_idle = int(m_stats_counters[counters::num_utp_idle]);
		s.utp_stats.num_syn_sent = int(m_stats_counters[counters::num_utp_syn_sent]);
		s.utp_stats.num_connected = int(m_stats_counters[counters::num_utp_connected]);
		s.utp_stats.num_fin_sent = int(m_stats_counters[counters::num_utp_fin_sent]);
		s.utp_stats.num_close_wait = int(m_stats_counters[counters::num_utp_close_wait]);

		// this loop is potentially expensive. It could be optimized by
		// simply keeping a global counter
		s.peerlist_size = std::accumulate(m_torrents.begin(), m_torrents.end(), 0
			, [](int const acc, std::pair<sha1_hash, std::shared_ptr<torrent>> const& t)
			{ return acc + t.second->num_known_peers(); });

		return s;
	}
#endif // TORRENT_ABI_VERSION

	void session_impl::get_cache_info(torrent_handle h, cache_status* ret, int flags) const
	{
		storage_index_t st{0};
		bool whole_session = true;
		std::shared_ptr<torrent> t = h.m_torrent.lock();
		if (t)
		{
			if (t->has_storage())
			{
				st = t->storage();
				whole_session = false;
			}
			else
				flags = session::disk_cache_no_pieces;
		}
		m_disk_thread.get_cache_info(ret, st
			, flags & session::disk_cache_no_pieces, whole_session);
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::start_dht()
	{
		INVARIANT_CHECK;

		stop_dht();

		if (!m_settings.get_bool(settings_pack::enable_dht)) return;

		// postpone starting the DHT if we're still resolving the DHT router
		if (m_outstanding_router_lookups > 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("not starting DHT, outstanding router lookups: %d"
				, m_outstanding_router_lookups);
#endif
			return;
		}

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			session_log("not starting DHT, aborting");
#endif
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		session_log("starting DHT, running: %s, router lookups: %d"
			, m_dht ? "true" : "false", m_outstanding_router_lookups);
#endif

		// TODO: refactor, move the storage to dht_tracker
		m_dht_storage = m_dht_storage_constructor(m_dht_settings);
		m_dht = std::make_shared<dht::dht_tracker>(
			static_cast<dht::dht_observer*>(this)
			, m_io_service
			, [=](aux::listen_socket_handle const& sock
				, udp::endpoint const& ep
				, span<char const> p
				, error_code& ec
				, udp_send_flags_t const flags)
				{ send_udp_packet_listen(sock, ep, p, ec, flags); }
			, m_dht_settings
			, m_stats_counters
			, *m_dht_storage
			, std::move(m_dht_state));

		for (auto& s : m_listen_sockets)
		{
			if (s->ssl != transport::ssl
				&& !(s->flags & listen_socket_t::local_network))
			{
				m_dht->new_socket(s);
			}
		}

		for (auto const& n : m_dht_router_nodes)
		{
			m_dht->add_router_node(n);
		}

		for (auto const& n : m_dht_nodes)
		{
			m_dht->add_node(n);
		}
		m_dht_nodes.clear();
		m_dht_nodes.shrink_to_fit();

		auto cb = [this](
			std::vector<std::pair<dht::node_entry, std::string>> const&)
		{
			if (m_alerts.should_post<dht_bootstrap_alert>())
				m_alerts.emplace_alert<dht_bootstrap_alert>();
		};

		m_dht->start(cb);
	}

	void session_impl::stop_dht()
	{
#ifndef TORRENT_DISABLE_LOGGING
		session_log("about to stop DHT, running: %s", m_dht ? "true" : "false");
#endif

		if (m_dht)
		{
			m_dht->stop();
			m_dht.reset();
		}

		m_dht_storage.reset();
	}

	void session_impl::set_dht_settings(dht::dht_settings const& settings)
	{
		static_cast<dht::dht_settings&>(m_dht_settings) = settings;
		if (m_dht_settings.upload_rate_limit > std::numeric_limits<int>::max() / 3)
			m_dht_settings.upload_rate_limit = std::numeric_limits<int>::max() / 3;
		m_settings.set_int(settings_pack::dht_upload_rate_limit, m_dht_settings.upload_rate_limit);
	}

	void session_impl::set_dht_state(dht::dht_state&& state)
	{
		m_dht_state = std::move(state);
	}

	void session_impl::set_dht_storage(dht::dht_storage_constructor_type sc)
	{
		m_dht_storage_constructor = std::move(sc);
	}

#if TORRENT_ABI_VERSION == 1
	entry session_impl::dht_state() const
	{
		return m_dht ? dht::save_dht_state(m_dht->state()) : entry();
	}

	void session_impl::start_dht_deprecated(entry const& startup_state)
	{
		m_settings.set_bool(settings_pack::enable_dht, true);
		std::vector<char> tmp;
		bencode(std::back_inserter(tmp), startup_state);

		bdecode_node e;
		error_code ec;
		if (tmp.empty() || bdecode(&tmp[0], &tmp[0] + tmp.size(), e, ec) != 0)
			return;
		m_dht_state = dht::read_dht_state(e);
		start_dht();
	}
#endif

	void session_impl::add_dht_node_name(std::pair<std::string, int> const& node)
	{
		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_name_lookup");
		m_host_resolver.async_resolve(node.first, resolver::abort_on_shutdown
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
					operation_t::hostname_lookup, e);
			return;
		}

		for (auto const& addr : addresses)
		{
			udp::endpoint ep(addr, std::uint16_t(port));
			add_dht_node(ep);
		}
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		ADD_OUTSTANDING_ASYNC("session_impl::on_dht_router_name_lookup");
		++m_outstanding_router_lookups;
		m_host_resolver.async_resolve(node.first, resolver::abort_on_shutdown
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
					operation_t::hostname_lookup, e);

			if (m_outstanding_router_lookups == 0) start_dht();
			return;
		}


		for (auto const& addr : addresses)
		{
			// router nodes should be added before the DHT is started (and bootstrapped)
			udp::endpoint ep(addr, std::uint16_t(port));
			if (m_dht) m_dht->add_router_node(ep);
			m_dht_router_nodes.push_back(ep);
		}

		if (m_outstanding_router_lookups == 0) start_dht();
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
			, this, _1, _2), std::move(salt));
	}

	namespace {

		void on_dht_put_immutable_item(alert_manager& alerts, sha1_hash target, int num)
		{
			if (alerts.should_post<dht_put_alert>())
				alerts.emplace_alert<dht_put_alert>(target, num);
		}

		void on_dht_put_mutable_item(alert_manager& alerts, dht::item const& i, int num)
		{
			if (alerts.should_post<dht_put_alert>())
			{
				dht::signature const sig = i.sig();
				dht::public_key const pk = i.pk();
				dht::sequence_number const seq = i.seq();
				std::string salt = i.salt();
				alerts.emplace_alert<dht_put_alert>(pk.bytes, sig.bytes
					, std::move(salt), seq.value, num);
			}
		}

		void put_mutable_callback(dht::item& i
			, std::function<void(entry&, std::array<char, 64>&
				, std::int64_t&, std::string const&)> cb)
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
		, std::int64_t&, std::string const&)> cb
		, std::string salt)
	{
		if (!m_dht) return;
		m_dht->put_item(dht::public_key(key.data())
			, std::bind(&on_dht_put_mutable_item, std::ref(m_alerts), _1, _2)
			, std::bind(&put_mutable_callback, _1, std::move(cb)), salt);
	}

	void session_impl::dht_get_peers(sha1_hash const& info_hash)
	{
		if (!m_dht) return;
		m_dht->get_peers(info_hash, std::bind(&on_dht_get_peers, std::ref(m_alerts), info_hash, _1));
	}

	void session_impl::dht_announce(sha1_hash const& info_hash, int port, dht::announce_flags_t const flags)
	{
		if (!m_dht) return;
		m_dht->announce(info_hash, port, flags, std::bind(&on_dht_get_peers, std::ref(m_alerts), info_hash, _1));
	}

	void session_impl::dht_live_nodes(sha1_hash const& nid)
	{
		if (!m_dht) return;
		auto nodes = m_dht->live_nodes(nid);
		m_alerts.emplace_alert<dht_live_nodes_alert>(nid, nodes);
	}

	void session_impl::dht_sample_infohashes(udp::endpoint const& ep, sha1_hash const& target)
	{
		if (!m_dht) return;
		m_dht->sample_infohashes(ep, target, [this, ep](time_duration const interval
			, int const num, std::vector<sha1_hash> samples
			, std::vector<std::pair<sha1_hash, udp::endpoint>> nodes)
		{
			m_alerts.emplace_alert<dht_sample_infohashes_alert>(ep
				, interval, num, std::move(samples), std::move(nodes));
		});
	}

	void session_impl::dht_direct_request(udp::endpoint const& ep, entry& e, void* userdata)
	{
		if (!m_dht) return;
		m_dht->direct_request(ep, e, std::bind(&on_direct_response, std::ref(m_alerts), userdata, _1));
	}

#endif

#if !defined TORRENT_DISABLE_ENCRYPTION
	void session_impl::add_obfuscated_hash(sha1_hash const& obfuscated
		, std::weak_ptr<torrent> const& t)
	{
		m_obfuscated_torrents.insert(std::make_pair(obfuscated, t.lock()));
	}
#endif // TORRENT_DISABLE_ENCRYPTION

	bool session_impl::is_listening() const
	{
		return !m_listen_sockets.empty();
	}

	session_impl::~session_impl()
	{
		// since we're destructing the session, no more alerts will make it out to
		// the user. So stop posting them now
		m_alerts.set_alert_mask({});

		// this is not allowed to be the network thread!
//		TORRENT_ASSERT(is_not_thread());
// TODO: asserts that no outstanding async operations are still in flight

		// this can happen if we end the io_service run loop with an exception
		m_connections.clear();
		for (auto& t : m_torrents)
		{
			t.second->panic();
			t.second->abort();
		}
		m_torrents.clear();
#if !defined TORRENT_DISABLE_ENCRYPTION
		m_obfuscated_torrents.clear();
#endif
#if TORRENT_ABI_VERSION == 1
		m_uuids.clear();
#endif

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
			for (wakeup_t const& w : _wakeups)
			{
				bool const idle_wakeup = w.context_switches > prev_csw;
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
	}

#if TORRENT_ABI_VERSION == 1
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
		INVARIANT_CHECK;
		settings_pack p;
		p.set_int(settings_pack::local_download_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_local_upload_rate_limit(int bytes_per_second)
	{
		INVARIANT_CHECK;
		settings_pack p;
		p.set_int(settings_pack::local_upload_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_download_rate_limit_depr(int bytes_per_second)
	{
		INVARIANT_CHECK;
		settings_pack p;
		p.set_int(settings_pack::download_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_upload_rate_limit_depr(int bytes_per_second)
	{
		INVARIANT_CHECK;
		settings_pack p;
		p.set_int(settings_pack::upload_rate_limit, bytes_per_second);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_max_connections(int limit)
	{
		INVARIANT_CHECK;
		settings_pack p;
		p.set_int(settings_pack::connections_limit, limit);
		apply_settings_pack_impl(p);
	}

	void session_impl::set_max_uploads(int limit)
	{
		INVARIANT_CHECK;
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
#endif // DEPRECATE


	namespace {
		template <typename Socket>
		void set_tos(Socket& s, int v, error_code& ec)
		{
#if defined IPV6_TCLASS
			if (is_v6(s.local_endpoint(ec)))
				s.set_option(traffic_class(char(v)), ec);
			else if (!ec)
#endif
				s.set_option(type_of_service(char(v)), ec);
		}
	}

	// TODO: 2 this should be factored into the udp socket, so we only have the
	// code once
	void session_impl::update_peer_tos()
	{
		int const tos = m_settings.get_int(settings_pack::peer_tos);
		for (auto const& l : m_listen_sockets)
		{
			if (l->sock)
			{
				error_code ec;
				set_tos(*l->sock, tos, ec);

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log(">>> SET_TOS [ tcp (%s %d) tos: %x e: %s ]"
						, l->sock->local_endpoint().address().to_string().c_str()
						, l->sock->local_endpoint().port(), tos, ec.message().c_str());
				}
#endif
			}

			if (l->udp_sock)
			{
				error_code ec;
				set_tos(l->udp_sock->sock, tos, ec);

#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					session_log(">>> SET_TOS [ udp (%s %d) tos: %x e: %s ]"
						, l->udp_sock->sock.local_endpoint().address().to_string().c_str()
						, l->udp_sock->sock.local_port()
						, tos, ec.message().c_str());
				}
#endif
			}
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

		if (settings().get_int(settings_pack::choking_algorithm) != settings_pack::fixed_slots_choker)
			return;

		if (allowed_upload_slots == std::numeric_limits<int>::max())
		{
			// this means we're not aplpying upload slot limits, unchoke
			// everyone
			for (auto const& p : m_connections)
			{
				if (p->is_disconnecting()
					|| p->is_connecting()
					|| !p->is_choked()
					|| p->in_handshake()
					|| p->ignore_unchoke_slots()
					)
					continue;

				auto const t = p->associated_torrent().lock();
				t->unchoke_peer(*p);
			}
		}
		else
		{
			// trigger recalculating unchoke slots
			m_unchoke_time_scaler = 0;
		}
	}

	void session_impl::update_connection_speed()
	{
		if (m_settings.get_int(settings_pack::connection_speed) < 0)
			m_settings.set_int(settings_pack::connection_speed, 200);
	}

	void session_impl::update_queued_disk_bytes()
	{
		int const cache_size = m_settings.get_int(settings_pack::cache_size);
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
		if (settings().get_int(settings_pack::choking_algorithm) != settings_pack::fixed_slots_choker) return false;
		return m_stats_counters[counters::num_peers_up_unchoked]
			< m_stats_counters[counters::num_unchoke_slots]
			|| m_settings.get_int(settings_pack::unchoke_slots_limit) < 0;
	}

	void session_impl::update_dht_upload_rate_limit()
	{
#ifndef TORRENT_DISABLE_DHT
		m_dht_settings.upload_rate_limit = m_settings.get_int(settings_pack::dht_upload_rate_limit);
		if (m_dht_settings.upload_rate_limit > std::numeric_limits<int>::max() / 3)
		{
			m_settings.set_int(settings_pack::dht_upload_rate_limit, std::numeric_limits<int>::max() / 3);
			m_dht_settings.upload_rate_limit = std::numeric_limits<int>::max() / 3;
		}
#endif
	}

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
	}

	void session_impl::update_report_web_seed_downloads()
	{
		// if this flag changed, update all web seed connections
		bool report = m_settings.get_bool(settings_pack::report_web_seed_downloads);
		for (auto const& c : m_connections)
		{
			connection_type const type = c->type();
			if (type == connection_type::url_seed
				|| type == connection_type::http_seed)
				c->ignore_stats(!report);
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

		m_io_service.post([this]{ this->wrap(&session_impl::on_trigger_auto_manage); });
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
		for (auto const& l : m_listen_sockets)
		{
			error_code ec;
			set_socket_buffer_size(l->udp_sock->sock, m_settings, ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (ec && should_log())
			{
				error_code err;
				session_log("listen socket buffer size [ udp %s:%d ] %s"
					, l->udp_sock->sock.local_endpoint().address().to_string(err).c_str()
					, l->udp_sock->sock.local_port(), print_error(ec).c_str());
			}
#endif
			ec.clear();
			set_socket_buffer_size(*l->sock, m_settings, ec);
#ifndef TORRENT_DISABLE_LOGGING
			if (ec && should_log())
			{
				error_code err;
				session_log("listen socket buffer size [ tcp %s:%d] %s"
					, l->sock->local_endpoint().address().to_string(err).c_str()
					, l->sock->local_endpoint().port(), print_error(ec).c_str());
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
		int delay = std::max(m_settings.get_int(settings_pack::dht_announce_interval)
			/ std::max(int(m_torrents.size()), 1), 1);
		m_dht_announce_timer.expires_from_now(seconds(delay), ec);
		m_dht_announce_timer.async_wait([this](error_code const& e) {
			this->wrap(&session_impl::on_dht_announce, e); });
#endif
	}

#if TORRENT_ABI_VERSION == 1
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

		if (limit <= 0) limit = max_open_files();

		m_settings.set_int(settings_pack::connections_limit, limit);

		if (num_connections() > m_settings.get_int(settings_pack::connections_limit)
			&& !m_torrents.empty())
		{
			// if we have more connections that we're allowed, disconnect
			// peers from the torrents so that they are all as even as possible

			int to_disconnect = num_connections() - m_settings.get_int(settings_pack::connections_limit);

			int last_average = 0;
			int average = m_settings.get_int(settings_pack::connections_limit) / int(m_torrents.size());

			// the number of slots that are unused by torrents
			int extra = m_settings.get_int(settings_pack::connections_limit) % int(m_torrents.size());

			// run 3 iterations of this, then we're probably close enough
			for (int iter = 0; iter < 4; ++iter)
			{
				// the number of torrents that are above average
				int num_above = 0;
				for (auto const& t : m_torrents)
				{
					int const num = t.second->num_peers();
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

			for (auto const& t : m_torrents)
			{
				int const num = t.second->num_peers();
				if (num <= average) continue;

				// distribute the remainder
				int my_average = average;
				if (extra > 0)
				{
					++my_average;
					--extra;
				}

				int const disconnect = std::min(to_disconnect, num - my_average);
				to_disconnect -= disconnect;
				t.second->disconnect_peers(disconnect, errors::too_many_connections);
			}
		}
	}

	void session_impl::update_alert_mask()
	{
		m_alerts.set_alert_mask(alert_category_t(
			static_cast<std::uint32_t>(m_settings.get_int(settings_pack::alert_mask))));
	}

	void session_impl::update_validate_https()
	{
#ifdef TORRENT_USE_OPENSSL
		using boost::asio::ssl::context;
		auto const flags = m_settings.get_bool(settings_pack::validate_https_trackers)
			? context::verify_peer
				| context::verify_fail_if_no_peer_cert
				| context::verify_client_once
			: context::verify_none;
		error_code ec;
		m_ssl_ctx.set_verify_mode(flags, ec);
#endif
	}

	void session_impl::pop_alerts(std::vector<alert*>* alerts)
	{
		m_alerts.get_all(*alerts);
	}

#if TORRENT_ABI_VERSION == 1
	void session_impl::update_rate_limit_utp()
	{
		if (m_settings.get_bool(settings_pack::rate_limit_utp))
		{
			// allow the global or local peer class to limit uTP peers
			m_peer_class_type_filter.allow(peer_class_type_filter::utp_socket
				, m_global_class);
			m_peer_class_type_filter.allow(peer_class_type_filter::ssl_utp_socket
				, m_global_class);
		}
		else
		{
			// don't add the global or local peer class to limit uTP peers
			m_peer_class_type_filter.disallow(peer_class_type_filter::utp_socket
				, m_global_class);
			m_peer_class_type_filter.disallow(peer_class_type_filter::ssl_utp_socket
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
		if (m_alert_pointer_pos >= int(m_alert_pointers.size()))
		{
			pop_alerts(&m_alert_pointers);
			m_alert_pointer_pos = 0;
		}
	}

	alert const* session_impl::pop_alert()
	{
		if (m_alert_pointer_pos >= int(m_alert_pointers.size()))
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

#if TORRENT_ABI_VERSION == 1
	std::size_t session_impl::set_alert_queue_size_limit(std::size_t queue_size_limit_)
	{
		m_settings.set_int(settings_pack::alert_queue_size, int(queue_size_limit_));
		return std::size_t(m_alerts.set_alert_queue_size_limit(int(queue_size_limit_)));
	}
#endif

	void session_impl::start_ip_notifier()
	{
		INVARIANT_CHECK;

		if (m_ip_notifier) return;

		m_ip_notifier = create_ip_notifier(m_io_service);
		m_ip_notifier->async_wait([this](error_code const& e)
			{ this->wrap(&session_impl::on_ip_change, e); });
	}

	void session_impl::start_lsd()
	{
		INVARIANT_CHECK;

		for (auto& s : m_listen_sockets)
		{
			// we're not looking for local peers when we're using a proxy. We
			// want all traffic to go through the proxy
			if (s->flags & listen_socket_t::proxy) continue;
			if (s->lsd) continue;
			s->lsd = std::make_shared<lsd>(m_io_service, *this, s->local_endpoint.address()
				, s->netmask);
			error_code ec;
			s->lsd->start(ec);
			if (ec)
			{
				if (m_alerts.should_post<lsd_error_alert>())
					m_alerts.emplace_alert<lsd_error_alert>(ec);
				s->lsd.reset();
			}
		}
	}

	void session_impl::start_natpmp()
	{
		INVARIANT_CHECK;
		for (auto& s : m_listen_sockets)
		{
			start_natpmp(*s);
			remap_ports(remap_natpmp, *s);
		}
	}

	void session_impl::start_upnp()
	{
		INVARIANT_CHECK;
		for (auto& s : m_listen_sockets)
		{
			start_upnp(*s);
			remap_ports(remap_upnp, *s);
		}
	}

	void session_impl::start_upnp(aux::listen_socket_t& s)
	{
		// until we support SSDP over an IPv6 network (
		// https://en.wikipedia.org/wiki/Simple_Service_Discovery_Protocol )
		// there's no point in starting upnp on one.
		if (is_v6(s.local_endpoint))
			return;

		// there's no point in starting the UPnP mapper for a network that isn't
		// connected to the internet. The whole point is to forward ports through
		// the gateway
		if ((s.flags & listen_socket_t::local_network)
			|| (s.flags & listen_socket_t::proxy))
			return;

		if (!s.upnp_mapper)
		{
			// the upnp constructor may fail and call the callbacks
			// into the session_impl.
			s.upnp_mapper = std::make_shared<upnp>(m_io_service, m_settings
				, *this, s.local_endpoint.address().to_v4(), s.netmask.to_v4(), s.device);
			s.upnp_mapper->start();
		}
	}

	std::vector<port_mapping_t> session_impl::add_port_mapping(portmap_protocol const t
		, int const external_port
		, int const local_port)
	{
		std::vector<port_mapping_t> ret;
		for (auto& s : m_listen_sockets)
		{
			if (s->upnp_mapper) ret.push_back(s->upnp_mapper->add_mapping(t, external_port
				, tcp::endpoint(s->local_endpoint.address(), static_cast<std::uint16_t>(local_port))));
			if (s->natpmp_mapper) ret.push_back(s->natpmp_mapper->add_mapping(t, external_port
				, tcp::endpoint(s->local_endpoint.address(), static_cast<std::uint16_t>(local_port))));
		}
		return ret;
	}

	void session_impl::delete_port_mapping(port_mapping_t handle)
	{
		for (auto& s : m_listen_sockets)
		{
			if (s->upnp_mapper) s->upnp_mapper->delete_mapping(handle);
			if (s->natpmp_mapper) s->natpmp_mapper->delete_mapping(handle);
		}
	}

	void session_impl::stop_ip_notifier()
	{
		if (!m_ip_notifier) return;

		m_ip_notifier->cancel();
		m_ip_notifier.reset();
	}

	void session_impl::stop_lsd()
	{
		for (auto& s : m_listen_sockets)
		{
			if (!s->lsd) continue;
			s->lsd->close();
			s->lsd.reset();
		}
	}

	void session_impl::stop_natpmp()
	{
		for (auto& s : m_listen_sockets)
		{
			s->tcp_port_mapping[portmap_transport::natpmp] = listen_port_mapping();
			s->udp_port_mapping[portmap_transport::natpmp] = listen_port_mapping();
			if (!s->natpmp_mapper) continue;
			s->natpmp_mapper->close();
			s->natpmp_mapper.reset();
		}
	}

	void session_impl::stop_upnp()
	{
		for (auto& s : m_listen_sockets)
		{
			if (!s->upnp_mapper) continue;
			s->tcp_port_mapping[portmap_transport::upnp] = listen_port_mapping();
			s->udp_port_mapping[portmap_transport::upnp] = listen_port_mapping();
			s->upnp_mapper->close();
			s->upnp_mapper.reset();
		}
	}

	external_ip session_impl::external_address() const
	{
		address ips[2][2];

		// take the first IP we find which matches each category
		for (auto const& i : m_listen_sockets)
		{
			address external_addr = i->external_address.external_address();
			if (ips[0][external_addr.is_v6()] == address())
				ips[0][external_addr.is_v6()] = external_addr;
			address local_addr = i->local_endpoint.address();
			if (ips[is_local(local_addr)][local_addr.is_v6()] == address())
				ips[is_local(local_addr)][local_addr.is_v6()] = local_addr;
		}

		return {ips[1][0], ips[0][0], ips[1][1], ips[0][1]};
	}

	// this is the DHT observer version. DHT is the implied source
	void session_impl::set_external_address(aux::listen_socket_handle const& iface
		, address const& ip, address const& source)
	{
		auto i = iface.m_sock.lock();
		TORRENT_ASSERT(i);
		if (!i) return;
		set_external_address(i, ip, source_dht, source);
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
	bool session_impl::should_log(module_t) const
	{
		return m_alerts.should_post<dht_log_alert>();
	}

	TORRENT_FORMAT(3,4)
	void session_impl::log(module_t m, char const* fmt, ...)
	{
		if (!m_alerts.should_post<dht_log_alert>()) return;

		va_list v;
		va_start(v, fmt);
		m_alerts.emplace_alert<dht_log_alert>(
			static_cast<dht_log_alert::dht_module_t>(m), fmt, v);
		va_end(v);
	}

	void session_impl::log_packet(message_direction_t dir, span<char const> pkt
		, udp::endpoint const& node)
	{
		if (!m_alerts.should_post<dht_pkt_alert>()) return;

		dht_pkt_alert::direction_t d = dir == dht::dht_logger::incoming_message
			? dht_pkt_alert::incoming : dht_pkt_alert::outgoing;

		m_alerts.emplace_alert<dht_pkt_alert>(pkt, d, node);
	}

	bool session_impl::should_log_portmap(portmap_transport) const
	{
		return m_alerts.should_post<portmap_log_alert>();
	}

	void session_impl::log_portmap(portmap_transport transport, char const* msg) const
	{
		if (m_alerts.should_post<portmap_log_alert>())
			m_alerts.emplace_alert<portmap_log_alert>(transport, msg);
	}

	bool session_impl::should_log_lsd() const
	{
		return m_alerts.should_post<log_alert>();
	}

	void session_impl::log_lsd(char const* msg) const
	{
		if (m_alerts.should_post<log_alert>())
			m_alerts.emplace_alert<log_alert>(msg);
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

	void session_impl::set_external_address(
		tcp::endpoint const& local_endpoint, address const& ip
		, ip_source_t const source_type, address const& source)
	{
		auto sock = std::find_if(m_listen_sockets.begin(), m_listen_sockets.end()
			, [&](std::shared_ptr<listen_socket_t> const& v)
			{ return v->local_endpoint.address() == local_endpoint.address(); });

		if (sock != m_listen_sockets.end())
			set_external_address(*sock, ip, source_type, source);
	}

	void session_impl::set_external_address(std::shared_ptr<listen_socket_t> const& sock
		, address const& ip, ip_source_t const source_type, address const& source)
	{
		if (!sock->external_address.cast_vote(ip, source_type, source)) return;

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			session_log("external address updated for %s [ new-ip: %s type: %d last-voter: %s ]"
				, sock->device.empty() ? print_endpoint(sock->local_endpoint).c_str() : sock->device.c_str()
				, print_address(ip).c_str()
				, static_cast<std::uint8_t>(source_type)
				, print_address(source).c_str());
		}
#endif

		if (m_alerts.should_post<external_ip_alert>())
			m_alerts.emplace_alert<external_ip_alert>(ip);

		for (auto const& t : m_torrents)
		{
			t.second->new_external_ip();
		}

		// since we have a new external IP now, we need to
		// restart the DHT with a new node ID

#ifndef TORRENT_DISABLE_DHT
		if (m_dht) m_dht->update_node_id(sock);
#endif
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void session_impl::check_invariant() const
	{
		TORRENT_ASSERT(is_single_thread());

		if (m_settings.get_int(settings_pack::unchoke_slots_limit) < 0
			&& m_settings.get_int(settings_pack::choking_algorithm) == settings_pack::fixed_slots_choker)
			TORRENT_ASSERT(m_stats_counters[counters::num_unchoke_slots] == std::numeric_limits<int>::max());

		for (torrent_list_index_t l{}; l != m_torrent_lists.end_index(); ++l)
		{
			std::vector<torrent*> const& list = m_torrent_lists[l];
			for (auto const& i : list)
			{
				TORRENT_ASSERT(i->m_links[l].in_list());
			}

			queue_position_t idx{};
			for (auto t : m_download_queue)
			{
				TORRENT_ASSERT(t->queue_position() == idx);
				++idx;
			}
		}

		int const num_gauges = counters::num_error_torrents - counters::num_checking_torrents + 1;
		aux::array<int, num_gauges> torrent_state_gauges;
		torrent_state_gauges.fill(0);

#if defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		std::unordered_set<queue_position_t> unique;
#endif

		int num_active_downloading = 0;
		int num_active_finished = 0;
		int total_downloaders = 0;
		for (auto const& tor : m_torrents)
		{
			std::shared_ptr<torrent> const& t = tor.second;
			if (t->want_peers_download()) ++num_active_downloading;
			if (t->want_peers_finished()) ++num_active_finished;
			TORRENT_ASSERT(!(t->want_peers_download() && t->want_peers_finished()));

			int const state = t->current_stats_state() - counters::num_checking_torrents;
			if (state != torrent::no_gauge_state)
			{
				++torrent_state_gauges[state];
			}

			queue_position_t const pos = t->queue_position();
			if (pos < queue_position_t{})
			{
				TORRENT_ASSERT(pos == no_pos);
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
		TORRENT_ASSERT(num_active_downloading == int(m_torrent_lists[torrent_want_peers_download].size()));
		TORRENT_ASSERT(num_active_finished == int(m_torrent_lists[torrent_want_peers_finished].size()));

		std::unordered_set<peer_connection*> unique_peers;

		int unchokes = 0;
		int unchokes_all = 0;
		int num_optimistic = 0;
		int disk_queue[2] = {0, 0};
		for (auto const& p : m_connections)
		{
			TORRENT_ASSERT(p);
			if (p->is_disconnecting()) continue;

			std::shared_ptr<torrent> t = p->associated_torrent().lock();
			TORRENT_ASSERT(unique_peers.find(p.get()) == unique_peers.end());
			unique_peers.insert(p.get());

			if (p->m_channel_state[0] & peer_info::bw_disk) ++disk_queue[0];
			if (p->m_channel_state[1] & peer_info::bw_disk) ++disk_queue[1];

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

		for (auto const& p : m_undead_peers)
		{
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

		int const unchoked_counter_all = int(m_stats_counters[counters::num_peers_up_unchoked_all]);
		int const unchoked_counter = int(m_stats_counters[counters::num_peers_up_unchoked]);
		int const unchoked_counter_optimistic
			= int(m_stats_counters[counters::num_peers_up_unchoked_optimistic]);

		TORRENT_ASSERT_VAL(unchoked_counter_all == unchokes_all, unchokes_all);
		TORRENT_ASSERT_VAL(unchoked_counter == unchokes, unchokes);
		TORRENT_ASSERT_VAL(unchoked_counter_optimistic == num_optimistic, num_optimistic);

		for (auto const& te : m_torrents)
		{
			TORRENT_ASSERT(te.second);
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
				, resp.interval.count()
				, print_address(resp.external_ip).c_str()
				, print_address(tracker_ip).c_str());

			for (auto const& p : resp.peers)
			{
				debug_log("  %16s %5d %s", p.hostname.c_str(), p.port
					, p.pid.is_all_zeros() ? "" : to_hex(p.pid).c_str());
			}
			for (auto const& p : resp.peers4)
			{
				debug_log("  %s:%d", print_address(address_v4(p.ip)).c_str(), p.port);
			}
			for (auto const& p : resp.peers6)
			{
				debug_log("  [%s]:%d", print_address(address_v6(p.ip)).c_str(), p.port);
			}
		}

		void tracker_logger::tracker_request_error(tracker_request const&
			, error_code const& ec, std::string const& str
			, seconds32 const retry_interval)
		{
			TORRENT_UNUSED(retry_interval);
			debug_log("*** tracker error: %s %s"
				, ec.message().c_str(), str.c_str());
		}

		bool tracker_logger::should_log() const
		{
			return m_ses.alerts().should_post<log_alert>();
		}

		void tracker_logger::debug_log(const char* fmt, ...) const noexcept try
		{
			if (!m_ses.alerts().should_post<log_alert>()) return;

			va_list v;
			va_start(v, fmt);
			m_ses.alerts().emplace_alert<log_alert>(fmt, v);
			va_end(v);
		}
		catch (std::exception const&) {}
#endif // TORRENT_DISABLE_LOGGING
}}
