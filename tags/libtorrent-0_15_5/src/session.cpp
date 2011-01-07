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

	std::string log_time()
	{
		static const ptime start = time_now_hires();
		char ret[200];
		std::sprintf(ret, "%d", total_milliseconds(time_now() - start));
		return ret;
	}

	namespace aux
	{
		filesystem_init::filesystem_init()
		{
#if BOOST_VERSION < 103400
			using namespace boost::filesystem;
			if (path::default_name_check_writable())
				path::default_name_check(no_check);
#endif
		}
	}

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

		// use 512 MB of cache
		set.cache_size = 32768;
		set.use_read_cache = true;
		set.cache_buffer_chunk_size = 128;
		set.read_cache_line_size = 512;
		set.write_cache_line_size = 512;
		set.low_prio_disk = false;
		// one hour expiration
		set.cache_expiry = 60 * 60;

		set.close_redundant_connections = true;

		set.max_rejects = 10;

		set.optimize_hashing_for_speed = true;

		// don't let connections linger for too long
		set.request_timeout = 10;
		set.peer_timeout = 20;
		set.inactivity_timeout = 20;

		set.active_limit = 2000;
		set.active_seeds = 2000;

		set.auto_upload_slots = false;

		// in order to be able to deliver very high
		// upload rates, this should be able to cover
		// the bandwidth delay product. Assuming an RTT
		// of 500 ms, and a send rate of 10 MB/s, the upper
		// limit should be 5 MB
		set.send_buffer_watermark = 5 * 1024 * 1024;

		// don't retry peers if they fail once. Let them
		// connect to us if they want to
		set.max_failcount = 1;

		return set;
	}

	session::session(
		fingerprint const& id
		, std::pair<int, int> listen_port_range
		, char const* listen_interface
		, int flags
		, int alert_mask
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, fs::path logpath
#endif
		)
		: m_impl(new session_impl(listen_port_range, id, listen_interface
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, logpath
#endif
		))
	{
#ifdef TORRENT_MEMDEBUG
		start_malloc_debug();
#endif
		// turn off the filename checking in boost.filesystem
		TORRENT_ASSERT(listen_port_range.first > 0);
		TORRENT_ASSERT(listen_port_range.first < listen_port_range.second);
#ifdef TORRENT_DEBUG
		// this test was added after it came to my attention
		// that devstudios managed c++ failed to generate
		// correct code for boost.function
		boost::function0<void> test = boost::ref(*m_impl);
		TORRENT_ASSERT(!test.empty());
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

	session::session(fingerprint const& id
		, int flags
		, int alert_mask
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, fs::path logpath
#endif
		)
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		: m_impl(new session_impl(std::make_pair(0, 0), id, "0.0.0.0", logpath))
#else
		: m_impl(new session_impl(std::make_pair(0, 0), id, "0.0.0.0"))
#endif
	{
#ifdef TORRENT_MEMDEBUG
		start_malloc_debug();
#endif
#ifdef TORRENT_DEBUG
		boost::function0<void> test = boost::ref(*m_impl);
		TORRENT_ASSERT(!test.empty());
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
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
#ifdef TORRENT_MEMDEBUG
		stop_malloc_debug();
#endif
		TORRENT_ASSERT(m_impl);
		// if there is at least one destruction-proxy
		// abort the session and let the destructor
		// of the proxy to syncronize
		if (!m_impl.unique())
			m_impl->abort();
	}

	void session::save_state(entry& e, boost::uint32_t flags) const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->save_state(e, flags, l);
	}

	void session::load_state(lazy_entry const& e)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->load_state(e);
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->add_extension(ext);
	}
#endif

#ifndef TORRENT_DISABLE_GEO_IP
	bool session::load_asnum_db(char const* file)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->load_asnum_db(file);
	}

	bool session::load_country_db(char const* file)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->load_country_db(file);
	}

	int session::as_for_ip(address const& addr)
	{
		aux::session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->as_for_ip(addr);
	}

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
	bool session::load_asnum_db(wchar_t const* file)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->load_asnum_db(file);
	}

	bool session::load_country_db(wchar_t const* file)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->load_country_db(file);
	}
#endif
#endif

#ifndef TORRENT_NO_DEPRECATE
	void session::load_state(entry const& ses_state)
	{
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		lazy_entry e;
		lazy_bdecode(&buf[0], &buf[0] + buf.size(), e);
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->load_state(e);
	}

	entry session::state() const
	{
		entry ret;
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->save_state(ret, 0xffffffff, l);
		return ret;
	}
#endif

	void session::set_ip_filter(ip_filter const& f)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_ip_filter(f);
	}
	
	ip_filter const& session::get_ip_filter() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->get_ip_filter();
	}

	void session::set_port_filter(port_filter const& f)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_port_filter(f);
	}

	void session::set_peer_id(peer_id const& id)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_peer_id(id);
	}
	
	peer_id session::id() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->get_peer_id();
	}

	io_service& session::get_io_service()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->m_io_service;
	}

	void session::set_key(int key)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_key(key);
	}

	std::vector<torrent_handle> session::get_torrents() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->get_torrents();
	}
	
	torrent_handle session::find_torrent(sha1_hash const& info_hash) const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->find_torrent_handle(info_hash);
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session::add_torrent(add_torrent_params const& params)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);

		error_code ec;
		torrent_handle ret = m_impl->add_torrent(params, ec);
		if (ec) throw libtorrent_exception(ec);
		return ret;
	}
#endif

	torrent_handle session::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->add_torrent(params, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		torrent_info const& ti
		, fs::path const& save_path
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
		, fs::path const& save_path
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
		, fs::path const& save_path
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
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->remove_torrent(h, options);
	}

	bool session::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->listen_on(port_range, net_interface);
	}

	unsigned short session::listen_port() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->listen_port();
	}

	session_status session::status() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->status();
	}

	void session::pause()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->pause();
	}

	void session::resume()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->resume();
	}

	bool session::is_paused() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->is_paused();
	}

	void session::get_cache_info(sha1_hash const& ih
		, std::vector<cached_piece_info>& ret) const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->m_disk_thread.get_cache_info(ih, ret);
	}

	cache_status session::get_cache_status() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->m_disk_thread.status();
	}

#ifndef TORRENT_DISABLE_DHT

	void session::start_dht()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		// the state is loaded in load_state()
		m_impl->start_dht();
	}

	void session::stop_dht()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->stop_dht();
	}

	void session::set_dht_settings(dht_settings const& settings)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_dht_settings(settings);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::start_dht(entry const& startup_state)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->start_dht(startup_state);
	}

	entry session::dht_state() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->dht_state(l);
	}
#endif
	
	void session::add_dht_node(std::pair<std::string, int> const& node)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->add_dht_node(node);
	}

	void session::add_dht_router(std::pair<std::string, int> const& node)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->add_dht_router(node);
	}

	bool session::is_dht_running() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->m_dht;
	}

#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session::set_pe_settings(pe_settings const& settings)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_pe_settings(settings);
	}

	pe_settings const& session::get_pe_settings() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->get_pe_settings();
	}
#endif

	bool session::is_listening() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->is_listening();
	}

	void session::set_settings(session_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_settings(s);
	}

	session_settings const& session::settings()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->settings();
	}

	void session::set_proxy(proxy_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_proxy(s);
	}

	proxy_settings const& session::proxy() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->proxy();
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_peer_proxy(proxy_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_peer_proxy(s);
	}

	void session::set_web_seed_proxy(proxy_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_web_seed_proxy(s);
	}

	void session::set_tracker_proxy(proxy_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_tracker_proxy(s);
	}

	proxy_settings const& session::peer_proxy() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->peer_proxy();
	}

	proxy_settings const& session::web_seed_proxy() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->web_seed_proxy();
	}

	proxy_settings const& session::tracker_proxy() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->tracker_proxy();
	}


#ifndef TORRENT_DISABLE_DHT
	void session::set_dht_proxy(proxy_settings const& s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_dht_proxy(s);
	}

	proxy_settings const& session::dht_proxy() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->dht_proxy();
	}
#endif
#endif // TORRENT_NO_DEPRECATE

	int session::max_uploads() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->max_uploads();
	}

	void session::set_max_uploads(int limit)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_max_uploads(limit);
	}

	int session::max_connections() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->max_connections();
	}

	void session::set_max_connections(int limit)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_max_connections(limit);
	}

	int session::max_half_open_connections() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->max_half_open_connections();
	}

	void session::set_max_half_open_connections(int limit)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_max_half_open_connections(limit);
	}

	int session::local_upload_rate_limit() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->local_upload_rate_limit();
	}

	int session::local_download_rate_limit() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->local_download_rate_limit();
	}

	int session::upload_rate_limit() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->upload_rate_limit();
	}

	int session::download_rate_limit() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->download_rate_limit();
	}

	void session::set_local_upload_rate_limit(int bytes_per_second)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_local_upload_rate_limit(bytes_per_second);
	}

	void session::set_local_download_rate_limit(int bytes_per_second)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_local_download_rate_limit(bytes_per_second);
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_upload_rate_limit(bytes_per_second);
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_download_rate_limit(bytes_per_second);
	}

	int session::num_uploads() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->num_uploads();
	}

	int session::num_connections() const
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->num_connections();
	}

	std::auto_ptr<alert> session::pop_alert()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->pop_alert();
	}

	void session::set_alert_dispatch(boost::function<void(alert const&)> const& fun)
	{
		// this function deliberately doesn't acquire the mutex
		return m_impl->set_alert_dispatch(fun);
	}

	alert const* session::wait_for_alert(time_duration max_wait)
	{
		// this function deliberately doesn't acquire the mutex
		return m_impl->wait_for_alert(max_wait);
	}

	void session::set_alert_mask(int m)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->set_alert_mask(m);
	}

	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->set_alert_queue_size_limit(queue_size_limit_);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_severity_level(alert::severity_t s)
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
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

		m_impl->set_alert_mask(m);
	}
#endif

	void session::start_lsd()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->start_lsd();
	}
	
	natpmp* session::start_natpmp()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		if (m_impl->m_natpmp) return m_impl->m_natpmp.get();

		// the natpmp constructor may fail and call the callbacks
		// into the session_impl. We cannot hold the mutex then
		l.unlock();
		natpmp* n = new (std::nothrow) natpmp(m_impl->m_io_service
			, m_impl->m_listen_interface.address()
			, boost::bind(&session_impl::on_port_mapping
				, m_impl.get(), _1, _2, _3, 0)
			, boost::bind(&session_impl::on_port_map_log
				, m_impl.get(), _1, 0));
		l.lock();

		if (n == 0) return 0;

		m_impl->start_natpmp(n);
		return n;
	}
	
	upnp* session::start_upnp()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);

		if (m_impl->m_upnp) return m_impl->m_upnp.get();

		// the upnp constructor may fail and call the callbacks
		// into the session_impl. We cannot hold the mutex then
		l.unlock();
		upnp* u = new (std::nothrow) upnp(m_impl->m_io_service
			, m_impl->m_half_open
			, m_impl->m_listen_interface.address()
			, m_impl->m_settings.user_agent
			, boost::bind(&session_impl::on_port_mapping
				, m_impl.get(), _1, _2, _3, 1)
			, boost::bind(&session_impl::on_port_map_log
				, m_impl.get(), _1, 1)
			, m_impl->m_settings.upnp_ignore_nonrouters);
		l.lock();

		if (u == 0) return 0;

		m_impl->start_upnp(u);
		return u;
	}
	
	void session::stop_lsd()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->stop_lsd();
	}
	
	void session::stop_natpmp()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->stop_natpmp();
	}
	
	void session::stop_upnp()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		m_impl->stop_upnp();
	}
	
	connection_queue& session::get_connection_queue()
	{
		session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->m_half_open;
	}
}

