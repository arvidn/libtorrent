/*

Copyright (c) 2006-2015, Arvid Norberg, Magnus Jonsson
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
#include "libtorrent/session_handle.hpp"
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
#include "libtorrent/lazy_entry.hpp"

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
	m_impl->get_io_service().dispatch(boost::bind(&session_impl:: x, m_impl.get()))

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

	void session::start(int flags, settings_pack const& pack, io_service* ios)
	{
#if defined _MSC_VER && defined TORRENT_DEBUG
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
		::_set_se_translator(straight_to_debugger);
#endif

		bool internal_executor = ios == NULL;

		if (internal_executor)
		{
			// the user did not provide an executor, we have to use our own
			m_io_service = boost::make_shared<io_service>();
			ios = m_io_service.get();
		}

		m_impl = boost::make_shared<session_impl>(boost::ref(*ios));

#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
			add_extension(create_smart_ban_plugin);
		}
#endif

		m_impl->start_session(pack);

		if (internal_executor)
		{
			// start a thread for the message pump
			m_thread = boost::make_shared<thread>(boost::bind(&io_service::run
				, m_io_service.get()));
		}
	}

	session::~session()
	{
		aux::dump_call_profile();

		TORRENT_ASSERT(m_impl);
		TORRENT_ASYNC_CALL(abort);

		if (m_thread && m_thread.unique())
			m_thread->join();
	}

	void session::save_state(entry& e, boost::uint32_t flags) const
	{
		session_handle(m_impl.get()).save_state(e, flags);
	}

	void session::load_state(bdecode_node const& e)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		session_handle(m_impl.get()).load_state(e);
	}

#ifndef TORRENT_NO_DEPRECATE
	feed_handle session::add_feed(feed_settings const& feed)
	{
		return session_handle(m_impl.get()).add_feed(feed);
	}

	void session::remove_feed(feed_handle h)
	{
		session_handle(m_impl.get()).remove_feed(h);
	}

	void session::get_feeds(std::vector<feed_handle>& f) const
	{
		session_handle(m_impl.get()).get_feeds(f);
	}
#endif

	void session::set_load_function(user_load_function_t fun)
	{
		session_handle(m_impl.get()).set_load_function(fun);
	}

	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
		session_handle(m_impl.get()).add_extension(ext);
	}

	void session::add_extension(boost::shared_ptr<plugin> ext)
	{
		session_handle(m_impl.get()).add_extension(ext);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::load_asnum_db(char const*) {}
	void session::load_country_db(char const*) {}

	int session::as_for_ip(address const&)
	{ return 0; }

#if TORRENT_USE_WSTRING
	void session::load_asnum_db(wchar_t const*) {}
	void session::load_country_db(wchar_t const*) {}
#endif // TORRENT_USE_WSTRING

	void session::load_state(entry const& ses_state)
	{
		session_handle(m_impl.get()).load_state(ses_state);
	}

	void session::load_state(lazy_entry const& ses_state)
	{
		session_handle(m_impl.get()).load_state(ses_state);
	}

	entry session::state() const
	{
		return session_handle(m_impl.get()).state();
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_ip_filter(ip_filter const& f)
	{
		session_handle(m_impl.get()).set_ip_filter(f);
	}

	ip_filter session::get_ip_filter() const
	{
		return session_handle(m_impl.get()).get_ip_filter();
	}

	void session::set_port_filter(port_filter const& f)
	{
		session_handle(m_impl.get()).set_port_filter(f);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_peer_id(peer_id const& id)
	{
		session_handle(m_impl.get()).set_peer_id(id);
	}
#endif

	peer_id session::id() const
	{
		return session_handle(m_impl.get()).id();
	}

	io_service& session::get_io_service()
	{
		return session_handle(m_impl.get()).get_io_service();
	}

	void session::set_key(int key)
	{
		session_handle(m_impl.get()).set_key(key);
	}

	void session::get_torrent_status(std::vector<torrent_status>* ret
		, boost::function<bool(torrent_status const&)> const& pred
		, boost::uint32_t flags) const
	{
		session_handle(m_impl.get()).get_torrent_status(ret, pred, flags);
	}

	void session::refresh_torrent_status(std::vector<torrent_status>* ret
		, boost::uint32_t flags) const
	{
		session_handle(m_impl.get()).refresh_torrent_status(ret, flags);
	}

	void session::post_torrent_updates(boost::uint32_t flags)
	{
		session_handle(m_impl.get()).post_torrent_updates(flags);
	}

	void session::post_session_stats()
	{
		session_handle(m_impl.get()).post_session_stats();
	}

	void session::post_dht_stats()
	{
		session_handle(m_impl.get()).post_dht_stats();
	}

	std::vector<torrent_handle> session::get_torrents() const
	{
		return session_handle(m_impl.get()).get_torrents();
	}

	torrent_handle session::find_torrent(sha1_hash const& info_hash) const
	{
		return session_handle(m_impl.get()).find_torrent(info_hash);
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session::add_torrent(add_torrent_params const& params)
	{
		return session_handle(m_impl.get()).add_torrent(params);
	}
#endif

	torrent_handle session::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		return session_handle(m_impl.get()).add_torrent(params, ec);
	}

	void session::async_add_torrent(add_torrent_params const& params)
	{
		session_handle(m_impl.get()).async_add_torrent(params);
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
		return session_handle(m_impl.get()).add_torrent(ti, save_path
			, resume_data, storage_mode, paused, sc);
	}

	torrent_handle session::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		return session_handle(m_impl.get()).add_torrent(tracker_url, info_hash
			, name, save_path, resume_data, storage_mode, paused, sc, userdata);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // BOOST_NO_EXCEPTIONS

	void session::remove_torrent(const torrent_handle& h, int options)
	{
		session_handle(m_impl.get()).remove_torrent(h, options);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::listen_on(
		std::pair<int, int> const& port_range
		, error_code& ec
		, const char* net_interface, int flags)
	{
		session_handle(m_impl.get()).listen_on(port_range, ec, net_interface, flags);
	}

	void session::use_interfaces(char const* interfaces)
	{
		session_handle(m_impl.get()).use_interfaces(interfaces);
	}
#endif

	unsigned short session::listen_port() const
	{
		return session_handle(m_impl.get()).listen_port();
	}

	unsigned short session::ssl_listen_port() const
	{
		return session_handle(m_impl.get()).ssl_listen_port();
	}

	void session::pause()
	{
		session_handle(m_impl.get()).pause();
	}

	void session::resume()
	{
		session_handle(m_impl.get()).resume();
	}

	bool session::is_paused() const
	{
		return session_handle(m_impl.get()).is_paused();
	}

#ifndef TORRENT_NO_DEPRECATE
	session_status session::status() const
	{
		return session_handle(m_impl.get()).status();
	}

	void session::get_cache_info(sha1_hash const& ih
		, std::vector<cached_piece_info>& ret) const
	{
		session_handle(m_impl.get()).get_cache_info(ih, ret);
	}

	cache_status session::get_cache_status() const
	{
		return session_handle(m_impl.get()).get_cache_status();
	}
#endif

	void session::get_cache_info(cache_status* ret
		, torrent_handle h, int flags) const
	{
		session_handle(m_impl.get()).get_cache_info(ret, h, flags);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht()
	{
		session_handle(m_impl.get()).start_dht();
	}

	void session::stop_dht()
	{
		session_handle(m_impl.get()).stop_dht();
	}
#endif

	void session::set_dht_settings(dht_settings const& settings)
	{
		session_handle(m_impl.get()).set_dht_settings(settings);
	}

	dht_settings session::get_dht_settings() const
	{
		return session_handle(m_impl.get()).get_dht_settings();
	}


#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht(entry const& startup_state)
	{
		session_handle(m_impl.get()).start_dht(startup_state);
	}

	entry session::dht_state() const
	{
		return session_handle(m_impl.get()).dht_state();
	}
#endif // TORRENT_NO_DEPRECATE

	void session::add_dht_node(std::pair<std::string, int> const& node)
	{
		session_handle(m_impl.get()).add_dht_node(node);
	}

	void session::add_dht_router(std::pair<std::string, int> const& node)
	{
		session_handle(m_impl.get()).add_dht_router(node);
	}

	bool session::is_dht_running() const
	{
		return session_handle(m_impl.get()).is_dht_running();
	}

	void session::dht_get_item(sha1_hash const& target)
	{
		session_handle(m_impl.get()).dht_get_item(target);
	}

	void session::dht_get_item(boost::array<char, 32> key
		, std::string salt)
	{
		session_handle(m_impl.get()).dht_get_item(key, salt);
	}

	sha1_hash session::dht_put_item(entry data)
	{
		return session_handle(m_impl.get()).dht_put_item(data);
	}

	void session::dht_put_item(boost::array<char, 32> key
		, boost::function<void(entry&, boost::array<char,64>&
			, boost::uint64_t&, std::string const&)> cb
		, std::string salt)
	{
		session_handle(m_impl.get()).dht_put_item(key, cb, salt);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_pe_settings(pe_settings const& r)
	{
		session_handle(m_impl.get()).set_pe_settings(r);
	}

	pe_settings session::get_pe_settings() const
	{
		return session_handle(m_impl.get()).get_pe_settings();
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_peer_class_filter(ip_filter const& f)
	{
		session_handle(m_impl.get()).set_peer_class_filter(f);
	}

	void session::set_peer_class_type_filter(peer_class_type_filter const& f)
	{
		session_handle(m_impl.get()).set_peer_class_type_filter(f);
	}

	int session::create_peer_class(char const* name)
	{
		return session_handle(m_impl.get()).create_peer_class(name);
	}

	void session::delete_peer_class(int cid)
	{
		session_handle(m_impl.get()).delete_peer_class(cid);
	}

	peer_class_info session::get_peer_class(int cid)
	{
		return session_handle(m_impl.get()).get_peer_class(cid);
	}

	void session::set_peer_class(int cid, peer_class_info const& pci)
	{
		session_handle(m_impl.get()).set_peer_class(cid, pci);
	}

	bool session::is_listening() const
	{
		return session_handle(m_impl.get()).is_listening();
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_settings(session_settings const& s)
	{
		session_handle(m_impl.get()).set_settings(s);
	}

	session_settings session::settings() const
	{
		return session_handle(m_impl.get()).settings();
	}
#endif

	void session::apply_settings(settings_pack const& s)
	{
		session_handle(m_impl.get()).apply_settings(s);
	}

	settings_pack session::get_settings() const
	{
		return session_handle(m_impl.get()).get_settings();
	}

#ifndef TORRENT_NO_DEPRECATE

	void session::set_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_proxy(s);
	}

	proxy_settings session::proxy() const
	{
		return session_handle(m_impl.get()).proxy();
	}

	void session::set_peer_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_peer_proxy(s);
	}

	void session::set_web_seed_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_web_seed_proxy(s);
	}

	void session::set_tracker_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_tracker_proxy(s);
	}

	proxy_settings session::peer_proxy() const
	{
		return session_handle(m_impl.get()).peer_proxy();
	}

	proxy_settings session::web_seed_proxy() const
	{
		return session_handle(m_impl.get()).web_seed_proxy();
	}

	proxy_settings session::tracker_proxy() const
	{
		return session_handle(m_impl.get()).tracker_proxy();
	}

	void session::set_dht_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_dht_proxy(s);
	}

	proxy_settings session::dht_proxy() const
	{
		return session_handle(m_impl.get()).dht_proxy();
	}

	void session::set_i2p_proxy(proxy_settings const& s)
	{
		session_handle(m_impl.get()).set_i2p_proxy(s);
	}

	proxy_settings session::i2p_proxy() const
	{
		return session_handle(m_impl.get()).i2p_proxy();
	}

	void session::set_max_half_open_connections(int) {}
	int session::max_half_open_connections() const { return 8; }

	int session::max_uploads() const
	{
		return session_handle(m_impl.get()).max_uploads();
	}

	void session::set_max_uploads(int limit)
	{
		session_handle(m_impl.get()).set_max_uploads(limit);
	}

	int session::max_connections() const
	{
		return session_handle(m_impl.get()).max_connections();
	}

	void session::set_max_connections(int limit)
	{
		session_handle(m_impl.get()).set_max_connections(limit);
	}

	int session::local_upload_rate_limit() const
	{
		return session_handle(m_impl.get()).local_upload_rate_limit();
	}

	int session::local_download_rate_limit() const
	{
		return session_handle(m_impl.get()).local_download_rate_limit();
	}

	int session::upload_rate_limit() const
	{
		return session_handle(m_impl.get()).upload_rate_limit();
	}

	int session::download_rate_limit() const
	{
		return session_handle(m_impl.get()).download_rate_limit();
	}

	void session::set_local_upload_rate_limit(int bytes_per_second)
	{
		session_handle(m_impl.get()).set_local_upload_rate_limit(bytes_per_second);
	}

	void session::set_local_download_rate_limit(int bytes_per_second)
	{
		session_handle(m_impl.get()).set_local_download_rate_limit(bytes_per_second);
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		session_handle(m_impl.get()).set_upload_rate_limit(bytes_per_second);
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		session_handle(m_impl.get()).set_download_rate_limit(bytes_per_second);
	}

	int session::num_uploads() const
	{
		return session_handle(m_impl.get()).num_uploads();
	}

	int session::num_connections() const
	{
		return session_handle(m_impl.get()).num_connections();
	}

	void session::set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		session_handle(m_impl.get()).set_alert_dispatch(fun);
	}
#endif // TORRENT_NO_DEPRECATE

	alert* session::wait_for_alert(time_duration max_wait)
	{
		return session_handle(m_impl.get()).wait_for_alert(max_wait);
	}

	// the alerts are const, they may not be deleted by the client
	void session::pop_alerts(std::vector<alert*>* alerts)
	{
		session_handle(m_impl.get()).pop_alerts(alerts);
	}

	void session::set_alert_notify(boost::function<void()> const& fun)
	{
		session_handle(m_impl.get()).set_alert_notify(fun);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::pop_alerts(std::deque<alert*>* alerts)
	{
		session_handle(m_impl.get()).pop_alerts(alerts);
	}

	std::auto_ptr<alert> session::pop_alert()
	{
		return session_handle(m_impl.get()).pop_alert();
	}

	void session::set_alert_mask(boost::uint32_t m)
	{
		session_handle(m_impl.get()).set_alert_mask(m);
	}

	boost::uint32_t session::get_alert_mask() const
	{
		return session_handle(m_impl.get()).get_alert_mask();
	}

	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return session_handle(m_impl.get()).set_alert_queue_size_limit(queue_size_limit_);
	}

	void session::set_severity_level(alert::severity_t s)
	{
		session_handle(m_impl.get()).set_severity_level(s);
	}

	void session::start_lsd()
	{
		session_handle(m_impl.get()).start_lsd();
	}

	void session::start_natpmp()
	{
		session_handle(m_impl.get()).start_natpmp();
	}

	void session::start_upnp()
	{
		session_handle(m_impl.get()).start_upnp();
	}

	void session::stop_lsd()
	{
		session_handle(m_impl.get()).stop_lsd();
	}

	void session::stop_natpmp()
	{
		session_handle(m_impl.get()).stop_natpmp();
	}

	void session::stop_upnp()
	{
		session_handle(m_impl.get()).stop_upnp();
	}
#endif // TORRENT_NO_DEPRECATED

	int session::add_port_mapping(protocol_type t, int external_port, int local_port)
	{
		return session_handle(m_impl.get()).add_port_mapping(t, external_port, local_port);
	}

	void session::delete_port_mapping(int handle)
	{
		session_handle(m_impl.get()).delete_port_mapping(handle);
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

	session_proxy::~session_proxy()
	{
		if (m_thread && m_thread.unique())
			m_thread->join();
	}
}

