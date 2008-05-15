/*

Copyright (c) 2003, Arvid Norberg
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
#include <numeric>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/assert.hpp"

using namespace libtorrent;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;
using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace
{

	enum
	{
		// wait 60 seconds before retrying a failed tracker
		tracker_retry_delay_min = 60
		// when tracker_failed_max trackers
		// has failed, wait 10 minutes instead
		, tracker_retry_delay_max = 10 * 60
		, tracker_failed_max = 5
	};

	int calculate_block_size(const torrent_info& i, int default_block_size)
	{
		if (default_block_size < 1024) default_block_size = 1024;

		// if pieces are too small, adjust the block size
		if (i.piece_length() < default_block_size)
		{
			return i.piece_length();
		}

		// otherwise, go with the default
		return default_block_size;
	}

	struct find_peer_by_ip
	{
		find_peer_by_ip(tcp::endpoint const& a, const torrent* t)
			: ip(a)
			, tor(t)
		{ TORRENT_ASSERT(t != 0); }
		
		bool operator()(session_impl::connection_map::value_type const& c) const
		{
			tcp::endpoint const& sender = c->remote();
			if (sender.address() != ip.address()) return false;
			if (tor != c->associated_torrent().lock().get()) return false;
			return true;
		}

		tcp::endpoint const& ip;
		torrent const* tor;
	};

	struct peer_by_id
	{
		peer_by_id(const peer_id& i): pid(i) {}
		
		bool operator()(session_impl::connection_map::value_type const& p) const
		{
			if (p->pid() != pid) return false;
			// have a special case for all zeros. We can have any number
			// of peers with that pid, since it's used to indicate no pid.
			if (std::count(pid.begin(), pid.end(), 0) == 20) return false;
			return true;
		}

		peer_id const& pid;
	};
}

namespace libtorrent
{

	torrent::torrent(
		session_impl& ses
		, boost::intrusive_ptr<torrent_info> tf
		, fs::path const& save_path
		, tcp::endpoint const& net_interface
		, storage_mode_t storage_mode
		, int block_size
		, storage_constructor_type sc
		, bool paused
		, entry const* resume_data
		, int seq
		, bool auto_managed)
		: m_torrent_file(tf)
		, m_abort(false)
		, m_paused(paused)
		, m_just_paused(false)
		, m_auto_managed(auto_managed)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(time_now())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_host_resolver(ses.m_io_service)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_announce_timer(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_last_dht_announce(time_now() - minutes(15))
#endif
		, m_ses(ses)
		, m_picker(0)
		, m_trackers(m_torrent_file->trackers())
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_storage_mode(storage_mode)
		, m_state(torrent_status::queued_for_checking)
		, m_progress(0.f)
		, m_default_block_size(block_size)
		, m_connections_initialized(true)
		, m_settings(ses.settings())
		, m_storage_constructor(sc)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
		, m_deficit_counter(0)
		, m_policy(this)
		, m_sequence_number(seq)
		, m_active_time(seconds(0))
		, m_seeding_time(seconds(0))
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_started(time_now())
	{
		if (resume_data) m_resume_data = *resume_data;
#ifndef NDEBUG
		m_files_checked = false;
#endif
	}

	torrent::torrent(
		session_impl& ses
		, char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, fs::path const& save_path
		, tcp::endpoint const& net_interface
		, storage_mode_t storage_mode
		, int block_size
		, storage_constructor_type sc
		, bool paused
		, entry const* resume_data
		, int seq
		, bool auto_managed)
		: m_torrent_file(new torrent_info(info_hash))
		, m_abort(false)
		, m_paused(paused)
		, m_just_paused(false)
		, m_auto_managed(auto_managed)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(time_now())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_host_resolver(ses.m_io_service)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_announce_timer(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_last_dht_announce(time_now() - minutes(15))
#endif
		, m_ses(ses)
		, m_picker(0)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_storage_mode(storage_mode)
		, m_state(torrent_status::queued_for_checking)
		, m_progress(0.f)
		, m_default_block_size(block_size)
		, m_connections_initialized(false)
		, m_settings(ses.settings())
		, m_storage_constructor(sc)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
		, m_deficit_counter(0)
		, m_policy(this)
		, m_sequence_number(seq)
		, m_active_time(seconds(0))
		, m_seeding_time(seconds(0))
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_started(time_now())
	{
		if (resume_data) m_resume_data = *resume_data;
#ifndef NDEBUG
		m_files_checked = false;
#endif
		INVARIANT_CHECK;

		if (name) m_name.reset(new std::string(name));

		if (tracker_url)
		{
			m_trackers.push_back(announce_entry(tracker_url));
			m_torrent_file->add_tracker(tracker_url);
		}
	}

	void torrent::start()
	{
		boost::weak_ptr<torrent> self(shared_from_this());
		if (m_torrent_file->is_valid()) init();
		if (m_abort) return;
		error_code ec;
		m_announce_timer.expires_from_now(seconds(1), ec);
		m_announce_timer.async_wait(
			bind(&torrent::on_announce_disp, self, _1));
	}

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		if (m_ses.m_listen_sockets.empty()) return false;

		if (!m_ses.m_dht) return false;

		// don't announce private torrents
		if (m_torrent_file->is_valid() && m_torrent_file->priv()) return false;
	
		if (m_trackers.empty()) return true;
			
		return m_failed_trackers > 0 || !m_ses.settings().use_dht_as_fallback;
	}
#endif

	torrent::~torrent()
	{
		// The invariant can't be maintained here, since the torrent
		// is being destructed, all weak references to it have been
		// reset, which means that all its peers already have an
		// invalidated torrent pointer (so it cannot be verified to be correct)
		
		// i.e. the invariant can only be maintained if all connections have
		// been closed by the time the torrent is destructed. And they are
		// supposed to be closed. So we can still do the invariant check.

		TORRENT_ASSERT(m_connections.empty());
		
		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*(*i)->m_logger) << "*** DESTRUCTING TORRENT\n";
		}
#endif

		TORRENT_ASSERT(m_abort);
		if (!m_connections.empty())
			disconnect_all();
	}

	peer_request torrent::to_req(piece_block const& p)
	{
		int block_offset = p.block_index * m_block_size;
		int block_size = (std::min)(torrent_file().piece_size(
			p.piece_index) - block_offset, m_block_size);
		TORRENT_ASSERT(block_size > 0);
		TORRENT_ASSERT(block_size <= m_block_size);

		peer_request r;
		r.piece = p.piece_index;
		r.start = block_offset;
		r.length = block_size;
		return r;
	}

	std::string torrent::name() const
	{
		if (valid_metadata()) return m_torrent_file->name();
		if (m_name) return *m_name;
		return "";
	}

#ifndef TORRENT_DISABLE_EXTENSIONS

	void torrent::add_extension(boost::shared_ptr<torrent_plugin> ext)
	{
		m_extensions.push_back(ext);
	}

	void torrent::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
		, void* userdata)
	{
		boost::shared_ptr<torrent_plugin> tp(ext(this, userdata));
		if (!tp) return;

		add_extension(tp);
		
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			boost::shared_ptr<peer_plugin> pp(tp->new_connection(p));
			if (pp) p->add_extension(pp);
		}

		// if files are checked for this torrent, call the extension
		// to let it initialize itself
		if (m_connections_initialized)
			tp->on_files_checked();
	}

#endif

	// this may not be called from a constructor because of the call to
	// shared_from_this()
	void torrent::init()
	{
		TORRENT_ASSERT(m_torrent_file->is_valid());
		TORRENT_ASSERT(m_torrent_file->num_files() > 0);
		TORRENT_ASSERT(m_torrent_file->total_size() >= 0);

		m_have_pieces.resize(m_torrent_file->num_pieces(), false);
		// the shared_from_this() will create an intentional
		// cycle of ownership, se the hpp file for description.
		m_owning_storage = new piece_manager(shared_from_this(), m_torrent_file
			, m_save_path, m_ses.m_files, m_ses.m_disk_thread, m_storage_constructor
			, m_storage_mode);
		m_storage = m_owning_storage.get();
		m_block_size = calculate_block_size(*m_torrent_file, m_default_block_size);
		m_picker.reset(new piece_picker(
			m_torrent_file->piece_length() / m_block_size
			, int((m_torrent_file->total_size()+m_block_size-1)/m_block_size)));

		std::vector<std::string> const& url_seeds = m_torrent_file->url_seeds();
		std::copy(url_seeds.begin(), url_seeds.end(), std::inserter(m_web_seeds
			, m_web_seeds.begin()));

		m_state = torrent_status::queued_for_checking;

		if (m_resume_data.type() == entry::dictionary_t) read_resume_data(m_resume_data);
		
		m_storage->async_check_fastresume(&m_resume_data
			, bind(&torrent::on_resume_data_checked
			, shared_from_this(), _1, _2));
	}

	void torrent::on_resume_data_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.str));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
					" error: " << j.str <<
					" torrent: " << torrent_file().name() <<
					" ]\n";
#endif
			}
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;
			auto_managed(false);
			pause();
			return;
		}

		// parse out "peers" from the resume data and add them to the peer list
		entry const* peers_entry = m_resume_data.find_key("peers");
		if (peers_entry && peers_entry->type() == entry::list_t)
		{
			peer_id id;
			std::fill(id.begin(), id.end(), 0);
			entry::list_type const& peer_list = peers_entry->list();

			for (entry::list_type::const_iterator i = peer_list.begin();
				i != peer_list.end(); ++i)
			{
				if (i->type() != entry::dictionary_t) continue;
				entry const* ip = i->find_key("ip");
				entry const* port = i->find_key("port");
				if (ip == 0 || port == 0
					|| ip->type() != entry::string_t
					|| port->type() != entry::int_t)
					continue;
				tcp::endpoint a(
					address::from_string(ip->string())
					, (unsigned short)port->integer());
				m_policy.peer_from_tracker(a, id, peer_info::resume_data, 0);
			}
		}

		// parse out "banned_peers" and add them as banned
		entry const* banned_peers_entry = m_resume_data.find_key("banned_peers");
		if (banned_peers_entry != 0 && banned_peers_entry->type() == entry::list_t)
		{
			peer_id id;
			std::fill(id.begin(), id.end(), 0);
			entry::list_type const& peer_list = banned_peers_entry->list();

			for (entry::list_type::const_iterator i = peer_list.begin();
				i != peer_list.end(); ++i)
			{
				if (i->type() != entry::dictionary_t) continue;
				entry const* ip = i->find_key("ip");
				entry const* port = i->find_key("port");
				if (ip == 0 || port == 0
					|| ip->type() != entry::string_t
					|| port->type() != entry::int_t)
					continue;
				tcp::endpoint a(
					address::from_string(ip->string())
					, (unsigned short)port->integer());
				policy::peer* p = m_policy.peer_from_tracker(a, id, peer_info::resume_data, 0);
				if (p) p->banned = true;
			}
		}

		bool fastresume_rejected = !j.str.empty();
		
		if (fastresume_rejected && m_ses.m_alerts.should_post(alert::warning))
		{
			m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle(), j.str));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << "fastresume data for "
				<< torrent_file().name() << " rejected: "
				<< j.str << "\n";
#endif
		}

		if (ret == 0)
		{
			// there are either no files for this torrent
			// or the resume_data was accepted

			m_num_pieces = 0;
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			if (!fastresume_rejected)
			{
				TORRENT_ASSERT(m_resume_data.type() == entry::dictionary_t);

				// parse have bitmask
				entry const* pieces = m_resume_data.find_key("pieces");
				if (pieces && pieces->type() == entry::string_t
					&& pieces->string().length() == m_torrent_file->num_pieces())
				{
					std::string const& pieces_str = pieces->string();
					for (int i = 0, end(pieces_str.size()); i < end; ++i)
					{
						bool have = pieces_str[i] & 1;
						m_have_pieces[i] = have;
						m_num_pieces += have;
					}
				}

				// parse unfinished pieces
				int num_blocks_per_piece =
					static_cast<int>(torrent_file().piece_length()) / block_size();

				entry const* unfinished_ent = m_resume_data.find_key("unfinished");
				if (unfinished_ent != 0 && unfinished_ent->type() == entry::list_t)
				{
					entry::list_type const& unfinished = unfinished_ent->list();
					int index = 0;
					for (entry::list_type::const_iterator i = unfinished.begin();
						i != unfinished.end(); ++i, ++index)
					{
						if (i->type() != entry::dictionary_t) continue;
						entry const* piece = i->find_key("piece");
						if (piece == 0 || piece->type() != entry::int_t) continue;
						int piece_index = int(piece->integer());
						if (piece_index < 0 || piece_index >= torrent_file().num_pieces())
							continue;

						if (m_have_pieces[piece_index])
						{
							m_have_pieces[piece_index] = false;
							--m_num_pieces;
						}

						entry const* bitmask_ent = i->find_key("bitmask");
						if (bitmask_ent == 0 || bitmask_ent->type() != entry::string_t) break;
						std::string const& bitmask = bitmask_ent->string();

						const int num_bitmask_bytes = (std::max)(num_blocks_per_piece / 8, 1);
						if ((int)bitmask.size() != num_bitmask_bytes) continue;
						for (int j = 0; j < num_bitmask_bytes; ++j)
						{
							unsigned char bits = bitmask[j];
							int num_bits = (std::min)(num_blocks_per_piece - j*8, 8);
							for (int k = 0; k < num_bits; ++k)
							{
								const int bit = j * 8 + k;
								if (bits & (1 << k))
								{
									m_picker->mark_as_finished(piece_block(piece_index, bit), 0);
									if (m_picker->is_piece_finished(piece_index))
										async_verify_piece(piece_index, bind(&torrent::piece_finished
											, shared_from_this(), piece_index, _1));
								}
							}
						}
					}
				}

				int index = 0;
				for (std::vector<bool>::iterator i = m_have_pieces.begin()
					, end(m_have_pieces.end()); i != end; ++i, ++index)
				{
					if (*i) m_picker->we_have(index);
				}
			}

			files_checked();
		}
		else
		{
			// either the fastresume data was rejected or there are
			// some files
			m_ses.check_torrent(shared_from_this());
		}
	}

	void torrent::start_checking()
	{
		m_state = torrent_status::checking_files;

		m_storage->async_check_files(bind(
			&torrent::on_piece_checked
			, shared_from_this(), _1, _2));
	}
	
	void torrent::on_piece_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.str));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
					" error: " << j.str <<
					" torrent: " << torrent_file().name() <<
					" ]\n";
#endif
			}
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;
			auto_managed(false);
			pause();
			m_ses.done_checking(shared_from_this());
			return;
		}

		m_progress = j.piece / float(torrent_file().num_pieces());

		if (j.offset >= 0 && !m_have_pieces[j.offset])
		{
			m_have_pieces[j.offset] = true;
			++m_num_pieces;
			TORRENT_ASSERT(m_picker);
			m_picker->we_have(j.offset);
		}

		// we're not done checking yet
		// this handler will be called repeatedly until
		// we're done, or encounter a failure
		if (ret == piece_manager::need_full_check) return;

		m_ses.done_checking(shared_from_this());
		files_checked();
	}

	void torrent::use_interface(const char* net_interface)
	{
		INVARIANT_CHECK;

		m_net_interface = tcp::endpoint(address::from_string(net_interface), 0);
	}

	void torrent::on_announce_disp(boost::weak_ptr<torrent> p
		, error_code const& e)
	{
		if (e) return;
		boost::shared_ptr<torrent> t = p.lock();
		if (!t) return;
		t->on_announce();
	}

	void torrent::on_announce()
	{
		if (m_abort) return;

		boost::weak_ptr<torrent> self(shared_from_this());

		error_code ec;
		if (!m_torrent_file->priv())
		{
			// announce on local network every 5 minutes
			m_announce_timer.expires_from_now(minutes(5), ec);
			m_announce_timer.async_wait(
				bind(&torrent::on_announce_disp, self, _1));

			// announce with the local discovery service
			if (!m_paused)
				m_ses.announce_lsd(m_torrent_file->info_hash());
		}
		else
		{
			m_announce_timer.expires_from_now(minutes(15), ec);
			m_announce_timer.async_wait(
				bind(&torrent::on_announce_disp, self, _1));
		}

#ifndef TORRENT_DISABLE_DHT
		if (m_paused) return;
		if (!m_ses.m_dht) return;
		ptime now = time_now();
		if (should_announce_dht() && now - m_last_dht_announce > minutes(14))
		{
			m_last_dht_announce = now;
			m_ses.m_dht->announce(m_torrent_file->info_hash()
				, m_ses.m_listen_sockets.front().external_port
				, bind(&torrent::on_dht_announce_response_disp, self, _1));
		}
#endif
	}

#ifndef TORRENT_DISABLE_DHT

	void torrent::on_dht_announce_response_disp(boost::weak_ptr<libtorrent::torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		boost::shared_ptr<libtorrent::torrent> tor = t.lock();
		if (!tor) return;
		tor->on_dht_announce_response(peers);
	}

	void torrent::on_dht_announce_response(std::vector<tcp::endpoint> const& peers)
	{
		if (peers.empty()) return;

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peers.size(), "DHT", "Got peers from DHT"));
		}
		std::for_each(peers.begin(), peers.end(), bind(
			&policy::peer_from_tracker, boost::ref(m_policy), _1, peer_id(0)
			, peer_info::dht, 0));
	}

#endif

	void torrent::scrape_tracker()
	{
		if (m_trackers.empty()) return;

		TORRENT_ASSERT(m_currently_trying_tracker >= 0);
		TORRENT_ASSERT(m_currently_trying_tracker < int(m_trackers.size()));
		
		tracker_request req;
		req.info_hash = m_torrent_file->info_hash();
		req.kind = tracker_request::scrape_request;
		req.url = m_trackers[m_currently_trying_tracker].url;
		m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
			, tracker_login(), m_ses.m_listen_interface.address(), shared_from_this());
	}

	// returns true if it is time for this torrent to make another
	// tracker request
	bool torrent::should_request()
	{
//		INVARIANT_CHECK;
		
		if (m_trackers.empty()) return false;

		if (m_just_paused)
		{
			m_just_paused = false;
			return true;
		}
		return !m_paused && m_next_request < time_now();
	}

	void torrent::tracker_warning(tracker_request const& req, std::string const& msg)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			m_ses.m_alerts.post_alert(tracker_warning_alert(get_handle(), req.url, msg));
		}
	}
	
 	void torrent::tracker_scrape_response(tracker_request const& req
 		, int complete, int incomplete, int downloaded)
 	{
 		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
 
 		INVARIANT_CHECK;
		TORRENT_ASSERT(req.kind == tracker_request::scrape_request);
 
 		if (complete >= 0) m_complete = complete;
 		if (incomplete >= 0) m_incomplete = incomplete;
 
 		if (m_ses.m_alerts.should_post(alert::info))
 		{
 			m_ses.m_alerts.post_alert(scrape_reply_alert(
 				get_handle(), m_incomplete, m_complete, req.url, "got scrape response from tracker"));
 		}
 	}
 
	void torrent::tracker_response(
		tracker_request const& r
		, std::vector<peer_entry>& peer_list
		, int interval
		, int complete
		, int incomplete
		, address const& external_ip)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		TORRENT_ASSERT(r.kind == tracker_request::announce_request);

		if (external_ip != address())
			m_ses.set_external_address(external_ip);

		m_failed_trackers = 0;
		// announce intervals less than 5 minutes
		// are insane.
		if (interval < 60 * 5) interval = 60 * 5;

		m_last_working_tracker
			= prioritize_tracker(m_currently_trying_tracker);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		m_next_request = time_now() + seconds(m_duration);

		if (complete >= 0) m_complete = complete;
		if (incomplete >= 0) m_incomplete = incomplete;

		// connect to random peers from the list
		std::random_shuffle(peer_list.begin(), peer_list.end());

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		std::stringstream s;
		s << "TRACKER RESPONSE:\n"
			"interval: " << m_duration << "\n"
			"peers:\n";
		for (std::vector<peer_entry>::const_iterator i = peer_list.begin();
			i != peer_list.end(); ++i)
		{
			s << "  " << std::setfill(' ') << std::setw(16) << i->ip
				<< " " << std::setw(5) << std::dec << i->port << "  ";
			if (!i->pid.is_all_zeros()) s << " " << i->pid << " " << identify_client(i->pid);
			s << "\n";
		}
		s << "external ip: " << external_ip << "\n";
		debug_log(s.str());
#endif
		// for each of the peers we got from the tracker
		for (std::vector<peer_entry>::iterator i = peer_list.begin();
			i != peer_list.end(); ++i)
		{
			// don't make connections to ourself
			if (i->pid == m_ses.get_peer_id())
				continue;

			error_code ec;
			tcp::endpoint a(address::from_string(i->ip, ec), i->port);

			if (ec)
			{
				// assume this is because we got a hostname instead of
				// an ip address from the tracker

				tcp::resolver::query q(i->ip, boost::lexical_cast<std::string>(i->port));
				m_host_resolver.async_resolve(q,
					bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i->pid));
			}
			else
			{
				if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
				{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
					debug_log("blocked ip from tracker: " + i->ip);
#endif
					if (m_ses.m_alerts.should_post(alert::info))
					{	
						m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()
							, "peer from tracker blocked by IP filter"));
					}

					continue;
				}

				m_policy.peer_from_tracker(a, i->pid, peer_info::tracker, 0);
			}
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peer_list.size(), r.url, "got response from tracker"));
		}
		m_got_tracker_response = true;
	}

	void torrent::on_peer_name_lookup(error_code const& e, tcp::resolver::iterator host
		, peer_id pid)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (e || host == tcp::resolver::iterator() ||
			m_ses.is_aborted()) return;

		if (m_ses.m_ip_filter.access(host->endpoint().address()) & ip_filter::blocked)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			debug_log("blocked ip from tracker: " + host->endpoint().address().to_string());
#endif
			if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(peer_blocked_alert(host->endpoint().address()
					, "peer from tracker blocked by IP filter"));
			}

			return;
		}
			
		m_policy.peer_from_tracker(*host, pid, peer_info::tracker, 0);
	}

	size_type torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return -1;
		return m_torrent_file->total_size()
			- quantized_bytes_done();
	}

	size_type torrent::quantized_bytes_done() const
	{
//		INVARIANT_CHECK;

		if (!valid_metadata()) return 0;

		if (m_torrent_file->num_pieces() == 0)
			return 0;

		if (is_seed()) return m_torrent_file->total_size();

		const int last_piece = m_torrent_file->num_pieces() - 1;

		size_type total_done
			= size_type(m_num_pieces) * m_torrent_file->piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			int corr = m_torrent_file->piece_size(last_piece)
				- m_torrent_file->piece_length();
			total_done += corr;
		}
		return total_done;
	}

	// the first value is the total number of bytes downloaded
	// the second value is the number of bytes of those that haven't
	// been filtered as not wanted we have downloaded
	tuple<size_type, size_type> torrent::bytes_done() const
	{
		INVARIANT_CHECK;

		if (!valid_metadata() || m_torrent_file->num_pieces() == 0)
			return tuple<size_type, size_type>(0,0);

		const int last_piece = m_torrent_file->num_pieces() - 1;
		const int piece_size = m_torrent_file->piece_length();

		if (is_seed())
			return make_tuple(m_torrent_file->total_size()
				, m_torrent_file->total_size());

		TORRENT_ASSERT(m_num_pieces >= m_picker->num_have_filtered());
		size_type wanted_done = size_type(m_num_pieces - m_picker->num_have_filtered())
			* piece_size;
		TORRENT_ASSERT(wanted_done >= 0);
		
		size_type total_done
			= size_type(m_num_pieces) * piece_size;
		TORRENT_ASSERT(m_num_pieces < m_torrent_file->num_pieces());

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			TORRENT_ASSERT(total_done >= piece_size);
			int corr = m_torrent_file->piece_size(last_piece)
				- piece_size;
			TORRENT_ASSERT(corr <= 0);
			TORRENT_ASSERT(corr > -piece_size);
			total_done += corr;
			if (m_picker->piece_priority(last_piece) != 0)
			{
				TORRENT_ASSERT(wanted_done >= piece_size);
				wanted_done += corr;
			}
		}

		TORRENT_ASSERT(total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(wanted_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(total_done >= wanted_done);

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = piece_size / m_block_size;

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			if (m_have_pieces[index]) continue;
			TORRENT_ASSERT(i->finished <= m_picker->blocks_in_piece(index));

#ifndef NDEBUG
			for (std::vector<piece_picker::downloading_piece>::const_iterator j = boost::next(i);
				j != dl_queue.end(); ++j)
			{
				TORRENT_ASSERT(j->index != index);
			}
#endif

			for (int j = 0; j < blocks_per_piece; ++j)
			{
				TORRENT_ASSERT(m_picker->is_finished(piece_block(index, j)) == (i->info[j].state == piece_picker::block_info::state_finished));
				corr += (i->info[j].state == piece_picker::block_info::state_finished) * m_block_size;
				TORRENT_ASSERT(corr >= 0);
				TORRENT_ASSERT(index != last_piece || j < m_picker->blocks_in_last_piece()
					|| i->info[j].state != piece_picker::block_info::state_finished);
			}

			// correction if this was the last piece
			// and if we have the last block
			if (i->index == last_piece
				&& i->info[m_picker->blocks_in_last_piece()-1].state
					== piece_picker::block_info::state_finished)
			{
				corr -= m_block_size;
				corr += m_torrent_file->piece_size(last_piece) % m_block_size;
			}
			total_done += corr;
			if (m_picker->piece_priority(index) != 0)
				wanted_done += corr;
		}

		TORRENT_ASSERT(total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(wanted_done <= m_torrent_file->total_size());

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection* pc = *i;
			boost::optional<piece_block_progress> p
				= pc->downloading_piece_progress();
			if (p)
			{
				if (m_have_pieces[p->piece_index])
					continue;

				piece_block block(p->piece_index, p->block_index);
				if (m_picker->is_finished(block))
					continue;

				std::map<piece_block, int>::iterator dp
					= downloading_piece.find(block);
				if (dp != downloading_piece.end())
				{
					if (dp->second < p->bytes_downloaded)
						dp->second = p->bytes_downloaded;
				}
				else
				{
					downloading_piece[block] = p->bytes_downloaded;
				}
#ifndef NDEBUG
				TORRENT_ASSERT(p->bytes_downloaded <= p->full_block_bytes);
				int last_piece = m_torrent_file->num_pieces() - 1;
				if (p->piece_index == last_piece
					&& p->block_index == m_torrent_file->piece_size(last_piece) / block_size())
					TORRENT_ASSERT(p->full_block_bytes == m_torrent_file->piece_size(last_piece) % block_size());
				else
					TORRENT_ASSERT(p->full_block_bytes == block_size());
#endif
			}
		}
		for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
			i != downloading_piece.end(); ++i)
		{
			total_done += i->second;
			if (m_picker->piece_priority(i->first.piece_index) != 0)
				wanted_done += i->second;
		}

#ifndef NDEBUG

		if (total_done >= m_torrent_file->total_size())
		{
			// Thist happens when a piece has been downloaded completely
			// but not yet verified against the hash
			std::copy(m_have_pieces.begin(), m_have_pieces.end()
				, std::ostream_iterator<bool>(std::cerr, " "));
			std::cerr << std::endl;
			std::cerr << "num_pieces: " << m_num_pieces << std::endl;
			
			std::cerr << "unfinished:" << std::endl;
			
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				std::cerr << "   " << i->index << " ";
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					std::cerr << (i->info[j].state == piece_picker::block_info::state_finished ? "1" : "0");
				}
				std::cerr << std::endl;
			}
			
			std::cerr << "downloading pieces:" << std::endl;

			for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
				i != downloading_piece.end(); ++i)
			{
				std::cerr << "   " << i->first.piece_index << ":" << i->first.block_index
					<< "  " << i->second << std::endl;
			}

		}

		TORRENT_ASSERT(total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(wanted_done <= m_torrent_file->total_size());

#endif

		TORRENT_ASSERT(total_done >= wanted_done);
		return make_tuple(total_done, wanted_done);
	}

	// passed_hash_check
	// 0: success, piece passed check
	// -1: disk failure
	// -2: piece failed check
	void torrent::piece_finished(int index, int passed_hash_check)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << time_now_string() << " *** PIECE_FINISHED [ p: "
			<< index << " chk: " << ((passed_hash_check == 0)
				?"passed":passed_hash_check == -1
				?"disk failed":"failed") << " ]\n";
#endif

		bool was_seed = is_seed();
		bool was_finished = m_picker->num_filtered() + num_pieces()
			== torrent_file().num_pieces();

		if (passed_hash_check == 0)
		{
			if (m_ses.m_alerts.should_post(alert::debug))
			{
				m_ses.m_alerts.post_alert(piece_finished_alert(get_handle()
					, index, "piece finished"));
			}
			// the following call may cause picker to become invalid
			// in case we just became a seed
			announce_piece(index);
			TORRENT_ASSERT(valid_metadata());
			// if we just became a seed, picker is now invalid, since it
			// is deallocated by the torrent once it starts seeding
			if (!was_finished
				&& (is_seed()
					|| m_picker->num_filtered() + num_pieces()
					== torrent_file().num_pieces()))
			{
				// torrent finished
				// i.e. all the pieces we're interested in have
				// been downloaded. Release the files (they will open
				// in read only mode if needed)
				finished();
			}
		}
		else if (passed_hash_check == -2)
		{
			piece_failed(index);
		}
		else
		{
			TORRENT_ASSERT(passed_hash_check == -1);
			m_picker->restore_piece(index);
		}

		m_policy.piece_finished(index, passed_hash_check == 0);

		if (!was_seed && is_seed())
		{
			TORRENT_ASSERT(passed_hash_check == 0);
			completed();
		}
	}

	void torrent::piece_failed(int index)
	{
		// if the last piece fails the peer connection will still
		// think that it has received all of it until this function
		// resets the download queue. So, we cannot do the
		// invariant check here since it assumes:
		// (total_done == m_torrent_file->total_size()) => is_seed()
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_storage);
		TORRENT_ASSERT(m_storage->refcount() > 0);
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
	  	TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "hash for piece " << index << " failed";
			m_ses.m_alerts.post_alert(hash_failed_alert(get_handle(), index, s.str()));
		}
		// increase the total amount of failed bytes
		m_total_failed_bytes += m_torrent_file->piece_size(index);

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#ifndef NDEBUG
		for (std::vector<void*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i)
		{
			policy::peer* p = (policy::peer*)*i;
			if (p && p->connection)
			{
				p->connection->piece_failed = true;
			}
		}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(*i)->on_piece_failed(index);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

		for (std::set<void*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			policy::peer* p = static_cast<policy::peer*>(*i);
			if (p == 0) continue;
			if (p->connection) p->connection->received_invalid_data(index);

			// either, we have received too many failed hashes
			// or this was the only peer that sent us this piece.
			// TODO: make this a changable setting
			if (p->trust_points <= -7
				|| peers.size() == 1)
			{
				// we don't trust this peer anymore
				// ban it.
				if (m_ses.m_alerts.should_post(alert::info))
				{
					m_ses.m_alerts.post_alert(peer_ban_alert(
						p->ip
						, get_handle()
						, "banning peer because of too many corrupt pieces"));
				}

				// mark the peer as banned
				p->banned = true;

				if (p->connection)
				{
#ifdef TORRENT_LOGGING
					(*m_ses.m_logger) << time_now_string() << " *** BANNING PEER [ " << p->ip
						<< " ] 'too many corrupt pieces'\n";
#endif
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
					(*p->connection->m_logger) << "*** BANNING PEER [ " << p->ip
						<< " ] 'too many corrupt pieces'\n";
#endif
					p->connection->disconnect("too many corrupt pieces, banning peer");
				}
			}
		}

		// we have to let the piece_picker know that
		// this piece failed the check as it can restore it
		// and mark it as being interesting for download
		// TODO: do this more intelligently! and keep track
		// of how much crap (data that failed hash-check) and
		// how much redundant data we have downloaded
		// if some clients has sent more than one piece
		// start with redownloading the pieces that the client
		// that has sent the least number of pieces
		m_picker->restore_piece(index);
		TORRENT_ASSERT(m_storage);

		TORRENT_ASSERT(m_have_pieces[index] == false);

#ifndef NDEBUG
		for (std::vector<void*>::iterator i = downloaders.begin()
			, end(downloaders.end()); i != end; ++i)
		{
			policy::peer* p = (policy::peer*)*i;
			if (p && p->connection)
			{
				p->connection->piece_failed = false;
			}
		}
#endif
	}

	void torrent::abort()
	{
		INVARIANT_CHECK;

		m_abort = true;
		// if the torrent is paused, it doesn't need
		// to announce with even=stopped again.
		if (!m_paused)
			m_event = tracker_request::stopped;
		// disconnect all peers and close all
		// files belonging to the torrents

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*(*i)->m_logger) << "*** ABORTING TORRENT\n";
		}
#endif

		disconnect_all();
		if (m_owning_storage.get())
			m_storage->async_release_files(
				bind(&torrent::on_files_released, shared_from_this(), _1, _2));
			
		m_owning_storage = 0;
		error_code ec;
		m_announce_timer.cancel(ec);
		m_host_resolver.cancel();
	}

	void torrent::on_files_deleted(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post(alert::warning))
		{
			if (ret != 0)
			{
				alerts().post_alert(torrent_deleted_alert(get_handle(), "delete files failed: " + j.str));
			}
			else
			{
				alerts().post_alert(torrent_deleted_alert(get_handle(), "files deleted"));
			}
		}
	}

	void torrent::on_files_released(int ret, disk_io_job const& j)
	{
/*
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post(alert::warning))
		{
			alerts().post_alert(torrent_paused_alert(get_handle(), "torrent paused"));
		}
*/
	}

	void torrent::on_save_resume_data(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post(alert::warning))
		{
			char const* msg;
			if (j.resume_data)
			{
				write_resume_data(*j.resume_data);
				msg = "resume data generated";
			}
			else
			{
				msg = j.str.c_str();
			}
			alerts().post_alert(save_resume_data_alert(j.resume_data
				, get_handle(), msg));
		}
	}

	void torrent::on_torrent_paused(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post(alert::warning))
		{
			alerts().post_alert(torrent_paused_alert(get_handle(), "torrent paused"));
		}
	}

	void torrent::announce_piece(int index)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		if (!m_have_pieces[index])
			m_num_pieces++;
		m_have_pieces[index] = true;

		TORRENT_ASSERT(std::accumulate(m_have_pieces.begin(), m_have_pieces.end(), 0)
			== m_num_pieces);

		m_picker->we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			(*i)->announce_piece(index);

		for (std::set<void*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			policy::peer* p = static_cast<policy::peer*>(*i);
			if (p == 0) continue;
			p->on_parole = false;
			++p->trust_points;
			// TODO: make this limit user settable
			if (p->trust_points > 20) p->trust_points = 20;
			if (p->connection) p->connection->received_valid_data(index);
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(*i)->on_piece_pass(index);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif
		if (is_seed())
		{
			m_state = torrent_status::seeding;
			m_picker.reset();
		}
	}

	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}

	void torrent::piece_availability(std::vector<int>& avail) const
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed())
		{
			avail.clear();
			return;
		}

		m_picker->get_availability(avail);
	}

	void torrent::set_piece_priority(int index, int priority)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		bool was_finished = is_finished();
		bool filter_updated = m_picker->set_piece_priority(index, priority);
		TORRENT_ASSERT(m_num_pieces >= m_picker->num_have_filtered());
		if (filter_updated) update_peer_interest(was_finished);
	}

	int torrent::piece_priority(int index) const
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return 1;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		return m_picker->piece_priority(index);
	}

	void torrent::prioritize_pieces(std::vector<int> const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		TORRENT_ASSERT(m_picker.get());

		int index = 0;
		bool filter_updated = false;
		bool was_finished = is_finished();
		for (std::vector<int>::const_iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i, ++index)
		{
			TORRENT_ASSERT(*i >= 0);
			TORRENT_ASSERT(*i <= 7);
			filter_updated |= m_picker->set_piece_priority(index, *i);
			TORRENT_ASSERT(m_num_pieces >= m_picker->num_have_filtered());
		}
		if (filter_updated) update_peer_interest(was_finished);
	}

	void torrent::piece_priorities(std::vector<int>& pieces) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed())
		{
			pieces.clear();
			pieces.resize(m_torrent_file->num_pieces(), 1);
			return;
		}

		TORRENT_ASSERT(m_picker.get());
		m_picker->piece_priorities(pieces);
	}

	namespace
	{
		void set_if_greater(int& piece_prio, int file_prio)
		{
			if (file_prio > piece_prio) piece_prio = file_prio;
		}
	}

	void torrent::prioritize_files(std::vector<int> const& files)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the bitmask need to have exactly one bit for every file
		// in the torrent
		TORRENT_ASSERT(int(files.size()) == m_torrent_file->num_files());
		
		size_type position = 0;

		if (m_torrent_file->num_pieces() == 0) return;

		bool was_finished = is_finished();

		int piece_length = m_torrent_file->piece_length();
		// initialize the piece priorities to 0, then only allow
		// setting higher priorities
		std::vector<int> pieces(m_torrent_file->num_pieces(), 0);
		for (int i = 0; i < int(files.size()); ++i)
		{
			size_type start = position;
			size_type size = m_torrent_file->file_at(i).size;
			if (size == 0) continue;
			position += size;
			// mark all pieces of the file with this file's priority
			// but only if the priority is higher than the pieces
			// already set (to avoid problems with overlapping pieces)
			int start_piece = int(start / piece_length);
			int last_piece = int((position - 1) / piece_length);
			TORRENT_ASSERT(last_piece <= int(pieces.size()));
			// if one piece spans several files, we might
			// come here several times with the same start_piece, end_piece
			std::for_each(pieces.begin() + start_piece
				, pieces.begin() + last_piece + 1
				, bind(&set_if_greater, _1, files[i]));
		}
		prioritize_pieces(pieces);
		update_peer_interest(was_finished);
	}

	// this is called when piece priorities have been updated
	// updates the interested flag in peers
	void torrent::update_peer_interest(bool was_finished)
	{
		for (peer_iterator i = begin(); i != end(); ++i)
			(*i)->update_interest();

		// if we used to be finished, but we aren't anymore
		// we may need to connect to peers again
		if (!is_finished() && was_finished)
			m_policy.recalculate_connect_candidates();
	
		// the torrent just became finished
		if (is_finished() && !was_finished)
			finished();
	}

	void torrent::filter_piece(int index, bool filter)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		bool was_finished = is_finished();
		m_picker->set_piece_priority(index, filter ? 1 : 0);
		update_peer_interest(was_finished);
	}

	void torrent::filter_pieces(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return;

		TORRENT_ASSERT(m_picker.get());

		bool was_finished = is_finished();
		int index = 0;
		for (std::vector<bool>::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if ((m_picker->piece_priority(index) == 0) == *i) continue;
			if (*i)
				m_picker->set_piece_priority(index, 0);
			else
				m_picker->set_piece_priority(index, 1);
		}
		update_peer_interest(was_finished);
	}

	bool torrent::is_piece_filtered(int index) const
	{
		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed()) return false;
		
		TORRENT_ASSERT(m_picker.get());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		return m_picker->piece_priority(index) == 0;
	}

	void torrent::filtered_pieces(std::vector<bool>& bitmask) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		TORRENT_ASSERT(valid_metadata());
		if (is_seed())
		{
			bitmask.clear();
			bitmask.resize(m_torrent_file->num_pieces(), false);
			return;
		}

		TORRENT_ASSERT(m_picker.get());
		m_picker->filtered_pieces(bitmask);
	}

	void torrent::filter_files(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the bitmask need to have exactly one bit for every file
		// in the torrent
		TORRENT_ASSERT((int)bitmask.size() == m_torrent_file->num_files());
		
		size_type position = 0;

		if (m_torrent_file->num_pieces())
		{
			int piece_length = m_torrent_file->piece_length();
			// mark all pieces as filtered, then clear the bits for files
			// that should be downloaded
			std::vector<bool> piece_filter(m_torrent_file->num_pieces(), true);
			for (int i = 0; i < (int)bitmask.size(); ++i)
			{
				size_type start = position;
				position += m_torrent_file->file_at(i).size;
				// is the file selected for download?
				if (!bitmask[i])
				{           
					// mark all pieces of the file as downloadable
					int start_piece = int(start / piece_length);
					int last_piece = int(position / piece_length);
					// if one piece spans several files, we might
					// come here several times with the same start_piece, end_piece
					std::fill(piece_filter.begin() + start_piece, piece_filter.begin()
						+ last_piece + 1, false);
				}
			}
			filter_pieces(piece_filter);
		}
	}

	void torrent::replace_trackers(std::vector<announce_entry> const& urls)
	{
		m_trackers = urls;
		if (m_currently_trying_tracker >= (int)m_trackers.size())
			m_currently_trying_tracker = (int)m_trackers.size()-1;
		m_last_working_tracker = -1;
	}

	tracker_request torrent::generate_tracker_request()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_trackers.empty());

		m_next_request = time_now() + seconds(tracker_retry_delay_max);

		tracker_request req;
		req.info_hash = m_torrent_file->info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;
		req.event = m_event;
		tcp::endpoint ep = m_ses.get_ipv6_interface();
		if (ep != tcp::endpoint())
			req.ipv6 = ep.address().to_string();

		if (m_event != tracker_request::stopped)
			m_event = tracker_request::none;
		req.url = m_trackers[m_currently_trying_tracker].url;
		req.num_want = m_settings.num_want;
		// if we are aborting. we don't want any new peers
		if (req.event == tracker_request::stopped)
			req.num_want = 0;

		// default initialize, these should be set by caller
		// before passing the request to the tracker_manager
		req.listen_port = 0;
		req.key = 0;

		return req;
	}

	void torrent::choke_peer(peer_connection& c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!c.is_choked());
		TORRENT_ASSERT(m_num_uploads > 0);
		c.send_choke();
		--m_num_uploads;
	}
	
	bool torrent::unchoke_peer(peer_connection& c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(c.is_choked());
		if (m_num_uploads >= m_max_uploads) return false;
		c.send_unchoke();
		++m_num_uploads;
		return true;
	}

	void torrent::cancel_block(piece_block block)
	{
		INVARIANT_CHECK;

		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			(*i)->cancel_request(block);
		}
	}

	void torrent::remove_peer(peer_connection* p)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(p != 0);

		peer_iterator i = m_connections.find(p);
		if (i == m_connections.end())
		{
			TORRENT_ASSERT(false);
			return;
		}

		if (ready_for_connections())
		{
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

			if (p->is_seed())
			{
				if (m_picker.get())
				{
					TORRENT_ASSERT(!is_seed());
					m_picker->dec_refcount_all();
				}
			}
			else
			{
				if (m_picker.get())
				{
					const std::vector<bool>& pieces = p->get_bitfield();
					TORRENT_ASSERT(std::count(pieces.begin(), pieces.end(), true)
						< int(pieces.size()));
					m_picker->dec_refcount(pieces);
				}
			}
		}

		if (!p->is_choked())
			--m_num_uploads;

		m_policy.connection_closed(*p);
		p->set_peer_info(0);
		TORRENT_ASSERT(i != m_connections.end());
		m_connections.erase(i);

		// remove from bandwidth request-queue
		for (int c = 0; c < 2; ++c)
		{
			for (queue_t::iterator i = m_bandwidth_queue[c].begin()
				, end(m_bandwidth_queue[c].end()); i != end; ++i)
			{
				if (i->peer != p) continue;
				m_bandwidth_queue[c].erase(i);
				break;
			}
		}
	}

	void torrent::connect_to_url_seed(std::string const& url)
	{
		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " resolving web seed: " << url << "\n";
#endif

		std::string protocol;
		std::string auth;
		std::string hostname;
		int port;
		std::string path;
		boost::tie(protocol, auth, hostname, port, path)
			= parse_url_components(url);

#ifdef TORRENT_USE_OPENSSL
		if (protocol != "http" && protocol != "https")
#else
		if (protocol != "http")
#endif
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, "unknown protocol"));
			}
			// never try it again
			remove_url_seed(url);
			return;
		}

		if (hostname.empty())
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, "invalid hostname"));
			}
			// never try it again
			remove_url_seed(url);
			return;
		}

		if (port == 0)
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, "invalid port"));
			}
			// never try it again
			remove_url_seed(url);
			return;
		}

		m_resolving_web_seeds.insert(url);
		proxy_settings const& ps = m_ses.web_seed_proxy();
		if (ps.type == proxy_settings::http
			|| ps.type == proxy_settings::http_pw)
		{
			// use proxy
			tcp::resolver::query q(ps.hostname
				, boost::lexical_cast<std::string>(ps.port));
			m_host_resolver.async_resolve(q,
				bind(&torrent::on_proxy_name_lookup, shared_from_this(), _1, _2, url));
		}
		else
		{
			if (m_ses.m_port_filter.access(port) & port_filter::blocked)
			{
				if (m_ses.m_alerts.should_post(alert::warning))
				{
					m_ses.m_alerts.post_alert(
						url_seed_alert(get_handle(), url, "port blocked by port-filter"));
				}
				// never try it again
				remove_url_seed(url);
				return;
			}

			tcp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
			m_host_resolver.async_resolve(q,
				bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url
					, tcp::endpoint()));
		}

	}

	void torrent::on_proxy_name_lookup(error_code const& e, tcp::resolver::iterator host
		, std::string url)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " completed resolve proxy hostname for: " << url << "\n";
#endif

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				std::stringstream msg;
				msg << "HTTP seed proxy hostname lookup failed: " << e.message();
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, msg.str()));
			}

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_url_seed(url);
			return;
		}

		if (m_ses.is_aborted()) return;

		tcp::endpoint a(host->endpoint());

		using boost::tuples::ignore;
		std::string hostname;
		int port;
		boost::tie(ignore, ignore, hostname, port, ignore)
			= parse_url_components(url);

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()
					, "proxy (" + hostname + ") blocked by IP filter"));
			}
			return;
		}

		tcp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
		m_host_resolver.async_resolve(q,
			bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url, a));
	}

	void torrent::on_name_lookup(error_code const& e, tcp::resolver::iterator host
		, std::string url, tcp::endpoint proxy)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " completed resolve: " << url << "\n";
#endif

		std::set<std::string>::iterator i = m_resolving_web_seeds.find(url);
		if (i != m_resolving_web_seeds.end()) m_resolving_web_seeds.erase(i);

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post(alert::warning))
			{
				std::stringstream msg;
				msg << "HTTP seed hostname lookup failed: " << e.message();
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, msg.str()));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << url << "\n";
#endif

			// the name lookup failed for the http host. Don't try
			// this host again
			remove_url_seed(url);
			return;
		}

		if (m_ses.is_aborted()) return;

		tcp::endpoint a(host->endpoint());

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()
					, "web seed (" + url + ") blocked by IP filter"));
			}
			return;
		}
		
		boost::shared_ptr<socket_type> s(new (std::nothrow) socket_type(m_ses.m_io_service));
		if (!s) return;
	
		bool ret = instantiate_connection(m_ses.m_io_service, m_ses.web_seed_proxy(), *s);
		TORRENT_ASSERT(ret);

		if (m_ses.web_seed_proxy().type == proxy_settings::http
			|| m_ses.web_seed_proxy().type == proxy_settings::http_pw)
		{
			// the web seed connection will talk immediately to
			// the proxy, without requiring CONNECT support
			s->get<http_stream>().set_no_connect(true);
		}

		std::pair<int, int> const& out_ports = m_settings.outgoing_ports;
		error_code ec;
		if (out_ports.first > 0 && out_ports.second >= out_ports.first)
			s->bind(tcp::endpoint(address(), m_ses.next_port()), ec);
		
		boost::intrusive_ptr<peer_connection> c(new (std::nothrow) web_peer_connection(
			m_ses, shared_from_this(), s, a, url, 0));
		if (!c) return;
			
#ifndef NDEBUG
		c->m_in_constructor = false;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<peer_plugin> pp((*i)->new_connection(c.get()));
			if (pp) c->add_extension(pp);
		}
#endif

		// add the newly connected peer to this torrent's peer list
		m_connections.insert(boost::get_pointer(c));
		m_ses.m_connections.insert(c);

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(boost::get_pointer(c));
			m_ses.m_connections.insert(c);
			c->start();

			m_ses.m_half_open.enqueue(
				bind(&peer_connection::connect, c, _1)
				, bind(&peer_connection::timed_out, c)
				, seconds(settings().peer_connect_timeout));
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << e.what() << "\n";
#endif
			c->disconnect(e.what(), 1);
		}
#endif
	}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	namespace
	{
		unsigned long swap_bytes(unsigned long a)
		{
			return (a >> 24) | ((a & 0xff0000) >> 8) | ((a & 0xff00) << 8) | (a << 24);
		}
	}
	
	void torrent::resolve_peer_country(boost::intrusive_ptr<peer_connection> const& p) const
	{
		if (m_resolving_country
			|| p->has_country()
			|| p->is_connecting()
			|| p->is_queued()
			|| p->in_handshake()
			|| p->remote().address().is_v6()) return;

		m_resolving_country = true;
		asio::ip::address_v4 reversed(swap_bytes(p->remote().address().to_v4().to_ulong()));
		tcp::resolver::query q(reversed.to_string() + ".zz.countries.nerd.dk", "0");
		m_host_resolver.async_resolve(q,
			bind(&torrent::on_country_lookup, shared_from_this(), _1, _2, p));
	}

	namespace
	{
		struct country_entry
		{
			int code;
			char const* name;
		};
	}

	void torrent::on_country_lookup(error_code const& error, tcp::resolver::iterator i
		, intrusive_ptr<peer_connection> p) const
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		
		m_resolving_country = false;

		// must be ordered in increasing order
		static const country_entry country_map[] =
		{
			  {  4,  "AF"}, {  8,  "AL"}, { 10,  "AQ"}, { 12,  "DZ"}, { 16,  "AS"}
			, { 20,  "AD"}, { 24,  "AO"}, { 28,  "AG"}, { 31,  "AZ"}, { 32,  "AR"}
			, { 36,  "AU"}, { 40,  "AT"}, { 44,  "BS"}, { 48,  "BH"}, { 50,  "BD"}
			, { 51,  "AM"}, { 52,  "BB"}, { 56,  "BE"}, { 60,  "BM"}, { 64,  "BT"}
			, { 68,  "BO"}, { 70,  "BA"}, { 72,  "BW"}, { 74,  "BV"}, { 76,  "BR"}
			, { 84,  "BZ"}, { 86,  "IO"}, { 90,  "SB"}, { 92,  "VG"}, { 96,  "BN"}
			, {100,  "BG"}, {104,  "MM"}, {108,  "BI"}, {112,  "BY"}, {116,  "KH"}
			, {120,  "CM"}, {124,  "CA"}, {132,  "CV"}, {136,  "KY"}, {140,  "CF"}
			, {144,  "LK"}, {148,  "TD"}, {152,  "CL"}, {156,  "CN"}, {158,  "TW"}
			, {162,  "CX"}, {166,  "CC"}, {170,  "CO"}, {174,  "KM"}, {175,  "YT"}
			, {178,  "CG"}, {180,  "CD"}, {184,  "CK"}, {188,  "CR"}, {191,  "HR"}
			, {192,  "CU"}, {203,  "CZ"}, {204,  "BJ"}, {208,  "DK"}, {212,  "DM"}
			, {214,  "DO"}, {218,  "EC"}, {222,  "SV"}, {226,  "GQ"}, {231,  "ET"}
			, {232,  "ER"}, {233,  "EE"}, {234,  "FO"}, {238,  "FK"}, {239,  "GS"}
			, {242,  "FJ"}, {246,  "FI"}, {248,  "AX"}, {250,  "FR"}, {254,  "GF"}
			, {258,  "PF"}, {260,  "TF"}, {262,  "DJ"}, {266,  "GA"}, {268,  "GE"}
			, {270,  "GM"}, {275,  "PS"}, {276,  "DE"}, {288,  "GH"}, {292,  "GI"}
			, {296,  "KI"}, {300,  "GR"}, {304,  "GL"}, {308,  "GD"}, {312,  "GP"}
			, {316,  "GU"}, {320,  "GT"}, {324,  "GN"}, {328,  "GY"}, {332,  "HT"}
			, {334,  "HM"}, {336,  "VA"}, {340,  "HN"}, {344,  "HK"}, {348,  "HU"}
			, {352,  "IS"}, {356,  "IN"}, {360,  "ID"}, {364,  "IR"}, {368,  "IQ"}
			, {372,  "IE"}, {376,  "IL"}, {380,  "IT"}, {384,  "CI"}, {388,  "JM"}
			, {392,  "JP"}, {398,  "KZ"}, {400,  "JO"}, {404,  "KE"}, {408,  "KP"}
			, {410,  "KR"}, {414,  "KW"}, {417,  "KG"}, {418,  "LA"}, {422,  "LB"}
			, {426,  "LS"}, {428,  "LV"}, {430,  "LR"}, {434,  "LY"}, {438,  "LI"}
			, {440,  "LT"}, {442,  "LU"}, {446,  "MO"}, {450,  "MG"}, {454,  "MW"}
			, {458,  "MY"}, {462,  "MV"}, {466,  "ML"}, {470,  "MT"}, {474,  "MQ"}
			, {478,  "MR"}, {480,  "MU"}, {484,  "MX"}, {492,  "MC"}, {496,  "MN"}
			, {498,  "MD"}, {500,  "MS"}, {504,  "MA"}, {508,  "MZ"}, {512,  "OM"}
			, {516,  "NA"}, {520,  "NR"}, {524,  "NP"}, {528,  "NL"}, {530,  "AN"}
			, {533,  "AW"}, {540,  "NC"}, {548,  "VU"}, {554,  "NZ"}, {558,  "NI"}
			, {562,  "NE"}, {566,  "NG"}, {570,  "NU"}, {574,  "NF"}, {578,  "NO"}
			, {580,  "MP"}, {581,  "UM"}, {583,  "FM"}, {584,  "MH"}, {585,  "PW"}
			, {586,  "PK"}, {591,  "PA"}, {598,  "PG"}, {600,  "PY"}, {604,  "PE"}
			, {608,  "PH"}, {612,  "PN"}, {616,  "PL"}, {620,  "PT"}, {624,  "GW"}
			, {626,  "TL"}, {630,  "PR"}, {634,  "QA"}, {634,  "QA"}, {638,  "RE"}
			, {642,  "RO"}, {643,  "RU"}, {646,  "RW"}, {654,  "SH"}, {659,  "KN"}
			, {660,  "AI"}, {662,  "LC"}, {666,  "PM"}, {670,  "VC"}, {674,  "SM"}
			, {678,  "ST"}, {682,  "SA"}, {686,  "SN"}, {690,  "SC"}, {694,  "SL"}
			, {702,  "SG"}, {703,  "SK"}, {704,  "VN"}, {705,  "SI"}, {706,  "SO"}
			, {710,  "ZA"}, {716,  "ZW"}, {724,  "ES"}, {732,  "EH"}, {736,  "SD"}
			, {740,  "SR"}, {744,  "SJ"}, {748,  "SZ"}, {752,  "SE"}, {756,  "CH"}
			, {760,  "SY"}, {762,  "TJ"}, {764,  "TH"}, {768,  "TG"}, {772,  "TK"}
			, {776,  "TO"}, {780,  "TT"}, {784,  "AE"}, {788,  "TN"}, {792,  "TR"}
			, {795,  "TM"}, {796,  "TC"}, {798,  "TV"}, {800,  "UG"}, {804,  "UA"}
			, {807,  "MK"}, {818,  "EG"}, {826,  "GB"}, {834,  "TZ"}, {840,  "US"}
			, {850,  "VI"}, {854,  "BF"}, {858,  "UY"}, {860,  "UZ"}, {862,  "VE"}
			, {876,  "WF"}, {882,  "WS"}, {887,  "YE"}, {891,  "CS"}, {894,  "ZM"}
		};

		if (error || i == tcp::resolver::iterator())
		{
			// this is used to indicate that we shouldn't
			// try to resolve it again
			p->set_country("--");
			return;
		}

		while (i != tcp::resolver::iterator()
			&& !i->endpoint().address().is_v4()) ++i;
		if (i != tcp::resolver::iterator())
		{
			// country is an ISO 3166 country code
			int country = i->endpoint().address().to_v4().to_ulong() & 0xffff;
			
			// look up the country code in the map
			const int size = sizeof(country_map)/sizeof(country_map[0]);
			country_entry tmp = {country, ""};
			country_entry const* i =
				std::lower_bound(country_map, country_map + size, tmp
					, bind(&country_entry::code, _1) < bind(&country_entry::code, _2));
			if (i == country_map + size
				|| i->code != country)
			{
				// unknown country!
				p->set_country("!!");
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_ses.m_logger) << "IP " << p->remote().address() << " was mapped to unknown country: " << country << "\n";
#endif
				return;
			}
			
			p->set_country(i->name);
		}
	}
#endif

	void torrent::read_resume_data(entry const& rd)
	{
		entry const* e = 0;
		e = rd.find_key("total_uploaded");
		m_total_uploaded = (e != 0 && e->type() == entry::int_t)?e->integer():0;
		e = rd.find_key("total_downloaded");
		m_total_downloaded = (e != 0 && e->type() == entry::int_t)?e->integer():0;

		e = rd.find_key("active_time");
		m_active_time = seconds((e != 0 && e->type() == entry::int_t)?e->integer():0);
		e = rd.find_key("seeding_time");
		m_seeding_time = seconds((e != 0 && e->type() == entry::int_t)?e->integer():0);
	}
	
	void torrent::write_resume_data(entry& ret) const
	{
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;

		ret["total_uploaded"] = m_total_uploaded;
		ret["total_downloaded"] = m_total_downloaded;

		ret["active_time"] = total_seconds(m_active_time);
		ret["seeding_time"] = total_seconds(m_seeding_time);
		
		ret["allocation"] = m_storage_mode == storage_mode_sparse?"sparse"
			:m_storage_mode == storage_mode_allocate?"full":"compact";

		const sha1_hash& info_hash = torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

		// blocks per piece
		int num_blocks_per_piece =
			static_cast<int>(torrent_file().piece_length()) / block_size();
		ret["blocks per piece"] = num_blocks_per_piece;

		// if this torrent is a seed, we won't have a piece picker
		// and there will be no half-finished pieces.
		if (!is_seed())
		{
			const std::vector<piece_picker::downloading_piece>& q
				= m_picker->get_download_queue();

			// unfinished pieces
			ret["unfinished"] = entry::list_type();
			entry::list_type& up = ret["unfinished"].list();

			// info for each unfinished piece
			for (std::vector<piece_picker::downloading_piece>::const_iterator i
				= q.begin(); i != q.end(); ++i)
			{
				if (i->finished == 0) continue;

				entry piece_struct(entry::dictionary_t);

				// the unfinished piece's index
				piece_struct["piece"] = i->index;

				std::string bitmask;
				const int num_bitmask_bytes
					= (std::max)(num_blocks_per_piece / 8, 1);

				for (int j = 0; j < num_bitmask_bytes; ++j)
				{
					unsigned char v = 0;
					int bits = (std::min)(num_blocks_per_piece - j*8, 8);
					for (int k = 0; k < bits; ++k)
						v |= (i->info[j*8+k].state == piece_picker::block_info::state_finished)
						? (1 << k) : 0;
					bitmask.insert(bitmask.end(), v);
					TORRENT_ASSERT(bits == 8 || j == num_bitmask_bytes - 1);
				}
				piece_struct["bitmask"] = bitmask;
				// push the struct onto the unfinished-piece list
				up.push_back(piece_struct);
			}
		}

		// write have bitmask
		entry::string_type& pieces = ret["pieces"].string();
		pieces.resize(m_torrent_file->num_pieces());
		for (int i = 0, end(pieces.size()); i < end; ++i)
			pieces[i] = m_have_pieces[i] ? 1 : 0;

		// write local peers

		entry::list_type& peer_list = ret["peers"].list();
		entry::list_type& banned_peer_list = ret["banned_peers"].list();
		
		int max_failcount = m_ses.m_settings.max_failcount;

		for (policy::const_iterator i = m_policy.begin_peer()
			, end(m_policy.end_peer()); i != end; ++i)
		{
			error_code ec;
			if (i->second.banned)
			{
				tcp::endpoint ip = i->second.ip;
				entry peer(entry::dictionary_t);
				peer["ip"] = ip.address().to_string(ec);
				if (ec) continue;
				peer["port"] = ip.port();
				banned_peer_list.push_back(peer);
				continue;
			}
			// we cannot save remote connection
			// since we don't know their listen port
			// unless they gave us their listen port
			// through the extension handshake
			// so, if the peer is not connectable (i.e. we
			// don't know its listen port) or if it has
			// been banned, don't save it.
			if (i->second.type == policy::peer::not_connectable) continue;

			// don't save peers that doesn't work
			if (i->second.failcount >= max_failcount) continue;

			tcp::endpoint ip = i->second.ip;
			entry peer(entry::dictionary_t);
			peer["ip"] = ip.address().to_string(ec);
			if (ec) continue;
			peer["port"] = ip.port();
			peer_list.push_back(peer);
		}
	}

	void torrent::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		v.clear();
		v.reserve(m_policy.num_peers());
		for (policy::const_iterator i = m_policy.begin_peer();
			i != m_policy.end_peer(); ++i)
		{
			peer_list_entry e;
			e.ip = i->second.ip;
			e.flags = i->second.banned ? peer_list_entry::banned : 0;
			e.failcount = i->second.failcount;
			e.source = i->second.source;
			v.push_back(e);
		}
	}

	void torrent::get_peer_info(std::vector<peer_info>& v)
	{
		v.clear();
		for (peer_iterator i = begin();
			i != end(); ++i)
		{
			peer_connection* peer = *i;

			// incoming peers that haven't finished the handshake should
			// not be included in this list
			if (peer->associated_torrent().expired()) continue;

			v.push_back(peer_info());
			peer_info& p = v.back();
			
			peer->get_peer_info(p);
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
			if (resolving_countries())
				resolve_peer_country(intrusive_ptr<peer_connection>(peer));
#endif
		}
	}

	void torrent::get_download_queue(std::vector<partial_piece_info>& queue)
	{
		queue.clear();
		if (!valid_metadata() || is_seed()) return;
		piece_picker const& p = picker();
		std::vector<piece_picker::downloading_piece> const& q
			= p.get_download_queue();

		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin(); i != q.end(); ++i)
		{
			partial_piece_info pi;
			pi.piece_state = (partial_piece_info::state_t)i->state;
			pi.blocks_in_piece = p.blocks_in_piece(i->index);
			pi.finished = (int)i->finished;
			pi.writing = (int)i->writing;
			pi.requested = (int)i->requested;
			int piece_size = int(torrent_file().piece_size(i->index));
			for (int j = 0; j < pi.blocks_in_piece; ++j)
			{
				block_info& bi = pi.blocks[j];
				bi.state = i->info[j].state;
				bi.block_size = j < pi.blocks_in_piece - 1 ? m_block_size
					: piece_size - (j * m_block_size);
				bool complete = bi.state == block_info::writing
					|| bi.state == block_info::finished;
				if (i->info[j].peer == 0)
				{
					bi.peer = tcp::endpoint();
					bi.bytes_progress = complete ? bi.block_size : 0;
				}
				else
				{
					policy::peer* p = static_cast<policy::peer*>(i->info[j].peer);
					if (p->connection)
					{
						bi.peer = p->connection->remote();
						if (bi.state == block_info::requested)
						{
							boost::optional<piece_block_progress> pbp
								= p->connection->downloading_piece_progress();
							if (pbp && pbp->piece_index == i->index && pbp->block_index == j)
							{
								bi.bytes_progress = pbp->bytes_downloaded;
								TORRENT_ASSERT(bi.bytes_progress <= bi.block_size);
							}
							else
							{
								bi.bytes_progress = 0;
							}
						}
						else
						{
							bi.bytes_progress = complete ? bi.block_size : 0;
						}
					}
					else
					{
						bi.peer = p->ip;
						bi.bytes_progress = complete ? bi.block_size : 0;
					}
				}

				pi.blocks[j].num_peers = i->info[j].num_peers;
			}
			pi.piece_index = i->index;
			queue.push_back(pi);
		}
	
	}
	
	bool torrent::connect_to_peer(policy::peer* peerinfo)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(peerinfo);
		TORRENT_ASSERT(peerinfo->connection == 0);

		peerinfo->connected = time_now();
#ifndef NDEBUG
		// this asserts that we don't have duplicates in the policy's peer list
		peer_iterator i_ = std::find_if(m_connections.begin(), m_connections.end()
			, bind(&peer_connection::remote, _1) == peerinfo->ip);
		TORRENT_ASSERT(i_ == m_connections.end()
			|| dynamic_cast<bt_peer_connection*>(*i_) == 0);
#endif

		TORRENT_ASSERT(want_more_peers());
		TORRENT_ASSERT(m_ses.num_connections() < m_ses.max_connections());

		tcp::endpoint const& a(peerinfo->ip);
		TORRENT_ASSERT((m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked) == 0);

		boost::shared_ptr<socket_type> s(new socket_type(m_ses.m_io_service));

		bool ret = instantiate_connection(m_ses.m_io_service, m_ses.peer_proxy(), *s);
		TORRENT_ASSERT(ret);
		std::pair<int, int> const& out_ports = m_ses.settings().outgoing_ports;
		error_code ec;
		if (out_ports.first > 0 && out_ports.second >= out_ports.first)
			s->bind(tcp::endpoint(address(), m_ses.next_port()), ec);

		boost::intrusive_ptr<peer_connection> c(new bt_peer_connection(
			m_ses, shared_from_this(), s, a, peerinfo));

#ifndef NDEBUG
		c->m_in_constructor = false;
#endif

 		c->add_stat(peerinfo->prev_amount_download, peerinfo->prev_amount_upload);
 		peerinfo->prev_amount_download = 0;
 		peerinfo->prev_amount_upload = 0;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				boost::shared_ptr<peer_plugin> pp((*i)->new_connection(c.get()));
				if (pp) c->add_extension(pp);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

		// add the newly connected peer to this torrent's peer list
		m_connections.insert(boost::get_pointer(c));
		m_ses.m_connections.insert(c);
		peerinfo->connection = c.get();
		c->start();

		int timeout = settings().peer_connect_timeout;
		if (peerinfo) timeout += 3 * peerinfo->failcount;

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			m_ses.m_half_open.enqueue(
				bind(&peer_connection::connect, c, _1)
				, bind(&peer_connection::timed_out, c)
				, seconds(timeout));
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
			std::set<peer_connection*>::iterator i
				= m_connections.find(boost::get_pointer(c));
			if (i != m_connections.end()) m_connections.erase(i);
			c->disconnect(e.what());
			return false;
		}
#endif
		return true;
	}

	bool torrent::set_metadata(lazy_entry const& metadata, std::string& error)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_torrent_file->is_valid());
		if (!m_torrent_file->parse_info_section(metadata, error))
		{
			// parse failed
			return false;
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle(), "metadata successfully received from swarm"));
		}

		init();

		return true;
	}

	bool torrent::attach_peer(peer_connection* p)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(!p->is_local());

		if ((m_state == torrent_status::queued_for_checking
			|| m_state == torrent_status::checking_files)
			&& valid_metadata())
		{
			p->disconnect("torrent is not ready to accept peers");
			return false;
		}
		
		if (m_ses.m_connections.find(p) == m_ses.m_connections.end())
		{
			p->disconnect("peer is not properly constructed");
			return false;
		}

		if (m_ses.is_aborted())
		{
			p->disconnect("session is closing");
			return false;
		}

		if (int(m_connections.size()) >= m_max_connections)
		{
			p->disconnect("reached connection limit");
			return false;
		}

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				boost::shared_ptr<peer_plugin> pp((*i)->new_connection(p));
				if (pp) p->add_extension(pp);
			}
#endif
			if (!m_policy.new_connection(*p))
				return false;
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
#if defined TORRENT_LOGGING
			(*m_ses.m_logger) << time_now_string() << " CLOSING CONNECTION "
				<< p->remote() << " policy::new_connection threw: " << e.what() << "\n";
#endif
			p->disconnect(e.what());
			return false;
		}
#endif
		TORRENT_ASSERT(m_connections.find(p) == m_connections.end());
		peer_iterator ci = m_connections.insert(p).first;
#ifndef NDEBUG
		error_code ec;
		TORRENT_ASSERT(p->remote() == p->get_socket()->remote_endpoint(ec) || ec);
#endif

#if !defined NDEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		m_policy.check_invariant();
#endif
		return true;
	}

	bool torrent::want_more_peers() const
	{
		return int(m_connections.size()) < m_max_connections
			&& !m_paused
			&& m_state != torrent_status::checking_files
			&& m_state != torrent_status::queued_for_checking
			&& m_policy.num_connect_candidates() > 0;
	}

	void torrent::disconnect_all()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		while (!m_connections.empty())
		{
			peer_connection* p = *m_connections.begin();
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			if (m_abort)
				(*p->m_logger) << "*** CLOSING CONNECTION 'aborting'\n";
			else
				(*p->m_logger) << "*** CLOSING CONNECTION 'pausing'\n";
#endif
#ifndef NDEBUG
			std::size_t size = m_connections.size();
#endif
			if (p->is_disconnecting())
				m_connections.erase(m_connections.begin());
			else
				p->disconnect(m_abort?"stopping torrent":"pausing torrent");
			TORRENT_ASSERT(m_connections.size() <= size);
		}
	}

	namespace
	{
		// this returns true if lhs is a better disconnect candidate than rhs
		bool compare_disconnect_peer(peer_connection const* lhs, peer_connection const* rhs)
		{
			// prefer to disconnect peers we're not interested in
			if (lhs->is_interesting() != rhs->is_interesting())
				return rhs->is_interesting();

			// prefer to disconnect peers that are not seeds
			if (lhs->is_seed() != rhs->is_seed())
				return rhs->is_seed();

			// prefer to disconnect peers that are on parole
			if (lhs->on_parole() != rhs->on_parole())
				return lhs->on_parole();

			// prefer to disconnect peers that send data at a lower rate
			size_type lhs_transferred = lhs->statistics().total_payload_download();
			size_type rhs_transferred = rhs->statistics().total_payload_download();

			if (lhs_transferred != rhs_transferred
				&& lhs_transferred > 0
				&& rhs_transferred > 0)
			{
				ptime now = time_now();
				size_type lhs_time_connected = total_seconds(now - lhs->connected_time());
				size_type rhs_time_connected = total_seconds(now - rhs->connected_time());

				double lhs_rate = double(lhs_transferred) / (lhs_time_connected + 1);
				double rhs_rate = double(rhs_transferred) / (rhs_time_connected + 1);
			
				return lhs_rate < rhs_rate;
			}

			// prefer to disconnect peers that chokes us
			if (lhs->is_choked() != rhs->is_choked())
				return lhs->is_choked();

			return lhs->last_received() < rhs->last_received();
		}
	}

	int torrent::disconnect_peers(int num)
	{
		int ret = 0;
		// buils a list of all connected peers and sort it by 'disconnectability'.
		std::vector<peer_connection*> peers(m_connections.size());
		std::copy(m_connections.begin(), m_connections.end(), peers.begin());
		std::sort(peers.begin(), peers.end(), boost::bind(&compare_disconnect_peer, _1, _2));

		// never disconnect peers that connected less than 90 seconds ago
		ptime cut_off = time_now() - seconds(90);

		for (std::vector<peer_connection*>::iterator i = peers.begin()
			, end(peers.end()); i != end && ret < num; ++i)
		{
			peer_connection* p = *i;
			if (p->connected_time() > cut_off) continue;
			++ret;
			p->disconnect("optimistic disconnect");
		}
		return ret;
	}

	int torrent::bandwidth_throttle(int channel) const
	{
		return m_bandwidth_limit[channel].throttle();
	}

	int torrent::bandwidth_queue_size(int channel) const
	{
		return (int)m_bandwidth_queue[channel].size();
	}

	void torrent::request_bandwidth(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, int max_block_size, int priority)
	{
		TORRENT_ASSERT(max_block_size > 0);
		TORRENT_ASSERT(m_bandwidth_limit[channel].throttle() > 0);
		TORRENT_ASSERT(p->max_assignable_bandwidth(channel) > 0);
		TORRENT_ASSERT(p->m_channel_state[channel] == peer_info::bw_torrent);
		int block_size = (std::min)(m_bandwidth_limit[channel].throttle() / 10
			, max_block_size);
		if (block_size <= 0) block_size = 1;

		if (m_bandwidth_limit[channel].max_assignable() > 0)
		{
			perform_bandwidth_request(channel, p, block_size, priority);
		}
		else
		{
			// skip forward in the queue until we find a prioritized peer
			// or hit the front of it.
			queue_t::reverse_iterator i = m_bandwidth_queue[channel].rbegin();
			while (i != m_bandwidth_queue[channel].rend() && priority > i->priority)
			{
				++i->priority;
				++i;
			}
			m_bandwidth_queue[channel].insert(i.base(), bw_queue_entry<peer_connection, torrent>(
				p, block_size, priority));
		}
	}

	void torrent::expire_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		
		TORRENT_ASSERT(amount > 0);
		m_bandwidth_limit[channel].expire(amount);
		queue_t tmp;
		while (!m_bandwidth_queue[channel].empty())
		{
			bw_queue_entry<peer_connection, torrent> qe = m_bandwidth_queue[channel].front();
			if (m_bandwidth_limit[channel].max_assignable() == 0)
				break;
			m_bandwidth_queue[channel].pop_front();
			if (qe.peer->max_assignable_bandwidth(channel) <= 0)
			{
				TORRENT_ASSERT(m_ses.m_bandwidth_manager[channel]->is_in_history(qe.peer.get()));
				if (!qe.peer->is_disconnecting()) tmp.push_back(qe);
				continue;
			}
			perform_bandwidth_request(channel, qe.peer
				, qe.max_block_size, qe.priority);
		}
		m_bandwidth_queue[channel].insert(m_bandwidth_queue[channel].begin(), tmp.begin(), tmp.end());
	}

	void torrent::perform_bandwidth_request(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, int block_size
		, int priority)
	{
		TORRENT_ASSERT(p->m_channel_state[channel] == peer_info::bw_torrent);
		p->m_channel_state[channel] = peer_info::bw_global;
		m_ses.m_bandwidth_manager[channel]->request_bandwidth(p
			, block_size, priority);
		m_bandwidth_limit[channel].assign(block_size);
	}

	void torrent::assign_bandwidth(int channel, int amount, int blk)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		TORRENT_ASSERT(amount > 0);
		TORRENT_ASSERT(amount <= blk);
		if (amount < blk)
			expire_bandwidth(channel, blk - amount);
	}

	// called when torrent is finished (all interesting
	// pieces have been downloaded)
	void torrent::finished()
	{
		INVARIANT_CHECK;

		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()
				, "torrent has finished downloading"));
		}

		m_state = torrent_status::finished;

	// disconnect all seeds
	// TODO: should disconnect all peers that have the pieces we have
	// not just seeds
		std::vector<peer_connection*> seeds;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
			if (p->is_seed())
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*p->m_logger) << "*** SEED, CLOSING CONNECTION\n";
#endif
				seeds.push_back(p);
			}
		}
		std::for_each(seeds.begin(), seeds.end()
			, bind(&peer_connection::disconnect, _1, "torrent finished, disconnecting seed", 0));

		TORRENT_ASSERT(m_storage);
		// we need to keep the object alive during this operation
		m_storage->async_release_files(
			bind(&torrent::on_files_released, shared_from_this(), _1, _2));
	}
	
	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
		INVARIANT_CHECK;

		// make the next tracker request
		// be a completed-event
		m_event = tracker_request::completed;
		m_state = torrent_status::seeding;
		force_tracker_request();
	}

	// this will move the tracker with the given index
	// to a prioritized position in the list (move it towards
	// the begining) and return the new index to the tracker.
	int torrent::prioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		if (index >= (int)m_trackers.size()) return (int)m_trackers.size()-1;

		while (index > 0 && m_trackers[index].tier == m_trackers[index-1].tier)
		{
			std::swap(m_trackers[index].url, m_trackers[index-1].url);
			--index;
		}
		return index;
	}

	void torrent::try_next_tracker()
	{
		INVARIANT_CHECK;

		++m_currently_trying_tracker;

		if ((unsigned)m_currently_trying_tracker >= m_trackers.size())
		{
			int delay = tracker_retry_delay_min
				+ (std::min)(m_failed_trackers, (int)tracker_failed_max)
				* (tracker_retry_delay_max - tracker_retry_delay_min)
				/ tracker_failed_max;

			++m_failed_trackers;
			// if we've looped the tracker list, wait a bit before retrying
			m_currently_trying_tracker = 0;
			m_next_request = time_now() + seconds(delay);

#ifndef TORRENT_DISABLE_DHT
			if (m_abort) return;

			// only start the announce if we want to announce with the dht
			ptime now = time_now();
			if (should_announce_dht() && now - m_last_dht_announce > minutes(14))
			{
				// force the DHT to reannounce
				m_last_dht_announce = now;
				boost::weak_ptr<torrent> self(shared_from_this());
				m_ses.m_dht->announce(m_torrent_file->info_hash()
					, m_ses.m_listen_sockets.front().external_port
					, bind(&torrent::on_dht_announce_response_disp, self, _1));
			}
#endif

		}
		else
		{
			// don't delay before trying the next tracker
			m_next_request = time_now();
		}

	}

	void torrent::files_checked()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		TORRENT_ASSERT(m_torrent_file->is_valid());
		INVARIANT_CHECK;

		m_state = torrent_status::connecting_to_tracker;

		if (!is_seed())
		{
			m_picker->init(m_have_pieces);
			if (m_sequential_download)
				picker().sequential_download(m_sequential_download);
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(*i)->on_files_checked();
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

		if (is_seed())
		{
			m_state = torrent_status::seeding;
			m_picker.reset();
		}

		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			for (torrent::peer_iterator i = m_connections.begin()
				, end(m_connections.end()); i != end;)
			{
				boost::intrusive_ptr<peer_connection> pc = *i;
				++i;
#ifndef BOOST_NO_EXCEPTIONS
				try
				{
#endif
					pc->on_metadata();
					pc->init();
#ifndef BOOST_NO_EXCEPTIONS
				}
				catch (std::exception& e)
				{
					pc->disconnect(e.what());
				}
#endif
			}
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(torrent_checked_alert(
				get_handle()
				, "torrent finished checking"));
		}
		
#ifndef NDEBUG
		m_files_checked = true;
#endif
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	fs::path torrent::save_path() const
	{
		return m_save_path;
	}

	void torrent::move_storage(fs::path const& save_path)
	{
		INVARIANT_CHECK;

		if (m_owning_storage.get())
		{
			m_owning_storage->async_move_storage(save_path
				, bind(&torrent::on_storage_moved, shared_from_this(), _1, _2));
		}
		else
		{
			m_save_path = save_path;
		}
	}

	void torrent::on_storage_moved(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post(alert::warning))
		{
			alerts().post_alert(storage_moved_alert(get_handle(), j.str));
		}
	}

	piece_manager& torrent::filesystem()
	{
		TORRENT_ASSERT(m_owning_storage.get());
		return *m_owning_storage;
	}


	torrent_handle torrent::get_handle()
	{
		return torrent_handle(shared_from_this());
	}

	session_settings const& torrent::settings() const
	{
		return m_ses.settings();
	}

#ifndef NDEBUG
	void torrent::check_invariant() const
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		TORRENT_ASSERT(m_resume_data.type() == entry::dictionary_t
			|| m_resume_data.type() == entry::undefined_t);

		TORRENT_ASSERT(m_bandwidth_queue[0].size() <= m_connections.size());
		TORRENT_ASSERT(m_bandwidth_queue[1].size() <= m_connections.size());

		for (int c = 0; c < 2; ++c)
		{
			queue_t::const_iterator j = m_bandwidth_queue[c].begin();
			if (j == m_bandwidth_queue[c].end()) continue;
			++j;
			for (queue_t::const_iterator i = m_bandwidth_queue[c].begin()
				, end(m_bandwidth_queue[c].end()); i != end && j != end; ++i, ++j)
				TORRENT_ASSERT(i->priority >= j->priority);
		}

		int num_uploads = 0;
		std::map<piece_block, int> num_requests;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
			peer_connection const& p = *(*i);
			for (std::deque<piece_block>::const_iterator i = p.request_queue().begin()
				, end(p.request_queue().end()); i != end; ++i)
				++num_requests[*i];
			for (std::deque<piece_block>::const_iterator i = p.download_queue().begin()
				, end(p.download_queue().end()); i != end; ++i)
				++num_requests[*i];
			if (!p.is_choked()) ++num_uploads;
			torrent* associated_torrent = p.associated_torrent().lock().get();
			if (associated_torrent != this)
				TORRENT_ASSERT(false);
		}
		TORRENT_ASSERT(num_uploads == m_num_uploads);

		if (has_picker())
		{
			for (std::map<piece_block, int>::iterator i = num_requests.begin()
				, end(num_requests.end()); i != end; ++i)
			{
				if (!m_picker->is_downloaded(i->first))
					TORRENT_ASSERT(m_picker->num_peers(i->first) == i->second);
			}
			TORRENT_ASSERT(m_num_pieces >= m_picker->num_have_filtered());
		}

		if (valid_metadata())
		{
			TORRENT_ASSERT(m_abort || int(m_have_pieces.size()) == m_torrent_file->num_pieces());
		}
		else
		{
			TORRENT_ASSERT(m_abort || m_have_pieces.empty());
		}

		for (policy::const_iterator i = m_policy.begin_peer()
			, end(m_policy.end_peer()); i != end; ++i)
		{
			TORRENT_ASSERT(i->second.ip.address() == i->first);
		}

		size_type total_done = quantized_bytes_done();
		if (m_torrent_file->is_valid())
		{
			if (is_seed())
				TORRENT_ASSERT(total_done == m_torrent_file->total_size());
			else
				TORRENT_ASSERT(total_done != m_torrent_file->total_size());
		}
		else
		{
			TORRENT_ASSERT(total_done == 0);
		}

		if (m_picker && !m_abort)
		{
			// make sure that pieces that have completed the download
			// of all their blocks are in the disk io thread's queue
			// to be checked.
			const std::vector<piece_picker::downloading_piece>& dl_queue
				= m_picker->get_download_queue();
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				const int blocks_per_piece = m_picker->blocks_in_piece(i->index);

				bool complete = true;
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					if (i->info[j].state == piece_picker::block_info::state_finished)
						continue;
					complete = false;
					break;
				}
			}
		}
			
// This check is very expensive.
		TORRENT_ASSERT(m_num_pieces
			== std::count(m_have_pieces.begin(), m_have_pieces.end(), true));
		TORRENT_ASSERT(!valid_metadata() || m_block_size > 0);
		TORRENT_ASSERT(!valid_metadata() || (m_torrent_file->piece_length() % m_block_size) == 0);
//		if (is_seed()) TORRENT_ASSERT(m_picker.get() == 0);
	}
#endif

	void torrent::set_sequential_download(bool sd)
	{
		if (has_picker())
		{
			picker().sequential_download(sd);
		}
		else
		{
			m_sequential_download = sd;
		}
	}


	void torrent::set_max_uploads(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_uploads = limit;
	}

	void torrent::set_max_connections(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_connections = limit;
	}

	void torrent::set_peer_upload_limit(tcp::endpoint ip, int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		peer_iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, bind(&peer_connection::remote, _1) == ip);
		if (i == m_connections.end()) return;
		(*i)->set_upload_limit(limit);
	}

	void torrent::set_peer_download_limit(tcp::endpoint ip, int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		peer_iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, bind(&peer_connection::remote, _1) == ip);
		if (i == m_connections.end()) return;
		(*i)->set_download_limit(limit);
	}

	void torrent::set_upload_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_bandwidth_limit[peer_connection::upload_channel].throttle(limit);
	}

	int torrent::upload_limit() const
	{
		int limit = m_bandwidth_limit[peer_connection::upload_channel].throttle();
		if (limit == (std::numeric_limits<int>::max)()) limit = -1;
		return limit;
	}

	void torrent::set_download_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_bandwidth_limit[peer_connection::download_channel].throttle(limit);
	}

	int torrent::download_limit() const
	{
		int limit = m_bandwidth_limit[peer_connection::download_channel].throttle();
		if (limit == (std::numeric_limits<int>::max)()) limit = -1;
		return limit;
	}

	void torrent::delete_files()
	{
#if defined TORRENT_VERBOSE_LOGGING
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*(*i)->m_logger) << "*** DELETING FILES IN TORRENT\n";
		}
#endif

		disconnect_all();
		if (!m_paused)
			m_just_paused = true;
		m_paused = true;
		// tell the tracker that we stopped
		m_event = tracker_request::stopped;

		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_delete_files(
				bind(&torrent::on_files_deleted, shared_from_this(), _1, _2));
		}
	}

	void torrent::auto_managed(bool a)
	{
		INVARIANT_CHECK;

		if (m_auto_managed == a) return;
		m_auto_managed = a;
		// recalculate which torrents should be
		// paused
		m_ses.m_auto_manage_time_scaler = 0;
	}

	// the higher seed rank, the more important to seed
	int torrent::seed_rank(session_settings const& s) const
	{
		enum flags
		{
			seed_ratio_not_met = 0x400000,
			recently_started = 0x200000,
			no_seeds = 0x100000,
			prio_mask = 0xfffff
		};

		if (!is_seed()) return 0;

		int ret = 0;

		ptime now(time_now());

		int seed_time = total_seconds(m_seeding_time);
		int download_time = total_seconds(m_active_time) - seed_time;

		// if we haven't yet met the seed limits, set the seed_ratio_not_met
		// flag. That will make this seed prioritized
		size_type downloaded = (std::max)(m_total_downloaded, m_torrent_file->total_size());
		if (seed_time < s.seed_time_limit
			&& (seed_time > 1 && download_time / float(seed_time) < s.seed_time_ratio_limit)
			&& m_total_uploaded / downloaded < s.share_ratio_limit)
			ret |= seed_ratio_not_met;

		// if this torrent is running, and it was started less
		// than 30 minutes ago, give it priority, to avoid oscillation
		if (!is_paused() && now - m_started < minutes(30))
			ret |= recently_started;

		// if we have any scrape data, use it to calculate
		// seed rank
		int seeds = 0;
		int downloaders = 0;

		if (m_complete >= 0) seeds = m_complete;
		else seeds = m_policy.num_seeds();

		if (m_incomplete >= 0) downloaders = m_incomplete;
		else downloaders = m_policy.num_peers() - m_policy.num_seeds();

		if (seeds == 0)
		{
			ret |= no_seeds;
			ret |= downloaders & prio_mask;
		}
		else
		{
			ret |= (downloaders  * 100 / seeds) & prio_mask;
		}

		return ret;
	}

	// this is an async operation triggered by the client	
	void torrent::save_resume_data()
	{
		INVARIANT_CHECK;
	
		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_save_resume_data(
				bind(&torrent::on_save_resume_data, shared_from_this(), _1, _2));
		}
		else
		{
			if (alerts().should_post(alert::warning))
			{
				alerts().post_alert(save_resume_data_alert(boost::shared_ptr<entry>()
					, get_handle(), "save resume data failed, torrent is being destructed"));
			}
		}
	}
	
	void torrent::pause()
	{
		INVARIANT_CHECK;

		if (m_paused) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				if ((*i)->on_pause()) return;
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*(*i)->m_logger) << "*** PAUSING TORRENT\n";
		}
#endif

		disconnect_all();
		m_paused = true;
		// tell the tracker that we stopped
		m_event = tracker_request::stopped;
		m_just_paused = true;
		// this will make the storage close all
		// files and flush all cached data
		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_release_files(
				bind(&torrent::on_torrent_paused, shared_from_this(), _1, _2));
		}
		else
		{
			if (alerts().should_post(alert::warning))
			{
				alerts().post_alert(torrent_paused_alert(get_handle(), "torrent paused"));
			}
		}
	}

	void torrent::resume()
	{
		INVARIANT_CHECK;

		if (!m_paused) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				if ((*i)->on_resume()) return;
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

		m_paused = false;
		m_started = time_now();

		// tell the tracker that we're back
		m_event = tracker_request::started;
		force_tracker_request();

		// make pulse be called as soon as possible
		m_time_scaler = 0;
	}

	void torrent::second_tick(stat& accumulator, float tick_interval)
	{
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(*i)->tick();
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}
#endif

		if (m_paused)
		{
			// let the stats fade out to 0
 			m_stat.second_tick(tick_interval);
			return;
		}

		time_duration since_last_tick = microsec(tick_interval * 1000000L);
		if (is_seed()) m_seeding_time += since_last_tick;
		m_active_time += since_last_tick;

		// ---- WEB SEEDS ----

		// re-insert urls that are to be retries into the m_web_seeds
		typedef std::map<std::string, ptime>::iterator iter_t;
		for (iter_t i = m_web_seeds_next_retry.begin(); i != m_web_seeds_next_retry.end();)
		{
			iter_t erase_element = i++;
			if (erase_element->second <= time_now())
			{
				m_web_seeds.insert(erase_element->first);
				m_web_seeds_next_retry.erase(erase_element);
			}
		}

		// if we have everything we want we don't need to connect to any web-seed
		if (!is_finished() && !m_web_seeds.empty())
		{
			// keep trying web-seeds if there are any
			// first find out which web seeds we are connected to
			std::set<std::string> web_seeds;
			for (peer_iterator i = m_connections.begin();
				i != m_connections.end(); ++i)
			{
				web_peer_connection* p
					= dynamic_cast<web_peer_connection*>(*i);
				if (!p) continue;
				web_seeds.insert(p->url());
			}

			for (std::set<std::string>::iterator i = m_resolving_web_seeds.begin()
				, end(m_resolving_web_seeds.end()); i != end; ++i)
				web_seeds.insert(web_seeds.begin(), *i);

			// from the list of available web seeds, subtract the ones we are
			// already connected to.
			std::vector<std::string> not_connected_web_seeds;
			std::set_difference(m_web_seeds.begin(), m_web_seeds.end(), web_seeds.begin()
				, web_seeds.end(), std::back_inserter(not_connected_web_seeds));

			// connect to all of those that we aren't connected to
			std::for_each(not_connected_web_seeds.begin(), not_connected_web_seeds.end()
				, bind(&torrent::connect_to_url_seed, this, _1));
		}
		
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* p = *i;
			++i;
			p->calc_ip_overhead();
			m_stat += p->statistics();
			// updates the peer connection's ul/dl bandwidth
			// resource requests
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				p->second_tick(tick_interval);
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*p->m_logger) << "**ERROR**: " << e.what() << "\n";
#endif
				p->disconnect(e.what(), 1);
			}
#endif
		}
		accumulator += m_stat;
		m_total_uploaded += m_stat.last_payload_uploaded();
		m_total_downloaded += m_stat.last_payload_downloaded();
		m_stat.second_tick(tick_interval);

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;
			m_policy.pulse();
		}
	}

	void torrent::retry_url_seed(std::string const& url)
	{
		m_web_seeds_next_retry[url] = time_now()
			+ seconds(m_ses.settings().urlseed_wait_retry);
	}

	bool torrent::try_connect_peer()
	{
		TORRENT_ASSERT(want_more_peers());
		if (m_deficit_counter < 100) return false;
		m_deficit_counter -= 100;
		bool ret = m_policy.connect_one_peer();
		return ret;
	}

	void torrent::give_connect_points(int points)
	{
		TORRENT_ASSERT(points <= 100);
		TORRENT_ASSERT(points > 0);
		TORRENT_ASSERT(want_more_peers());
		m_deficit_counter += points;
	}

	void torrent::async_verify_piece(int piece_index, boost::function<void(int)> const& f)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(m_storage);
		TORRENT_ASSERT(m_storage->refcount() > 0);
		TORRENT_ASSERT(piece_index >= 0);
		TORRENT_ASSERT(piece_index < m_torrent_file->num_pieces());
		TORRENT_ASSERT(piece_index < (int)m_have_pieces.size());
#ifndef NDEBUG
		if (m_picker)
		{
			int blocks_in_piece = m_picker->blocks_in_piece(piece_index);
			for (int i = 0; i < blocks_in_piece; ++i)
			{
				TORRENT_ASSERT(m_picker->num_peers(piece_block(piece_index, i)) == 0);
			}
		}
#endif

		m_storage->async_hash(piece_index, bind(&torrent::on_piece_verified
			, shared_from_this(), _1, _2, f));
#if !defined NDEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		check_invariant();
#endif
	}

	void torrent::on_piece_verified(int ret, disk_io_job const& j
		, boost::function<void(int)> f)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		// return value:
		// 0: success, piece passed hash check
		// -1: disk failure
		// -2: hash check failed

		if (ret == -1)
		{
			if (alerts().should_post(alert::fatal))
			{
				alerts().post_alert(file_error_alert(j.error_file, get_handle(), j.str));
			}
			pause();
		}
		f(ret);
	}

	const tcp::endpoint& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	void torrent::file_progress(std::vector<float>& fp) const
	{
		TORRENT_ASSERT(valid_metadata());
	
		fp.clear();
		fp.resize(m_torrent_file->num_files(), 0.f);
		
		for (int i = 0; i < m_torrent_file->num_files(); ++i)
		{
			peer_request ret = m_torrent_file->map_file(i, 0, 0);
			size_type size = m_torrent_file->file_at(i).size;

// zero sized files are considered
// 100% done all the time
			if (size == 0)
			{
				fp[i] = 1.f;
				continue;
			}

			size_type done = 0;
			while (size > 0)
			{
				size_type bytes_step = (std::min)(size_type(m_torrent_file->piece_size(ret.piece)
					- ret.start), size);
				if (m_have_pieces[ret.piece]) done += bytes_step;
				++ret.piece;
				ret.start = 0;
				size -= bytes_step;
			}
			TORRENT_ASSERT(size == 0);

			fp[i] = static_cast<float>(done) / m_torrent_file->file_at(i).size;
		}
	}
	
	torrent_status torrent::status() const
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

		st.up_bandwidth_queue = (int)m_bandwidth_queue[peer_connection::upload_channel].size();
		st.down_bandwidth_queue = (int)m_bandwidth_queue[peer_connection::download_channel].size();

		st.num_peers = (int)std::count_if(m_connections.begin(), m_connections.end()
			, !boost::bind(&peer_connection::is_connecting, _1));

		st.list_peers = m_policy.num_peers();
		st.list_seeds = m_policy.num_seeds();
		st.connect_candidates = m_policy.num_connect_candidates();
		st.seed_rank = seed_rank(m_ses.m_settings);

		st.all_time_upload = m_total_uploaded;
		st.all_time_download = m_total_downloaded;

		st.active_time = total_seconds(m_active_time);
		st.seeding_time = total_seconds(m_seeding_time);

		st.storage_mode = m_storage_mode;

		st.num_complete = m_complete;
		st.num_incomplete = m_incomplete;
		st.paused = m_paused;
		boost::tie(st.total_done, st.total_wanted_done) = bytes_done();
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		// payload transfer
		st.total_payload_download = m_stat.total_payload_download();
		st.total_payload_upload = m_stat.total_payload_upload();

		// total transfer
		st.total_download = m_stat.total_payload_download()
			+ m_stat.total_protocol_download();
		st.total_upload = m_stat.total_payload_upload()
			+ m_stat.total_protocol_upload();

		// failed bytes
		st.total_failed_bytes = m_total_failed_bytes;
		st.total_redundant_bytes = m_total_redundant_bytes;

		// transfer rate
		st.download_rate = m_stat.download_rate();
		st.upload_rate = m_stat.upload_rate();
		st.download_payload_rate = m_stat.download_payload_rate();
		st.upload_payload_rate = m_stat.upload_payload_rate();

		st.next_announce = boost::posix_time::seconds(
			total_seconds(next_announce() - time_now()));
		if (st.next_announce.is_negative())
			st.next_announce = boost::posix_time::seconds(0);

		st.announce_interval = boost::posix_time::seconds(m_duration);

		if (m_last_working_tracker >= 0)
		{
			st.current_tracker
				= m_trackers[m_last_working_tracker].url;
		}

		st.num_uploads = m_num_uploads;
		st.uploads_limit = m_max_uploads;
		st.num_connections = int(m_connections.size());
		st.connections_limit = m_max_connections;
		// if we don't have any metadata, stop here

		st.state = m_state;

		if (!valid_metadata())
		{
			if (m_got_tracker_response == false)
				st.state = torrent_status::connecting_to_tracker;
			else
				st.state = torrent_status::downloading_metadata;

			st.progress = m_progress;
			st.block_size = 0;
			return st;
		}

		st.block_size = block_size();

		// fill in status that depends on metadata

		st.total_wanted = m_torrent_file->total_size();
		TORRENT_ASSERT(st.total_wanted >= 0);

		if (m_picker.get() && (m_picker->num_filtered() > 0
			|| m_picker->num_have_filtered() > 0))
		{
			int filtered_pieces = m_picker->num_filtered()
				+ m_picker->num_have_filtered();
			int last_piece_index = m_torrent_file->num_pieces() - 1;
			if (m_picker->piece_priority(last_piece_index) == 0)
			{
				st.total_wanted -= m_torrent_file->piece_size(last_piece_index);
				--filtered_pieces;
			}
			
			st.total_wanted -= filtered_pieces * m_torrent_file->piece_length();
		}

		TORRENT_ASSERT(st.total_wanted >= st.total_wanted_done);

		if (m_state == torrent_status::checking_files)
			st.progress = m_progress;
		else if (st.total_wanted == 0) st.progress = 1.f;
		else st.progress = st.total_wanted_done
			/ static_cast<float>(st.total_wanted);

		st.pieces = &m_have_pieces;
		st.num_pieces = m_num_pieces;
		st.num_seeds = num_seeds();
		if (m_picker.get())
			st.distributed_copies = m_picker->distributed_copies();
		else
			st.distributed_copies = -1;
		return st;
	}

	int torrent::num_seeds() const
	{
		INVARIANT_CHECK;

		return (int)std::count_if(m_connections.begin(), m_connections.end()
			, boost::bind(&peer_connection::is_seed, _1));
	}

	void torrent::tracker_request_timed_out(
		tracker_request const& r)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		debug_log("*** tracker timed out");
#endif

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			if (r.kind == tracker_request::announce_request)
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, m_failed_trackers + 1, 0, r.url, "tracker timed out"));
			}
			else if (r.kind == tracker_request::scrape_request)
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle(), r.url, "tracker timed out"));
			}
		}

		if (r.kind == tracker_request::announce_request)
			try_next_tracker();
	}

	// TODO: with some response codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(tracker_request const& r
		, int response_code, const std::string& str)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		debug_log(std::string("*** tracker error: ") + str);
#endif
		if (m_ses.m_alerts.should_post(alert::warning))
		{
			if (r.kind == tracker_request::announce_request)
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, m_failed_trackers + 1, response_code, r.url, str));
			}
			else if (r.kind == tracker_request::scrape_request)
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle(), r.url, str));
			}
		}

		if (r.kind == tracker_request::announce_request)
			try_next_tracker();
	}


#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << time_now_string() << " " << line << "\n";
	}
#endif

}

