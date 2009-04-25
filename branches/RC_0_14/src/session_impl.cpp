/*

Copyright (c) 2006, Arvid Norberg, Magnus Jonsson
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

#include "libtorrent/pch.hpp"

#include <ctime>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/limits.hpp>
#include <boost/bind.hpp>
#include <boost/function_equal.hpp>

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
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/lsd.hpp"

#ifndef TORRENT_WINDOWS
#include <sys/resource.h>
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION

#include <openssl/crypto.h>

namespace
{
	// openssl requires this to clean up internal
	// structures it allocates
	struct openssl_cleanup
	{
		~openssl_cleanup() { CRYPTO_cleanup_all_ex_data(); }
	} openssl_global_destructor;
}

#endif
#ifdef _WIN32
// for ERROR_SEM_TIMEOUT
#include <winerror.h>
#endif

using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace libtorrent {

namespace fs = boost::filesystem;

namespace detail
{

	std::string generate_auth_string(std::string const& user
		, std::string const& passwd)
	{
		if (user.empty()) return std::string();
		return user + ":" + passwd;
	}
	

}
namespace aux {

	struct seed_random_generator
	{
		seed_random_generator()
		{
			std::srand(total_microseconds(time_now() - min_time()));
		}
	};

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, fingerprint const& cl_fprint
		, char const* listen_interface
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, fs::path const& logpath
#endif
		)
		: 
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		m_send_buffers(send_buffer_size),
#endif
		  m_files(40)
		, m_io_service()
		, m_disk_thread(m_io_service)
		, m_half_open(m_io_service)
		, m_download_channel(m_io_service, peer_connection::download_channel)
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		, m_upload_channel(m_io_service, peer_connection::upload_channel, true)
#else
		, m_upload_channel(m_io_service, peer_connection::upload_channel)
#endif
		, m_tracker_manager(m_settings, m_tracker_proxy)
		, m_listen_port_retries(listen_port_range.second - listen_port_range.first)
		, m_abort(false)
		, m_paused(false)
		, m_max_uploads(8)
		, m_allowed_upload_slots(8)
		, m_max_connections(200)
		, m_num_unchoked(0)
		, m_unchoke_time_scaler(0)
		, m_auto_manage_time_scaler(0)
		, m_optimistic_unchoke_time_scaler(0)
		, m_disconnect_time_scaler(90)
		, m_auto_scrape_time_scaler(180)
		, m_incoming_connection(false)
		, m_last_tick(time_now())
#ifndef TORRENT_DISABLE_DHT
		, m_dht_same_port(true)
		, m_external_udp_port(0)
		, m_dht_socket(m_io_service, bind(&session_impl::on_receive_udp, this, _1, _2, _3, _4)
			, m_half_open)
#endif
		, m_timer(m_io_service)
		, m_next_connect_torrent(0)
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, m_logpath(logpath)
#endif
#ifndef TORRENT_DISABLE_GEO_IP
		, m_asnum_db(0)
		, m_country_db(0)
#endif
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
	{
		error_code ec;
		m_listen_interface = tcp::endpoint(address::from_string(listen_interface, ec), listen_port_range.first);
		TORRENT_ASSERT(!ec);

		m_tcp_mapping[0] = -1;
		m_tcp_mapping[1] = -1;
		m_udp_mapping[0] = -1;
		m_udp_mapping[1] = -1;
#ifdef WIN32
		// windows XP has a limit on the number of
		// simultaneous half-open TCP connections
		DWORD windows_version = ::GetVersion();
		if ((windows_version & 0xff) >= 6)
		{
			// on vista the limit is 5 (in home edition)
			m_half_open.limit(4);
		}
		else
		{
			// on XP SP2 it's 10	
			m_half_open.limit(8);
		}
#endif

		m_bandwidth_manager[peer_connection::download_channel] = &m_download_channel;
		m_bandwidth_manager[peer_connection::upload_channel] = &m_upload_channel;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		m_logger = create_log("main_session", listen_port(), false);
		(*m_logger) << time_now_string() << "\n";

		(*m_logger) << "sizeof(torrent): " << sizeof(torrent) << "\n";
		(*m_logger) << "sizeof(peer_connection): " << sizeof(peer_connection) << "\n";
		(*m_logger) << "sizeof(bt_peer_connection): " << sizeof(bt_peer_connection) << "\n";
		(*m_logger) << "sizeof(policy::peer): " << sizeof(policy::peer) << "\n";
		(*m_logger) << "sizeof(tcp::endpoint): " << sizeof(tcp::endpoint) << "\n";
		(*m_logger) << "sizeof(address): " << sizeof(address) << "\n";
		(*m_logger) << "sizeof(address_v4): " << sizeof(address_v4) << "\n";
		(*m_logger) << "sizeof(address_v6): " << sizeof(address_v6) << "\n";
		(*m_logger) << "sizeof(void*): " << sizeof(void*) << "\n";
#endif

#ifdef TORRENT_STATS
		m_stats_logger.open("session_stats.log", std::ios::trunc);
		m_stats_logger <<
			"1. second\n"
			"2. upload rate\n"
			"3. download rate\n"
			"4. downloading torrents\n"
			"5. seeding torrents\n"
			"6. peers\n"
			"7. connecting peers\n"
			"8. disk block buffers\n"
			"\n";
		m_buffer_usage_logger.open("buffer_stats.log", std::ios::trunc);
		m_second_counter = 0;
		m_buffer_allocations = 0;
#endif

		// ---- generate a peer id ----
		static seed_random_generator seeder;

		m_key = rand() + (rand() << 15) + (rand() << 30);
		std::string print = cl_fprint.to_string();
		TORRENT_ASSERT(print.length() <= 20);

		// the client's fingerprint
		std::copy(
			print.begin()
			, print.begin() + print.length()
			, m_peer_id.begin());

		// http-accepted characters:
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz-_.!~*'()";

		// the random number
		for (unsigned char* i = m_peer_id.begin() + print.length();
			i != m_peer_id.end(); ++i)
		{
			*i = printable[rand() % (sizeof(printable)-1)];
		}

		m_timer.expires_from_now(seconds(1), ec);
		m_timer.async_wait(
			bind(&session_impl::second_tick, this, _1));

		m_thread.reset(new boost::thread(boost::ref(*this)));
	}

#ifndef TORRENT_DISABLE_GEO_IP
	namespace
	{
		struct free_ptr
		{
			void* ptr_;
			free_ptr(void* p): ptr_(p) {}
			~free_ptr() { free(ptr_); }
		};
	}

	char const* session_impl::country_for_ip(address const& a)
	{
		if (!a.is_v4() || m_country_db == 0) return 0;
		return GeoIP_country_code_by_ipnum(m_country_db, a.to_v4().to_ulong());
	}

	int session_impl::as_for_ip(address const& a)
	{
		if (!a.is_v4() || m_asnum_db == 0) return 0;
		char* name = GeoIP_name_by_ipnum(m_asnum_db, a.to_v4().to_ulong());
		if (name == 0) return 0;
		free_ptr p(name);
		// GeoIP returns the name as AS??? where ? is the AS-number
		return atoi(name + 2);
	}

	std::string session_impl::as_name_for_ip(address const& a)
	{
		if (!a.is_v4() || m_asnum_db == 0) return std::string();
		char* name = GeoIP_name_by_ipnum(m_asnum_db, a.to_v4().to_ulong());
		if (name == 0) return std::string();
		free_ptr p(name);
		char* tmp = std::strchr(name, ' ');
		if (tmp == 0) return std::string();
		return tmp + 1;
	}

	std::pair<const int, int>* session_impl::lookup_as(int as)
	{
		std::map<int, int>::iterator i = m_as_peak.lower_bound(as);

		if (i == m_as_peak.end() || i->first != as)
		{
			// we don't have any data for this AS, insert a new entry
			i = m_as_peak.insert(i, std::pair<int, int>(as, 0));
		}
		return &(*i);
	}

	bool session_impl::load_asnum_db(char const* file)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_asnum_db) GeoIP_delete(m_asnum_db);
		m_asnum_db = GeoIP_open(file, GEOIP_STANDARD);
		return m_asnum_db;
	}

	bool session_impl::load_country_db(char const* file)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_country_db) GeoIP_delete(m_country_db);
		m_country_db = GeoIP_open(file, GEOIP_STANDARD);
		return m_country_db;
	}

#endif

	void session_impl::load_state(entry const& ses_state)
	{
		if (ses_state.type() != entry::dictionary_t) return;
		mutex_t::scoped_lock l(m_mutex);
#ifndef TORRENT_DISABLE_GEO_IP
		entry const* as_map = ses_state.find_key("AS map");
		if (as_map && as_map->type() == entry::dictionary_t)
		{
			entry::dictionary_type const& as_peak = as_map->dict();
			for (entry::dictionary_type::const_iterator i = as_peak.begin()
				, end(as_peak.end()); i != end; ++i)
			{
				int as_num = atoi(i->first.c_str());
				if (i->second.type() != entry::int_t || i->second.integer() == 0) continue;
				int& peak = m_as_peak[as_num];
				if (peak < i->second.integer()) peak = i->second.integer();
			}
		}
#endif
	}

	entry session_impl::state() const
	{
		mutex_t::scoped_lock l(m_mutex);
		entry ret;
#ifndef TORRENT_DISABLE_GEO_IP
		entry::dictionary_type& as_map = ret["AS map"].dict();
		char buf[10];
		for (std::map<int, int>::const_iterator i = m_as_peak.begin()
			, end(m_as_peak.end()); i != end; ++i)
		{
			if (i->second == 0) continue;
			sprintf(buf, "%05d", i->first);
			as_map[buf] = i->second;
		}
#endif
		return ret;
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session_impl::add_extension(
		boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
		TORRENT_ASSERT(ext);

		typedef boost::shared_ptr<torrent_plugin>(*function_t)(torrent*, void*);
		function_t const* f = ext.target<function_t>();

		if (f)
		{
			for (extension_list_t::iterator i = m_extensions.begin(); i != m_extensions.end(); ++i)
				if (function_equal(*i, *f)) return;
		}

		m_extensions.push_back(ext);
	}
#endif

#ifndef TORRENT_DISABLE_DHT
	void session_impl::add_dht_node(udp::endpoint n)
	{
		if (m_dht) m_dht->add_node(n);
	}
#endif

	void session_impl::pause()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_paused) return;
		m_paused = true;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent& t = *i->second;
			if (!t.is_torrent_paused()) t.do_pause();
		}
	}

	void session_impl::resume()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (!m_paused) return;
		m_paused = false;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent& t = *i->second;
			t.do_resume();
		}
	}
	
	void session_impl::abort()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_abort) return;
#if defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " *** ABORT CALLED ***\n";
#endif
		// abort the main thread
		m_abort = true;
		m_queued_for_checking.clear();
		if (m_lsd) m_lsd->close();
		if (m_upnp) m_upnp->close();
		if (m_natpmp) m_natpmp->close();
#ifndef TORRENT_DISABLE_DHT
		if (m_dht) m_dht->stop();
		m_dht_socket.close();
#endif
		error_code ec;
		m_timer.cancel(ec);

		// close the listen sockets
		for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin()
			, end(m_listen_sockets.end()); i != end; ++i)
		{
			i->sock->close(ec);
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all torrents (" << m_torrents.size() << ")\n";
#endif
		// abort all torrents
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->abort();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all tracker requests\n";
#endif
		m_tracker_manager.abort_all_requests();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " sending event=stopped to trackers\n";
		int counter = 0;
#endif
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			torrent& t = *i->second;
			t.abort();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " sent " << counter << " tracker stop requests\n";
#endif

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all connections (" << m_connections.size() << ")\n";
#endif
		m_half_open.close();

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " connection queue: " << m_half_open.size() << "\n";
#endif

		// abort all connections
		while (!m_connections.empty())
		{
#ifdef TORRENT_DEBUG
			int conn = m_connections.size();
#endif
			(*m_connections.begin())->disconnect("stopping torrent");
			TORRENT_ASSERT(conn == int(m_connections.size()) + 1);
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " connection queue: " << m_half_open.size() << "\n";
#endif
		TORRENT_ASSERT(m_half_open.size() == 0);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " shutting down connection queue\n";
#endif

		m_download_channel.close();
		m_upload_channel.close();
	}

	void session_impl::set_port_filter(port_filter const& f)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_port_filter = f;
	}

	void session_impl::set_ip_filter(ip_filter const& f)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->ip_filter_updated();
	}

	void session_impl::set_settings(session_settings const& s)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		TORRENT_ASSERT(s.file_pool_size > 0);

		// less than 5 seconds unchoke interval is insane
		TORRENT_ASSERT(s.unchoke_interval >= 5);
		if (m_settings.cache_size != s.cache_size)
			m_disk_thread.set_cache_size(s.cache_size);
		if (m_settings.cache_expiry != s.cache_expiry)
			m_disk_thread.set_cache_expiry(s.cache_expiry);
		// if queuing settings were changed, recalculate
		// queued torrents sooner
		if ((m_settings.active_downloads != s.active_downloads
			|| m_settings.active_seeds != s.active_seeds
			|| m_settings.active_limit != s.active_limit)
			&& m_auto_manage_time_scaler > 2)
			m_auto_manage_time_scaler = 2;
		m_settings = s;
 		if (m_settings.connection_speed <= 0) m_settings.connection_speed = 200;
 
		m_files.resize(m_settings.file_pool_size);
		if (!s.auto_upload_slots) m_allowed_upload_slots = m_max_uploads;
		// replace all occurances of '\n' with ' '.
		std::string::iterator i = m_settings.user_agent.begin();
		while ((i = std::find(i, m_settings.user_agent.end(), '\n'))
			!= m_settings.user_agent.end())
			*i = ' ';
	}

	tcp::endpoint session_impl::get_ipv6_interface() const
	{
		return m_ipv6_interface;
	}

	tcp::endpoint session_impl::get_ipv4_interface() const
	{
		return m_ipv4_interface;
	}

	session_impl::listen_socket_t session_impl::setup_listener(tcp::endpoint ep
		, int retries, bool v6_only)
	{
		error_code ec;
		listen_socket_t s;
		s.sock.reset(new socket_acceptor(m_io_service));
		s.sock->open(ep.protocol(), ec);
		s.sock->set_option(socket_acceptor::reuse_address(true), ec);
		if (ep.protocol() == tcp::v6())
		{
			s.sock->set_option(v6only(v6_only), ec);
#ifdef TORRENT_WINDOWS
			// enable Teredo on windows
			s.sock->set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), ec);
#endif
		}
		s.sock->bind(ep, ec);
		while (ec && retries > 0)
		{
			ec = error_code();
			TORRENT_ASSERT(!ec);
			--retries;
			ep.port(ep.port() + 1);
			s.sock->bind(ep, ec);
		}
		if (ec)
		{
			// instead of giving up, try
			// let the OS pick a port
			ep.port(0);
			ec = error_code();
			s.sock->bind(ep, ec);
		}
		if (ec)
		{
			// not even that worked, give up
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(ep, ec));
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::stringstream msg;
			msg << "cannot bind to interface '";
			print_endpoint(msg, ep) << "' " << ec.message();
			(*m_logger) << msg.str() << "\n";
#endif
			return listen_socket_t();
		}
		s.external_port = s.sock->local_endpoint(ec).port();
		s.sock->listen(5, ec);
		if (ec)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(ep, ec));
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::stringstream msg;
			msg << "cannot listen on interface '";
			print_endpoint(msg, ep) << "' " << ec.message();
			(*m_logger) << msg.str() << "\n";
#endif
			return listen_socket_t();
		}

		if (m_alerts.should_post<listen_succeeded_alert>())
			m_alerts.post_alert(listen_succeeded_alert(ep));

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << "listening on: " << ep
			<< " external port: " << s.external_port << "\n";
#endif
		return s;
	}
	
	void session_impl::open_listen_port()
	{
		// close the open listen sockets
		m_listen_sockets.clear();
		m_incoming_connection = false;

		m_ipv6_interface = tcp::endpoint();
		m_ipv4_interface = tcp::endpoint();

		if (is_any(m_listen_interface.address()))
		{
			// this means we should open two listen sockets
			// one for IPv4 and one for IPv6
		
			listen_socket_t s = setup_listener(
				tcp::endpoint(address_v4::any(), m_listen_interface.port())
				, m_listen_port_retries);

			if (s.sock)
			{
				m_listen_sockets.push_back(s);
				async_accept(s.sock);
			}

			// only try to open the IPv6 port if IPv6 is installed
			if (supports_ipv6())
			{
				s = setup_listener(
					tcp::endpoint(address_v6::any(), m_listen_interface.port())
					, m_listen_port_retries, true);

				if (s.sock)
				{
					m_listen_sockets.push_back(s);
					async_accept(s.sock);
				}
			}

			// set our main IPv4 and IPv6 interfaces
			// used to send to the tracker
			error_code ec;
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
			// we should only open a single listen socket, that
			// binds to the given interface

			listen_socket_t s = setup_listener(
				m_listen_interface, m_listen_port_retries);

			if (s.sock)
			{
				m_listen_sockets.push_back(s);
				async_accept(s.sock);

				if (m_listen_interface.address().is_v6())
					m_ipv6_interface = m_listen_interface;
				else
					m_ipv4_interface = m_listen_interface;
			}
		}

		if (!m_listen_sockets.empty())
		{
			error_code ec;
			tcp::endpoint local = m_listen_sockets.front().sock->local_endpoint(ec);
			if (!ec)
			{
				if (m_natpmp.get())
				{
					if (m_tcp_mapping[0] != -1) m_natpmp->delete_mapping(m_tcp_mapping[0]);
					m_tcp_mapping[0] = m_natpmp->add_mapping(natpmp::tcp
						, local.port(), local.port());
				}
				if (m_upnp.get())
				{
					if (m_tcp_mapping[1] != -1) m_upnp->delete_mapping(m_tcp_mapping[1]);
					m_tcp_mapping[1] = m_upnp->add_mapping(upnp::tcp
						, local.port(), local.port());
				}
			}
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		m_logger = create_log("main_session", listen_port(), false);
#endif
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::on_receive_udp(error_code const& e
		, udp::endpoint const& ep, char const* buf, int len)
	{
		if (e)
		{
			if (e == asio::error::connection_refused
				|| e == asio::error::connection_reset
				|| e == asio::error::connection_aborted)
				m_dht->on_unreachable(ep);

			if (m_alerts.should_post<udp_error_alert>())
				m_alerts.post_alert(udp_error_alert(ep, e));
			return;
		}

		if (len > 20 && *buf == 'd' && m_dht)
		{
			// this is probably a dht message
			m_dht->on_receive(ep, buf, len);
		}
	}

#endif
	
	void session_impl::async_accept(boost::shared_ptr<socket_acceptor> const& listener)
	{
		shared_ptr<socket_type> c(new socket_type(m_io_service));
		c->instantiate<stream_socket>(m_io_service);
		listener->async_accept(c->get<stream_socket>()
			, bind(&session_impl::on_incoming_connection, this, c
			, boost::weak_ptr<socket_acceptor>(listener), _1));
	}

	void session_impl::on_incoming_connection(shared_ptr<socket_type> const& s
		, weak_ptr<socket_acceptor> listen_socket, error_code const& e)
	{
		boost::shared_ptr<socket_acceptor> listener = listen_socket.lock();
		if (!listener) return;
		
		if (e == asio::error::operation_aborted) return;

		mutex_t::scoped_lock l(m_mutex);
		if (m_abort) return;

		error_code ec;
		if (e)
		{
			tcp::endpoint ep = listener->local_endpoint(ec);
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			std::string msg = "error accepting connection on '"
				+ boost::lexical_cast<std::string>(ep) + "' " + e.message();
			(*m_logger) << msg << "\n";
#endif
#ifdef TORRENT_WINDOWS
			// Windows sometimes generates this error. It seems to be
			// non-fatal and we have to do another async_accept.
			if (e.value() == ERROR_SEM_TIMEOUT)
			{
				async_accept(listener);
				return;
			}
#endif
#ifdef TORRENT_BSD
			// Leopard sometimes generates an "invalid argument" error. It seems to be
			// non-fatal and we have to do another async_accept.
			if (e.value() == EINVAL)
			{
				async_accept(listener);
				return;
			}
#endif
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(ep, e));
			return;
		}
		async_accept(listener);

		// we got a connection request!
		tcp::endpoint endp = s->remote_endpoint(ec);

		if (ec)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << endp << " <== INCOMING CONNECTION FAILED, could "
				"not retrieve remote endpoint " << ec.message() << "\n";
#endif
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " <== INCOMING CONNECTION " << endp << "\n";
#endif

		// local addresses do not count, since it's likely
		// coming from our own client through local service discovery
		// and it does not reflect whether or not a router is open
		// for incoming connections or not.
		if (!is_local(endp.address()))
			m_incoming_connection = true;

		if (m_ip_filter.access(endp.address()) & ip_filter::blocked)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << "filtered blocked ip\n";
#endif
			if (m_alerts.should_post<peer_blocked_alert>())
				m_alerts.post_alert(peer_blocked_alert(endp.address()));
			return;
		}

		// don't allow more connections than the max setting
		if (num_connections() >= max_connections())
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << "number of connections limit exceeded (conns: "
				<< num_connections() << ", limit: " << max_connections()
				<< "), connection rejected\n";
#endif
			return;
		}

		// check if we have any active torrents
		// if we don't reject the connection
		if (m_torrents.empty())
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << " There are no torrents, disconnect\n";
#endif
		  	return;
		}

		bool has_active_torrent = false;
		for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
		{
			if (!i->second->is_paused())
			{
				has_active_torrent = true;
				break;
			}
		}
		if (!has_active_torrent)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_logger) << " There are no _active_ torrents, disconnect\n";
#endif
		  	return;
		}

		boost::intrusive_ptr<peer_connection> c(
			new bt_peer_connection(*this, s, endp, 0));
#ifdef TORRENT_DEBUG
		c->m_in_constructor = false;
#endif

		if (!c->is_disconnecting())
		{
			m_connections.insert(c);
			c->start();
		}
	}
	void session_impl::close_connection(peer_connection const* p
		, char const* message)
	{
		mutex_t::scoped_lock l(m_mutex);

// too expensive
//		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
//		for (aux::session_impl::torrent_map::const_iterator i = m_torrents.begin()
//			, end(m_torrents.end()); i != end; ++i)
//			TORRENT_ASSERT(!i->second->has_peer((peer_connection*)p));
#endif

#if defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " CLOSING CONNECTION "
			<< p->remote() << " : " << message << "\n";
#endif

		TORRENT_ASSERT(p->is_disconnecting());

		if (!p->is_choked()) --m_num_unchoked;
//		connection_map::iterator i = std::lower_bound(m_connections.begin(), m_connections.end()
//			, p, bind(&boost::intrusive_ptr<peer_connection>::get, _1) < p);
//		if (i->get() != p) i == m_connections.end();
		connection_map::iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, bind(&boost::intrusive_ptr<peer_connection>::get, _1) == p);
		if (i != m_connections.end()) m_connections.erase(i);
	}

	void session_impl::set_peer_id(peer_id const& id)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_peer_id = id;
	}

	void session_impl::set_key(int key)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_key = key;
	}

	void session_impl::unchoke_peer(peer_connection& c)
	{
		torrent* t = c.associated_torrent().lock().get();
		TORRENT_ASSERT(t);
		if (t->unchoke_peer(c))
			++m_num_unchoked;
	}

	int session_impl::next_port()
	{
		std::pair<int, int> const& out_ports = m_settings.outgoing_ports;
		if (m_next_port < out_ports.first || m_next_port > out_ports.second)
			m_next_port = out_ports.first;
	
		int port = m_next_port;
		++m_next_port;
		if (m_next_port > out_ports.second) m_next_port = out_ports.first;
#if defined TORRENT_LOGGING
		(*m_logger) << time_now_string() << " *** BINDING OUTGOING CONNECTION [ "
			"port: " << port << " ]\n";
#endif
		return port;
	}

	void session_impl::second_tick(error_code const& e)
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

// too expensive
//		INVARIANT_CHECK;

		if (m_abort) return;

		if (e)
		{
#if defined TORRENT_LOGGING
			(*m_logger) << "*** SECOND TIMER FAILED " << e.message() << "\n";
#endif
			::abort();
			return;
		}

		float tick_interval = total_microseconds(time_now() - m_last_tick) / 1000000.f;
		m_last_tick = time_now();

		error_code ec;
		m_timer.expires_from_now(seconds(1), ec);
		m_timer.async_wait(
			bind(&session_impl::second_tick, this, _1));

#ifdef TORRENT_STATS
		++m_second_counter;
		int downloading_torrents = 0;
		int seeding_torrents = 0;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			if (i->second->is_seed())
				++seeding_torrents;
			else
				++downloading_torrents;
		}
		int num_complete_connections = 0;
		int num_half_open = 0;
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			if ((*i)->is_connecting())
				++num_half_open;
			else
				++num_complete_connections;
		}
		
		m_stats_logger
			<< m_second_counter << "\t"
			<< m_stat.upload_rate() << "\t"
			<< m_stat.download_rate() << "\t"
			<< downloading_torrents << "\t"
			<< seeding_torrents << "\t"
			<< num_complete_connections << "\t"
			<< num_half_open << "\t"
			<< m_disk_thread.disk_allocations() << "\t"
			<< std::endl;
#endif

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
			if (m_last_tick - p->connected_time() > seconds(m_settings.handshake_timeout))
				p->disconnect("timeout: incoming connection");
		}

		// --------------------------------------------------------------
		// second_tick every torrent
		// --------------------------------------------------------------

		int congested_torrents = 0;
		int uncongested_torrents = 0;

		// count the number of seeding torrents vs. downloading
		// torrents we are running
		int num_seeds = 0;
		int num_downloads = 0;

		// count the number of peers of downloading torrents
		int num_downloads_peers = 0;

		torrent_map::iterator least_recently_scraped = m_torrents.begin();
		int num_paused_auto_managed = 0;

		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end();)
		{
			torrent& t = *i->second;
			TORRENT_ASSERT(!t.is_aborted());
			if (t.bandwidth_queue_size(peer_connection::upload_channel))
				++congested_torrents;
			else
				++uncongested_torrents;

			if (t.is_auto_managed() && t.is_paused() && !t.has_error())
			{
				++num_paused_auto_managed;
				if (!least_recently_scraped->second->is_auto_managed()
					|| !least_recently_scraped->second->is_paused()
					|| least_recently_scraped->second->last_scrape() > t.last_scrape())
				{
					least_recently_scraped = i;
				}
			}

			if (t.is_finished())
			{
				++num_seeds;
			}
			else
			{
				++num_downloads;
				num_downloads_peers += t.num_peers();
			}

			t.second_tick(m_stat, tick_interval);
			++i;
		}

		if (m_settings.rate_limit_ip_overhead)
		{
			// drain the IP overhead from the bandwidth limiters
			m_download_channel.drain(m_stat.download_ip_overhead());
			m_upload_channel.drain(m_stat.upload_ip_overhead());
		}

		m_stat.second_tick(tick_interval);

		// --------------------------------------------------------------
		// scrape paused torrents that are auto managed
		// --------------------------------------------------------------
		if (!is_paused())
		{
			--m_auto_scrape_time_scaler;
			if (m_auto_scrape_time_scaler <= 0)
			{
				m_auto_scrape_time_scaler = m_settings.auto_scrape_interval
					/ (std::max)(1, num_paused_auto_managed);
				if (m_auto_scrape_time_scaler < m_settings.auto_scrape_min_interval)
					m_auto_scrape_time_scaler = m_settings.auto_scrape_min_interval;

				if (least_recently_scraped != m_torrents.end())
				{
					least_recently_scraped->second->scrape_tracker();
				}
			}
		}

		// --------------------------------------------------------------
		// connect new peers
		// --------------------------------------------------------------

		// let torrents connect to peers if they want to
		// if there are any torrents and any free slots

		// this loop will "hand out" max(connection_speed
		// , half_open.free_slots()) to the torrents, in a
		// round robin fashion, so that every torrent is
		// equallt likely to connect to a peer

		int free_slots = m_half_open.free_slots();
		if (!m_torrents.empty()
			&& free_slots > -m_half_open.limit()
			&& num_connections() < m_max_connections
			&& !m_abort)
		{
			// this is the maximum number of connections we will
			// attempt this tick
			int max_connections = m_settings.connection_speed;
			int average_peers = 0;
			if (num_downloads > 0)
				average_peers = num_downloads_peers / num_downloads;

			torrent_map::iterator i = m_torrents.begin();
			if (m_next_connect_torrent < int(m_torrents.size()))
				std::advance(i, m_next_connect_torrent);
			else
				m_next_connect_torrent = 0;
			int steps_since_last_connect = 0;
			int num_torrents = int(m_torrents.size());
			for (;;)
			{
				torrent& t = *i->second;
				if (t.want_more_peers())
				{
					int connect_points = 100;
					// have a bias against torrents with more peers
					// than average
					if (!t.is_seed() && t.num_peers() > average_peers)
						connect_points /= 2;
					// if this is a seed and there is a torrent that
					// is downloading, lower the rate at which this
					// torrent gets connections.
					// dividing by num_seeds will have the effect
					// that all seed will get as many connections
					// together, as a single downloading torrent.
					if (t.is_seed() && num_downloads > 0)
						connect_points /= num_seeds + 1;
					if (connect_points <= 0) connect_points = 1;
					t.give_connect_points(connect_points);
					try
					{
						if (t.try_connect_peer())
						{
							--max_connections;
							--free_slots;
							steps_since_last_connect = 0;
						}
					}
					catch (std::bad_alloc&)
					{
						// we ran out of memory trying to connect to a peer
						// lower the global limit to the number of peers
						// we already have
						m_max_connections = num_connections();
						if (m_max_connections < 2) m_max_connections = 2;
					}
				}
				++m_next_connect_torrent;
				++steps_since_last_connect;
				++i;
				if (i == m_torrents.end())
				{
					TORRENT_ASSERT(m_next_connect_torrent == num_torrents);
					i = m_torrents.begin();
					m_next_connect_torrent = 0;
				}
				// if we have gone two whole loops without
				// handing out a single connection, break
				if (steps_since_last_connect > num_torrents * 2) break;
				// if there are no more free connection slots, abort
				if (free_slots <= -m_half_open.limit()) break;
				// if we should not make any more connections
				// attempts this tick, abort
				if (max_connections == 0) break;
				// maintain the global limit on number of connections
				if (num_connections() >= m_max_connections) break;
			}
		}

		// --------------------------------------------------------------
		// auto managed torrent
		// --------------------------------------------------------------
		m_auto_manage_time_scaler--;
		if (m_auto_manage_time_scaler <= 0)
		{
			m_auto_manage_time_scaler = settings().auto_manage_interval;
			recalculate_auto_managed_torrents();
		}

		// --------------------------------------------------------------
		// unchoke set and optimistic unchoke calculations
		// --------------------------------------------------------------
		m_unchoke_time_scaler--;
		if (m_unchoke_time_scaler <= 0 && !m_connections.empty())
		{
			m_unchoke_time_scaler = settings().unchoke_interval;
			recalculate_unchoke_slots(congested_torrents
				, uncongested_torrents);
		}

		// --------------------------------------------------------------
		// disconnect peers when we have too many
		// --------------------------------------------------------------
		--m_disconnect_time_scaler;
		if (m_disconnect_time_scaler <= 0)
		{
			m_disconnect_time_scaler = 90;

			// every 90 seconds, disconnect the worst peers
			// if we have reached the connection limit
			if (num_connections() >= max_connections() * m_settings.peer_turnover_cutoff
				&& !m_torrents.empty())
			{
				torrent_map::iterator i = std::max_element(m_torrents.begin(), m_torrents.end()
					, bind(&torrent::num_peers, bind(&torrent_map::value_type::second, _1))
					< bind(&torrent::num_peers, bind(&torrent_map::value_type::second, _2)));
			
				TORRENT_ASSERT(i != m_torrents.end());
				int peers_to_disconnect = (std::min)((std::max)(int(i->second->num_peers()
					* m_settings.peer_turnover), 1)
					, i->second->get_policy().num_connect_candidates());
				i->second->disconnect_peers(peers_to_disconnect);
			}
			else
			{
				// if we haven't reached the global max. see if any torrent
				// has reached its local limit
				for (torrent_map::iterator i = m_torrents.begin()
					, end(m_torrents.end()); i != end; ++i)
				{
					boost::shared_ptr<torrent> t = i->second;
					if (t->num_peers() < t->max_connections() * m_settings.peer_turnover_cutoff)
						continue;

					int peers_to_disconnect = (std::min)((std::max)(int(i->second->num_peers()
						* m_settings.peer_turnover), 1)
						, i->second->get_policy().num_connect_candidates());
					t->disconnect_peers(peers_to_disconnect);
				}
			}
		}
	}

	namespace
	{
		bool is_active(torrent* t, session_settings const& s)
		{
			return !(s.dont_count_slow_torrents
				&& t->statistics().upload_payload_rate() == 0.f
				&& t->statistics().download_payload_rate() == 0.f
				&& time_now() - t->started() > seconds(s.auto_manage_startup));
		}
	}

	void session_impl::recalculate_auto_managed_torrents()
	{
		// these vectors are filled with auto managed torrents
		std::vector<torrent*> downloaders;
		downloaders.reserve(m_torrents.size());
		std::vector<torrent*> seeds;
		seeds.reserve(m_torrents.size());

		// these counters are set to the number of torrents
		// of each kind we're allowed to have active
		int num_downloaders = settings().active_downloads;
		int num_seeds = settings().active_seeds;
		int hard_limit = settings().active_limit;

		if (num_downloaders == -1)
			num_downloaders = (std::numeric_limits<int>::max)();
		if (num_seeds == -1)
			num_seeds = (std::numeric_limits<int>::max)();
		if (hard_limit == -1)
			hard_limit = (std::numeric_limits<int>::max)();
            
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			torrent* t = i->second.get();
			TORRENT_ASSERT(t);
			if (t->is_auto_managed() && !t->has_error())
			{
				// this torrent is auto managed, add it to
				// the list (depending on if it's a seed or not)
				if (t->is_finished())
					seeds.push_back(t);
				else
					downloaders.push_back(t);
			}
			else if (!t->is_paused())
			{
				--hard_limit;
			  	if (is_active(t, settings()))
				{
					// this is not an auto managed torrent,
					// if it's running and active, decrease the
					// counters.
					--num_downloaders;
					--num_seeds;
				}
			}
		}

		bool handled_by_extension = false;

#ifndef TORRENT_DISABLE_EXTENSIONS
		// TODO: allow extensions to sort torrents for queuing
#endif

		if (!handled_by_extension)
		{
			std::sort(downloaders.begin(), downloaders.end()
				, bind(&torrent::sequence_number, _1) < bind(&torrent::sequence_number, _2));

			std::sort(seeds.begin(), seeds.end()
				, bind(&torrent::seed_rank, _1, boost::ref(m_settings))
				> bind(&torrent::seed_rank, _2, boost::ref(m_settings)));
		}

		int total_running = 0;
		for (std::vector<torrent*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i)
		{
			torrent* t = *i;
			if (!t->is_paused() && !is_active(t, settings())
				&& hard_limit > 0)
			{
				--hard_limit;
				++total_running;
				continue;
			}

			if (num_downloaders > 0 && hard_limit > 0)
			{
				--hard_limit;
				++total_running;
				--num_downloaders;
				if (t->is_paused()) t->resume();
			}
			else
			{
				if (!t->is_paused()) t->pause();
			}
		}

		for (std::vector<torrent*>::iterator i = seeds.begin()
			, end(seeds.end()); i != end; ++i)
		{
			torrent* t = *i;
			if (!t->is_paused() && !is_active(t, settings())
				&& hard_limit > 0)
			{
				--hard_limit;
				++total_running;
				continue;
			}

			if (num_seeds > 0 && hard_limit > 0)
			{
				--hard_limit;
				--num_seeds;
				++total_running;
				if (t->is_paused()) t->resume();
			}
			else
			{
				if (!t->is_paused()) t->pause();
			}
		}
	}

	void session_impl::recalculate_unchoke_slots(int congested_torrents
		, int uncongested_torrents)
	{
		std::vector<peer_connection*> peers;
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			peer_connection* p = i->get();
			TORRENT_ASSERT(p);
			torrent* t = p->associated_torrent().lock().get();
			if (!p->peer_info_struct()
				|| t == 0
				|| !p->is_peer_interested()
				|| p->is_disconnecting()
				|| p->is_connecting()
				|| (p->share_diff() < -free_upload_amount
					&& !t->is_seed()))
			{
				if (!(*i)->is_choked() && t)
				{
					policy::peer* pi = p->peer_info_struct();
					if (pi && pi->optimistically_unchoked)
					{
						pi->optimistically_unchoked = false;
						// force a new optimistic unchoke
						m_optimistic_unchoke_time_scaler = 0;
					}
					t->choke_peer(*(*i));
				}
				continue;
			}
			peers.push_back(i->get());
		}

		// sorts the peers that are eligible for unchoke by download rate and secondary
		// by total upload. The reason for this is, if all torrents are being seeded,
		// the download rate will be 0, and the peers we have sent the least to should
		// be unchoked
		std::sort(peers.begin(), peers.end()
			, bind(&peer_connection::unchoke_compare, _1, _2));

		std::for_each(m_connections.begin(), m_connections.end()
			, bind(&peer_connection::reset_choke_counters, _1));

		// auto unchoke
		int upload_limit = m_bandwidth_manager[peer_connection::upload_channel]->throttle();
		if (m_settings.auto_upload_slots && upload_limit != bandwidth_limit::inf)
		{
			// if our current upload rate is less than 90% of our 
			// limit AND most torrents are not "congested", i.e.
			// they are not holding back because of a per-torrent
			// limit
			if (m_stat.upload_rate() < upload_limit * 0.9f
				&& m_allowed_upload_slots <= m_num_unchoked + 1
				&& congested_torrents < uncongested_torrents
				&& m_upload_channel.queue_size() < 2)
			{
				++m_allowed_upload_slots;
			}
			else if (m_upload_channel.queue_size() > 1
				&& m_allowed_upload_slots > m_max_uploads)
			{
				--m_allowed_upload_slots;
			}
		}

		// reserve one upload slot for optimistic unchokes
		int unchoke_set_size = m_allowed_upload_slots - 1;

		m_num_unchoked = 0;
		// go through all the peers and unchoke the first ones and choke
		// all the other ones.
		for (std::vector<peer_connection*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_connection* p = *i;
			TORRENT_ASSERT(p);
			torrent* t = p->associated_torrent().lock().get();
			TORRENT_ASSERT(t);
			if (unchoke_set_size > 0)
			{
				if (p->is_choked())
				{
					if (!t->unchoke_peer(*p))
						continue;
				}

				--unchoke_set_size;
				++m_num_unchoked;

				TORRENT_ASSERT(p->peer_info_struct());
				if (p->peer_info_struct()->optimistically_unchoked)
				{
					// force a new optimistic unchoke
					// since this one just got promoted into the
					// proper unchoke set
					m_optimistic_unchoke_time_scaler = 0;
					p->peer_info_struct()->optimistically_unchoked = false;
				}
			}
			else
			{
				TORRENT_ASSERT(p->peer_info_struct());
				if (!p->is_choked() && !p->peer_info_struct()->optimistically_unchoked)
					t->choke_peer(*p);
				if (!p->is_choked())
					++m_num_unchoked;
			}
		}

		if (m_allowed_upload_slots > 0)
		{
			m_optimistic_unchoke_time_scaler--;
			if (m_optimistic_unchoke_time_scaler <= 0)
			{
				m_optimistic_unchoke_time_scaler
					= settings().optimistic_unchoke_multiplier;

				// find the peer that has been waiting the longest to be optimistically
				// unchoked
				connection_map::iterator current_optimistic_unchoke = m_connections.end();
				connection_map::iterator optimistic_unchoke_candidate = m_connections.end();
				ptime last_unchoke = max_time();

				for (connection_map::iterator i = m_connections.begin()
					, end(m_connections.end()); i != end; ++i)
				{
					peer_connection* p = i->get();
					TORRENT_ASSERT(p);
					policy::peer* pi = p->peer_info_struct();
					if (!pi) continue;
					torrent* t = p->associated_torrent().lock().get();
					if (!t) continue;

					if (pi->optimistically_unchoked)
					{
						TORRENT_ASSERT(!p->is_choked());
						TORRENT_ASSERT(current_optimistic_unchoke == m_connections.end());
						current_optimistic_unchoke = i;
					}

					if (pi->last_optimistically_unchoked < last_unchoke
						&& !p->is_connecting()
						&& !p->is_disconnecting()
						&& p->is_peer_interested()
						&& t->free_upload_slots()
						&& p->is_choked()
						&& t->valid_metadata())
					{
						last_unchoke = pi->last_optimistically_unchoked;
						optimistic_unchoke_candidate = i;
					}
				}

				if (optimistic_unchoke_candidate != m_connections.end()
					&& optimistic_unchoke_candidate != current_optimistic_unchoke)
				{
					if (current_optimistic_unchoke != m_connections.end())
					{
						torrent* t = (*current_optimistic_unchoke)->associated_torrent().lock().get();
						TORRENT_ASSERT(t);
						(*current_optimistic_unchoke)->peer_info_struct()->optimistically_unchoked = false;
						t->choke_peer(*current_optimistic_unchoke->get());
					}
					else
					{
						++m_num_unchoked;
					}

					torrent* t = (*optimistic_unchoke_candidate)->associated_torrent().lock().get();
					TORRENT_ASSERT(t);
					bool ret = t->unchoke_peer(*optimistic_unchoke_candidate->get());
					TORRENT_ASSERT(ret);
					(*optimistic_unchoke_candidate)->peer_info_struct()->optimistically_unchoked = true;
				}
			}
		}
	}

	void session_impl::operator()()
	{
		eh_initializer();

		{
			session_impl::mutex_t::scoped_lock l(m_mutex);
			if (m_listen_interface.port() != 0) open_listen_port();
		}

		do
		{
			error_code ec;
			m_io_service.run(ec);
			TORRENT_ASSERT(m_abort == true);
			if (ec)
			{
#ifdef TORRENT_DEBUG
				std::cerr << ec.message() << "\n";
				std::string err = ec.message();
#endif
				TORRENT_ASSERT(false);
			}
			m_io_service.reset();
		}
		while (!m_abort);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " locking mutex\n";
#endif

		session_impl::mutex_t::scoped_lock l(m_mutex);
/*
#ifdef TORRENT_DEBUG
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			TORRENT_ASSERT(i->second->num_peers() == 0);
		}
#endif
*/
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " cleaning up torrents\n";
#endif
		m_torrents.clear();

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());
	}


	// the return value from this function is valid only as long as the
	// session is locked!
	boost::weak_ptr<torrent> session_impl::find_torrent(sha1_hash const& info_hash)
	{
		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i
			= m_torrents.find(info_hash);
#ifdef TORRENT_DEBUG
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			torrent* p = boost::get_pointer(j->second);
			TORRENT_ASSERT(p);
		}
#endif
		if (i != m_torrents.end()) return i->second;
		return boost::weak_ptr<torrent>();
	}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	boost::shared_ptr<logger> session_impl::create_log(std::string const& name
		, int instance, bool append)
	{
		// current options are file_logger, cout_logger and null_logger
		return boost::shared_ptr<logger>(new logger(m_logpath, name + ".log", instance, append));
	}
#endif

	std::vector<torrent_handle> session_impl::get_torrents()
	{
		mutex_t::scoped_lock l(m_mutex);
		std::vector<torrent_handle> ret;

		for (session_impl::torrent_map::iterator i
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

	torrent_handle session_impl::add_torrent(add_torrent_params const& params)
	{
		TORRENT_ASSERT(!params.save_path.empty());

		if (params.ti && params.ti->num_files() == 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw std::runtime_error("no files in torrent");
#else
			return torrent_handle();
#endif
		}

		// lock the session and the checker thread (the order is important!)
		mutex_t::scoped_lock l(m_mutex);

//		INVARIANT_CHECK;

		if (is_aborted())
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw std::runtime_error("session is closing");
#else
			return torrent_handle();
#endif
		}
		
		// figure out the info hash of the torrent
		sha1_hash const* ih = 0;
		if (params.ti) ih = &params.ti->info_hash();
		else ih = &params.info_hash;

		// is the torrent already active?
		boost::shared_ptr<torrent> torrent_ptr = find_torrent(*ih).lock();
		if (torrent_ptr)
		{
			if (!params.duplicate_is_error)
				return torrent_handle(torrent_ptr);

#ifndef BOOST_NO_EXCEPTIONS
			throw duplicate_torrent();
#else
			return torrent_handle();
#endif
		}

		int queue_pos = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			int pos = i->second->queue_position();
			if (pos >= queue_pos) queue_pos = pos + 1;
		}

		// create the torrent and the data associated with
		// the checker thread and store it before starting
		// the thread
		if (params.ti)
		{
			torrent_ptr.reset(new torrent(*this, params.ti, params.save_path
				, m_listen_interface, params.storage_mode, 16 * 1024
				, params.storage, params.paused, params.resume_data
				, queue_pos, params.auto_managed));
		}
		else
		{
			torrent_ptr.reset(new torrent(*this, params.tracker_url, *ih, params.name
				, params.save_path, m_listen_interface, params.storage_mode, 16 * 1024
				, params.storage, params.paused, params.resume_data
				, queue_pos, params.auto_managed));
		}
		torrent_ptr->start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<torrent_plugin> tp((*i)(torrent_ptr.get(), params.userdata));
			if (tp) torrent_ptr->add_extension(tp);
		}
#endif

#ifndef TORRENT_DISABLE_DHT
		if (m_dht && params.ti)
		{
			torrent_info::nodes_t const& nodes = params.ti->nodes();
			std::for_each(nodes.begin(), nodes.end(), bind(
				(void(dht::dht_tracker::*)(std::pair<std::string, int> const&))
				&dht::dht_tracker::add_node
				, boost::ref(m_dht), _1));
		}
#endif

		m_torrents.insert(std::make_pair(*ih, torrent_ptr));

		// if this is an auto managed torrent, force a recalculation
		// of which torrents to have active
		if (params.auto_managed && m_auto_manage_time_scaler > 2)
			m_auto_manage_time_scaler = 2;

		return torrent_handle(torrent_ptr);
	}

	void session_impl::check_torrent(boost::shared_ptr<torrent> const& t)
	{
		if (m_abort) return;
		TORRENT_ASSERT(t->should_check_files());
		TORRENT_ASSERT(t->state() != torrent_status::checking_files);
		if (m_queued_for_checking.empty()) t->start_checking();
		else t->set_state(torrent_status::queued_for_checking);
		TORRENT_ASSERT(std::find(m_queued_for_checking.begin()
			, m_queued_for_checking.end(), t) == m_queued_for_checking.end());
		m_queued_for_checking.push_back(t);
	}

	void session_impl::done_checking(boost::shared_ptr<torrent> const& t)
	{
		INVARIANT_CHECK;

		if (m_queued_for_checking.empty()) return;
		boost::shared_ptr<torrent> next_check = *m_queued_for_checking.begin();
		check_queue_t::iterator done = m_queued_for_checking.end();
		for (check_queue_t::iterator i = m_queued_for_checking.begin()
			, end(m_queued_for_checking.end()); i != end; ++i)
		{
			TORRENT_ASSERT(*i == t || (*i)->should_check_files());
			if (*i == t) done = i;
			if (next_check == t || next_check->queue_position() > (*i)->queue_position())
				next_check = *i;
		}
		// only start a new one if we removed the one that is checking
		if (done == m_queued_for_checking.end()) return;

		if (next_check != t && t->state() == torrent_status::checking_files)
			next_check->start_checking();
		m_queued_for_checking.erase(done);
	}

	void session_impl::remove_torrent(const torrent_handle& h, int options)
	{
		boost::shared_ptr<torrent> tptr = h.m_torrent.lock();
		if (!tptr)
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw invalid_handle();
#endif

		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		session_impl::torrent_map::iterator i =
			m_torrents.find(tptr->torrent_file().info_hash());

		if (i != m_torrents.end())
		{
			torrent& t = *i->second;
			if (options & session::delete_files)
				t.delete_files();
			t.abort();

#ifdef TORRENT_DEBUG
			sha1_hash i_hash = t.torrent_file().info_hash();
#endif
			t.set_queue_position(-1);
			m_torrents.erase(i);
			std::list<boost::shared_ptr<torrent> >::iterator k
				= std::find(m_queued_for_checking.begin(), m_queued_for_checking.end(), tptr);
			if (k != m_queued_for_checking.end()) m_queued_for_checking.erase(k);
			TORRENT_ASSERT(m_torrents.find(i_hash) == m_torrents.end());
			return;
		}
	}

	bool session_impl::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		tcp::endpoint new_interface;
		if (net_interface && std::strlen(net_interface) > 0)
		{
			error_code ec;
			new_interface = tcp::endpoint(address::from_string(net_interface, ec), port_range.first);
			if (ec)
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_logger) << time_now_string() << "listen_on: " << net_interface
					<< " failed: " << ec.message() << "\n";
#endif
				return false;
			}
		}
		else
			new_interface = tcp::endpoint(address_v4::any(), port_range.first);

		m_listen_port_retries = port_range.second - port_range.first;

		// if the interface is the same and the socket is open
		// don't do anything
		if (new_interface == m_listen_interface
			&& !m_listen_sockets.empty()) return true;

		m_listen_interface = new_interface;

		open_listen_port();

		bool new_listen_address = m_listen_interface.address() != new_interface.address();

#ifndef TORRENT_DISABLE_DHT
		if ((new_listen_address || m_dht_same_port) && m_dht)
		{
			if (m_dht_same_port)
				m_dht_settings.service_port = new_interface.port();
			// the listen interface changed, rebind the dht listen socket as well
			m_dht_socket.bind(m_dht_settings.service_port);
			if (m_natpmp.get())
			{
				if (m_udp_mapping[0] != -1) m_natpmp->delete_mapping(m_udp_mapping[0]);
				m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
					, m_dht_settings.service_port
					, m_dht_settings.service_port);
			}
			if (m_upnp.get())
			{
				if (m_udp_mapping[1] != -1) m_upnp->delete_mapping(m_udp_mapping[1]);
				m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
					, m_dht_settings.service_port
					, m_dht_settings.service_port);
			}
		}
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		m_logger = create_log("main_session", listen_port(), false);
		(*m_logger) << time_now_string() << "\n";
#endif

		return !m_listen_sockets.empty();
	}

	unsigned short session_impl::listen_port() const
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_listen_sockets.empty()) return 0;
		return m_listen_sockets.front().external_port;;
	}

	void session_impl::announce_lsd(sha1_hash const& ih)
	{
		mutex_t::scoped_lock l(m_mutex);
		// use internal listen port for local peers
		if (m_lsd.get())
			m_lsd->announce(ih, m_listen_interface.port());
	}

	void session_impl::on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = find_torrent(ih).lock();
		if (!t) return;
		// don't add peers from lsd to private torrents
		if (t->torrent_file().priv()) return;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string()
			<< ": added peer from local discovery: " << peer << "\n";
#endif
		t->get_policy().peer_from_tracker(peer, peer_id(0), peer_info::lsd, 0);
	}

	void session_impl::on_port_mapping(int mapping, int port
		, std::string const& errmsg, int map_transport)
	{
#ifndef TORRENT_DISABLE_DHT
		if (mapping == m_udp_mapping[map_transport] && port != 0)
		{
			m_external_udp_port = port;
			m_dht_settings.service_port = port;
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
			return;
		}
#endif

		if (mapping == m_tcp_mapping[map_transport] && port != 0)
		{
			if (!m_listen_sockets.empty())
				m_listen_sockets.front().external_port = port;
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
			return;
		}

		if (!errmsg.empty())
		{
			if (m_alerts.should_post<portmap_error_alert>())
				m_alerts.post_alert(portmap_error_alert(mapping
					, map_transport, errmsg));
		}
		else
		{
			if (m_alerts.should_post<portmap_alert>())
				m_alerts.post_alert(portmap_alert(mapping, port
					, map_transport));
		}
	}

	session_status session_impl::status() const
	{
		mutex_t::scoped_lock l(m_mutex);

//		INVARIANT_CHECK;

		session_status s;

		s.num_peers = (int)m_connections.size();
		s.num_unchoked = m_num_unchoked;
		s.allowed_upload_slots = m_allowed_upload_slots;

		s.total_redundant_bytes = m_total_redundant_bytes;
		s.total_failed_bytes = m_total_failed_bytes;

		s.up_bandwidth_queue = m_upload_channel.queue_size();
		s.down_bandwidth_queue = m_download_channel.queue_size();

		s.has_incoming_connections = m_incoming_connection;

		s.download_rate = m_stat.download_rate();
		s.upload_rate = m_stat.upload_rate();

		s.payload_download_rate = m_stat.download_payload_rate();
		s.payload_upload_rate = m_stat.upload_payload_rate();

		s.total_download = m_stat.total_protocol_download()
			+ m_stat.total_payload_download();

		s.total_upload = m_stat.total_protocol_upload()
			+ m_stat.total_payload_upload();

		s.total_payload_download = m_stat.total_payload_download();
		s.total_payload_upload = m_stat.total_payload_upload();

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			m_dht->dht_status(s);
		}
		else
		{
			s.dht_nodes = 0;
			s.dht_node_cache = 0;
			s.dht_torrents = 0;
			s.dht_global_nodes = 0;
		}
#endif

		return s;
	}

#ifndef TORRENT_DISABLE_DHT

	void session_impl::start_dht(entry const& startup_state)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (m_dht)
		{
			m_dht->stop();
			m_dht = 0;
		}
		if (m_dht_settings.service_port == 0
			|| m_dht_same_port)
		{
			m_dht_same_port = true;
			if (m_listen_interface.port() > 0)
				m_dht_settings.service_port = m_listen_interface.port();
			else
				m_dht_settings.service_port = 45000 + (rand() % 10000);
		}
		m_external_udp_port = m_dht_settings.service_port;
		if (m_natpmp.get() && m_udp_mapping[0] == -1)
		{
			m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
				, m_dht_settings.service_port
				, m_dht_settings.service_port);
		}
		if (m_upnp.get() && m_udp_mapping[1] == -1)
		{
			m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
				, m_dht_settings.service_port
				, m_dht_settings.service_port);
		}
		m_dht = new dht::dht_tracker(m_dht_socket, m_dht_settings, &startup_state);
		if (!m_dht_socket.is_open() || m_dht_socket.local_port() != m_dht_settings.service_port)
		{
			m_dht_socket.bind(m_dht_settings.service_port);
		}

		for (std::list<std::pair<std::string, int> >::iterator i = m_dht_router_nodes.begin()
			, end(m_dht_router_nodes.end()); i != end; ++i)
		{
			m_dht->add_router_node(*i);
		}
		std::list<std::pair<std::string, int> >().swap(m_dht_router_nodes);

		m_dht->start(startup_state);
	}

	void session_impl::stop_dht()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (!m_dht) return;
		m_dht->stop();
		m_dht = 0;
	}

	void session_impl::set_dht_settings(dht_settings const& settings)
	{
		mutex_t::scoped_lock l(m_mutex);
		// only change the dht listen port in case the settings
		// contains a vaiid port, and if it is different from
		// the current setting
		if (settings.service_port != 0)
			m_dht_same_port = false;
		else
			m_dht_same_port = true;
		if (!m_dht_same_port
			&& settings.service_port != m_dht_settings.service_port
			&& m_dht)
		{
			m_dht_socket.bind(settings.service_port);

			if (m_natpmp.get())
			{
				if (m_udp_mapping[0] != -1) m_upnp->delete_mapping(m_udp_mapping[0]);
				m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
					, m_dht_settings.service_port
					, m_dht_settings.service_port);
			}
			if (m_upnp.get())
			{
				if (m_udp_mapping[1] != -1) m_upnp->delete_mapping(m_udp_mapping[1]);
				m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
					, m_dht_settings.service_port
					, m_dht_settings.service_port);
			}
			m_external_udp_port = settings.service_port;
		}
		m_dht_settings = settings;
		if (m_dht_same_port)
			m_dht_settings.service_port = m_listen_interface.port();
	}

	void session_impl::dht_state_callback(boost::condition& c
		, entry& e, bool& done) const
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_dht) e = m_dht->state();
		done = true;
		c.notify_all();
	}

	entry session_impl::dht_state() const
	{
		boost::condition cond;
		mutex_t::scoped_lock l(m_mutex);
		if (!m_dht) return entry();
		entry e;
		bool done = false;
		m_io_service.post(boost::bind(&session_impl::dht_state_callback
			, this, boost::ref(cond), boost::ref(e), boost::ref(done)));
		while (!done) cond.wait(l);
		return e;
	}

	void session_impl::add_dht_node(std::pair<std::string, int> const& node)
	{
		TORRENT_ASSERT(m_dht);
		mutex_t::scoped_lock l(m_mutex);
		m_dht->add_node(node);
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		// router nodes should be added before the DHT is started (and bootstrapped)
		mutex_t::scoped_lock l(m_mutex);
		if (m_dht) m_dht->add_router_node(node);
		else m_dht_router_nodes.push_back(node);
	}

#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session_impl::set_pe_settings(pe_settings const& settings)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_pe_settings = settings;
	}
#endif

	bool session_impl::is_listening() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return !m_listen_sockets.empty();
	}

	session_impl::~session_impl()
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << "\n\n *** shutting down session *** \n\n";
#endif
		abort();
		TORRENT_ASSERT(m_connections.empty());

		// we need to wait for the disk-io thread to
		// die first, to make sure it won't post any
		// more messages to the io_service containing references
		// to disk_io_pool inside the disk_io_thread. Once
		// the main thread has handled all the outstanding requests
		// we know it's safe to destruct the disk thread.
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " waiting for disk io thread\n";
#endif
		m_disk_thread.join();

#ifndef TORRENT_DISABLE_GEO_IP
		if (m_asnum_db) GeoIP_delete(m_asnum_db);
		if (m_country_db) GeoIP_delete(m_country_db);
#endif
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " waiting for main thread\n";
#endif
		m_thread->join();

		TORRENT_ASSERT(m_torrents.empty());
		TORRENT_ASSERT(m_connections.empty());
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " shutdown complete!\n";
#endif
		TORRENT_ASSERT(m_connections.empty());
	}

	void session_impl::set_max_uploads(int limit)
	{
		TORRENT_ASSERT(limit >= 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		if (m_max_uploads == limit) return;
		m_max_uploads = limit;
		m_allowed_upload_slots = limit;
	}

	void session_impl::set_max_connections(int limit)
	{
		TORRENT_ASSERT(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0)
		{
			limit = (std::numeric_limits<int>::max)();
#ifndef TORRENT_WINDOWS
			rlimit l;
			if (getrlimit(RLIMIT_NOFILE, &l) == 0
				&& l.rlim_cur != RLIM_INFINITY)
			{
				limit = l.rlim_cur - m_settings.file_pool_size;
				if (limit < 5) limit = 5;
			}
#endif
		}
		m_max_connections = limit;
	}

	void session_impl::set_max_half_open_connections(int limit)
	{
		TORRENT_ASSERT(limit > 0 || limit == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_half_open.limit(limit);
	}

	void session_impl::set_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASSERT(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = bandwidth_limit::inf;
		m_bandwidth_manager[peer_connection::download_channel]->throttle(bytes_per_second);
	}

	void session_impl::set_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASSERT(bytes_per_second > 0 || bytes_per_second == -1);
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = bandwidth_limit::inf;
		m_bandwidth_manager[peer_connection::upload_channel]->throttle(bytes_per_second);
	}

	std::auto_ptr<alert> session_impl::pop_alert()
	{
		mutex_t::scoped_lock l(m_mutex);

// too expensive
//		INVARIANT_CHECK;

		if (m_alerts.pending())
			return m_alerts.get();
		return std::auto_ptr<alert>(0);
	}
	
	alert const* session_impl::wait_for_alert(time_duration max_wait)
	{
		return m_alerts.wait_for_alert(max_wait);
	}

	void session_impl::set_alert_mask(int m)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_alerts.set_alert_mask(m);
	}

	size_t session_impl::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_alerts.set_alert_queue_size_limit(queue_size_limit_);
	}

	int session_impl::upload_rate_limit() const
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		int ret = m_bandwidth_manager[peer_connection::upload_channel]->throttle();
		return ret == (std::numeric_limits<int>::max)() ? -1 : ret;
	}

	int session_impl::download_rate_limit() const
	{
		mutex_t::scoped_lock l(m_mutex);
		int ret = m_bandwidth_manager[peer_connection::download_channel]->throttle();
		return ret == (std::numeric_limits<int>::max)() ? -1 : ret;
	}

	void session_impl::start_lsd()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (m_lsd) return;

		m_lsd = new lsd(m_io_service
			, m_listen_interface.address()
			, bind(&session_impl::on_lsd_peer, this, _1, _2));
	}
	
	natpmp* session_impl::start_natpmp()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (m_natpmp) return m_natpmp.get();

		m_natpmp = new natpmp(m_io_service
			, m_listen_interface.address()
			, bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, 0));

		if (m_listen_interface.port() > 0)
		{
			m_tcp_mapping[0] = m_natpmp->add_mapping(natpmp::tcp
				, m_listen_interface.port(), m_listen_interface.port());
		}
#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp
				, m_dht_settings.service_port 
				, m_dht_settings.service_port);
#endif
		return m_natpmp.get();
	}

	upnp* session_impl::start_upnp()
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		if (m_upnp) return m_upnp.get();

		m_upnp = new upnp(m_io_service, m_half_open
			, m_listen_interface.address()
			, m_settings.user_agent
			, bind(&session_impl::on_port_mapping
				, this, _1, _2, _3, 1)
			, m_settings.upnp_ignore_nonrouters);

		m_upnp->discover_device();
		if (m_listen_interface.port() > 0)
		{
			m_tcp_mapping[1] = m_upnp->add_mapping(upnp::tcp
				, m_listen_interface.port(), m_listen_interface.port());
		}
#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
			m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp
				, m_dht_settings.service_port 
				, m_dht_settings.service_port);
#endif
		return m_upnp.get();
	}

	void session_impl::stop_lsd()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_lsd.get())
			m_lsd->close();
		m_lsd = 0;
	}
	
	void session_impl::stop_natpmp()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_natpmp.get())
			m_natpmp->close();
		m_natpmp = 0;
	}
	
	void session_impl::stop_upnp()
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_upnp.get())
		{
			m_upnp->close();
			m_udp_mapping[1] = -1;
			m_tcp_mapping[1] = -1;
		}
		m_upnp = 0;
	}
	
	void session_impl::set_external_address(address const& ip)
	{
		TORRENT_ASSERT(ip != address());

		if (is_local(ip)) return;
		if (is_loopback(ip)) return;
		if (m_external_address == ip) return;

		m_external_address = ip;
		if (m_alerts.should_post<external_ip_alert>())
			m_alerts.post_alert(external_ip_alert(ip));
	}

	void session_impl::free_disk_buffer(char* buf)
	{
		m_disk_thread.free_buffer(buf);
	}

	char* session_impl::allocate_disk_buffer()
	{
		return m_disk_thread.allocate_buffer();
	}
	
	std::pair<char*, int> session_impl::allocate_buffer(int size)
	{
		TORRENT_ASSERT(size > 0);
		int num_buffers = (size + send_buffer_size - 1) / send_buffer_size;
		TORRENT_ASSERT(num_buffers > 0);

		boost::mutex::scoped_lock l(m_send_buffer_mutex);
#ifdef TORRENT_STATS
		TORRENT_ASSERT(m_buffer_allocations >= 0);
		m_buffer_allocations += num_buffers;
		m_buffer_usage_logger << log_time() << " protocol_buffer: "
			<< (m_buffer_allocations * send_buffer_size) << std::endl;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		int num_bytes = num_buffers * send_buffer_size;
		return std::make_pair((char*)malloc(num_bytes), num_bytes);
#else
		return std::make_pair((char*)m_send_buffers.ordered_malloc(num_buffers)
			, num_buffers * send_buffer_size);
#endif
	}

#ifdef TORRENT_STATS
	void session_impl::log_buffer_usage()
	{
		int send_buffer_capacity = 0;
		int used_send_buffer = 0;
		for (connection_map::const_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			send_buffer_capacity += (*i)->send_buffer_capacity();
			used_send_buffer += (*i)->send_buffer_size();
		}
		TORRENT_ASSERT(send_buffer_capacity >= used_send_buffer);
		m_buffer_usage_logger << log_time() << " send_buffer_size: " << send_buffer_capacity << std::endl;
		m_buffer_usage_logger << log_time() << " used_send_buffer: " << used_send_buffer << std::endl;
		m_buffer_usage_logger << log_time() << " send_buffer_utilization: "
			<< (used_send_buffer * 100.f / send_buffer_capacity) << std::endl;
	}
#endif

	void session_impl::free_buffer(char* buf, int size)
	{
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(size % send_buffer_size == 0);
		int num_buffers = size / send_buffer_size;
		TORRENT_ASSERT(num_buffers > 0);

		boost::mutex::scoped_lock l(m_send_buffer_mutex);
#ifdef TORRENT_STATS
		m_buffer_allocations -= num_buffers;
		TORRENT_ASSERT(m_buffer_allocations >= 0);
		m_buffer_usage_logger << log_time() << " protocol_buffer: "
			<< (m_buffer_allocations * send_buffer_size) << std::endl;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		free(buf);
#else
		m_send_buffers.ordered_free(buf, num_buffers);
#endif
	}	

#ifdef TORRENT_DEBUG
	void session_impl::check_invariant() const
	{
		std::set<int> unique;
		int total_downloaders = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			int pos = i->second->queue_position();
			if (pos < 0)
			{
				TORRENT_ASSERT(pos == -1);
				continue;
			}
			++total_downloaders;
			unique.insert(i->second->queue_position());
		}
		TORRENT_ASSERT(int(unique.size()) == total_downloaders);

		TORRENT_ASSERT(m_max_connections > 0);
		TORRENT_ASSERT(m_max_uploads > 0);
		TORRENT_ASSERT(m_allowed_upload_slots >= m_max_uploads);
		int unchokes = 0;
		int num_optimistic = 0;
		for (connection_map::const_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			boost::shared_ptr<torrent> t = (*i)->associated_torrent().lock();

			peer_connection* p = i->get();
			TORRENT_ASSERT(!p->is_disconnecting());
			if (!p->is_choked()) ++unchokes;
			if (p->peer_info_struct()
				&& p->peer_info_struct()->optimistically_unchoked)
			{
				++num_optimistic;
				TORRENT_ASSERT(!p->is_choked());
			}
			if (t && p->peer_info_struct())
			{
				TORRENT_ASSERT(t->get_policy().has_connection(p));
			}
		}
		TORRENT_ASSERT(num_optimistic == 0 || num_optimistic == 1);
		if (m_num_unchoked != unchokes)
		{
			TORRENT_ASSERT(false);
		}
		for (std::map<sha1_hash, boost::shared_ptr<torrent> >::const_iterator j
			= m_torrents.begin(); j != m_torrents.end(); ++j)
		{
			TORRENT_ASSERT(boost::get_pointer(j->second));
		}
	}
#endif

}}

