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
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/smart_ban.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/extensions.hpp" // for add_peer_flags_t

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	// declared in extensions.hpp
	// remove this once C++17 is required
	constexpr feature_flags_t plugin::optimistic_unchoke_feature;
	constexpr feature_flags_t plugin::tick_feature;
	constexpr feature_flags_t plugin::dht_request_feature;
	constexpr feature_flags_t plugin::alert_feature;
#endif

namespace aux {
	constexpr torrent_list_index_t session_interface::torrent_state_updates;
	constexpr torrent_list_index_t session_interface::torrent_want_tick;
	constexpr torrent_list_index_t session_interface::torrent_want_peers_download;
	constexpr torrent_list_index_t session_interface::torrent_want_peers_finished;
	constexpr torrent_list_index_t session_interface::torrent_want_scrape;
	constexpr torrent_list_index_t session_interface::torrent_downloading_auto_managed;
	constexpr torrent_list_index_t session_interface::torrent_seeding_auto_managed;
	constexpr torrent_list_index_t session_interface::torrent_checking_auto_managed;
}

#ifndef TORRENT_DISABLE_EXTENSIONS
constexpr add_peer_flags_t torrent_plugin::first_time;
constexpr add_peer_flags_t torrent_plugin::filtered;
#endif

namespace {

#if defined TORRENT_ASIO_DEBUGGING
	void wait_for_asio_handlers()
	{
		int counter = 0;
		while (log_async())
		{
			std::this_thread::sleep_for(seconds(1));
			++counter;
			std::printf("\x1b[2J\x1b[0;0H\x1b[33m==== Waiting to shut down: %d ==== \x1b[0m\n\n"
				, counter);
		}
		async_dec_threads();

		std::fprintf(stderr, "\n\nEXPECTS NO MORE ASYNC OPS\n\n\n");
	}
#endif
} // anonymous namespace

	settings_pack min_memory_usage()
	{
		settings_pack set;
#if TORRENT_ABI_VERSION == 1
		// receive data directly into disk buffers
		// this yields more system calls to read() and
		// kqueue(), but saves RAM.
		set.set_bool(settings_pack::contiguous_recv_buffer, false);
#endif

		set.set_int(settings_pack::max_peer_recv_buffer_size, 32 * 1024 + 200);

		set.set_int(settings_pack::disk_io_write_mode, settings_pack::disable_os_cache);
		set.set_int(settings_pack::disk_io_read_mode, settings_pack::disable_os_cache);

		// keep 2 blocks outstanding when hashing
		set.set_int(settings_pack::checking_mem_usage, 2);

		// don't use any extra threads to do SHA-1 hashing
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

		// never keep more than one 16kB block in
		// the send buffer
		set.set_int(settings_pack::send_buffer_watermark, 9);

		// don't use any disk cache
		set.set_int(settings_pack::cache_size, 0);
		set.set_bool(settings_pack::use_read_cache, false);

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
		return set;
	}

	settings_pack high_performance_seed()
	{
		settings_pack set;
		// don't throttle TCP, assume there is
		// plenty of bandwidth
		set.set_int(settings_pack::mixed_mode_algorithm, settings_pack::prefer_tcp);

		set.set_int(settings_pack::max_out_request_queue, 1500);
		set.set_int(settings_pack::max_allowed_in_request_queue, 2000);

		set.set_int(settings_pack::max_peer_recv_buffer_size, 5 * 1024 * 1024);

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

		// unchoke all peers
		set.set_int(settings_pack::unchoke_slots_limit, -1);

		// use 1 GB of cache
		set.set_int(settings_pack::cache_size, 32768 * 2);
		set.set_bool(settings_pack::use_read_cache, true);
		set.set_int(settings_pack::read_cache_line_size, 32);
		set.set_int(settings_pack::write_cache_line_size, 256);
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

		set.set_int(settings_pack::send_not_sent_low_watermark, 524288);

		// don't let connections linger for too long
		set.set_int(settings_pack::request_timeout, 10);
		set.set_int(settings_pack::peer_timeout, 20);
		set.set_int(settings_pack::inactivity_timeout, 20);

		set.set_int(settings_pack::active_limit, 20000);
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

		// number of disk threads for low level file operations
		set.set_int(settings_pack::aio_threads, 8);

		set.set_int(settings_pack::checking_mem_usage, 2048);

		return set;
	}

	session_params read_session_params(bdecode_node const& e, save_state_flags_t const flags)
	{
		session_params params;

		bdecode_node settings;
		if (e.type() != bdecode_node::dict_t) return params;

		if (flags & session_handle::save_settings)
		{
			settings = e.dict_find_dict("settings");
			if (settings)
			{
				params.settings = load_pack_from_dict(settings);
			}
		}

#ifndef TORRENT_DISABLE_DHT
		if (flags & session_handle::save_dht_settings)
		{
			settings = e.dict_find_dict("dht");
			if (settings)
			{
				params.dht_settings = dht::read_dht_settings(settings);
			}
		}

		if (flags & session_handle::save_dht_state)
		{
			settings = e.dict_find_dict("dht state");
			if (settings)
			{
				params.dht_state = dht::read_dht_state(settings);
			}
		}
#endif

		return params;
	}

	void session::start(session_params&& params, io_service* ios)
	{
		bool const internal_executor = ios == nullptr;

		if (internal_executor)
		{
			// the user did not provide an executor, we have to use our own
			m_io_service = std::make_shared<io_service>(1);
			ios = m_io_service.get();
		}

		m_impl = std::make_shared<aux::session_impl>(std::ref(*ios), std::ref(params.settings));
		*static_cast<session_handle*>(this) = session_handle(m_impl);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (auto& ext : params.extensions)
		{
			m_impl->add_ses_extension(std::move(ext));
		}
#endif

#ifndef TORRENT_DISABLE_DHT
		if (params.settings.has_val(settings_pack::dht_upload_rate_limit))
			params.dht_settings.upload_rate_limit = params.settings.get_int(settings_pack::dht_upload_rate_limit);

		m_impl->set_dht_settings(std::move(params.dht_settings));
		m_impl->set_dht_state(std::move(params.dht_state));

		TORRENT_ASSERT(params.dht_storage_constructor);
		m_impl->set_dht_storage(std::move(params.dht_storage_constructor));
#endif

		m_impl->start_session();

		if (internal_executor)
		{
			// start a thread for the message pump
			m_thread = std::make_shared<std::thread>(
				[&] { m_io_service->run(); });
		}
	}

namespace {

		std::vector<std::shared_ptr<plugin>> default_plugins(
			bool empty = false)
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			if (empty) return {};
			using wrapper = aux::session_impl::session_plugin_wrapper;
			return {
				std::make_shared<wrapper>(create_ut_pex_plugin),
				std::make_shared<wrapper>(create_ut_metadata_plugin),
				std::make_shared<wrapper>(create_smart_ban_plugin)
			};
#else
			TORRENT_UNUSED(empty);
			return {};
#endif
		}
	}

	void session::start(session_flags_t const flags, settings_pack&& sp, io_service* ios)
	{
		start({std::move(sp),
			default_plugins(!(flags & add_default_plugins))}, ios);
	}

	session::~session()
	{
		if (!m_impl) return;

		aux::dump_call_profile();

		// capture the shared_ptr in the dispatched function
		// to keep the session_impl alive
		m_impl->call_abort();

		if (m_thread && m_thread.use_count() == 1)
		{
#if defined TORRENT_ASIO_DEBUGGING
			wait_for_asio_handlers();
#endif
			m_thread->join();
		}
	}

	session_proxy session::abort()
	{
		// stop calling the alert notify function now, to avoid it thinking the
		// session is still alive
		m_impl->alerts().set_notify_function({});
		return session_proxy(m_io_service, m_thread, m_impl);
	}

	session_proxy::session_proxy() = default;
	session_proxy::session_proxy(std::shared_ptr<io_service> ios
		, std::shared_ptr<std::thread> t
		, std::shared_ptr<aux::session_impl> impl)
		: m_io_service(std::move(ios))
		, m_thread(std::move(t))
		, m_impl(std::move(impl))
	{}
	session_proxy::session_proxy(session_proxy const&) = default;
	session_proxy& session_proxy::operator=(session_proxy const&) = default;
	session_proxy::session_proxy(session_proxy&&) noexcept = default;
	session_proxy& session_proxy::operator=(session_proxy&&) noexcept = default;
	session_proxy::~session_proxy()
	{
		if (m_thread && m_thread.use_count() == 1)
		{
#if defined TORRENT_ASIO_DEBUGGING
			wait_for_asio_handlers();
#endif
			m_thread->join();
		}
	}

	session_params::session_params(settings_pack&& sp)
		: session_params(std::move(sp), default_plugins())
	{}

	session_params::session_params(settings_pack const& sp)
		: session_params(sp, default_plugins())
	{}

	session_params::session_params()
		: extensions(default_plugins())
#ifndef TORRENT_DISABLE_DHT
		, dht_storage_constructor(dht::dht_default_storage_constructor)
#endif
	{}

	session_params::session_params(settings_pack&& sp
		, std::vector<std::shared_ptr<plugin>> exts)
		: settings(std::move(sp))
		, extensions(std::move(exts))
#ifndef TORRENT_DISABLE_DHT
		, dht_storage_constructor(dht::dht_default_storage_constructor)
#endif
	{}

	session_params::session_params(settings_pack const& sp
		, std::vector<std::shared_ptr<plugin>> exts)
		: settings(sp)
		, extensions(std::move(exts))
#ifndef TORRENT_DISABLE_DHT
		, dht_storage_constructor(dht::dht_default_storage_constructor)
#endif
	{}
}
