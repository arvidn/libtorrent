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
#include <set>
#include <deque>
#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/smart_ban.hpp"
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
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/magnet_uri.hpp"

#ifdef TORRENT_PROFILE_CALLS
#include <boost/unordered_map.hpp>
#endif

using boost::shared_ptr;
using boost::weak_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	TORRENT_EXPORT void min_memory_usage(settings_pack& set)
	{
		// receive data directly into disk buffers
		// this yields more system calls to read() and
		// kqueue(), but saves RAM.
		set.set_bool(settings_pack::contiguous_recv_buffer, false);

		set.set_int(settings_pack::disk_io_write_mode, settings_pack::disable_os_cache);
		set.set_int(settings_pack::disk_io_read_mode, settings_pack::disable_os_cache);

		// keep 2 blocks outstanding when hashing
		set.set_int(settings_pack::checking_mem_usage, 2);

		// don't use any extra threads to do SHA-1 hashing
		set.set_int(settings_pack::hashing_threads, 0);
		set.set_int(settings_pack::network_threads, 0);
		set.set_int(settings_pack::aio_threads, 1);

		set.set_int(settings_pack::alert_queue_size, 100);

		set.set_int(settings_pack::max_out_request_queue, 300);
		set.set_int(settings_pack::max_allowed_in_request_queue, 100);

		// setting this to a low limit, means more
		// peers are more likely to request from the
		// same piece. Which means fewer partial
		// pieces and fewer entries in the partial
		// piece list
		set.set_int(settings_pack::whole_pieces_threshold, 2);
		set.set_bool(settings_pack::use_parole_mode, false);
		set.set_bool(settings_pack::prioritize_partial_pieces, true);

		// connect to 5 peers per second
		set.set_int(settings_pack::connection_speed, 5);

		// be extra nice on the hard drive when running
		// on embedded devices. This might slow down
		// torrent checking
		set.set_int(settings_pack::file_checks_delay_per_block, 5);

		// only have 4 files open at a time
		set.set_int(settings_pack::file_pool_size, 4);

		// we want to keep the peer list as small as possible
		set.set_bool(settings_pack::allow_multiple_connections_per_ip, false);
		set.set_int(settings_pack::max_failcount, 2);
		set.set_int(settings_pack::inactivity_timeout, 120);

		// whenever a peer has downloaded one block, write
		// it to disk, and don't read anything from the
		// socket until the disk write is complete
		set.set_int(settings_pack::max_queued_disk_bytes, 1);

		// don't keep track of all upnp devices, keep
		// the device list small
		set.set_bool(settings_pack::upnp_ignore_nonrouters, true);

		// never keep more than one 16kB block in
		// the send buffer
		set.set_int(settings_pack::send_buffer_watermark, 9);

		// don't use any disk cache
		set.set_int(settings_pack::cache_size, 0);
		set.set_int(settings_pack::cache_buffer_chunk_size, 1);
		set.set_bool(settings_pack::use_read_cache, false);
		set.set_bool(settings_pack::use_disk_read_ahead, false);

		set.set_bool(settings_pack::close_redundant_connections, true);

		set.set_int(settings_pack::max_peerlist_size, 500);
		set.set_int(settings_pack::max_paused_peerlist_size, 50);

		// udp trackers are cheaper to talk to
		set.set_bool(settings_pack::prefer_udp_trackers, true);

		set.set_int(settings_pack::max_rejects, 10);

		set.set_int(settings_pack::recv_socket_buffer_size, 16 * 1024);
		set.set_int(settings_pack::send_socket_buffer_size, 16 * 1024);

		// use less memory when reading and writing
		// whole pieces
		set.set_bool(settings_pack::coalesce_reads, false);
		set.set_bool(settings_pack::coalesce_writes, false);

		// disallow the buffer size to grow for the uTP socket
		set.set_bool(settings_pack::utp_dynamic_sock_buf, false);
	}

	TORRENT_EXPORT void high_performance_seed(settings_pack& set)
	{
		// don't throttle TCP, assume there is
		// plenty of bandwidth
		set.set_int(settings_pack::mixed_mode_algorithm, settings_pack::prefer_tcp);

		set.set_int(settings_pack::max_out_request_queue, 1500);
		set.set_int(settings_pack::max_allowed_in_request_queue, 2000);

		// we will probably see a high rate of alerts, make it less
		// likely to loose alerts
		set.set_int(settings_pack::alert_queue_size, 10000);

		// allow 500 files open at a time
		set.set_int(settings_pack::file_pool_size, 500);

		// don't update access time for each read/write
		set.set_bool(settings_pack::no_atime_storage, true);

		// as a seed box, we must accept multiple peers behind
		// the same NAT
//		set.set_bool(settings_pack::allow_multiple_connections_per_ip, true);

		// connect to 50 peers per second
		set.set_int(settings_pack::connection_speed, 500);

		// allow 8000 peer connections
		set.set_int(settings_pack::connections_limit, 8000);

		// allow lots of peers to try to connect simultaneously
		set.set_int(settings_pack::listen_queue_size, 3000);

		// unchoke many peers
		set.set_int(settings_pack::unchoke_slots_limit, 2000);

		// we need more DHT capacity to ping more peers
		// candidates before trying to connect
		set.set_int(settings_pack::dht_upload_rate_limit, 20000);

		// use 1 GB of cache
		set.set_int(settings_pack::cache_size, 32768 * 2);
		set.set_bool(settings_pack::use_read_cache, true);
		set.set_int(settings_pack::cache_buffer_chunk_size, 0);
		set.set_int(settings_pack::read_cache_line_size, 32);
		set.set_int(settings_pack::write_cache_line_size, 256);
		set.set_bool(settings_pack::low_prio_disk, false);
		// 30 seconds expiration to save cache
		// space for active pieces
		set.set_int(settings_pack::cache_expiry, 30);
		// this is expensive and could add significant
		// delays when freeing a large number of buffers
		set.set_bool(settings_pack::lock_disk_cache, false);

		// in case the OS we're running on doesn't support
		// readv/writev, allocate contiguous buffers for
		// reads and writes
		// disable, since it uses a lot more RAM and a significant
		// amount of CPU to copy it around
		set.set_bool(settings_pack::coalesce_reads, false);
		set.set_bool(settings_pack::coalesce_writes, false);

		// the max number of bytes pending write before we throttle
		// download rate
		set.set_int(settings_pack::max_queued_disk_bytes, 7 * 1024 * 1024);

		set.set_bool(settings_pack::explicit_read_cache, false);
		// prevent fast pieces to interfere with suggested pieces
		// since we unchoke everyone, we don't need fast pieces anyway
		set.set_int(settings_pack::allowed_fast_set_size, 0);

		// suggest pieces in the read cache for higher cache hit rate
		set.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);

		set.set_bool(settings_pack::close_redundant_connections, true);

		set.set_int(settings_pack::max_rejects, 10);

		set.set_int(settings_pack::recv_socket_buffer_size, 1024 * 1024);
		set.set_int(settings_pack::send_socket_buffer_size, 1024 * 1024);

		// don't let connections linger for too long
		set.set_int(settings_pack::request_timeout, 10);
		set.set_int(settings_pack::peer_timeout, 20);
		set.set_int(settings_pack::inactivity_timeout, 20);

		set.set_int(settings_pack::active_limit, 2000);
		set.set_int(settings_pack::active_tracker_limit, 2000);
		set.set_int(settings_pack::active_dht_limit, 600);
		set.set_int(settings_pack::active_seeds, 2000);

		set.set_int(settings_pack::choking_algorithm, settings_pack::fixed_slots_choker);

		// of 500 ms, and a send rate of 4 MB/s, the upper
		// limit should be 2 MB
		set.set_int(settings_pack::send_buffer_watermark, 3 * 1024 * 1024);

		// put 1.5 seconds worth of data in the send buffer
		// this gives the disk I/O more heads-up on disk
		// reads, and can maximize throughput
		set.set_int(settings_pack::send_buffer_watermark_factor, 150);

		// always stuff at least 1 MiB down each peer
		// pipe, to quickly ramp up send rates
 		set.set_int(settings_pack::send_buffer_low_watermark, 1 * 1024 * 1024);

		// don't retry peers if they fail once. Let them
		// connect to us if they want to
		set.set_int(settings_pack::max_failcount, 1);

		// allow the buffer size to grow for the uTP socket
		set.set_bool(settings_pack::utp_dynamic_sock_buf, true);

		// we're likely to have more than 4 cores on a high
		// performance machine. One core is needed for the
		// network thread
		set.set_int(settings_pack::hashing_threads, 4);

		// the number of threads to use to call async_write_some
		// and read_some on peer sockets
		// this doesn't work. See comment in settings_pack.cpp
		set.set_int(settings_pack::network_threads, 0);

		// number of disk threads for low level file operations
		set.set_int(settings_pack::aio_threads, 8);

		// keep 5 MiB outstanding when checking hashes
		// of a resumed file
		set.set_int(settings_pack::checking_mem_usage, 320);

		// the disk cache performs better with the pool allocator
		set.set_bool(settings_pack::use_disk_cache_pool, true);
	}

#ifndef TORRENT_NO_DEPRECATE
	// this function returns a session_settings object
	// which will optimize libtorrent for minimum memory
	// usage, with no consideration of performance.
	TORRENT_EXPORT session_settings min_memory_usage()
	{
		aux::session_settings def;
		initialize_default_settings(def);
		settings_pack pack;
		min_memory_usage(pack);
		apply_pack(&pack, def, 0);
		session_settings ret;
		load_struct_from_settings(def, ret);
		return ret;
	}

	TORRENT_EXPORT session_settings high_performance_seed()
	{
		aux::session_settings def;
		initialize_default_settings(def);
		settings_pack pack;
		high_performance_seed(pack);
		apply_pack(&pack, def, 0);
		session_settings ret;
		load_struct_from_settings(def, ret);
		return ret;
	}
#endif

#define TORRENT_ASYNC_CALL(x) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get()))

#define TORRENT_ASYNC_CALL1(x, a1) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1))

#define TORRENT_ASYNC_CALL2(x, a1, a2) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1, a2))

#define TORRENT_ASYNC_CALL3(x, a1, a2, a3) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3))

#define TORRENT_SYNC_CALL(x) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get())))

#define TORRENT_SYNC_CALL1(x, a1) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))

#define TORRENT_SYNC_CALL2(x, a1, a2) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))

#define TORRENT_SYNC_CALL3(x, a1, a2, a3) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3)))

#define TORRENT_SYNC_CALL4(x, a1, a2, a3, a4) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3, a4)))

#define TORRENT_SYNC_CALL_RET(type, x) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get())))

#define TORRENT_SYNC_CALL_RET1(type, x, a1) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))

#define TORRENT_SYNC_CALL_RET2(type, x, a1, a2) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))

#define TORRENT_SYNC_CALL_RET3(type, x, a1, a2, a3) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3)))

#ifndef TORRENT_CFG
#error TORRENT_CFG is not defined!
#endif

	// this is a dummy function that's exported and named based
	// on the configuration. The session.hpp file will reference
	// it and if the library and the client are built with different
	// configurations this will give a link error
	void TORRENT_EXPORT TORRENT_CFG() {}

#if defined _MSC_VER && defined TORRENT_DEBUG
	static void straight_to_debugger(unsigned int, _EXCEPTION_POINTERS*)
	{ throw; }
#endif

	void session::init()
	{
#if defined _MSC_VER && defined TORRENT_DEBUG
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
		::_set_se_translator(straight_to_debugger);
#endif

		m_impl.reset(new session_impl());
	}

	void session::start(int flags, settings_pack const& pack)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
			add_extension(create_smart_ban_plugin);
		}
#endif

		m_impl->start_session(pack);
	}

	session::~session()
	{
		aux::dump_call_profile();

		TORRENT_ASSERT(m_impl);
		// if there is at least one destruction-proxy
		// abort the session and let the destructor
		// of the proxy to syncronize
		if (!m_impl.unique())
		{
			TORRENT_ASYNC_CALL(abort);
		}
	}

	void session::save_state(entry& e, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL2(save_state, &e, flags);
	}

	void session::load_state(bdecode_node const& e)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		TORRENT_SYNC_CALL1(load_state, &e);
	}

#ifndef TORRENT_NO_DEPRECATE
	feed_handle session::add_feed(feed_settings const& feed)
	{
		// if you have auto-download enabled, you must specify a download directory!
		TORRENT_ASSERT_PRECOND(!feed.auto_download || !feed.add_args.save_path.empty());
		return TORRENT_SYNC_CALL_RET1(feed_handle, add_feed, feed);
	}

	void session::remove_feed(feed_handle h)
	{
		TORRENT_ASYNC_CALL1(remove_feed, h);
	}

	void session::get_feeds(std::vector<feed_handle>& f) const
	{
		f.clear();
		TORRENT_SYNC_CALL1(get_feeds, &f);
	}
#endif

	void session::set_load_function(user_load_function_t fun)
	{
		TORRENT_ASYNC_CALL1(set_load_function, fun);
	}

	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		TORRENT_ASYNC_CALL1(add_extension, ext);
#endif
	}

	void session::add_extension(boost::shared_ptr<plugin> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		TORRENT_ASYNC_CALL1(add_ses_extension, ext);
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::load_asnum_db(char const* file) {}
	void session::load_country_db(char const* file) {}

	int session::as_for_ip(address const& addr)
	{ return 0; }

#if TORRENT_USE_WSTRING
	void session::load_asnum_db(wchar_t const* file) {}
	void session::load_country_db(wchar_t const* file) {}
#endif // TORRENT_USE_WSTRING

	void session::load_state(entry const& ses_state)
	{
		if (ses_state.type() == entry::undefined_t) return;
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		bdecode_node e;
		error_code ec;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) throw libtorrent_exception(ec);
#endif
		TORRENT_SYNC_CALL1(load_state, &e);
	}

	void session::load_state(lazy_entry const& ses_state)
	{
		if (ses_state.type() == lazy_entry::none_t) return;
		std::pair<char const*, int> buf = ses_state.data_section();
		bdecode_node e;
		error_code ec;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(buf.first, buf.first + buf.second, e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) throw libtorrent_exception(ec);
#endif
		TORRENT_SYNC_CALL1(load_state, &e);
	}

	entry session::state() const
	{
		entry ret;
		TORRENT_SYNC_CALL2(save_state, &ret, 0xffffffff);
		return ret;
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_ip_filter(ip_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_ip_filter, f);
	}
	
	ip_filter session::get_ip_filter() const
	{
		return TORRENT_SYNC_CALL_RET(ip_filter, get_ip_filter);
	}

	void session::set_port_filter(port_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_port_filter, f);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_peer_id(peer_id const& id)
	{
		settings_pack p;
		p.set_str(settings_pack::peer_fingerprint, id.to_string());
		apply_settings(p);
	}
#endif
	
	peer_id session::id() const
	{
		return TORRENT_SYNC_CALL_RET(peer_id, get_peer_id);
	}

	io_service& session::get_io_service()
	{
		return m_impl->m_io_service;
	}

	void session::set_key(int key)
	{
		TORRENT_ASYNC_CALL1(set_key, key);
	}

	void session::get_torrent_status(std::vector<torrent_status>* ret
		, boost::function<bool(torrent_status const&)> const& pred
		, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL3(get_torrent_status, ret, boost::ref(pred), flags);
	}

	void session::refresh_torrent_status(std::vector<torrent_status>* ret
		, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL2(refresh_torrent_status, ret, flags);
	}

	void session::post_torrent_updates(boost::uint32_t flags)
	{
		TORRENT_ASYNC_CALL1(post_torrent_updates, flags);
	}

	std::vector<stats_metric> session_stats_metrics()
	{
		std::vector<stats_metric> ret;
		// defined in session_stats.cpp
		extern void get_stats_metric_map(std::vector<stats_metric>& stats);
		get_stats_metric_map(ret);
		return ret;
	}

	void session::post_session_stats()
	{
		TORRENT_ASYNC_CALL(post_session_stats);
	}

	void session::post_dht_stats()
	{
		TORRENT_ASYNC_CALL(post_dht_stats);
	}

	std::vector<torrent_handle> session::get_torrents() const
	{
		return TORRENT_SYNC_CALL_RET(std::vector<torrent_handle>, get_torrents);
	}
	
	torrent_handle session::find_torrent(sha1_hash const& info_hash) const
	{
		return TORRENT_SYNC_CALL_RET1(torrent_handle, find_torrent_handle, info_hash);
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session::add_torrent(add_torrent_params const& params)
	{
		error_code ec;
		torrent_handle r = TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
		if (ec) throw libtorrent_exception(ec);
		return r;
	}
#endif

	torrent_handle session::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		ec.clear();
		return TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
	}

	void session::async_add_torrent(add_torrent_params const& params)
	{
		add_torrent_params* p = new add_torrent_params(params);
#ifndef TORRENT_NO_DEPRECATE
		if (params.tracker_url)
		{
			p->trackers.push_back(params.tracker_url);
			p->tracker_url = NULL;
		}
#endif
		TORRENT_ASYNC_CALL1(async_add_torrent, p);
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		torrent_info const& ti
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc)
	{
		boost::shared_ptr<torrent_info> tip(boost::make_shared<torrent_info>(ti));
		add_torrent_params p(sc);
		p.ti = tip;
		p.save_path = save_path;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(p.resume_data), resume_data);
		}
		p.storage_mode = storage_mode;
		p.paused = paused;
		return add_torrent(p);
	}

	torrent_handle session::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, std::string const& save_path
		, entry const& e
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		add_torrent_params p(sc);
		p.tracker_url = tracker_url;
		p.info_hash = info_hash;
		p.save_path = save_path;
		p.paused = paused;
		p.userdata = userdata;
		return add_torrent(p);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // BOOST_NO_EXCEPTIONS

	void session::remove_torrent(const torrent_handle& h, int options)
	{
		if (!h.is_valid())
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw_invalid_handle();
#endif
		TORRENT_ASYNC_CALL2(remove_torrent, h, options);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::listen_on(
		std::pair<int, int> const& port_range
		, error_code& ec
		, const char* net_interface, int flags)
	{
		settings_pack p;
		std::string interfaces_str;
		if (net_interface == NULL || strlen(net_interface) == 0)
			net_interface = "0.0.0.0";

		interfaces_str = print_endpoint(tcp::endpoint(address::from_string(net_interface, ec), port_range.first));
		if (ec) return;

		p.set_str(settings_pack::listen_interfaces, interfaces_str);
		p.set_int(settings_pack::max_retry_port_bind, port_range.second - port_range.first);
		p.set_bool(settings_pack::listen_system_port_fallback, (flags & session::listen_no_system_port) == 0);
		apply_settings(p);
	}

	void session::use_interfaces(char const* interfaces)
	{
		settings_pack pack;
		pack.set_str(settings_pack::outgoing_interfaces, interfaces);
		apply_settings(pack);
	}
#endif

	unsigned short session::listen_port() const
	{
		return TORRENT_SYNC_CALL_RET(unsigned short, listen_port);
	}

	unsigned short session::ssl_listen_port() const
	{
		return TORRENT_SYNC_CALL_RET(unsigned short, ssl_listen_port);
	}

	void session::pause()
	{
		TORRENT_ASYNC_CALL(pause);
	}

	void session::resume()
	{
		TORRENT_ASYNC_CALL(resume);
	}

	bool session::is_paused() const
	{
		return TORRENT_SYNC_CALL_RET(bool, is_paused);
	}

#ifndef TORRENT_NO_DEPRECATE
	session_status session::status() const
	{
		return TORRENT_SYNC_CALL_RET(session_status, status);
	}

	void session::get_cache_info(sha1_hash const& ih
		, std::vector<cached_piece_info>& ret) const
	{
		cache_status st;
		get_cache_info(&st, find_torrent(ih));
		ret.swap(st.pieces);
	}

	cache_status session::get_cache_status() const
	{
		cache_status st;
		get_cache_info(&st);
		return st;
	}
#endif

	void session::get_cache_info(cache_status* ret
		, torrent_handle h, int flags) const
	{
		piece_manager* st = 0;
		boost::shared_ptr<torrent> t = h.m_torrent.lock();
		if (t)
		{
			if (t->has_storage())
				st = &t->storage();
			else
				flags = session::disk_cache_no_pieces;
		}
		m_impl->m_disk_thread.get_cache_info(ret, flags & session::disk_cache_no_pieces, st);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, true);
		apply_settings(p);
	}

	void session::stop_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, false);
		apply_settings(p);
	}
#endif

	void session::set_dht_settings(dht_settings const& settings)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(set_dht_settings, settings);
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht(entry const& startup_state)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(start_dht, startup_state);
#endif
	}

	entry session::dht_state() const
	{
#ifndef TORRENT_DISABLE_DHT
		return TORRENT_SYNC_CALL_RET(entry, dht_state);
#else
		return entry();
#endif
	}
#endif // TORRENT_NO_DEPRECATE
	
	void session::add_dht_node(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(add_dht_node_name, node);
#endif
	}

	void session::add_dht_router(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(add_dht_router, node);
#endif
	}

	bool session::is_dht_running() const
	{
#ifndef TORRENT_DISABLE_DHT
		return TORRENT_SYNC_CALL_RET(bool, is_dht_running);
#else
		return false;
#endif
	}

	void session::dht_get_item(sha1_hash const& target)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(dht_get_immutable_item, target);
#endif
	}

	void session::dht_get_item(boost::array<char, 32> key
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL2(dht_get_mutable_item, key, salt);
#endif
	}

	sha1_hash session::dht_put_item(entry data)
	{
		std::vector<char> buf;
		bencode(std::back_inserter(buf), data);
		sha1_hash ret = hasher(&buf[0], buf.size()).final();
	
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL2(dht_put_item, data, ret);
#endif
		return ret;
	}

	void session::dht_put_item(boost::array<char, 32> key
		, boost::function<void(entry&, boost::array<char,64>&
			, boost::uint64_t&, std::string const&)> cb
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL3(dht_put_mutable_item, key, cb, salt);
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_pe_settings(pe_settings const& r)
	{
		settings_pack pack;
		pack.set_bool(settings_pack::prefer_rc4, r.prefer_rc4);
		pack.set_int(settings_pack::out_enc_policy, r.out_enc_policy);
		pack.set_int(settings_pack::in_enc_policy, r.in_enc_policy);
		pack.set_int(settings_pack::allowed_enc_level, r.allowed_enc_level);

		apply_settings(pack);
	}

	pe_settings session::get_pe_settings() const
	{
		aux::session_settings sett = get_settings();

		pe_settings r;
		r.prefer_rc4 = sett.get_bool(settings_pack::prefer_rc4);
		r.out_enc_policy = sett.get_int(settings_pack::out_enc_policy);
		r.in_enc_policy = sett.get_int(settings_pack::in_enc_policy);
		r.allowed_enc_level = sett.get_int(settings_pack::allowed_enc_level);
		return r;
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_peer_class_filter(ip_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_peer_class_filter, f);
	}

	void session::set_peer_class_type_filter(peer_class_type_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_peer_class_type_filter, f);
	}

	int session::create_peer_class(char const* name)
	{
		return TORRENT_SYNC_CALL_RET1(int, create_peer_class, name);
	}

	void session::delete_peer_class(int cid)
	{
		TORRENT_ASYNC_CALL1(delete_peer_class, cid);
	}

	peer_class_info session::get_peer_class(int cid)
	{
		return TORRENT_SYNC_CALL_RET1(peer_class_info, get_peer_class, cid);
	}

	void session::set_peer_class(int cid, peer_class_info const& pci)
	{
		TORRENT_ASYNC_CALL2(set_peer_class, cid, pci);
	}

	bool session::is_listening() const
	{
		return TORRENT_SYNC_CALL_RET(bool, is_listening);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_settings(session_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_settings, s);
	}

	session_settings session::settings() const
	{
		return TORRENT_SYNC_CALL_RET(session_settings, deprecated_settings);
	}
#endif

	void session::apply_settings(settings_pack const& s)
	{
		settings_pack* copy = new settings_pack(s);
		TORRENT_ASYNC_CALL1(apply_settings_pack, copy);
	}

	aux::session_settings session::get_settings() const
	{
		return TORRENT_SYNC_CALL_RET(aux::session_settings, settings);
	}

#ifndef TORRENT_NO_DEPRECATE

	void session::set_proxy(proxy_settings const& s)
	{
		settings_pack pack;
		pack.set_str(settings_pack::proxy_hostname, s.hostname);
		pack.set_str(settings_pack::proxy_username, s.username);
		pack.set_str(settings_pack::proxy_password, s.password);
		pack.set_int(settings_pack::proxy_type, s.type);
		pack.set_int(settings_pack::proxy_port, s.port);
		pack.set_bool(settings_pack::proxy_hostnames,s.proxy_hostnames);
		pack.set_bool(settings_pack::proxy_peer_connections, s.proxy_peer_connections);

		apply_settings(pack);
	}

	proxy_settings session::proxy() const
	{
		aux::session_settings sett = get_settings();
		return proxy_settings(sett);
	}

	void session::set_peer_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	void session::set_web_seed_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	void session::set_tracker_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	proxy_settings session::peer_proxy() const
	{
		return proxy();
	}

	proxy_settings session::web_seed_proxy() const
	{
		return proxy();
	}

	proxy_settings session::tracker_proxy() const
	{
		return proxy();
	}

	void session::set_dht_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	proxy_settings session::dht_proxy() const
	{
		return proxy();
	}

	void session::set_i2p_proxy(proxy_settings const& s)
	{
		settings_pack pack;
		pack.set_str(settings_pack::i2p_hostname, s.hostname);
		pack.set_int(settings_pack::i2p_port, s.port);

		apply_settings(pack);
	}
	
	proxy_settings session::i2p_proxy() const
	{
		proxy_settings ret;
		aux::session_settings sett = get_settings();
		ret.hostname = sett.get_str(settings_pack::i2p_hostname);
		ret.port = sett.get_int(settings_pack::i2p_port);
		return ret;
	}

	void session::set_max_half_open_connections(int limit) {}
	int session::max_half_open_connections() const { return 8; }

	int session::max_uploads() const
	{
		return TORRENT_SYNC_CALL_RET(int, max_uploads);
	}

	void session::set_max_uploads(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_uploads, limit);
	}

	int session::max_connections() const
	{
		return TORRENT_SYNC_CALL_RET(int, max_connections);
	}

	void session::set_max_connections(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_connections, limit);
	}

	int session::local_upload_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, local_upload_rate_limit);
	}

	int session::local_download_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, local_download_rate_limit);
	}

	int session::upload_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, upload_rate_limit);
	}

	int session::download_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, download_rate_limit);
	}

	void session::set_local_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_local_upload_rate_limit, bytes_per_second);
	}

	void session::set_local_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_local_download_rate_limit, bytes_per_second);
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_upload_rate_limit, bytes_per_second);
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_download_rate_limit, bytes_per_second);
	}

	int session::num_uploads() const
	{
		return TORRENT_SYNC_CALL_RET(int, num_uploads);
	}

	int session::num_connections() const
	{
		return TORRENT_SYNC_CALL_RET(int, num_connections);
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		TORRENT_ASYNC_CALL1(set_alert_dispatch, fun);
	}

	std::auto_ptr<alert> session::pop_alert()
	{
		return m_impl->pop_alert();
	}

	void session::pop_alerts(std::deque<alert*>* alerts)
	{
		for (std::deque<alert*>::iterator i = alerts->begin()
			, end(alerts->end()); i != end; ++i)
			delete *i;
		alerts->clear();
		m_impl->pop_alerts(alerts);
	}

	alert const* session::wait_for_alert(time_duration max_wait)
	{
		return m_impl->wait_for_alert(max_wait);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_alert_mask(boost::uint32_t m)
	{
		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(p);
	}

	boost::uint32_t session::get_alert_mask() const
	{
		return get_settings().get_int(settings_pack::alert_mask);
	}

	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return TORRENT_SYNC_CALL_RET1(size_t, set_alert_queue_size_limit, queue_size_limit_);
	}

	void session::set_severity_level(alert::severity_t s)
	{
		int m = 0;
		switch (s)
		{
			case alert::debug: m = alert::all_categories; break;
			case alert::info: m = alert::all_categories & ~(alert::debug_notification
				| alert::progress_notification | alert::dht_notification); break;
			case alert::warning: m = alert::all_categories & ~(alert::debug_notification
				| alert::status_notification | alert::progress_notification
				| alert::dht_notification); break;
			case alert::critical: m = alert::error_notification | alert::storage_notification; break;
			case alert::fatal: m = alert::error_notification; break;
			default: break;
		}

		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(p);
	}

	void session::start_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, true);
		apply_settings(p);
	}
	
	void session::start_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, true);
		apply_settings(p);
	}
	
	void session::start_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, true);
		apply_settings(p);
	}

	void session::stop_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, false);
		apply_settings(p);
	}
	
	void session::stop_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, false);
		apply_settings(p);
	}
	
	void session::stop_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, false);
		apply_settings(p);
	}
#endif
	
	int session::add_port_mapping(protocol_type t, int external_port, int local_port)
	{
		return TORRENT_SYNC_CALL_RET3(int, add_port_mapping, int(t), external_port, local_port);
	}

	void session::delete_port_mapping(int handle)
	{
		TORRENT_ASYNC_CALL1(delete_port_mapping, handle);
	}
	
#ifndef TORRENT_NO_DEPRECATE
	session_settings::session_settings(std::string const& user_agent_)
	{
		aux::session_settings def;
		initialize_default_settings(def);
		def.set_str(settings_pack::user_agent, user_agent_);
		load_struct_from_settings(def, *this);
	}

	session_settings::~session_settings() {}
#endif // TORRENT_NO_DEPRECATE

}

