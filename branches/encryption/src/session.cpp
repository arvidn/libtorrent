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
#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"

using namespace boost::posix_time;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace libtorrent
{

	namespace aux
	{
		filesystem_init::filesystem_init()
		{
			using namespace boost::filesystem;
			if (path::default_name_check_writable())
				path::default_name_check(no_check);
		}
	}

	session::session(
		fingerprint const& id
		, std::pair<int, int> listen_port_range
		, char const* listen_interface)
		: m_impl(new session_impl(listen_port_range, id, listen_interface))
	{
		// turn off the filename checking in boost.filesystem
		assert(listen_port_range.first > 0);
		assert(listen_port_range.first < listen_port_range.second);
#ifndef NDEBUG
		// this test was added after it came to my attention
		// that devstudios managed c++ failed to generate
		// correct code for boost.function
		boost::function0<void> test = boost::ref(*m_impl);
		assert(!test.empty());
#endif
	}

	session::session(fingerprint const& id)
		: m_impl(new session_impl(std::make_pair(0, 0), id))
	{
#ifndef NDEBUG
		boost::function0<void> test = boost::ref(*m_impl);
		assert(!test.empty());
#endif
	}

	session::~session()
	{
		assert(m_impl);
		// if there is at least one destruction-proxy
		// abort the session and let the destructor
		// of the proxy to syncronize
		if (!m_impl.unique())
			m_impl->abort();
	}

	void session::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*)> ext)
	{
		m_impl->add_extension(ext);
	}

	void session::set_ip_filter(ip_filter const& f)
	{
		m_impl->set_ip_filter(f);
	}

	void session::set_peer_id(peer_id const& id)
	{
		m_impl->set_peer_id(id);
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


	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session::add_torrent(
		torrent_info const& ti
		, boost::filesystem::path const& save_path
		, entry const& resume_data
		, bool compact_mode
		, int block_size)
	{
		return m_impl->add_torrent(ti, save_path, resume_data
			, compact_mode, block_size);
	}

	torrent_handle session::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, boost::filesystem::path const& save_path
		, entry const& e
		, bool compact_mode
		, int block_size)
	{
		return m_impl->add_torrent(tracker_url, info_hash, name, save_path, e
			, compact_mode, block_size);
	}

	void session::remove_torrent(const torrent_handle& h)
	{
		m_impl->remove_torrent(h);
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

	void session::set_max_uploads(int limit)
	{
		m_impl->set_max_uploads(limit);
	}

	void session::set_max_connections(int limit)
	{
		m_impl->set_max_connections(limit);
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

	void session::set_severity_level(alert::severity_t s)
	{
		m_impl->set_severity_level(s);
	}

}

