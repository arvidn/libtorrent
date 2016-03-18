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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <ctime>
#include <algorithm>
#include <set>
#include <deque>
#include <cctype>
#include <algorithm>

#include <boost/limits.hpp>
#include <boost/bind.hpp>

#ifdef TORRENT_PROFILE_CALLS
#include <boost/unordered_map.hpp>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

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
		// workaround for microsoft's
		// hardware exceptions that makes
		// it hard to debug stuff
		::_set_se_translator(straight_to_debugger);
#endif

		bool const internal_executor = ios == NULL;

		if (internal_executor)
		{
			// the user did not provide an executor, we have to use our own
			m_io_service = boost::make_shared<io_service>();
			ios = m_io_service.get();
		}

		m_impl = boost::make_shared<session_impl>(boost::ref(*ios));
		*static_cast<session_handle*>(this) = session_handle(m_impl.get());

#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
			add_extension(create_smart_ban_plugin);
		}
#else
		TORRENT_UNUSED(flags);
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
#endif

		if (m_thread && m_thread.unique())
			m_thread->join();
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

