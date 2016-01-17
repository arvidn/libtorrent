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
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/magnet_uri.hpp"

using boost::shared_ptr;
using boost::weak_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{

	TORRENT_EXPORT void TORRENT_LINK_TEST_NAME() {}

	// this function returns a session_settings object
	// which will optimize libtorrent for minimum memory
	// usage, with no consideration of performance.
	TORRENT_EXPORT session_settings min_memory_usage()
	{
		session_settings set;

		set.alert_queue_size = 100;

		set.max_allowed_in_request_queue = 100;

		// setting this to a low limit, means more
		// peers are more likely to request from the
		// same piece. Which means fewer partial
		// pieces and fewer entries in the partial
		// piece list
		set.whole_pieces_threshold = 2;
		set.use_parole_mode = false;
		set.prioritize_partial_pieces = true;

		// connect to 5 peers per second
		set.connection_speed = 5;

		// be extra nice on the hard drive when running
		// on embedded devices. This might slow down
		// torrent checking
		set.file_checks_delay_per_block = 5;

		// only have 4 files open at a time
		set.file_pool_size = 4;

		// we want to keep the peer list as small as possible
		set.allow_multiple_connections_per_ip = false;
		set.max_failcount = 2;
		set.inactivity_timeout = 120;

		// whenever a peer has downloaded one block, write
		// it to disk, and don't read anything from the
		// socket until the disk write is complete
		set.max_queued_disk_bytes = 1;

		// don't keep track of all upnp devices, keep
		// the device list small
		set.upnp_ignore_nonrouters = true;

		// never keep more than one 16kB block in
		// the send buffer
		set.send_buffer_watermark = 9;

		// don't use any disk cache
		set.cache_size = 0;
		set.cache_buffer_chunk_size = 1;
		set.use_read_cache = false;
		set.use_disk_read_ahead = false;

		set.close_redundant_connections = true;

		set.max_peerlist_size = 500;
		set.max_paused_peerlist_size = 50;

		// udp trackers are cheaper to talk to
		set.prefer_udp_trackers = true;

		set.max_rejects = 10;

		set.recv_socket_buffer_size = 16 * 1024;
		set.send_socket_buffer_size = 16 * 1024;

		// use less memory when checking pieces
		set.optimize_hashing_for_speed = false;

		// use less memory when reading and writing
		// whole pieces
		set.coalesce_reads = false;
		set.coalesce_writes = false;

		// disallow the buffer size to grow for the uTP socket
		set.utp_dynamic_sock_buf = false;
		
		// max 'bottled' http receive buffer/url torrent size
		set.max_http_recv_buffer_size = 1024 * 1024;

		return set;
	}

	TORRENT_EXPORT session_settings high_performance_seed()
	{
		session_settings set;

		// allow peers to request a lot of blocks at a time,
		// to be more likely to saturate the bandwidth-delay-
		// product.
		set.max_out_request_queue = 1500;
		set.max_allowed_in_request_queue = 2000;

		// don't throttle TCP, assume there is
		// plenty of bandwidth
		set.mixed_mode_algorithm = session_settings::prefer_tcp;

		// we will probably see a high rate of alerts, make it less
		// likely to loose alerts
		set.alert_queue_size = 50000;

		// allow 500 files open at a time
		set.file_pool_size = 500;

		// don't update access time for each read/write
		set.no_atime_storage = true;

		// as a seed box, we must accept multiple peers behind
		// the same NAT
		set.allow_multiple_connections_per_ip = true;

		// connect to 50 peers per second
		set.connection_speed = 50;

		// allow 8000 peer connections
		set.connections_limit = 8000;

		// allow lots of peers to try to connect simultaneously
		set.listen_queue_size = 200;

		// unchoke many peers
		set.unchoke_slots_limit = 500;

		// we need more DHT capacity to ping more peers
		// candidates before trying to connect
		set.dht_upload_rate_limit = 20000;

		// we're more interested in downloading than seeding
		// only service a read job every 1000 write job (when
		// disk is congested). Presumably on a big box, writes
		// are extremely cheap and reads are relatively expensive
		// so that's the main reason this ratio should be adjusted
		set.read_job_every = 100;

		// use 1 GB of cache
		set.cache_size = 32768 * 2;
		set.use_read_cache = true;
		set.cache_buffer_chunk_size = 128;
		set.read_cache_line_size = 32;
		set.write_cache_line_size = 32;
		set.low_prio_disk = false;
		// one hour expiration
		set.cache_expiry = 60 * 60;
		// this is expensive and could add significant
		// delays when freeing a large number of buffers
		set.lock_disk_cache = false;

		// the max number of bytes pending write before we throttle
		// download rate
		set.max_queued_disk_bytes = 10 * 1024 * 1024;
		// flush write cache in a way to minimize the amount we need to
		// read back once we want to hash-check the piece. i.e. try to
		// flush all blocks in-order
		set.disk_cache_algorithm = session_settings::avoid_readback;

		set.explicit_read_cache = false;
		// prevent fast pieces to interfere with suggested pieces
		// since we unchoke everyone, we don't need fast pieces anyway
		set.allowed_fast_set_size = 0;
		// suggest pieces in the read cache for higher cache hit rate
		set.suggest_mode = session_settings::suggest_read_cache;

		set.close_redundant_connections = true;

		set.max_rejects = 10;

		set.recv_socket_buffer_size = 1024 * 1024;
		set.send_socket_buffer_size = 1024 * 1024;

		set.optimize_hashing_for_speed = true;

		// don't let connections linger for too long
		set.request_timeout = 10;
		set.peer_timeout = 20;
		set.inactivity_timeout = 20;

		set.active_limit = 2000;
		set.active_tracker_limit = 2000;
		set.active_dht_limit = 600;
		set.active_seeds = 2000;

		set.choking_algorithm = session_settings::fixed_slots_choker;

		// in order to be able to deliver very high
		// upload rates, this should be able to cover
		// the bandwidth delay product. Assuming an RTT
		// of 500 ms, and a send rate of 20 MB/s, the upper
		// limit should be 10 MB
		set.send_buffer_watermark = 3 * 1024 * 1024;

		// put 1.5 seconds worth of data in the send buffer
		// this gives the disk I/O more heads-up on disk
		// reads, and can maximize throughput
		set.send_buffer_watermark_factor = 150;

		// always stuff at least 1 MiB down each peer
		// pipe, to quickly ramp up send rates
 		set.send_buffer_low_watermark = 1 * 1024 * 1024;

		// don't retry peers if they fail once. Let them
		// connect to us if they want to
		set.max_failcount = 1;

		// allow the buffer size to grow for the uTP socket
		set.utp_dynamic_sock_buf = true;

		// max 'bottled' http receive buffer/url torrent size
		set.max_http_recv_buffer_size = 6 * 1024 * 1024;

		// the disk cache performs better with the pool allocator
		set.use_disk_cache_pool = true;

		return set;
	}

	// wrapper around a function that's executed in the network thread
	// ans synchronized in the client thread
	template <class R>
	void fun_ret(R* ret, bool* done, condition_variable* e, mutex* m, boost::function<R(void)> f)
	{
		*ret = f();
		mutex::scoped_lock l(*m);
		*done = true;
		e->notify_all();
	}

	void fun_wrap(bool* done, condition_variable* e, mutex* m, boost::function<void(void)> f)
	{
		f();
		mutex::scoped_lock l(*m);
		*done = true;
		e->notify_all();
	}

#define TORRENT_ASYNC_CALL(x) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get()))

#define TORRENT_ASYNC_CALL1(x, a1) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1))

#define TORRENT_ASYNC_CALL2(x, a1, a2) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1, a2))

#define TORRENT_ASYNC_CALL3(x, a1, a2, a3) \
	m_impl->m_io_service.dispatch(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3))

#define TORRENT_WAIT \
	mutex::scoped_lock l(m_impl->mut); \
	while (!done) { m_impl->cond.wait(l); };

#define TORRENT_SYNC_CALL(x) \
	bool done = false; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get())))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL1(x, a1) \
	bool done = false; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL2(x, a1, a2) \
	bool done = false; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL3(x, a1, a2, a3) \
	bool done = false; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL4(x, a1, a2, a3, a4) \
	bool done = false; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3, a4)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL_RET(type, x) \
	bool done = false; \
	type r; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_ret<type >, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get())))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL_RET1(type, x, a1) \
	bool done = false; \
	type r; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_ret<type >, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL_RET2(type, x, a1, a2) \
	bool done = false; \
	type r; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_ret<type >, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))); \
	TORRENT_WAIT

#define TORRENT_SYNC_CALL_RET3(type, x, a1, a2, a3) \
	bool done = false; \
	type r; \
	m_impl->m_io_service.dispatch(boost::bind(&fun_ret<type >, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3)))); \
	TORRENT_WAIT

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

	void session::init(std::pair<int, int> listen_range, char const* listen_interface
		, fingerprint const& id, boost::uint32_t alert_mask)
	{
#if defined _MSC_VER && defined TORRENT_DEBUG
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
		::_set_se_translator(straight_to_debugger);
#endif

		m_impl.reset(new session_impl(listen_range, id, listen_interface, alert_mask));
	}

	void session::set_log_path(std::string const& p)
	{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING \
	|| defined TORRENT_ERROR_LOGGING
		m_impl->set_log_path(p);
#endif
	}

	void session::start(int flags)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
			add_extension(create_smart_ban_plugin);
		}
#endif

		m_impl->start_session();

		if (flags & start_default_features)
		{
			start_upnp();
			start_natpmp();
#ifndef TORRENT_DISABLE_DHT
			start_dht();
#endif
			start_lsd();
		}
	}

	session::~session()
	{
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

	void session::load_state(lazy_entry const& e)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		TORRENT_SYNC_CALL1(load_state, &e);
	}

	feed_handle session::add_feed(feed_settings const& feed)
	{
		// if you have auto-download enabled, you must specify a download directory!
		TORRENT_ASSERT_PRECOND(!feed.auto_download || !feed.add_args.save_path.empty());
		TORRENT_SYNC_CALL_RET1(feed_handle, add_feed, feed);
		return r;
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

	void session::load_asnum_db(char const* file)
	{
#ifndef TORRENT_DISABLE_GEO_IP
		TORRENT_ASYNC_CALL1(load_asnum_db, std::string(file));
#endif
	}

	void session::load_country_db(char const* file)
	{
#ifndef TORRENT_DISABLE_GEO_IP
		TORRENT_ASYNC_CALL1(load_country_db, std::string(file));
#endif
	}

	int session::as_for_ip(address const& addr)
	{
#ifndef TORRENT_DISABLE_GEO_IP
		return m_impl->as_for_ip(addr);
#else
		return 0;
#endif
	}

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	void session::load_asnum_db(wchar_t const* file)
	{
#ifndef TORRENT_DISABLE_GEO_IP
		TORRENT_ASYNC_CALL1(load_asnum_dbw, std::wstring(file));
#endif
	}

	void session::load_country_db(wchar_t const* file)
	{
#ifndef TORRENT_DISABLE_GEO_IP
		TORRENT_ASYNC_CALL1(load_country_dbw, std::wstring(file));
#endif
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

#ifndef TORRENT_NO_DEPRECATE
	void session::load_state(entry const& ses_state)
	{
		if (ses_state.type() == entry::undefined_t) return;
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		lazy_entry e;
		error_code ec;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec);

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
#endif

	void session::set_ip_filter(ip_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_ip_filter, f);
	}
	
	ip_filter session::get_ip_filter() const
	{
		TORRENT_SYNC_CALL_RET(ip_filter, get_ip_filter);
		return r;
	}

	void session::set_port_filter(port_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_port_filter, f);
	}

	void session::set_peer_id(peer_id const& id)
	{
		TORRENT_ASYNC_CALL1(set_peer_id, id);
	}
	
	peer_id session::id() const
	{
		TORRENT_SYNC_CALL_RET(peer_id, get_peer_id);
		return r;
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

	void session::post_torrent_updates()
	{
		TORRENT_ASYNC_CALL(post_torrent_updates);
	}

	std::vector<torrent_handle> session::get_torrents() const
	{
		TORRENT_SYNC_CALL_RET(std::vector<torrent_handle>, get_torrents);
		return r;
	}
	
	torrent_handle session::find_torrent(sha1_hash const& info_hash) const
	{
		TORRENT_SYNC_CALL_RET1(torrent_handle, find_torrent_handle, info_hash);
		return r;
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session::add_torrent(add_torrent_params const& params)
	{
		error_code ec;
		TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
		if (ec) throw libtorrent_exception(ec);
		return r;
	}
#endif

	torrent_handle session::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		ec.clear();
		TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
		return r;
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
		boost::intrusive_ptr<torrent_info> tip(new torrent_info(ti));
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
		boost::intrusive_ptr<torrent_info> ti
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		add_torrent_params p(sc);
		p.ti = ti;
		p.save_path = save_path;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(p.resume_data), resume_data);
		}
		p.storage_mode = storage_mode;
		p.paused = paused;
		p.userdata = userdata;
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
	bool session::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface, int flags)
	{
		error_code ec;
		TORRENT_SYNC_CALL4(listen_on, port_range, boost::ref(ec), net_interface, flags);
		return !!ec;
	}
#endif

	void session::listen_on(
		std::pair<int, int> const& port_range
		, error_code& ec
		, const char* net_interface, int flags)
	{
		TORRENT_SYNC_CALL4(listen_on, port_range, boost::ref(ec), net_interface, flags);
	}

	unsigned short session::listen_port() const
	{
		TORRENT_SYNC_CALL_RET(unsigned short, listen_port);
		return r;
	}

	unsigned short session::ssl_listen_port() const
	{
		TORRENT_SYNC_CALL_RET(unsigned short, ssl_listen_port);
		return r;
	}

	session_status session::status() const
	{
		TORRENT_SYNC_CALL_RET(session_status, status);
		return r;
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
		TORRENT_SYNC_CALL_RET(bool, is_paused);
		return r;
	}

	void session::get_cache_info(sha1_hash const& ih
		, std::vector<cached_piece_info>& ret) const
	{
		m_impl->m_disk_thread.get_cache_info(ih, ret);
	}

	cache_status session::get_cache_status() const
	{
		return m_impl->m_disk_thread.status();
	}

	void session::start_dht()
	{
#ifndef TORRENT_DISABLE_DHT
		// the state is loaded in load_state()
		TORRENT_ASYNC_CALL(start_dht);
#endif
	}

	void session::stop_dht()
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL(stop_dht);
#endif
	}

	void session::set_dht_settings(dht_settings const& settings)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(set_dht_settings, settings);
#endif
	}

	dht_settings session::get_dht_settings() const
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_SYNC_CALL_RET(dht_settings, get_dht_settings);
#else
		dht_settings r;
#endif
		return r;
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
		TORRENT_SYNC_CALL_RET(entry, dht_state);
		return r;
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
		TORRENT_SYNC_CALL_RET(bool, is_dht_running);
		return r;
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

	void session::set_pe_settings(pe_settings const& settings)
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_ASYNC_CALL1(set_pe_settings, settings);
#endif
	}

	pe_settings session::get_pe_settings() const
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_SYNC_CALL_RET(pe_settings, get_pe_settings);
#else
		pe_settings r;
#endif
		return r;
	}

	bool session::is_listening() const
	{
		TORRENT_SYNC_CALL_RET(bool, is_listening);
		return r;
	}

	void session::set_settings(session_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_settings, s);
	}

	session_settings session::settings() const
	{
		TORRENT_SYNC_CALL_RET(session_settings, settings);
		return r;
	}

	void session::set_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_proxy, s);
	}

	proxy_settings session::proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, proxy);
		return r;
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_peer_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_peer_proxy, s);
	}

	void session::set_web_seed_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_web_seed_proxy, s);
	}

	void session::set_tracker_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_tracker_proxy, s);
	}

	proxy_settings session::peer_proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, peer_proxy);
		return r;
	}

	proxy_settings session::web_seed_proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, web_seed_proxy);
		return r;
	}

	proxy_settings session::tracker_proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, tracker_proxy);
		return r;
	}


	void session::set_dht_proxy(proxy_settings const& s)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(set_dht_proxy, s);
#endif
	}

	proxy_settings session::dht_proxy() const
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_SYNC_CALL_RET(proxy_settings, dht_proxy);
		return r;
#else
		return proxy_settings();
#endif
	}
#endif // TORRENT_NO_DEPRECATE

	void session::set_i2p_proxy(proxy_settings const& s)
	{
#if TORRENT_USE_I2P
		TORRENT_ASYNC_CALL1(set_i2p_proxy, s);
#endif
	}
	
	proxy_settings session::i2p_proxy() const
	{
#if TORRENT_USE_I2P
		TORRENT_SYNC_CALL_RET(proxy_settings, i2p_proxy);
#else
		proxy_settings r;
#endif
		return r;
	}

#ifdef TORRENT_STATS
	void session::enable_stats_logging(bool s)
	{
		TORRENT_ASYNC_CALL1(enable_stats_logging, s);
	}
#endif

#ifndef TORRENT_NO_DEPRECATE
	int session::max_uploads() const
	{
		TORRENT_SYNC_CALL_RET(int, max_uploads);
		return r;
	}

	void session::set_max_uploads(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_uploads, limit);
	}

	int session::max_connections() const
	{
		TORRENT_SYNC_CALL_RET(int, max_connections);
		return r;
	}

	void session::set_max_connections(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_connections, limit);
	}

	int session::max_half_open_connections() const
	{
		TORRENT_SYNC_CALL_RET(int, max_half_open_connections);
		return r;
	}

	void session::set_max_half_open_connections(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_half_open_connections, limit);
	}

	int session::local_upload_rate_limit() const
	{
		TORRENT_SYNC_CALL_RET(int, local_upload_rate_limit);
		return r;
	}

	int session::local_download_rate_limit() const
	{
		TORRENT_SYNC_CALL_RET(int, local_download_rate_limit);
		return r;
	}

	int session::upload_rate_limit() const
	{
		TORRENT_SYNC_CALL_RET(int, upload_rate_limit);
		return r;
	}

	int session::download_rate_limit() const
	{
		TORRENT_SYNC_CALL_RET(int, download_rate_limit);
		return r;
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
		TORRENT_SYNC_CALL_RET(int, num_uploads);
		return r;
	}

	int session::num_connections() const
	{
		TORRENT_SYNC_CALL_RET(int, num_connections);
		return r;
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

	void session::set_alert_mask(boost::uint32_t m)
	{
		TORRENT_ASYNC_CALL1(set_alert_mask, m);
	}

#ifndef TORRENT_NO_DEPRECATE
	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		TORRENT_SYNC_CALL_RET1(size_t, set_alert_queue_size_limit, queue_size_limit_);
		return r;
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

		TORRENT_ASYNC_CALL1(set_alert_mask, m);
	}
#endif

	void session::start_lsd()
	{
		TORRENT_ASYNC_CALL(start_lsd);
	}
	
	void session::start_natpmp()
	{
		TORRENT_ASYNC_CALL(start_natpmp);
	}
	
	void session::start_upnp()
	{
		TORRENT_ASYNC_CALL(start_upnp);
	}

	int session::add_port_mapping(protocol_type t, int external_port, int local_port)
	{
		TORRENT_SYNC_CALL_RET3(int, add_port_mapping, int(t), external_port, local_port);
		return r;
	}

	void session::delete_port_mapping(int handle)
	{
		TORRENT_ASYNC_CALL1(delete_port_mapping, handle);
	}
	
	void session::stop_lsd()
	{
		TORRENT_ASYNC_CALL(stop_lsd);
	}
	
	void session::stop_natpmp()
	{
		TORRENT_ASYNC_CALL(stop_natpmp);
	}
	
	void session::stop_upnp()
	{
		TORRENT_ASYNC_CALL(stop_upnp);
	}
	
	connection_queue& session::get_connection_queue()
	{
		return m_impl->m_half_open;
	}

	session_settings::session_settings(std::string const& user_agent_)
		: version(LIBTORRENT_VERSION_NUM)
		, user_agent(user_agent_)
		, tracker_completion_timeout(30)
		, tracker_receive_timeout(10)
		, stop_tracker_timeout(5)
		, tracker_maximum_response_length(1024*1024)
		, piece_timeout(20)
		, request_timeout(50)
		, request_queue_time(3)
		, max_allowed_in_request_queue(500)
		, max_out_request_queue(500)
		, whole_pieces_threshold(20)
		, peer_timeout(120)
		, urlseed_timeout(20)
		, urlseed_pipeline_size(5)
		, urlseed_wait_retry(30)
		, file_pool_size(40)
		, allow_multiple_connections_per_ip(false)
		, max_failcount(3)
		, min_reconnect_time(60)
		, peer_connect_timeout(15)
		, ignore_limits_on_local_network(true)
		, connection_speed(6)
		, send_redundant_have(true)
		, lazy_bitfields(false)
		, inactivity_timeout(600)
		, unchoke_interval(15)
		, optimistic_unchoke_interval(30)
		, num_want(200)
		, initial_picker_threshold(4)
		, allowed_fast_set_size(10)
		, suggest_mode(no_piece_suggestions)
		, max_queued_disk_bytes(1024 * 1024)
		, max_queued_disk_bytes_low_watermark(0)
		, handshake_timeout(10)
		, use_dht_as_fallback(false)
		, free_torrent_hashes(true)
		, upnp_ignore_nonrouters(false)
		, send_buffer_low_watermark(512)
		, send_buffer_watermark(500 * 1024)
		, send_buffer_watermark_factor(50)
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16
		, auto_upload_slots(true)
		, auto_upload_slots_rate_based(true)
#endif
		, choking_algorithm(fixed_slots_choker)
		, seed_choking_algorithm(round_robin)
		, use_parole_mode(true)
		, cache_size(1024)
		, cache_buffer_chunk_size(16)
		, cache_expiry(300)
		, use_read_cache(true)
		, explicit_read_cache(0)
		, explicit_cache_interval(30)
		, disk_io_write_mode(0)
		, disk_io_read_mode(0)
		, coalesce_reads(false)
		, coalesce_writes(false)
		, outgoing_ports(0,0)
		, peer_tos(0)
		, active_downloads(3)
		, active_seeds(5)
		, active_dht_limit(88) // don't announce more than once every 40 seconds
		, active_tracker_limit(1600) // don't announce to trackers more than once every 1.125 seconds
		, active_lsd_limit(60) // don't announce to local network more than once every 5 seconds
		, active_limit(15)
		, auto_manage_prefer_seeds(false)
		, dont_count_slow_torrents(true)
		, auto_manage_interval(30)
		, share_ratio_limit(2.f)
		, seed_time_ratio_limit(7.f)
		, seed_time_limit(24 * 60 * 60) // 24 hours
		, peer_turnover_interval(300)
		, peer_turnover(2 / 50.f)
		, peer_turnover_cutoff(.9f)
		, close_redundant_connections(true)
		, auto_scrape_interval(1800)
		, auto_scrape_min_interval(300)
		, max_peerlist_size(4000)
		, max_paused_peerlist_size(4000)
		, min_announce_interval(5 * 60)
		, prioritize_partial_pieces(false)
		, auto_manage_startup(60)
		, rate_limit_ip_overhead(true)
		, announce_to_all_trackers(false)
		, announce_to_all_tiers(false)
		, prefer_udp_trackers(true)
		, strict_super_seeding(false)
		, seeding_piece_quota(20)
#ifdef TORRENT_WINDOWS
		, max_sparse_regions(30000)
#else
		, max_sparse_regions(0)
#endif
		, lock_disk_cache(false)
		, max_rejects(50)
		, recv_socket_buffer_size(0)
		, send_socket_buffer_size(0)
		, optimize_hashing_for_speed(true)
		, file_checks_delay_per_block(0)
		, disk_cache_algorithm(avoid_readback)
		, read_cache_line_size(32)
		, write_cache_line_size(32)
		, optimistic_disk_retry(10 * 60)
		, disable_hash_checks(false)
		, allow_reordered_disk_operations(true)
		, allow_i2p_mixed(false)
		, max_suggest_pieces(10)
		, drop_skipped_requests(false)
		, low_prio_disk(true)
		, local_service_announce_interval(5 * 60)
		, dht_announce_interval(15 * 60)
		, udp_tracker_token_expiry(60)
		, volatile_read_cache(false)
		, guided_read_cache(false)
		, default_cache_min_age(1)
		, num_optimistic_unchoke_slots(0)
		, no_atime_storage(true)
		, default_est_reciprocation_rate(16000)
		, increase_est_reciprocation_rate(20)
		, decrease_est_reciprocation_rate(3)
		, incoming_starts_queued_torrents(false)
		, report_true_downloaded(false)
		, strict_end_game_mode(true)
		, broadcast_lsd(true)
		, enable_outgoing_utp(true)
		, enable_incoming_utp(true)
		, enable_outgoing_tcp(true)
		, enable_incoming_tcp(true)
		, max_pex_peers(50)
		, ignore_resume_timestamps(false)
		, no_recheck_incomplete_resume(false)
		, anonymous_mode(false)
		, force_proxy(false)
		, tick_interval(500)
		, report_web_seed_downloads(true)
		, share_mode_target(3)
		, upload_rate_limit(0)
		, download_rate_limit(0)
		, local_upload_rate_limit(0)
		, local_download_rate_limit(0)
		, dht_upload_rate_limit(4000)
		, unchoke_slots_limit(8)
		, half_open_limit(0)
		, connections_limit(200)
		, connections_slack(10)
		, utp_target_delay(100) // milliseconds
		, utp_gain_factor(3000) // bytes per rtt
		, utp_min_timeout(500) // milliseconds
		, utp_syn_resends(2)
		, utp_fin_resends(2)
		, utp_num_resends(3)
		, utp_connect_timeout(3000) // milliseconds
#ifndef TORRENT_NO_DEPRECATE
		, utp_delayed_ack(0) // milliseconds
#endif
		, utp_dynamic_sock_buf(false) // this doesn't seem quite reliable yet
		, utp_loss_multiplier(50) // specified in percent
		, mixed_mode_algorithm(peer_proportional)
		, rate_limit_utp(true)
		, listen_queue_size(5)
		, announce_double_nat(false)
		, torrent_connect_boost(10)
		, seeding_outgoing_connections(true)
		, no_connect_privileged_ports(true)
		, alert_queue_size(6000)
		, max_metadata_size(3*1024*1024)
		, smooth_connects(true)
		, always_send_user_agent(false)
		, apply_ip_filter_to_trackers(true)
		, read_job_every(10)
		, use_disk_read_ahead(true)
		, lock_files(false)
		, ssl_listen(4433)
		, tracker_backoff(250)
		, ban_web_seeds(true)
		, max_http_recv_buffer_size(4*1024*1024)
		, support_share_mode(true)
		, support_merkle_torrents(false)
		, report_redundant_bytes(true)
		, use_disk_cache_pool(false)
		, inactive_down_rate(2048)
		, inactive_up_rate(2048)
	{}

	session_settings::~session_settings() {}
}

