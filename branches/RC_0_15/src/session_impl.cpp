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
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/limits.hpp>
#include <boost/bind.hpp>
#include <boost/function_equal.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// for logging the size of DHT structures
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
#ifndef TORRENT_DISABLE_DHT

#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/closest_nodes.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/observer.hpp>

#endif

#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"

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
#include "libtorrent/utf8.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/lsd.hpp"
#include "libtorrent/instantiate_connection.hpp"

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

	// used to cache the current time
	// every 100 ms. This is cheaper
	// than a system call and can be
	// used where more accurate time
	// is not necessary
	ptime g_current_time = time_now_hires();
}

TORRENT_EXPORT ptime const& time_now() { return aux::g_current_time; }

namespace aux {

	struct seed_random_generator
	{
		seed_random_generator()
		{
			std::srand(total_microseconds(time_now_hires() - min_time()));
		}
	};

	enum { std_string = 0, character = 1, short_integer = 2
		, integer = 3, floating_point = 4, boolean = 5};

	// this is used to map struct entries
	// to names in a bencoded dictionary to
	// save and load the struct
	struct bencode_map_entry
	{
		char const* name;
		int offset; // struct offset
		int type;
	};

#define TORRENT_SETTING(t, x) {#x, offsetof(session_settings,x), t},

	bencode_map_entry session_settings_map[] =
	{
		TORRENT_SETTING(std_string, user_agent)
		TORRENT_SETTING(integer, tracker_completion_timeout)
		TORRENT_SETTING(integer, tracker_receive_timeout)
		TORRENT_SETTING(integer, stop_tracker_timeout)
		TORRENT_SETTING(integer, tracker_maximum_response_length)
		TORRENT_SETTING(integer, piece_timeout)
		TORRENT_SETTING(integer, request_timeout)
		TORRENT_SETTING(integer, request_queue_time)
		TORRENT_SETTING(integer, max_allowed_in_request_queue)
		TORRENT_SETTING(integer, max_out_request_queue)
		TORRENT_SETTING(integer, whole_pieces_threshold)
		TORRENT_SETTING(integer, peer_timeout)
		TORRENT_SETTING(integer, urlseed_timeout)
		TORRENT_SETTING(integer, urlseed_pipeline_size)
		TORRENT_SETTING(integer, urlseed_wait_retry)
		TORRENT_SETTING(integer, file_pool_size)
		TORRENT_SETTING(boolean, allow_multiple_connections_per_ip)
		TORRENT_SETTING(integer, max_failcount)
		TORRENT_SETTING(integer, min_reconnect_time)
		TORRENT_SETTING(integer, peer_connect_timeout)
		TORRENT_SETTING(boolean, ignore_limits_on_local_network)
		TORRENT_SETTING(integer, connection_speed)
		TORRENT_SETTING(boolean, send_redundant_have)
		TORRENT_SETTING(boolean, lazy_bitfields)
		TORRENT_SETTING(integer, inactivity_timeout)
		TORRENT_SETTING(integer, unchoke_interval)
		TORRENT_SETTING(integer, optimistic_unchoke_interval)
		TORRENT_SETTING(integer, num_want)
		TORRENT_SETTING(integer, initial_picker_threshold)
		TORRENT_SETTING(integer, allowed_fast_set_size)
		TORRENT_SETTING(integer, max_queued_disk_bytes)
		TORRENT_SETTING(integer, handshake_timeout)
#ifndef TORRENT_DISABLE_DHT
		TORRENT_SETTING(boolean, use_dht_as_fallback)
#endif
		TORRENT_SETTING(boolean, free_torrent_hashes)
		TORRENT_SETTING(boolean, upnp_ignore_nonrouters)
 		TORRENT_SETTING(integer, send_buffer_watermark)
		TORRENT_SETTING(boolean, auto_upload_slots)
		TORRENT_SETTING(boolean, auto_upload_slots_rate_based)
		TORRENT_SETTING(boolean, use_parole_mode)
		TORRENT_SETTING(integer, cache_size)
		TORRENT_SETTING(integer, cache_buffer_chunk_size)
		TORRENT_SETTING(integer, cache_expiry)
		TORRENT_SETTING(boolean, use_read_cache)
		TORRENT_SETTING(integer, disk_io_write_mode)
		TORRENT_SETTING(integer, disk_io_read_mode)
		TORRENT_SETTING(boolean, coalesce_reads)
		TORRENT_SETTING(boolean, coalesce_writes)
		TORRENT_SETTING(character, peer_tos)
		TORRENT_SETTING(integer, active_downloads)
		TORRENT_SETTING(integer, active_seeds)
		TORRENT_SETTING(integer, active_limit)
		TORRENT_SETTING(boolean, auto_manage_prefer_seeds)
		TORRENT_SETTING(boolean, dont_count_slow_torrents)
		TORRENT_SETTING(integer, auto_manage_interval)
		TORRENT_SETTING(floating_point, share_ratio_limit)
		TORRENT_SETTING(floating_point, seed_time_ratio_limit)
		TORRENT_SETTING(integer, seed_time_limit)
		TORRENT_SETTING(floating_point, peer_turnover)
		TORRENT_SETTING(floating_point, peer_turnover_cutoff)
		TORRENT_SETTING(boolean, close_redundant_connections)
		TORRENT_SETTING(integer, auto_scrape_interval)
		TORRENT_SETTING(integer, auto_scrape_min_interval)
		TORRENT_SETTING(integer, max_peerlist_size)
		TORRENT_SETTING(integer, max_paused_peerlist_size)
		TORRENT_SETTING(integer, min_announce_interval)
		TORRENT_SETTING(boolean, prioritize_partial_pieces)
		TORRENT_SETTING(integer, auto_manage_startup)
		TORRENT_SETTING(boolean, rate_limit_ip_overhead)
		TORRENT_SETTING(boolean, announce_to_all_trackers)
		TORRENT_SETTING(boolean, announce_to_all_tiers)
		TORRENT_SETTING(boolean, prefer_udp_trackers)
		TORRENT_SETTING(boolean, strict_super_seeding)
		TORRENT_SETTING(integer, seeding_piece_quota)
		TORRENT_SETTING(integer, max_sparse_regions)
#ifndef TORRENT_DISABLE_MLOCK
		TORRENT_SETTING(boolean, lock_disk_cache)
#endif
		TORRENT_SETTING(integer, max_rejects)
		TORRENT_SETTING(integer, recv_socket_buffer_size)
		TORRENT_SETTING(integer, send_socket_buffer_size)
		TORRENT_SETTING(boolean, optimize_hashing_for_speed)
		TORRENT_SETTING(integer, file_checks_delay_per_block)
		TORRENT_SETTING(integer, disk_cache_algorithm)
		TORRENT_SETTING(integer, read_cache_line_size)
		TORRENT_SETTING(integer, write_cache_line_size)
		TORRENT_SETTING(integer, optimistic_disk_retry)
		TORRENT_SETTING(boolean, disable_hash_checks)
		TORRENT_SETTING(boolean, allow_reordered_disk_operations)
		TORRENT_SETTING(integer, max_suggest_pieces)
		TORRENT_SETTING(boolean, drop_skipped_requests)
		TORRENT_SETTING(boolean, low_prio_disk)
		TORRENT_SETTING(integer, local_service_announce_interval)
		TORRENT_SETTING(integer, udp_tracker_token_expiry)
		TORRENT_SETTING(boolean, report_true_downloaded)
		TORRENT_SETTING(boolean, strict_end_game_mode)
	};

#undef TORRENT_SETTING
#define TORRENT_SETTING(t, x) {#x, offsetof(proxy_settings,x), t},

	bencode_map_entry proxy_settings_map[] =
	{
		TORRENT_SETTING(std_string, hostname)
		TORRENT_SETTING(integer, port)
		TORRENT_SETTING(std_string, username)
		TORRENT_SETTING(std_string, password)
		TORRENT_SETTING(integer, type)
	};
#undef TORRENT_SETTING

#ifndef TORRENT_DISABLE_DHT
#define TORRENT_SETTING(t, x) {#x, offsetof(dht_settings,x), t},
	bencode_map_entry dht_settings_map[] =
	{
		TORRENT_SETTING(integer, max_peers_reply)
		TORRENT_SETTING(integer, search_branching)
		TORRENT_SETTING(integer, service_port)
		TORRENT_SETTING(integer, max_fail_count)
	};
#undef TORRENT_SETTING
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
#define TORRENT_SETTING(t, x) {#x, offsetof(pe_settings,x), t},
	bencode_map_entry pe_settings_map[] = 
	{
		TORRENT_SETTING(integer, out_enc_policy)
		TORRENT_SETTING(integer, in_enc_policy)
		TORRENT_SETTING(integer, allowed_enc_level)
		TORRENT_SETTING(boolean, prefer_rc4)
	};
#undef TORRENT_SETTING
#endif

	void load_struct(lazy_entry const& e, void* s, bencode_map_entry const* m, int num)
	{
		for (int i = 0; i < num; ++i)
		{
			lazy_entry const* key = e.dict_find(m[i].name);
			if (key == 0) continue;
			void* dest = ((char*)s) + m[i].offset;
			switch (m[i].type)
			{
				case std_string:
				{
					if (key->type() != lazy_entry::string_t) continue;
					*((std::string*)dest) = key->string_value();
					break;
				}
				case character:
				case boolean:
				case integer:
				case floating_point:
				{
					if (key->type() != lazy_entry::int_t) continue;
					size_type val = key->int_value();
					switch (m[i].type)
					{
						case character: *((char*)dest) = val; break;
						case integer: *((int*)dest) = val; break;
						case floating_point: *((float*)dest) = float(val) / 1000.f; break;
						case boolean: *((bool*)dest) = val; break;
					}
				}
			}
		}
	}

	void save_struct(entry& e, void const* s, bencode_map_entry const* m, int num)
	{
		for (int i = 0; i < num; ++i)
		{
			char const* key = m[i].name;
			void const* src = ((char*)s) + m[i].offset;
			entry& val = e[key];
			TORRENT_ASSERT(val.type() == entry::undefined_t);
			switch (m[i].type)
			{
				case std_string:
				{
					val = *((std::string*)src);
					break;
				}
				case character: val = *((char*)src); break;
				case integer: val = *((int*)src); break;
				case floating_point: val = size_type(*((float*)src) * 1000.f); break;
				case boolean: val = *((bool*)src); break;
			}
		}
	}

#ifdef TORRENT_STATS
	int session_impl::logging_allocator::allocations = 0;
	int session_impl::logging_allocator::allocated_bytes = 0;
#endif

	session_impl::session_impl(
		std::pair<int, int> listen_port_range
		, fingerprint const& cl_fprint
		, char const* listen_interface
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, fs::path const& logpath
#endif
		)
		: m_ipv4_peer_pool(500)
#if TORRENT_USE_IPV6
		, m_ipv6_peer_pool(500)
#endif
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_send_buffers(send_buffer_size)
#endif
		, m_files(40)
		, m_io_service()
		, m_host_resolver(m_io_service)
		, m_alerts(m_io_service)
		, m_disk_thread(m_io_service, boost::bind(&session_impl::on_disk_queue, this), m_files)
		, m_half_open(m_io_service)
		, m_download_rate(peer_connection::download_channel)
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		, m_upload_rate(peer_connection::upload_channel, true)
#else
		, m_upload_rate(peer_connection::upload_channel)
#endif
		, m_tracker_manager(*this, m_tracker_proxy)
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
		, m_created(time_now_hires())
		, m_last_tick(m_created)
		, m_last_second_tick(m_created)
		, m_last_choke(m_created)
#ifndef TORRENT_DISABLE_DHT
		, m_dht_same_port(true)
		, m_external_udp_port(0)
		, m_dht_socket(new rate_limited_udp_socket(m_io_service, boost::bind(&session_impl::on_receive_udp
			, this, _1, _2, _3, _4), m_half_open))
#endif
		, m_timer(m_io_service)
		, m_lsd_announce_timer(m_io_service)
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
		m_next_lsd_torrent = m_torrents.begin();
		m_next_connect_torrent = m_torrents.begin();

		if (!listen_interface) listen_interface = "0.0.0.0";
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
		// here's a table:

		// windows version       half-open connections limit
		// --------------------- ---------------------------
		// XP sp1 and earlier    infinite
		// earlier than vista    8
		// vista sp1 and earlier 5
		// vista sp2 and later   infinite

		// windows release                     version number
		// ----------------------------------- --------------
		// Windows 7                           6.1
		// Windows Server 2008 R2              6.1
		// Windows Server 2008                 6.0
		// Windows Vista                       6.0
		// Windows Server 2003 R2              5.2
		// Windows Home Server                 5.2
		// Windows Server 2003                 5.2
		// Windows XP Professional x64 Edition 5.2
		// Windows XP                          5.1
		// Windows 2000                        5.0

 		OSVERSIONINFOEX osv;
		memset(&osv, 0, sizeof(osv));
		osv.dwOSVersionInfoSize = sizeof(osv);
		GetVersionEx((OSVERSIONINFO*)&osv);

		// the low two bytes of windows_version is the actual
		// version.
		boost::uint32_t windows_version
			= ((osv.dwMajorVersion & 0xff) << 16)
			| ((osv.dwMinorVersion & 0xff) << 8)
			| (osv.wServicePackMajor & 0xff);

		// this is the format of windows_version
		// xx xx xx
		// |  |  |
		// |  |  + service pack version
		// |  + minor version
		// + major version

		// the least significant byte is the major version
		// and the most significant one is the minor version
		if (windows_version >= 0x060100)
		{
			// windows 7 and up doesn't have a half-open limit
			m_half_open.limit(0);
		}
		else if (windows_version >= 0x060002)
		{
			// on vista SP 2 and up, there's no limit
			m_half_open.limit(0);
		}
		else if (windows_version >= 0x060000)
		{
			// on vista the limit is 5 (in home edition)
			m_half_open.limit(4);
		}
		else if (windows_version >= 0x050102)
		{
			// on XP SP2 the limit is 10	
			m_half_open.limit(9);
		}
		else
		{
			// before XP SP2, there was no limit
			m_half_open.limit(0);
		}
#endif

		m_bandwidth_channel[peer_connection::download_channel] = &m_download_channel;
		m_bandwidth_channel[peer_connection::upload_channel] = &m_upload_channel;

#ifdef TORRENT_UPNP_LOGGING
		m_upnp_log.open("upnp.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		m_logger = create_log("main_session", listen_port(), false);
		(*m_logger) << time_now_string() << "\n";

#define PRINT_SIZEOF(x) (*m_logger) << "sizeof(" #x "): " << sizeof(x) << "\n";
#define PRINT_OFFSETOF(x, y) (*m_logger) << "  offsetof(" #x "," #y "): " << offsetof(x, y) << "\n";

		PRINT_SIZEOF(udp_socket)
		PRINT_OFFSETOF(udp_socket, m_callback)
		PRINT_OFFSETOF(udp_socket, m_mutex)
		PRINT_OFFSETOF(udp_socket, m_ipv4_sock)
		PRINT_OFFSETOF(udp_socket, m_v4_ep)
		PRINT_OFFSETOF(udp_socket, m_v4_buf)
#if TORRENT_USE_IPV6
		PRINT_OFFSETOF(udp_socket, m_ipv6_sock)
		PRINT_OFFSETOF(udp_socket, m_v6_ep)
		PRINT_OFFSETOF(udp_socket, m_v6_buf)
#endif
		PRINT_OFFSETOF(udp_socket, m_bind_port)
		PRINT_OFFSETOF(udp_socket, m_v4_outstanding)
#if TORRENT_USE_IPV6
		PRINT_OFFSETOF(udp_socket, m_v6_outstanding)
#endif
		PRINT_OFFSETOF(udp_socket, m_socks5_sock)
		PRINT_OFFSETOF(udp_socket, m_connection_ticket)
		PRINT_OFFSETOF(udp_socket, m_proxy_settings)
		PRINT_OFFSETOF(udp_socket, m_cc)
		PRINT_OFFSETOF(udp_socket, m_resolver)
		PRINT_OFFSETOF(udp_socket, m_tmp_buf)
		PRINT_OFFSETOF(udp_socket, m_queue_packets)
		PRINT_OFFSETOF(udp_socket, m_tunnel_packets)
		PRINT_OFFSETOF(udp_socket, m_abort)
		PRINT_OFFSETOF(udp_socket, m_proxy_addr)
		PRINT_OFFSETOF(udp_socket, m_queue)
#ifdef TORRENT_DEBUG
		PRINT_OFFSETOF(udp_socket, m_started)
		PRINT_OFFSETOF(udp_socket, m_magic)
		PRINT_OFFSETOF(udp_socket, m_outstanding_when_aborted)
#endif

		PRINT_SIZEOF(tracker_connection)
		PRINT_SIZEOF(http_tracker_connection)

		PRINT_SIZEOF(udp_tracker_connection)
		PRINT_OFFSETOF(udp_tracker_connection, m_man)
		PRINT_OFFSETOF(udp_tracker_connection, m_name_lookup)
		PRINT_OFFSETOF(udp_tracker_connection, m_socket)
		PRINT_OFFSETOF(udp_tracker_connection, m_target)
		PRINT_OFFSETOF(udp_tracker_connection, m_endpoints)
		PRINT_OFFSETOF(udp_tracker_connection, m_transaction_id)
		PRINT_OFFSETOF(udp_tracker_connection, m_ses)
		PRINT_OFFSETOF(udp_tracker_connection, m_attempts)
		PRINT_OFFSETOF(udp_tracker_connection, m_state)

		PRINT_SIZEOF(torrent)
		PRINT_SIZEOF(peer_connection)
		PRINT_SIZEOF(bt_peer_connection)
		PRINT_SIZEOF(address)
		PRINT_SIZEOF(address_v4)
		PRINT_SIZEOF(address_v6)
		PRINT_SIZEOF(address_v4::bytes_type)
		PRINT_SIZEOF(address_v6::bytes_type)
		PRINT_SIZEOF(void*)
#ifndef TORRENT_DISABLE_DHT
		PRINT_SIZEOF(dht::node_entry)
#endif

		PRINT_SIZEOF(policy::peer)
		PRINT_OFFSETOF(policy::peer, connection)
		PRINT_OFFSETOF(policy::peer, last_optimistically_unchoked)
		PRINT_OFFSETOF(policy::peer, last_connected)
		PRINT_OFFSETOF(policy::peer, port)
		PRINT_OFFSETOF(policy::peer, hashfails)

		PRINT_SIZEOF(policy::ipv4_peer)
#if TORRENT_USE_IPV6
		PRINT_SIZEOF(policy::ipv6_peer)
#endif

#ifndef TORRENT_DISABLE_DHT
		PRINT_SIZEOF(dht::closest_nodes_observer)
		PRINT_SIZEOF(dht::find_data_observer)
		PRINT_SIZEOF(dht::announce_observer)
		PRINT_SIZEOF(dht::refresh_observer)
		PRINT_SIZEOF(dht::ping_observer)
		PRINT_SIZEOF(dht::null_observer)
#endif

#undef PRINT_OFFSETOF
#undef PRINT_SIZEOF

#endif

#ifdef TORRENT_STATS
		m_stats_logger.open("session_stats.log", std::ios::trunc);
		m_stats_logger <<
			"second:upload rate:download rate:downloading torrents:seeding torrents"
			":peers:connecting peers:disk block buffers:unchoked peers:num list peers"
			":peer allocations:peer storage bytes\n\n";
		m_buffer_usage_logger.open("buffer_stats.log", std::ios::trunc);
		m_second_counter = 0;
		m_buffer_allocations = 0;
#endif

#if defined TORRENT_BSD || defined TORRENT_LINUX
		// ---- auto-cap open files ----

		struct rlimit rl;
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " max number of open files: " << rl.rlim_cur << "\n";
#endif

			// deduct some margin for epoll/kqueue, log files,
			// futexes, shared objects etc.
			rl.rlim_cur -= 20;

			// 80% of the available file descriptors should go
			m_max_connections = (std::min)(m_max_connections, int(rl.rlim_cur * 8 / 10));
			// 20% goes towards regular files
			m_files.resize((std::min)(m_files.size_limit(), int(rl.rlim_cur * 2 / 10)));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << "   max connections: " << m_max_connections << "\n";
			(*m_logger) << time_now_string() << "   max files: " << m_files.size_limit() << "\n";
#endif
		}
#endif // TORRENT_BSD || TORRENT_LINUX


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
		// excluding ', since some buggy trackers don't support that
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz-_.!~*()";

		// the random number
		for (unsigned char* i = m_peer_id.begin() + print.length();
			i != m_peer_id.end(); ++i)
		{
			*i = printable[rand() % (sizeof(printable)-1)];
		}

		m_io_service.post(boost::bind(&session_impl::on_tick, this, ec));

		int delay = (std::max)(m_settings.local_service_announce_interval
			/ (std::max)(int(m_torrents.size()), 1), 1);
		m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
		m_lsd_announce_timer.async_wait(
			boost::bind(&session_impl::on_lsd_announce, this, _1));

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		(*m_logger) << time_now_string() << " spawning network thread\n";
#endif
		m_thread.reset(new boost::thread(boost::ref(*this)));
	}

	void session_impl::save_state(entry& e, boost::uint32_t flags, session_impl::mutex_t::scoped_lock& l) const
	{
		if (flags & session::save_settings)
		{
			// TODO: move these to session_settings
			entry& s = e["settings"];
			s["upload_rate_limit"] = upload_rate_limit();
			s["download_rate_limit"] = download_rate_limit();
			s["local_upload_rate_limit"] = local_upload_rate_limit();
			s["local_download_rate_limit"] = local_download_rate_limit();
			s["max_uploads"] = max_uploads();
			s["max_half_open_connections"] = max_half_open_connections();
			s["max_connections"] = max_connections();
		}

		if (flags & session::save_settings)
		{
			save_struct(e["settings"], &m_settings, session_settings_map
				, sizeof(session_settings_map)/sizeof(session_settings_map[0]));
		}
#ifndef TORRENT_DISABLE_DHT
		if (flags & session::save_dht_settings)
		{
			save_struct(e["dht"], &m_dht_settings, dht_settings_map
				, sizeof(dht_settings_map)/sizeof(dht_settings_map[0]));
		}
#ifndef TORRENT_NO_DEPRECATE
		if (flags & session::save_dht_proxy)
		{
			save_struct(e["dht proxy"], &m_dht_proxy, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}
#endif

		if (m_dht && (flags & session::save_dht_state))
		{
			boost::condition cond;
			entry& state = e["dht state"];
			bool done = false;
			m_io_service.post(boost::bind(&session_impl::on_dht_state_callback
				, this, boost::ref(cond), boost::ref(state), boost::ref(done)));
			while (!done) cond.wait(l);
		}

#endif
#if TORRENT_USE_I2P
		if (flags & session::save_i2p_proxy)
		{
			save_struct(e["i2p"], &i2p_proxy(), proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}
#endif
#ifndef TORRENT_DISABLE_ENCRYPTION
		if (flags & session::save_encryption_settings)
		{
			save_struct(e["encryption"], &m_pe_settings, pe_settings_map
				, sizeof(pe_settings_map)/sizeof(pe_settings_map[0]));
		}
#endif

#ifndef TORRENT_NO_DEPRECATE
		if (flags & session::save_peer_proxy)
		{
			save_struct(e["peer proxy"], &m_peer_proxy, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}
		if (flags & session::save_web_proxy)
		{
			save_struct(e["web proxy"], &m_web_seed_proxy, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}
		if (flags & session::save_tracker_proxy)
		{
			save_struct(e["tracker proxy"], &m_tracker_proxy, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}
#endif

		if (flags & session::save_proxy)
		{
			save_struct(e["proxy"], &m_peer_proxy, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
		}

#ifndef TORRENT_DISABLE_GEO_IP
		if (flags & session::save_as_map)
		{
			entry::dictionary_type& as_map = e["AS map"].dict();
			char buf[10];
			for (std::map<int, int>::const_iterator i = m_as_peak.begin()
				, end(m_as_peak.end()); i != end; ++i)
			{
				if (i->second == 0) continue;
					sprintf(buf, "%05d", i->first);
				as_map[buf] = i->second;
			}
		}
#endif

	}
	
	void session_impl::set_proxy(proxy_settings const& s)
	{
		m_peer_proxy = s;
		// in case we just set a socks proxy, we might have to
		// open the socks incoming connection
		if (!m_socks_listen_socket) open_new_incoming_socks_connection();
		m_web_seed_proxy = s;
		m_tracker_proxy = s;
#ifndef TORRENT_DISABLE_DHT
		m_dht_proxy = s;
		m_dht_socket->set_proxy_settings(s);
#endif
	}

	void session_impl::load_state(lazy_entry const& e)
	{
		lazy_entry const* settings;
	  
		if (e.type() != lazy_entry::dict_t) return;

		settings = e.dict_find_dict("settings");
		if (settings)
		{
			set_upload_rate_limit(settings->dict_find_int_value("upload_rate_limit"
				, upload_rate_limit()));
			set_download_rate_limit(settings->dict_find_int_value("download_rate_limit"
				, download_rate_limit()));
			set_local_upload_rate_limit(settings->dict_find_int_value("local_upload_rate_limit"
				, local_upload_rate_limit()));
			set_local_download_rate_limit(settings->dict_find_int_value("local_download_rate_limit"
				, local_download_rate_limit()));
			set_max_uploads(settings->dict_find_int_value("max_uploads", max_uploads()));
			set_max_half_open_connections(settings->dict_find_int_value("max_half_open_connections"
				, max_half_open_connections()));
			set_max_connections(settings->dict_find_int_value("max_connections", max_connections()));

			session_settings s;
			load_struct(*settings, &s, session_settings_map
				, sizeof(session_settings_map)/sizeof(session_settings_map[0]));
			set_settings(s);
		}

#ifndef TORRENT_DISABLE_DHT
		settings = e.dict_find_dict("dht");
		if (settings)
		{
			dht_settings s;
			load_struct(*settings, &s, dht_settings_map
				, sizeof(dht_settings_map)/sizeof(dht_settings_map[0]));
			set_dht_settings(s);
		}

		settings = e.dict_find_dict("dht proxy");
		if (!settings) settings = e.dict_find_dict("proxy");
		if (settings)
		{
			proxy_settings s;
			load_struct(*settings, &s, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
			set_dht_proxy(s);
		}

		settings = e.dict_find_dict("dht state");
		if (settings)
		{
			m_dht_state = *settings;
		}
#endif

#if TORRENT_USE_I2P
		settings = e.dict_find_dict("i2p");
		if (settings)
		{
			proxy_settings s;
			load_struct(*settings, &s, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
			set_i2p_proxy(s);
		}
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
		settings = e.dict_find_dict("encryption");
		if (settings)
		{
			pe_settings s;
			load_struct(*settings, &s, pe_settings_map
				, sizeof(pe_settings_map)/sizeof(pe_settings_map[0]));
			set_pe_settings(s);
		}
#endif

		settings = e.dict_find_dict("peer proxy");
		if (!settings) settings = e.dict_find_dict("proxy");
		if (settings)
		{
			proxy_settings s;
			load_struct(*settings, &s, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
			set_peer_proxy(s);
		}

		settings = e.dict_find_dict("web proxy");
		if (!settings) settings = e.dict_find_dict("proxy");
		if (settings)
		{
			proxy_settings s;
			load_struct(*settings, &s, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
			set_web_seed_proxy(s);
		}

		settings = e.dict_find_dict("tracker proxy");
		if (!settings) settings = e.dict_find_dict("proxy");
		if (settings)
		{
			proxy_settings s;
			load_struct(*settings, &s, proxy_settings_map
				, sizeof(proxy_settings_map)/sizeof(proxy_settings_map[0]));
			set_tracker_proxy(s);
		}

#ifndef TORRENT_DISABLE_GEO_IP
		settings  = e.dict_find_dict("AS map");
		if (settings)
		{
			for (int i = 0; i < settings->dict_size(); ++i)
			{
				std::pair<std::string, lazy_entry const*> item = settings->dict_at(i);
				int as_num = atoi(item.first.c_str());
				if (item.second->type() != lazy_entry::int_t || item.second->int_value() == 0) continue;
				int& peak = m_as_peak[as_num];
				if (peak < item.second->int_value()) peak = item.second->int_value();
			}
		}
#endif

 		if (m_settings.connection_speed < 0) m_settings.connection_speed = 200;

		if (m_settings.broadcast_lsd && m_lsd)
			m_lsd->use_broadcast(true);

		update_disk_thread_settings();
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
		if (m_asnum_db) GeoIP_delete(m_asnum_db);
		m_asnum_db = GeoIP_open(file, GEOIP_STANDARD);
		return m_asnum_db;
	}

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
	bool session_impl::load_asnum_db(wchar_t const* file)
	{
		if (m_asnum_db) GeoIP_delete(m_asnum_db);
		std::string utf8;
		wchar_utf8(file, utf8);
		m_asnum_db = GeoIP_open(utf8.c_str(), GEOIP_STANDARD);
		return m_asnum_db;
	}

	bool session_impl::load_country_db(wchar_t const* file)
	{
		if (m_country_db) GeoIP_delete(m_country_db);
		std::string utf8;
		wchar_utf8(file, utf8);
		m_country_db = GeoIP_open(utf8.c_str(), GEOIP_STANDARD);
		return m_country_db;
	}
#endif

	bool session_impl::load_country_db(char const* file)
	{
		if (m_country_db) GeoIP_delete(m_country_db);
		m_country_db = GeoIP_open(file, GEOIP_STANDARD);
		return m_country_db;
	}

#endif

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
		if (m_paused) return;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_logger) << time_now_string() << " *** session paused ***\n";
#endif
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
		if (m_abort) return;
#if defined TORRENT_LOGGING
		(*m_logger) << time_now_string() << " *** ABORT CALLED ***\n";
#endif
		// abort the main thread
		m_abort = true;
		m_queued_for_checking.clear();
		if (m_lsd) m_lsd->close();
		if (m_upnp) m_upnp->close();
		if (m_natpmp) m_natpmp->close();
#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			m_dht->stop();
			m_dht = 0;
		}
		m_dht_socket->close();
#endif
		error_code ec;
		m_timer.cancel(ec);
		m_lsd_announce_timer.cancel(ec);

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
#endif
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end(); ++i)
		{
			torrent& t = *i->second;
			t.abort();
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " aborting all connections (" << m_connections.size() << ")\n";
#endif
		// closing all the connections needs to be done from a callback,
		// when the session mutex is not held
		m_io_service.post(boost::bind(&connection_queue::close, &m_half_open));

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " connection queue: " << m_half_open.size() << "\n";
#endif

		// abort all connections
		while (!m_connections.empty())
		{
#ifdef TORRENT_DEBUG
			int conn = m_connections.size();
#endif
			(*m_connections.begin())->disconnect(errors::stopping_torrent);
			TORRENT_ASSERT(conn == int(m_connections.size()) + 1);
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " connection queue: " << m_half_open.size() << "\n";
#endif

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " shutting down connection queue\n";
#endif

		m_download_rate.close();
		m_upload_rate.close();
	}

	void session_impl::set_port_filter(port_filter const& f)
	{
		m_port_filter = f;
		// TODO: recalculate all connect candidates for all torrents
	}

	void session_impl::set_ip_filter(ip_filter const& f)
	{
		INVARIANT_CHECK;

		m_ip_filter = f;

		// Close connections whose endpoint is filtered
		// by the new ip-filter
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
			i->second->ip_filter_updated();
	}

	ip_filter const& session_impl::get_ip_filter() const
	{
		return m_ip_filter;
	}

	void session_impl::update_disk_thread_settings()
	{
		disk_io_job j;
		j.buffer = (char*)&m_settings;
		j.action = disk_io_job::update_settings;
		m_disk_thread.add_job(j);
	}

	void session_impl::set_settings(session_settings const& s)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(s.file_pool_size > 0);

		// less than 5 seconds unchoke interval is insane
		TORRENT_ASSERT(s.unchoke_interval >= 5);


		// if disk io thread settings were changed
		// post a notification to that thread
		bool update_disk_io_thread = false;
		if (m_settings.cache_size != s.cache_size
			|| m_settings.cache_expiry != s.cache_expiry
			|| m_settings.optimize_hashing_for_speed != s.optimize_hashing_for_speed
			|| m_settings.file_checks_delay_per_block != s.file_checks_delay_per_block
			|| m_settings.disk_cache_algorithm != s.disk_cache_algorithm
			|| m_settings.read_cache_line_size != s.read_cache_line_size
			|| m_settings.write_cache_line_size != s.write_cache_line_size
			|| m_settings.coalesce_writes != s.coalesce_writes
			|| m_settings.coalesce_reads != s.coalesce_reads
			|| m_settings.max_queued_disk_bytes != s.max_queued_disk_bytes
			|| m_settings.disable_hash_checks != s.disable_hash_checks
#ifndef TORRENT_DISABLE_MLOCK
			|| m_settings.lock_disk_cache != s.lock_disk_cache
#endif
			|| m_settings.use_read_cache != s.use_read_cache
			|| m_settings.disk_io_write_mode != s.disk_io_write_mode
			|| m_settings.disk_io_read_mode != s.disk_io_read_mode
			|| m_settings.allow_reordered_disk_operations != s.allow_reordered_disk_operations
			|| m_settings.file_pool_size != s.file_pool_size
			|| m_settings.low_prio_disk != s.low_prio_disk)
			update_disk_io_thread = true;

		if (m_settings.local_service_announce_interval != s.local_service_announce_interval)
		{
			error_code ec;
			int delay = (std::max)(s.local_service_announce_interval
				/ (std::max)(int(m_torrents.size()), 1), 1);
			m_lsd_announce_timer.expires_from_now(seconds(delay), ec);
			m_lsd_announce_timer.async_wait(
				boost::bind(&session_impl::on_lsd_announce, this, _1));
		}

		// if queuing settings were changed, recalculate
		// queued torrents sooner
		if ((m_settings.active_downloads != s.active_downloads
			|| m_settings.active_seeds != s.active_seeds
			|| m_settings.active_limit != s.active_limit)
			&& m_auto_manage_time_scaler > 2)
			m_auto_manage_time_scaler = 2;
		m_settings = s;

		if (m_settings.cache_buffer_chunk_size <= 0)
			m_settings.cache_buffer_chunk_size = 1;

 		if (m_settings.connection_speed < 0) m_settings.connection_speed = 200;
		if (m_settings.broadcast_lsd && m_lsd)
			m_lsd->use_broadcast(true);
 
		if (update_disk_io_thread)
			update_disk_thread_settings();

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
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		if (ec)
		{
			(*m_logger) << "failed to open socket: " << print_endpoint(ep)
				<< ": " << ec.message() << "\n" << "\n";
		}
#endif
#if TORRENT_USE_IPV6
		if (ep.protocol() == tcp::v6())
		{
			s.sock->set_option(v6only(v6_only), ec);
#ifdef TORRENT_WINDOWS

#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
			// enable Teredo on windows
			s.sock->set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), ec);
#endif
		}
#endif
		s.sock->bind(ep, ec);
		while (ec && retries > 0)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			char msg[200];
			snprintf(msg, 200, "failed to bind to interface \"%s\": %s"
				, print_endpoint(ep).c_str(), ec.message().c_str());
			(*m_logger) << time_now_string() << " " << msg << "\n";
#endif
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
			char msg[200];
			snprintf(msg, 200, "cannot bind to interface \"%s\": %s"
				, print_endpoint(ep).c_str(), ec.message().c_str());
			(*m_logger) << time_now_string() << msg << "\n";
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
			char msg[200];
			snprintf(msg, 200, "cannot listen on interface \"%s\": %s"
				, print_endpoint(ep).c_str(), ec.message().c_str());
			(*m_logger) << time_now_string() << msg << "\n";
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

#if TORRENT_USE_IPV6
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
#endif // TORRENT_USE_IPV6

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

		open_new_incoming_socks_connection();

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

	void session_impl::open_new_incoming_socks_connection()
	{
		if (m_peer_proxy.type != proxy_settings::socks5
			&& m_peer_proxy.type != proxy_settings::socks5_pw
			&& m_peer_proxy.type != proxy_settings::socks4)
			return;
		
		if (m_socks_listen_socket) return;

		m_socks_listen_socket = boost::shared_ptr<socket_type>(new socket_type(m_io_service));
		bool ret = instantiate_connection(m_io_service, m_peer_proxy
			, *m_socks_listen_socket);
		TORRENT_ASSERT(ret);

		socks5_stream& s = *m_socks_listen_socket->get<socks5_stream>();
		s.set_command(2); // 2 means BIND (as opposed to CONNECT)
		s.async_connect(tcp::endpoint(address_v4::any(), m_listen_interface.port())
			, boost::bind(&session_impl::on_socks_accept, this, m_socks_listen_socket, _1));
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
			{
				if (m_dht) m_dht->on_unreachable(ep);
			}

			// don't bubble up operation aborted errors to the user
			if (e != asio::error::operation_aborted
				&& m_alerts.should_post<udp_error_alert>())
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
		listener->async_accept(*c->get<stream_socket>()
			, boost::bind(&session_impl::on_accept_connection, this, c
			, boost::weak_ptr<socket_acceptor>(listener), _1));
	}

	void session_impl::on_accept_connection(shared_ptr<socket_type> const& s
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
				+ print_endpoint(ep) + "' " + e.message();
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

		incoming_connection(s);
	}

	void session_impl::incoming_connection(boost::shared_ptr<socket_type> const& s)
	{
		error_code ec;
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
			if (m_alerts.should_post<peer_disconnected_alert>())
			{
				m_alerts.post_alert(
					peer_disconnected_alert(torrent_handle(), endp, peer_id()
						, error_code(errors::too_many_connections, get_libtorrent_category())));
			}
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

		setup_socket_buffers(*s);

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

	void session_impl::setup_socket_buffers(socket_type& s)
	{
		error_code ec;
		if (m_settings.send_socket_buffer_size)
		{
			stream_socket::send_buffer_size option(
				m_settings.send_socket_buffer_size);
			s.set_option(option, ec);
		}
		if (m_settings.recv_socket_buffer_size)
		{
			stream_socket::receive_buffer_size option(
				m_settings.recv_socket_buffer_size);
			s.set_option(option, ec);
		}
	}

	void session_impl::on_socks_accept(boost::shared_ptr<socket_type> const& s
		, error_code const& e)
	{
		m_socks_listen_socket.reset();
		if (e == asio::error::operation_aborted) return;
		if (e)
		{
			if (m_alerts.should_post<listen_failed_alert>())
				m_alerts.post_alert(listen_failed_alert(tcp::endpoint(
					address_v4::any(), m_listen_interface.port()), e));
			return;
		}
		open_new_incoming_socks_connection();
		incoming_connection(s);
	}

	void session_impl::close_connection(peer_connection const* p
		, error_code const& ec)
	{
// too expensive
//		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
//		for (aux::session_impl::torrent_map::const_iterator i = m_torrents.begin()
//			, end(m_torrents.end()); i != end; ++i)
//			TORRENT_ASSERT(!i->second->has_peer((peer_connection*)p));
#endif

#if defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << " CLOSING CONNECTION "
			<< p->remote() << " : " << ec.message() << "\n";
#endif

		TORRENT_ASSERT(p->is_disconnecting());

		if (!p->is_choked() && !p->ignore_unchoke_slots()) --m_num_unchoked;
//		connection_map::iterator i = std::lower_bound(m_connections.begin(), m_connections.end()
//			, p, boost::bind(&boost::intrusive_ptr<peer_connection>::get, _1) < p);
//		if (i->get() != p) i == m_connections.end();
		connection_map::iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, boost::bind(&boost::intrusive_ptr<peer_connection>::get, _1) == p);
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

	void session_impl::unchoke_peer(peer_connection& c)
	{
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		torrent* t = c.associated_torrent().lock().get();
		TORRENT_ASSERT(t);
		if (t->unchoke_peer(c))
			++m_num_unchoked;
	}

	void session_impl::choke_peer(peer_connection& c)
	{
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		torrent* t = c.associated_torrent().lock().get();
		TORRENT_ASSERT(t);
		if (t->choke_peer(c))
			--m_num_unchoked;
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

	// this function is called from the disk-io thread
	// when the disk queue is low enough to post new
	// write jobs to it. It will go through all peer
	// connections that are blocked on the disk and
	// wake them up
	void session_impl::on_disk_queue()
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);
		
		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::intrusive_ptr<peer_connection> p = *i;
			++i;
			if (p->m_channel_state[peer_connection::download_channel]
				!= peer_info::bw_disk) continue;

			// setup_receive() may disconnect the connection
			// and clear it out from the m_connections list
			p->setup_receive();
		}
	}

	initialize_timer::initialize_timer()
	{
		g_current_time = time_now_hires();
	}

	void session_impl::on_tick(error_code const& e)
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

		ptime now = time_now_hires();
		aux::g_current_time = now;
// too expensive
//		INVARIANT_CHECK;

		if (m_abort) return;

		if (e == asio::error::operation_aborted) return;

		if (e)
		{
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
			(*m_logger) << "*** TICK TIMER FAILED " << e.message() << "\n";
#endif
			::abort();
			return;
		}

		error_code ec;
		m_timer.expires_at(now + milliseconds(100), ec);
		m_timer.async_wait(bind(&session_impl::on_tick, this, _1));

		m_download_rate.update_quotas(now - m_last_tick);
		m_upload_rate.update_quotas(now - m_last_tick);

		m_last_tick = now;

		// only tick the following once per second
		if (now - m_last_second_tick < seconds(1)) return;

		int tick_interval_ms = total_milliseconds(now - m_last_second_tick);
		m_last_second_tick = now;

		int session_time = total_seconds(now - m_created);
		if (session_time > 65000)
		{
			// we're getting close to the point where our timestamps
			// in policy::peer are wrapping. We need to step all counters back
			// four hours. This means that any timestamp that refers to a time
			// more than 18.2 - 4 = 14.2 hours ago, will be incremented to refer to
			// 14.2 hours ago.

			m_created += hours(4);

			const int four_hours = 60 * 60 * 4;
			for (torrent_map::iterator i = m_torrents.begin()
				, end(m_torrents.end()); i != end; ++i)
			{
				policy& p = i->second->get_policy();
				for (policy::iterator j = p.begin_peer()
					, end(p.end_peer()); j != end; ++j)
				{
					policy::peer* pe = *j;

					if (pe->last_optimistically_unchoked < four_hours)
						pe->last_optimistically_unchoked = 0;
					else
						pe->last_optimistically_unchoked -= four_hours;

					if (pe->last_connected < four_hours)
						pe->last_connected = 0;
					else
						pe->last_connected -= four_hours;
				}
			}
		}

#ifdef TORRENT_STATS
		++m_second_counter;
		int downloading_torrents = 0;
		int seeding_torrents = 0;
		static size_type downloaded = 0;
		static size_type uploaded = 0;
		size_type download_rate = (m_stat.total_download() - downloaded) * 1000 / tick_interval_ms;
		size_type upload_rate = (m_stat.total_upload() - uploaded) * 1000 / tick_interval_ms;
		downloaded = m_stat.total_download();
		uploaded = m_stat.total_upload();
		size_type num_peers = 0;
		for (torrent_map::iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			num_peers += i->second->get_policy().num_peers();
			if (i->second->is_seed())
				++seeding_torrents;
			else
				++downloading_torrents;
		}
		int num_complete_connections = 0;
		int num_half_open = 0;
		int unchoked_peers = 0;
		for (connection_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			if ((*i)->is_connecting())
				++num_half_open;
			else
			{
				++num_complete_connections;
				if (!(*i)->is_choked()) ++unchoked_peers;
			}
		}
		
		m_stats_logger
			<< m_second_counter << "\t"
			<< upload_rate << "\t"
			<< download_rate << "\t"
			<< downloading_torrents << "\t"
			<< seeding_torrents << "\t"
			<< num_complete_connections << "\t"
			<< num_half_open << "\t"
			<< m_disk_thread.disk_allocations() << "\t"
			<< unchoked_peers << "\t"
			<< num_peers << "\t"
			<< logging_allocator::allocations << "\t"
			<< logging_allocator::allocated_bytes << "\t"
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
				p->disconnect(errors::timed_out);
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

		torrent_map::iterator least_recently_scraped = m_torrents.end();
		int num_paused_auto_managed = 0;

		int num_checking = 0;
		int num_queued = 0;
		for (torrent_map::iterator i = m_torrents.begin();
			i != m_torrents.end();)
		{
			torrent& t = *i->second;
			TORRENT_ASSERT(!t.is_aborted());
			if (t.statistics().upload_rate() > t.upload_limit() * 9 / 10)
				++congested_torrents;
			else
				++uncongested_torrents;

			if (t.state() == torrent_status::checking_files) ++num_checking;
			else if (t.state() == torrent_status::queued_for_checking && !t.is_paused()) ++num_queued;

			if (t.is_auto_managed() && t.is_paused() && !t.has_error())
			{
				++num_paused_auto_managed;
				if (least_recently_scraped == m_torrents.end()
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

			t.second_tick(m_stat, tick_interval_ms);
			++i;
		}

		// some people claim that there sometimes can be cases where
		// there is no torrent being checked, but there are torrents
		// waiting to be checked. I have never seen this, and I can't 
		// see a way for it to happen. But, if it does, start one of
		// the queued torrents
		if (num_checking == 0 && num_queued > 0)
		{
			TORRENT_ASSERT(false);
			check_queue_t::iterator i = std::min_element(m_queued_for_checking.begin()
				, m_queued_for_checking.end(), boost::bind(&torrent::queue_position, _1)
				< boost::bind(&torrent::queue_position, _2));
			if (i != m_queued_for_checking.end())
			{
				(*i)->start_checking();
			}
		}

#ifndef TORRENT_DISABLE_DHT
		if (m_dht)
		{
			int dht_down;
			int dht_up;
			m_dht->network_stats(dht_up, dht_down);
			m_stat.sent_dht_bytes(dht_up);
			m_stat.received_dht_bytes(dht_down);
		}
#endif

		if (m_settings.rate_limit_ip_overhead)
		{
			m_download_channel.use_quota(m_stat.download_dht()
				+ m_stat.download_tracker());

			m_upload_channel.use_quota(m_stat.upload_dht()
				+ m_stat.upload_tracker());

			int up_limit = m_upload_channel.throttle();
			int down_limit = m_download_channel.throttle();

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

		m_stat.second_tick(tick_interval_ms);

		TORRENT_ASSERT(least_recently_scraped == m_torrents.end()
			|| least_recently_scraped->second->is_paused()
			&& least_recently_scraped->second->is_auto_managed());

		// --------------------------------------------------------------
		// scrape paused torrents that are auto managed
		// (unless the session is paused)
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
		// equally likely to connect to a peer

		int free_slots = m_half_open.free_slots();
		if (!m_torrents.empty()
			&& free_slots > -m_half_open.limit()
			&& num_connections() < m_max_connections
			&& !m_abort
			&& m_settings.connection_speed > 0)
		{
			// this is the maximum number of connections we will
			// attempt this tick
			int max_connections = m_settings.connection_speed;
			int average_peers = 0;
			if (num_downloads > 0)
				average_peers = num_downloads_peers / num_downloads;

			if (m_next_connect_torrent == m_torrents.end())
				m_next_connect_torrent = m_torrents.begin();

			int steps_since_last_connect = 0;
			int num_torrents = int(m_torrents.size());
			for (;;)
			{
				torrent& t = *m_next_connect_torrent->second;
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
#ifndef BOOST_NO_EXCEPTIONS
					try
					{
#endif
						if (t.try_connect_peer())
						{
							--max_connections;
							--free_slots;
							steps_since_last_connect = 0;
						}
#ifndef BOOST_NO_EXCEPTIONS
					}
					catch (std::bad_alloc&)
					{
						// we ran out of memory trying to connect to a peer
						// lower the global limit to the number of peers
						// we already have
						m_max_connections = num_connections();
						if (m_max_connections < 2) m_max_connections = 2;
					}
#endif
				}

				++m_next_connect_torrent;
				++steps_since_last_connect;
				if (m_next_connect_torrent == m_torrents.end())
					m_next_connect_torrent = m_torrents.begin();

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
		// unchoke set calculations
		// --------------------------------------------------------------
		m_unchoke_time_scaler--;
		if (m_unchoke_time_scaler <= 0 && !m_connections.empty())
		{
			m_unchoke_time_scaler = settings().unchoke_interval;
			recalculate_unchoke_slots(congested_torrents
				, uncongested_torrents);
		}

		// --------------------------------------------------------------
		// optimistic unchoke calculation
		// --------------------------------------------------------------
		m_optimistic_unchoke_time_scaler--;
		if (m_optimistic_unchoke_time_scaler <= 0)
		{
			m_optimistic_unchoke_time_scaler
				= settings().optimistic_unchoke_interval;
			recalculate_optimistic_unchoke_slot();
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
					, boost::bind(&torrent::num_peers, boost::bind(&torrent_map::value_type::second, _1))
					< boost::bind(&torrent::num_peers, boost::bind(&torrent_map::value_type::second, _2)));
			
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

//		m_peer_pool.release_memory();
	}

	void session_impl::on_lsd_announce(error_code const& e)
	{
		if (e) return;

		session_impl::mutex_t::scoped_lock l(m_mutex);
		if (m_abort) return;

		// announce on local network every 5 minutes
		int delay = (std::max)(m_settings.local_service_announce_interval
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

	namespace
	{
		bool is_active(torrent* t, session_settings const& s)
		{
			// if we count slow torrents, every torrent
			// is considered active
			if (!s.dont_count_slow_torrents) return true;
			
			// if the torrent started less than 2 minutes
			// ago (default), let it count as active since
			// the rates are probably not accurate yet
			if (time_now() - t->started() < seconds(s.auto_manage_startup)) return true;

			return t->statistics().upload_payload_rate() != 0.f
				|| t->statistics().download_payload_rate() != 0.f;
		}
	}
	
	int session_impl::auto_manage_torrents(std::vector<torrent*>& list
		, int hard_limit, int type_limit)
	{
		for (std::vector<torrent*>::iterator i = list.begin()
			, end(list.end()); i != end; ++i)
		{
			torrent* t = *i;
			if (!t->is_paused() && !is_active(t, settings())
				&& hard_limit > 0)
			{
				--hard_limit;
				continue;
			}

			if (type_limit > 0 && hard_limit > 0)
			{
				--hard_limit;
				--type_limit;
				if (t->is_paused()) t->resume();
			}
			else
			{
				if (!t->is_paused()) t->pause();
			}
		}
		return hard_limit;
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
					if (t->is_finished())
						--num_seeds;
					else
						--num_downloaders;
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
				, boost::bind(&torrent::sequence_number, _1) < boost::bind(&torrent::sequence_number, _2));

			std::sort(seeds.begin(), seeds.end()
				, boost::bind(&torrent::seed_rank, _1, boost::ref(m_settings))
				> boost::bind(&torrent::seed_rank, _2, boost::ref(m_settings)));
		}

		if (settings().auto_manage_prefer_seeds)
		{
			hard_limit = auto_manage_torrents(seeds, hard_limit, num_seeds);
			hard_limit = auto_manage_torrents(downloaders, hard_limit, num_downloaders);
		}
		else
		{
			hard_limit = auto_manage_torrents(downloaders, hard_limit, num_downloaders);
			hard_limit = auto_manage_torrents(seeds, hard_limit, num_seeds);
		}
            
	}

	void session_impl::recalculate_optimistic_unchoke_slot()
	{
		if (m_allowed_upload_slots == 0) return;
	
		// find the peer that has been waiting the longest to be optimistically
		// unchoked
		connection_map::iterator current_optimistic_unchoke = m_connections.end();
		connection_map::iterator optimistic_unchoke_candidate = m_connections.end();
		boost::uint32_t last_unchoke = UINT_MAX;

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
				&& !p->ignore_unchoke_slots()
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
			bool ret = t->unchoke_peer(*optimistic_unchoke_candidate->get(), true);
			TORRENT_ASSERT(ret);
			if (ret)
			{
				(*optimistic_unchoke_candidate)->peer_info_struct()->optimistically_unchoked = true;
				(*optimistic_unchoke_candidate)->peer_info_struct()->last_optimistically_unchoked = session_time();
			}

			// adjust the optimistic unchoke interval depending on the piece-size
			// the peer should be able to download one whole piece within the optimistic
			// unchoke interval, at a reasonable rate
			int piece_size = t->torrent_file().piece_length();
			int rate = 3000;
			// assume a reasonable rate is 3 kB/s, unless there's an upload limit and
			// a max number of slots, in which case we assume each upload slot gets
			// roughly the same amount of bandwidth
			TORRENT_ASSERT(m_upload_channel.throttle() != bandwidth_channel::inf);
			if (m_upload_channel.throttle() > 0 && m_max_uploads > 0)
				rate = (std::max)(m_upload_channel.throttle() / m_max_uploads, 1);

			// the time it takes to download one piece at this rate (in seconds)
			int piece_dl_time = piece_size / rate;
			m_optimistic_unchoke_time_scaler = piece_dl_time;
		}
	}

	void session_impl::recalculate_unchoke_slots(int congested_torrents
		, int uncongested_torrents)
	{
		INVARIANT_CHECK;

		ptime now = time_now();
		time_duration unchoke_interval = now - m_last_choke;
		m_last_choke = now;

		// build list of all peers that are
		// unchoke:able.
		std::vector<peer_connection*> peers;
		for (connection_map::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::intrusive_ptr<peer_connection> p = *i;
			TORRENT_ASSERT(p);
			++i;
			torrent* t = p->associated_torrent().lock().get();
			policy::peer* pi = p->peer_info_struct();
			if (p->ignore_unchoke_slots() || t == 0 || pi == 0) continue;

			if (!p->is_peer_interested()
				|| p->is_disconnecting()
				|| p->is_connecting()
				|| (p->share_diff() < -free_upload_amount
					&& !t->is_seed()))
			{
				// this peer is not unchokable. So, if it's unchoked
				// already, make sure to choke it.
				if (p->is_choked()) continue;
				if (pi && pi->optimistically_unchoked)
				{
					pi->optimistically_unchoked = false;
					// force a new optimistic unchoke
					m_optimistic_unchoke_time_scaler = 0;
				}
				t->choke_peer(*p);
				continue;
			}
			peers.push_back(p.get());
		}

		if (m_settings.auto_upload_slots_rate_based
			&& m_settings.auto_upload_slots)
		{
			m_allowed_upload_slots = 0;
			std::sort(peers.begin(), peers.end()
				, boost::bind(&peer_connection::upload_rate_compare, _1, _2));

#ifdef TORRENT_DEBUG
			for (std::vector<peer_connection*>::const_iterator i = peers.begin()
				, end(peers.end()), prev(peers.end()); i != end; ++i)
			{
				if (prev != end)
				{
					boost::shared_ptr<torrent> t1 = (*prev)->associated_torrent().lock();
					TORRENT_ASSERT(t1);
					boost::shared_ptr<torrent> t2 = (*i)->associated_torrent().lock();
					TORRENT_ASSERT(t2);
					TORRENT_ASSERT((*prev)->uploaded_since_unchoke() * 1000
						* (1 + t1->priority()) / total_milliseconds(unchoke_interval)
						>= (*i)->uploaded_since_unchoke() * 1000
						* (1 + t2->priority()) / total_milliseconds(unchoke_interval));
				}
				prev = i;
			}
#endif

			// TODO: make configurable
			int rate_threshold = 1024;

			for (std::vector<peer_connection*>::const_iterator i = peers.begin()
				, end(peers.end()); i != end; ++i)
			{
				peer_connection const& p = **i;
				int rate = p.uploaded_since_unchoke()
					* 1000 / total_milliseconds(unchoke_interval);

				if (rate < rate_threshold) break;

				++m_allowed_upload_slots;

				// TODO: make configurable
				rate_threshold += 1024;
			}
			// allow one optimistic unchoke
			++m_allowed_upload_slots;
		}

		// sorts the peers that are eligible for unchoke by download rate and secondary
		// by total upload. The reason for this is, if all torrents are being seeded,
		// the download rate will be 0, and the peers we have sent the least to should
		// be unchoked
		std::sort(peers.begin(), peers.end()
			, boost::bind(&peer_connection::unchoke_compare, _1, _2));

		// auto unchoke
		int upload_limit = m_bandwidth_channel[peer_connection::upload_channel]->throttle();
		if (!m_settings.auto_upload_slots_rate_based
			&& m_settings.auto_upload_slots
			&& upload_limit > 0)
		{
			// if our current upload rate is less than 90% of our 
			// limit AND most torrents are not "congested", i.e.
			// they are not holding back because of a per-torrent
			// limit
			if (m_stat.upload_rate() < upload_limit * 0.9f
				&& m_allowed_upload_slots <= m_num_unchoked + 1
				&& congested_torrents < uncongested_torrents
				&& m_upload_rate.queue_size() < 2)
			{
				++m_allowed_upload_slots;
			}
			else if (m_upload_rate.queue_size() > 1
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
				// no, this peer should be shoked
				TORRENT_ASSERT(p->peer_info_struct());
				if (!p->is_choked() && !p->peer_info_struct()->optimistically_unchoked)
					t->choke_peer(*p);
				if (!p->is_choked())
					++m_num_unchoked;
			}
		}
	}

	void session_impl::operator()()
	{
		eh_initializer();

#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " open listen port\n";
#endif
		if (m_listen_interface.port() != 0)
		{
			session_impl::mutex_t::scoped_lock l(m_mutex);
			open_listen_port();
		}

#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " done starting session\n";
#endif
		bool stop_loop = false;
		while (!stop_loop)
		{
			error_code ec;
			m_io_service.run(ec);
			if (ec)
			{
#ifdef TORRENT_DEBUG
				std::cerr << ec.message() << "\n";
				std::string err = ec.message();
#endif
				TORRENT_ASSERT(false);
			}
			m_io_service.reset();

			mutex_t::scoped_lock l(m_mutex);
			stop_loop = m_abort;
		}

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

	torrent_handle session_impl::add_torrent(add_torrent_params const& params
		, error_code& ec)
	{
		TORRENT_ASSERT(!params.save_path.empty());

		if (params.ti && params.ti->num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			return torrent_handle();
		}

//		INVARIANT_CHECK;

		if (is_aborted())
		{
			ec = errors::session_is_closing;
			return torrent_handle();
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

			ec = errors::duplicate_torrent;
			return torrent_handle();
		}

		int queue_pos = 0;
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			int pos = i->second->queue_position();
			if (pos >= queue_pos) queue_pos = pos + 1;
		}

		torrent_ptr.reset(new torrent(*this, m_listen_interface
			, 16 * 1024, queue_pos, params));
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
			std::for_each(nodes.begin(), nodes.end(), boost::bind(
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

	void session_impl::queue_check_torrent(boost::shared_ptr<torrent> const& t)
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

	void session_impl::dequeue_check_torrent(boost::shared_ptr<torrent> const& t)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(t->state() == torrent_status::checking_files
			|| t->state() == torrent_status::queued_for_checking);

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
		TORRENT_ASSERT(done != m_queued_for_checking.end());
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
			throw_invalid_handle();
#endif

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
			if (i == m_next_lsd_torrent)
				++m_next_lsd_torrent;
			if (i == m_next_connect_torrent)
				++m_next_connect_torrent;

			t.set_queue_position(-1);
			m_torrents.erase(i);

			if (m_next_lsd_torrent == m_torrents.end())
				m_next_lsd_torrent = m_torrents.begin();
			if (m_next_connect_torrent == m_torrents.end())
				m_next_connect_torrent = m_torrents.begin();

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
			error_code ec;
			m_dht_socket->bind(udp::endpoint(m_listen_interface.address(), m_dht_settings.service_port), ec);

			maybe_update_udp_mapping(0, m_dht_settings.service_port, m_dht_settings.service_port);
			maybe_update_udp_mapping(1, m_dht_settings.service_port, m_dht_settings.service_port);
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
		if (m_listen_sockets.empty()) return 0;
		return m_listen_sockets.front().external_port;
	}

	void session_impl::announce_lsd(sha1_hash const& ih)
	{
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
		t->get_policy().add_peer(peer, peer_id(0), peer_info::lsd, 0);
	}

	void session_impl::on_port_map_log(
		char const* msg, int map_transport)
	{
		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);
		// log message
#ifdef TORRENT_UPNP_LOGGING
		char const* transport_names[] = {"NAT-PMP", "UPnP"};
		m_upnp_log << time_now_string() << " "
			<< transport_names[map_transport] << ": " << msg;
#endif
		if (m_alerts.should_post<portmap_log_alert>())
			m_alerts.post_alert(portmap_log_alert(map_transport, msg));
	}

	void session_impl::on_port_mapping(int mapping, int port
		, error_code const& ec, int map_transport)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(map_transport >= 0 && map_transport <= 1);

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

	session_status session_impl::status() const
	{
//		INVARIANT_CHECK;

		session_status s;

		s.optimistic_unchoke_counter = m_optimistic_unchoke_time_scaler;
		s.unchoke_counter = m_unchoke_time_scaler;

		s.num_peers = (int)m_connections.size();
		s.num_unchoked = m_num_unchoked;
		s.allowed_upload_slots = m_allowed_upload_slots;

		s.total_redundant_bytes = m_total_redundant_bytes;
		s.total_failed_bytes = m_total_failed_bytes;

		s.up_bandwidth_queue = m_upload_rate.queue_size();
		s.down_bandwidth_queue = m_download_rate.queue_size();

		s.up_bandwidth_bytes_queue = m_upload_rate.queued_bytes();
		s.down_bandwidth_bytes_queue = m_download_rate.queued_bytes();

		s.has_incoming_connections = m_incoming_connection;

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

		// DHT protocol
		s.dht_download_rate = m_stat.transfer_rate(stat::download_dht_protocol);
		s.total_dht_download = m_stat.total_transfer(stat::download_dht_protocol);
		s.dht_upload_rate = m_stat.transfer_rate(stat::upload_dht_protocol);
		s.total_dht_upload = m_stat.total_transfer(stat::upload_dht_protocol);

		// tracker
		s.tracker_download_rate = m_stat.transfer_rate(stat::download_tracker_protocol);
		s.total_tracker_download = m_stat.total_transfer(stat::download_tracker_protocol);
		s.tracker_upload_rate = m_stat.transfer_rate(stat::upload_tracker_protocol);
		s.total_tracker_upload = m_stat.total_transfer(stat::upload_tracker_protocol);

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

	void session_impl::start_dht()
	{ start_dht(m_dht_state); }

	void session_impl::start_dht(entry const& startup_state)
	{
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
		maybe_update_udp_mapping(0, m_dht_settings.service_port, m_dht_settings.service_port);
		maybe_update_udp_mapping(1, m_dht_settings.service_port, m_dht_settings.service_port);
		m_dht = new dht::dht_tracker(*this, *m_dht_socket, m_dht_settings, &startup_state);
		if (!m_dht_socket->is_open() || m_dht_socket->local_port() != m_dht_settings.service_port)
		{
			error_code ec;
			m_dht_socket->bind(udp::endpoint(m_listen_interface.address(), m_dht_settings.service_port), ec);
		}

		for (std::list<udp::endpoint>::iterator i = m_dht_router_nodes.begin()
			, end(m_dht_router_nodes.end()); i != end; ++i)
		{
			m_dht->add_router_node(*i);
		}

		m_dht->start(startup_state);

		// announce all torrents we have to the DHT
		for (torrent_map::const_iterator i = m_torrents.begin()
			, end(m_torrents.end()); i != end; ++i)
		{
			i->second->force_dht_announce();
		}
	}

#ifndef TORRENT_DISABLE_DHT	
	void session_impl::maybe_update_udp_mapping(int nat, int local_port, int external_port)
	{
		int local, external, protocol;
		if (nat == 0 && m_natpmp.get())
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
		else if (nat == 1 && m_upnp.get())
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
#endif

	void session_impl::stop_dht()
	{
		if (!m_dht) return;
		m_dht->stop();
		m_dht = 0;
	}

	void session_impl::set_dht_settings(dht_settings const& settings)
	{
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
			error_code ec;
			m_dht_socket->bind(udp::endpoint(m_listen_interface.address(), settings.service_port), ec);

			maybe_update_udp_mapping(0, settings.service_port, settings.service_port);
			maybe_update_udp_mapping(1, settings.service_port, settings.service_port);
			m_external_udp_port = settings.service_port;
		}
		m_dht_settings = settings;
		if (m_dht_same_port)
			m_dht_settings.service_port = m_listen_interface.port();
	}

	void session_impl::on_dht_state_callback(boost::condition& c
		, entry& e, bool& done) const
	{
		mutex_t::scoped_lock l(m_mutex);
		if (m_dht) e = m_dht->state();
		done = true;
		c.notify_all();
	}

#ifndef TORRENT_NO_DEPRECATE
	entry session_impl::dht_state(session_impl::mutex_t::scoped_lock& l) const
	{
		boost::condition cond;
		if (!m_dht) return entry();
		entry e;
		bool done = false;
		m_io_service.post(boost::bind(&session_impl::on_dht_state_callback
			, this, boost::ref(cond), boost::ref(e), boost::ref(done)));
		while (!done) cond.wait(l);
		return e;
	}
#endif

	void session_impl::add_dht_node(std::pair<std::string, int> const& node)
	{
		if (m_dht) m_dht->add_node(node);
	}

	void session_impl::add_dht_router(std::pair<std::string, int> const& node)
	{
		char port[7];
		snprintf(port, sizeof(port), "%d", node.second);
		tcp::resolver::query q(node.first, port);
		m_host_resolver.async_resolve(q,
			boost::bind(&session_impl::on_dht_router_name_lookup, this, _1, _2));
	}

	void session_impl::on_dht_router_name_lookup(error_code const& e
		, tcp::resolver::iterator host)
	{
		if (e || host == tcp::resolver::iterator()) return;
		session_impl::mutex_t::scoped_lock l(m_mutex);
		// router nodes should be added before the DHT is started (and bootstrapped)
		udp::endpoint ep(host->endpoint().address(), host->endpoint().port());
		if (m_dht) m_dht->add_router_node(ep);
		m_dht_router_nodes.push_back(ep);
	}
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session_impl::set_pe_settings(pe_settings const& settings)
	{
		m_pe_settings = settings;
	}
#endif

	bool session_impl::is_listening() const
	{
		return !m_listen_sockets.empty();
	}

	session_impl::~session_impl()
	{
		session_impl::mutex_t::scoped_lock l(m_mutex);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_logger) << time_now_string() << "\n\n *** shutting down session *** \n\n";
#endif
		abort();
		TORRENT_ASSERT(m_connections.empty());

#ifndef TORRENT_DISABLE_GEO_IP
		if (m_asnum_db) GeoIP_delete(m_asnum_db);
		if (m_country_db) GeoIP_delete(m_country_db);
		m_asnum_db = 0;
		m_country_db = 0;
#endif

		l.unlock();
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

		INVARIANT_CHECK;

		if (limit < 0) limit = (std::numeric_limits<int>::max)();
		if (m_max_uploads == limit) return;
		m_max_uploads = limit;
		m_allowed_upload_slots = limit;
	}

	void session_impl::set_max_connections(int limit)
	{
		INVARIANT_CHECK;

		if (limit <= 0)
		{
			limit = (std::numeric_limits<int>::max)();
#if TORRENT_USE_RLIMIT
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
		INVARIANT_CHECK;

		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_half_open.limit(limit);
	}

	void session_impl::set_local_download_rate_limit(int bytes_per_second)
	{
		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = 0;
		m_local_download_channel.throttle(bytes_per_second);
	}

	void session_impl::set_local_upload_rate_limit(int bytes_per_second)
	{
		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = 0;
		m_local_upload_channel.throttle(bytes_per_second);
	}

	void session_impl::set_download_rate_limit(int bytes_per_second)
	{
		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = 0;
		m_download_channel.throttle(bytes_per_second);
	}

	void session_impl::set_upload_rate_limit(int bytes_per_second)
	{
		INVARIANT_CHECK;

		if (bytes_per_second <= 0) bytes_per_second = 0;
		m_upload_channel.throttle(bytes_per_second);
	}

	void session_impl::set_alert_dispatch(boost::function<void(alert const&)> const& fun)
	{
		m_alerts.set_dispatch_function(fun);
	}

	std::auto_ptr<alert> session_impl::pop_alert()
	{
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

	void session_impl::set_alert_mask(boost::uint32_t m)
	{
		m_alerts.set_alert_mask(m);
	}

	size_t session_impl::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return m_alerts.set_alert_queue_size_limit(queue_size_limit_);
	}

	int session_impl::local_upload_rate_limit() const
	{
		return m_local_upload_channel.throttle();
	}

	int session_impl::local_download_rate_limit() const
	{
		return m_local_download_channel.throttle();
	}

	int session_impl::upload_rate_limit() const
	{
		return m_upload_channel.throttle();
	}

	int session_impl::download_rate_limit() const
	{
		return m_download_channel.throttle();
	}

	void session_impl::start_lsd()
	{
		INVARIANT_CHECK;

		if (m_lsd) return;

		m_lsd = new lsd(m_io_service
			, m_listen_interface.address()
			, boost::bind(&session_impl::on_lsd_peer, this, _1, _2));
		if (m_settings.broadcast_lsd)
			m_lsd->use_broadcast(true);
	}
	
	void session_impl::start_natpmp(natpmp* n)
	{
		INVARIANT_CHECK;

		m_natpmp = n;

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
	}

	void session_impl::start_upnp(upnp* u)
	{
		INVARIANT_CHECK;

		m_upnp = u;

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
	}

	void session_impl::stop_lsd()
	{
		if (m_lsd.get())
			m_lsd->close();
		m_lsd = 0;
	}
	
	void session_impl::stop_natpmp()
	{
		if (m_natpmp.get())
			m_natpmp->close();
		m_natpmp = 0;
	}
	
	void session_impl::stop_upnp()
	{
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

	char* session_impl::allocate_disk_buffer(char const* category)
	{
		return m_disk_thread.allocate_buffer(category);
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

#if defined TORRENT_STATS && defined TORRENT_DISK_STATS
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
		int num_checking = 0;
		for (check_queue_t::const_iterator i = m_queued_for_checking.begin()
			, end(m_queued_for_checking.end()); i != end; ++i)
		{
			if ((*i)->state() == torrent_status::checking_files) ++num_checking;
		}

		// the queue is either empty, or it has exactly one checking torrent in it
		TORRENT_ASSERT(m_queued_for_checking.empty() || num_checking == 1);

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

		std::set<peer_connection*> unique_peers;
		TORRENT_ASSERT(m_max_connections > 0);
		TORRENT_ASSERT(m_max_uploads >= 0);
		if (!m_settings.auto_upload_slots_rate_based || !m_settings.auto_upload_slots)
			TORRENT_ASSERT(m_allowed_upload_slots >= m_max_uploads);
		int unchokes = 0;
		int num_optimistic = 0;
		for (connection_map::const_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			TORRENT_ASSERT(*i);
			boost::shared_ptr<torrent> t = (*i)->associated_torrent().lock();
			TORRENT_ASSERT(unique_peers.find(i->get()) == unique_peers.end());
			unique_peers.insert(i->get());

			peer_connection* p = i->get();
			TORRENT_ASSERT(!p->is_disconnecting());
			if (p->ignore_unchoke_slots()) continue;
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

