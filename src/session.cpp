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

using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using libtorrent::aux::session_impl;

#ifdef TORRENT_MEMDEBUG
void start_malloc_debug();
void stop_malloc_debug();
#endif

namespace libtorrent
{

	std::string log_time()
	{
		static const ptime start = time_now();
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

	session::session(
		fingerprint const& id
		, std::pair<int, int> listen_port_range
		, char const* listen_interface
		, int flags
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
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
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
#ifndef TORRENT_DISABLE_EXTENSIONS
		if (flags & add_default_plugins)
		{
			add_extension(create_ut_pex_plugin);
			add_extension(create_ut_metadata_plugin);
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

#ifndef TORRENT_DISABLE_EXTENSIONS
	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext)
	{
		m_impl->add_extension(ext);
	}
#endif

#ifndef TORRENT_DISABLE_GEO_IP
	bool session::load_asnum_db(char const* file)
	{
		return m_impl->load_asnum_db(file);
	}

	bool session::load_country_db(char const* file)
	{
		return m_impl->load_country_db(file);
	}

	int session::as_for_ip(address const& addr)
	{
		aux::session_impl::mutex_t::scoped_lock l(m_impl->m_mutex);
		return m_impl->as_for_ip(addr);
	}

#endif

	void session::load_state(entry const& ses_state)
	{
		m_impl->load_state(ses_state);
	}

	entry session::state() const
	{
		return m_impl->state();
	}

	void session::set_ip_filter(ip_filter const& f)
	{
		m_impl->set_ip_filter(f);
	}

	void session::set_port_filter(port_filter const& f)
	{
		m_impl->set_port_filter(f);
	}

	void session::set_peer_id(peer_id const& id)
	{
		m_impl->set_peer_id(id);
	}
	
	peer_id session::id() const
	{
		return m_impl->get_peer_id();
	}

	void session::set_key(int key)
	{
		m_impl->set_key(key);
	}

	std::vector<torrent_handle> session::get_torrents() const
	{
		return m_impl->get_torrents();
	}
	
	torrent_handle session::find_torrent(sha1_hash const& info_hash) const
	{
		return m_impl->find_torrent_handle(info_hash);
	}

	torrent_handle session::add_torrent(add_torrent_params const& params)
	{
		return m_impl->add_torrent(params);
	}

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
		return m_impl->add_torrent(p);
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
		return m_impl->add_torrent(p);
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
		return m_impl->add_torrent(p);
	}
#endif

	void session::remove_torrent(const torrent_handle& h, int options)
	{
		m_impl->remove_torrent(h, options);
	}

	bool session::listen_on(
		std::pair<int, int> const& port_range
		, const char* net_interface)
	{
		return m_impl->listen_on(port_range, net_interface);
	}

	unsigned short session::listen_port() const
	{
		return m_impl->listen_port();
	}

	session_status session::status() const
	{
		return m_impl->status();
	}

	void session::pause() { m_impl->pause(); }
	void session::resume() { m_impl->resume(); }
	bool session::is_paused() const { return m_impl->is_paused(); }

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

	void session::start_dht(entry const& startup_state)
	{
		m_impl->start_dht(startup_state);
	}

	void session::stop_dht()
	{
		m_impl->stop_dht();
	}

	void session::set_dht_settings(dht_settings const& settings)
	{
		m_impl->set_dht_settings(settings);
	}

	entry session::dht_state() const
	{
		return m_impl->dht_state();
	}
	
	void session::add_dht_node(std::pair<std::string, int> const& node)
	{
		m_impl->add_dht_node(node);
	}

	void session::add_dht_router(std::pair<std::string, int> const& node)
	{
		m_impl->add_dht_router(node);
	}

#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
	void session::set_pe_settings(pe_settings const& settings)
	{
		m_impl->set_pe_settings(settings);
	}

	pe_settings const& session::get_pe_settings() const
	{
		return m_impl->get_pe_settings();
	}
#endif

	bool session::is_listening() const
	{
		return m_impl->is_listening();
	}

	void session::set_settings(session_settings const& s)
	{
		m_impl->set_settings(s);
	}

	session_settings const& session::settings()
	{
		return m_impl->settings();
	}

	void session::set_peer_proxy(proxy_settings const& s)
	{
		m_impl->set_peer_proxy(s);
	}

	void session::set_web_seed_proxy(proxy_settings const& s)
	{
		m_impl->set_web_seed_proxy(s);
	}

	void session::set_tracker_proxy(proxy_settings const& s)
	{
		m_impl->set_tracker_proxy(s);
	}

	proxy_settings const& session::peer_proxy() const
	{
		return m_impl->peer_proxy();
	}

	proxy_settings const& session::web_seed_proxy() const
	{
		return m_impl->web_seed_proxy();
	}

	proxy_settings const& session::tracker_proxy() const
	{
		return m_impl->tracker_proxy();
	}


#ifndef TORRENT_DISABLE_DHT
	void session::set_dht_proxy(proxy_settings const& s)
	{
		m_impl->set_dht_proxy(s);
	}

	proxy_settings const& session::dht_proxy() const
	{
		return m_impl->dht_proxy();
	}
#endif

	void session::set_max_uploads(int limit)
	{
		m_impl->set_max_uploads(limit);
	}

	void session::set_max_connections(int limit)
	{
		m_impl->set_max_connections(limit);
	}

	int session::max_half_open_connections() const
	{
		return m_impl->max_half_open_connections();
	}

	void session::set_max_half_open_connections(int limit)
	{
		m_impl->set_max_half_open_connections(limit);
	}

	int session::upload_rate_limit() const
	{
		return m_impl->upload_rate_limit();
	}

	int session::download_rate_limit() const
	{
		return m_impl->download_rate_limit();
	}

	void session::set_upload_rate_limit(int bytes_per_second)
	{
		m_impl->set_upload_rate_limit(bytes_per_second);
	}

	void session::set_download_rate_limit(int bytes_per_second)
	{
		m_impl->set_download_rate_limit(bytes_per_second);
	}

	int session::num_uploads() const
	{
		return m_impl->num_uploads();
	}

	int session::num_connections() const
	{
		return m_impl->num_connections();
	}

	std::auto_ptr<alert> session::pop_alert()
	{
		return m_impl->pop_alert();
	}

	alert const* session::wait_for_alert(time_duration max_wait)
	{
		return m_impl->wait_for_alert(max_wait);
	}

	void session::set_alert_mask(int m)
	{
		m_impl->set_alert_mask(m);
	}

	size_t session::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return m_impl->set_alert_queue_size_limit(queue_size_limit_);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session::set_severity_level(alert::severity_t s)
	{
		int m = 0;
		switch (s)
		{
			case alert::debug: m = alert::all_categories; break;
			case alert::info: m = alert::all_categories & ~(alert::debug_notification
				| alert::progress_notification); break;
			case alert::warning: m = alert::all_categories & ~(alert::debug_notification
				| alert::status_notification | alert::progress_notification); break;
			case alert::critical: m = alert::error_notification | alert::storage_notification; break;
			case alert::fatal: m = alert::error_notification; break;
			default: break;
		}

		m_impl->set_alert_mask(m);
	}
#endif

	void session::start_lsd()
	{
		m_impl->start_lsd();
	}
	
	natpmp* session::start_natpmp()
	{
		return m_impl->start_natpmp();
	}
	
	upnp* session::start_upnp()
	{
		return m_impl->start_upnp();
	}
	
	void session::stop_lsd()
	{
		m_impl->stop_lsd();
	}
	
	void session::stop_natpmp()
	{
		m_impl->stop_natpmp();
	}
	
	void session::stop_upnp()
	{
		m_impl->stop_upnp();
	}
	
	connection_queue& session::get_connection_queue()
	{
		return m_impl->m_half_open;
	}
}

