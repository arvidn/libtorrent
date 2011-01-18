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
#include <algorithm>
#include <set>
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
#include "libtorrent/extensions/lt_trackers.hpp"
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

#ifdef TORRENT_MEMDEBUG
void start_malloc_debug();
void stop_malloc_debug();
#endif

namespace libtorrent
{

	TORRENT_EXPORT void TORRENT_LINK_TEST_NAME() {}

	// this function returns a session_settings object
	// which will optimize libtorrent for minimum memory
	// usage, with no consideration of performance.
	session_settings min_memory_usage()
	{
		session_settings set;
		// setting this to a low limit, means more
		// peers are more likely to request from the
		// same piece. Which means fewer partial
		// pieces and fewer entries in the partial
		// piece list
		set.whole_pieces_threshold = 2;
		set.use_parole_mode = false;
		set.prioritize_partial_pieces = true;

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

		return set;
	}

	session_settings high_performance_seed()
	{
		session_settings set;

		// allow 500 files open at a time
		set.file_pool_size = 500;

		// as a seed box, we must accept multiple peers behind
		// the same NAT
		set.allow_multiple_connections_per_ip = true;

		// use 1 GB of cache
		set.cache_size = 32768 * 2;
		set.use_read_cache = true;
		set.cache_buffer_chunk_size = 128;
		set.read_cache_line_size = 512;
		set.write_cache_line_size = 512;
		set.low_prio_disk = false;
		// one hour expiration
		set.cache_expiry = 60 * 60;
		// this is expensive and could add significant
		// delays when freeing a large number of buffers
		set.lock_disk_cache = false;

		// flush write cache based on largest contiguous block
		set.disk_cache_algorithm = session_settings::largest_contiguous;

		// explicitly cache rare pieces
		set.explicit_read_cache = true;
		// prevent fast pieces to interfere with suggested pieces
		// since we unchoke everyone, we don't need fast pieces anyway
		set.allowed_fast_set_size = 0;
		// suggest pieces in the read cache for higher cache hit rate
		set.suggest_mode = session_settings::suggest_read_cache;

		set.close_redundant_connections = true;

		set.max_rejects = 10;

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
		// of 500 ms, and a send rate of 10 MB/s, the upper
		// limit should be 5 MB
		set.send_buffer_watermark = 5 * 1024 * 1024;

		// put 10 seconds worth of data in the send buffer
		// this gives the disk I/O more heads-up on disk
		// reads, and can maximize throughput
		set.send_buffer_watermark_factor = 10;

		// don't retry peers if they fail once. Let them
		// connect to us if they want to
		set.max_failcount = 1;

		// allow the buffer size to grow for the uTP socket
		set.utp_dynamic_sock_buf = true;

		return set;
	}

	// wrapper around a function that's executed in the network thread
	// ans synchronized in the client thread
	template <class R>
	void fun_ret(R* ret, bool* done, condition* e, mutex* m, boost::function<R(void)> f)
	{
		*ret = f();
		mutex::scoped_lock l(*m);
		*done = true;
		e->signal_all(l);
	}

	void fun_wrap(bool* done, condition* e, mutex* m, boost::function<void(void)> f)
	{
		f();
		mutex::scoped_lock l(*m);
		*done = true;
		e->signal_all(l);
	}

#define TORRENT_ASYNC_CALL(x) \
	m_impl->m_io_service.post(boost::bind(&session_impl:: x, m_impl.get()))

#define TORRENT_ASYNC_CALL1(x, a1) \
	m_impl->m_io_service.post(boost::bind(&session_impl:: x, m_impl.get(), a1))

#define TORRENT_ASYNC_CALL2(x, a1, a2) \
	m_impl->m_io_service.post(boost::bind(&session_impl:: x, m_impl.get(), a1, a2))

#define TORRENT_SYNC_CALL(x) \
	bool done = false; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get())))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL1(x, a1) \
	bool done = false; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL2(x, a1, a2) \
	bool done = false; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_wrap, &done, &m_impl->cond, &m_impl->mut, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET(type, x) \
	bool done = false; \
	type r; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_ret<type>, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get())))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET1(type, x, a1) \
	bool done = false; \
	type r; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_ret<type>, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1)))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET2(type, x, a1, a2) \
	bool done = false; \
	type r; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_ret<type>, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2)))); \
	do { m_impl->cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET3(type, x, a1, a2, a3) \
	bool done = false; \
	type r; \
	mutex::scoped_lock l(m_impl->mut); \
	m_impl->m_io_service.post(boost::bind(&fun_ret<type>, &r, &done, &m_impl->cond, &m_impl->mut, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl.get(), a1, a2, a3)))); \
	do { m_impl->cond.wait(l); } while(!done)

	// this is a dummy function that's exported and named based
	// on the configuration. The session.hpp file will reference
	// it and if the library and the client are built with different
	// configurations this will give a link error
	void TORRENT_EXPORT TORRENT_CFG() {}

	void session::init(std::pair<int, int> listen_range, char const* listen_interface
		, fingerprint const& id, int flags, int alert_mask TORRENT_LOGPATH_ARG)
	{
		m_impl.reset(new session_impl(listen_range, id, listen_interface TORRENT_LOGPATH));

#ifdef TORRENT_MEMDEBUG
		start_malloc_debug();
#endif
		set_alert_mask(alert_mask);
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
			add_extension(create_lt_trackers_plugin);
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
#ifdef TORRENT_MEMDEBUG
		stop_malloc_debug();
#endif
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
		TORRENT_ASSERT(!feed.auto_download || !feed.add_args.save_path.empty());
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

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
		TORRENT_ASYNC_CALL1(add_extension, ext);
	}
#endif

#ifndef TORRENT_DISABLE_GEO_IP
	void session::load_asnum_db(char const* file)
	{
		TORRENT_ASYNC_CALL1(load_asnum_db, std::string(file));
	}

	void session::load_country_db(char const* file)
	{
		TORRENT_ASYNC_CALL1(load_country_db, std::string(file));
	}

	int session::as_for_ip(address const& addr)
	{
		return m_impl->as_for_ip(addr);
	}

#if TORRENT_USE_WSTRING
	void session::load_asnum_db(wchar_t const* file)
	{
		TORRENT_ASYNC_CALL1(load_asnum_dbw, std::wstring(file));
	}

	void session::load_country_db(wchar_t const* file)
	{
		TORRENT_ASYNC_CALL1(load_country_dbw, std::wstring(file));
	}
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_DISABLE_GEO_IP

#ifndef TORRENT_NO_DEPRECATE
	void session::load_state(entry const& ses_state)
	{
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		lazy_entry e;
		error_code ec;
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec);
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
		if (string_begins_no_case("magnet:", params.url.c_str()))
		{
			add_torrent_params p(params);
			p.url.clear();
			return add_magnet_uri(*this, params.url, p);
		}

		error_code ec;
		TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, ec);
		if (ec) throw libtorrent_exception(ec);
		return r;
	}
#endif

	torrent_handle session::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		if (string_begins_no_case("magnet:", params.url.c_str()))
		{
			add_torrent_params p(params);
			p.url.clear();
			return add_magnet_uri(*this, params.url, p, ec);
		}

		TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, ec);
		return r;
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
		std::vector<char> buf;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(buf), resume_data);
			p.resume_data = &buf;
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
		std::vector<char> buf;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(buf), resume_data);
			p.resume_data = &buf;
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
		TORRENT_ASYNC_CALL2(remove_torrent, h, options);
	}

	bool session::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface, int flags)
	{
		TORRENT_SYNC_CALL_RET3(bool, listen_on, port_range, net_interface, flags);
		return r;
	}

	unsigned short session::listen_port() const
	{
		TORRENT_SYNC_CALL_RET(unsigned short, listen_port);
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

#ifndef TORRENT_DISABLE_DHT

	void session::start_dht()
	{
		// the state is loaded in load_state()
		TORRENT_ASYNC_CALL(start_dht);
	}

	void session::stop_dht()
	{
		TORRENT_ASYNC_CALL(stop_dht);
	}

	void session::set_dht_settings(dht_settings const& settings)
	{
		TORRENT_ASYNC_CALL1(set_dht_settings, settings);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht(entry const& startup_state)
	{
		TORRENT_ASYNC_CALL1(start_dht, startup_state);
	}

	entry session::dht_state() const
	{
		TORRENT_SYNC_CALL_RET(entry, dht_state);
		return r;
	}
#endif
	
	void session::add_dht_node(std::pair<std::string, int> const& node)
	{
		TORRENT_ASYNC_CALL1(add_dht_node_name, node);
	}

	void session::add_dht_router(std::pair<std::string, int> const& node)
	{
		TORRENT_ASYNC_CALL1(add_dht_router, node);
	}

	bool session::is_dht_running() const
	{
		TORRENT_SYNC_CALL_RET(bool, is_dht_running);
		return r;
	}

#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session::set_pe_settings(pe_settings const& settings)
	{
		TORRENT_ASYNC_CALL1(set_pe_settings, settings);
	}

	pe_settings session::get_pe_settings() const
	{
		TORRENT_SYNC_CALL_RET(pe_settings, get_pe_settings);
		return r;
	}
#endif

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


#ifndef TORRENT_DISABLE_DHT
	void session::set_dht_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_dht_proxy, s);
	}

	proxy_settings session::dht_proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, dht_proxy);
		return r;
	}
#endif
#endif // TORRENT_NO_DEPRECATE

#if TORRENT_USE_I2P
	void session::set_i2p_proxy(proxy_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_i2p_proxy, s);
	}
	
	proxy_settings session::i2p_proxy() const
	{
		TORRENT_SYNC_CALL_RET(proxy_settings, i2p_proxy);
		return r;
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

	std::auto_ptr<alert> session::pop_alert()
	{
		return m_impl->pop_alert();
	}

	void session::set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		TORRENT_ASYNC_CALL1(set_alert_dispatch, fun);
	}

	alert const* session::wait_for_alert(time_duration max_wait)
	{
		return m_impl->wait_for_alert(max_wait);
	}

	void session::set_alert_mask(int m)
	{
		TORRENT_ASYNC_CALL1(set_alert_mask, m);
	}

	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		TORRENT_SYNC_CALL_RET1(size_t, set_alert_queue_size_limit, queue_size_limit_);
		return r;
	}

#ifndef TORRENT_NO_DEPRECATE
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
	
	natpmp* session::start_natpmp()
	{
		TORRENT_SYNC_CALL_RET(natpmp*, start_natpmp);
		return r;
	}
	
	upnp* session::start_upnp()
	{
		TORRENT_SYNC_CALL_RET(upnp*, start_upnp);
		return r;
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
}

