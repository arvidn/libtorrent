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
#include "libtorrent/parse_url.hpp"
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
#include "libtorrent/broadcast_socket.hpp"

using namespace libtorrent;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;
using boost::bind;
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
		, std::vector<char>* resume_data
		, int seq
		, bool auto_managed)
		: m_policy(this)
		, m_active_time(seconds(0))
		, m_seeding_time(seconds(0))
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_started(time_now())
		, m_last_scrape(min_time())
		, m_torrent_file(tf)
		, m_storage(0)
		, m_next_tracker_announce(time_now())
		, m_host_resolver(ses.m_io_service)
		, m_lsd_announce_timer(ses.m_io_service)
		, m_tracker_timer(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_last_dht_announce(time_now() - minutes(15))
#endif
		, m_ses(ses)
		, m_picker(new piece_picker())
		, m_trackers(m_torrent_file->trackers())
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_storage_mode(storage_mode)
		, m_state(torrent_status::checking_resume_data)
		, m_settings(ses.settings())
		, m_storage_constructor(sc)
		, m_progress(0.f)
		, m_ratio(0.f)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
		, m_block_size((std::min)(block_size, tf->piece_length()))
		, m_complete(-1)
		, m_incomplete(-1)
		, m_deficit_counter(0)
		, m_duration(1800)
		, m_sequence_number(seq)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_abort(false)
		, m_paused(paused)
		, m_auto_managed(auto_managed)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_connections_initialized(true)
		, m_has_incoming(false)
		, m_files_checked(false)
		, m_queued_for_checking(false)
		, m_announcing(false)
		, m_start_sent(false)
		, m_complete_sent(false)
	{
		if (resume_data) m_resume_data.swap(*resume_data);

#ifndef TORRENT_DISABLE_ENCRYPTION
		hasher h;
		h.update("req2", 4);
		h.update((char*)&tf->info_hash()[0], 20);
		m_obfuscated_hash = h.final();
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
		, std::vector<char>* resume_data
		, int seq
		, bool auto_managed)
		: m_policy(this)
		, m_active_time(seconds(0))
		, m_seeding_time(seconds(0))
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_started(time_now())
		, m_last_scrape(min_time())
		, m_torrent_file(new torrent_info(info_hash))
		, m_storage(0)
		, m_next_tracker_announce(time_now())
		, m_host_resolver(ses.m_io_service)
		, m_lsd_announce_timer(ses.m_io_service)
		, m_tracker_timer(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_last_dht_announce(time_now() - minutes(15))
#endif
		, m_ses(ses)
		, m_picker(new piece_picker())
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_storage_mode(storage_mode)
		, m_state(torrent_status::checking_resume_data)
		, m_settings(ses.settings())
		, m_storage_constructor(sc)
		, m_progress(0.f)
		, m_ratio(0.f)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
		, m_block_size(block_size)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_deficit_counter(0)
		, m_duration(1800)
		, m_sequence_number(seq)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_abort(false)
		, m_paused(paused)
		, m_auto_managed(auto_managed)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_connections_initialized(false)
		, m_has_incoming(false)
		, m_files_checked(false)
		, m_queued_for_checking(false)
		, m_announcing(false)
		, m_start_sent(false)
		, m_complete_sent(false)
	{
		if (resume_data) m_resume_data.swap(*resume_data);

#ifndef TORRENT_DISABLE_ENCRYPTION
		hasher h;
		h.update("req2", 4);
		h.update((char*)&info_hash[0], 20);
		m_obfuscated_hash = h.final();
#endif

#ifdef TORRENT_DEBUG
		m_files_checked = false;
#endif
		INVARIANT_CHECK;

		if (name) m_name.reset(new std::string(name));

		if (tracker_url && std::strlen(tracker_url) > 0)
		{
			m_trackers.push_back(announce_entry(tracker_url));
			m_torrent_file->add_tracker(tracker_url);
		}
	}

	void torrent::start()
	{
		if (!m_resume_data.empty())
		{
			if (lazy_bdecode(&m_resume_data[0], &m_resume_data[0]
				+ m_resume_data.size(), m_resume_entry) != 0)
			{
				std::vector<char>().swap(m_resume_data);
				if (m_ses.m_alerts.should_post<fastresume_rejected_alert>())
				{
					m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle(), "parse failed"));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
					(*m_ses.m_logger) << "fastresume data for "
						<< torrent_file().name() << " rejected: parse failed\n";
#endif
				}
			}
		}

		// we need to start announcing since we don't have any
		// metadata. To receive peers to ask for it.
		if (m_torrent_file->is_valid()) init();
		else
		{
			set_state(torrent_status::downloading_metadata);
			if (!m_trackers.empty()) start_announcing();
		}

		if (m_abort) return;
	}

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		if (m_ses.m_listen_sockets.empty()) return false;

		if (!m_ses.m_dht) return false;
		if (m_torrent_file->is_valid() && !m_files_checked) return false;

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

		m_file_priority.clear();
		m_file_priority.resize(m_torrent_file->num_files(), 1);

		m_block_size = (std::min)(m_block_size, m_torrent_file->piece_length());

		if (m_torrent_file->num_pieces()
			> piece_picker::max_pieces)
		{
			set_error("too many pieces in torrent");
			pause();
		}

		// the shared_from_this() will create an intentional
		// cycle of ownership, se the hpp file for description.
		m_owning_storage = new piece_manager(shared_from_this(), m_torrent_file
			, m_save_path, m_ses.m_files, m_ses.m_disk_thread, m_storage_constructor
			, m_storage_mode);
		m_storage = m_owning_storage.get();
		m_picker->init((std::max)(m_torrent_file->piece_length() / m_block_size, 1)
			, int((m_torrent_file->total_size()+m_block_size-1)/m_block_size));

		std::vector<std::string> const& url_seeds = m_torrent_file->url_seeds();
		std::copy(url_seeds.begin(), url_seeds.end(), std::inserter(m_web_seeds
			, m_web_seeds.begin()));

		set_state(torrent_status::checking_resume_data);

		if (m_resume_entry.type() == lazy_entry::dict_t)
		{
			char const* error = 0;
			if (m_resume_entry.dict_find_string_value("file-format") != "libtorrent resume file")
				error = "invalid file format tag";
	
			std::string info_hash = m_resume_entry.dict_find_string_value("info-hash");
			if (!error && info_hash.empty())
				error = "missing info-hash";

			if (!error && sha1_hash(info_hash) != m_torrent_file->info_hash())
				error = "mismatching info-hash";

			if (error && m_ses.m_alerts.should_post<fastresume_rejected_alert>())
			{
				m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle(), error));
			}

			if (error)
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_ses.m_logger) << "fastresume data for "
					<< torrent_file().name() << " rejected: "
					<< error << "\n";
#endif
				std::vector<char>().swap(m_resume_data);
				lazy_entry().swap(m_resume_entry);
			}
			else
			{
				read_resume_data(m_resume_entry);
			}
		}
	
		m_storage->async_check_fastresume(&m_resume_entry
			, bind(&torrent::on_resume_data_checked
			, shared_from_this(), _1, _2));
	}

	void torrent::on_resume_data_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post<file_error_alert>())
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.str));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
				" error: " << j.str <<
				" torrent: " << torrent_file().name() <<
				" ]\n";
#endif
			set_error(j.str);
			pause();

			std::vector<char>().swap(m_resume_data);
			lazy_entry().swap(m_resume_entry);

			return;
		}

		if (m_resume_entry.type() == lazy_entry::dict_t)
		{
			// parse out "peers" from the resume data and add them to the peer list
			if (lazy_entry const* peers_entry = m_resume_entry.dict_find_list("peers"))
			{
				peer_id id(0);

				for (int i = 0; i < peers_entry->list_size(); ++i)
				{
					lazy_entry const* e = peers_entry->list_at(i);
					if (e->type() != lazy_entry::dict_t) continue;
					std::string ip = e->dict_find_string_value("ip");
					int port = e->dict_find_int_value("port");
					if (ip.empty() || port == 0) continue;
					error_code ec;
					tcp::endpoint a(address::from_string(ip, ec), (unsigned short)port);
					if (ec) continue;
					m_policy.peer_from_tracker(a, id, peer_info::resume_data, 0);
				}
			}

			// parse out "banned_peers" and add them as banned
			if (lazy_entry const* banned_peers_entry = m_resume_entry.dict_find_list("banned_peers"))
			{
				peer_id id(0);
	
				for (int i = 0; i < banned_peers_entry->list_size(); ++i)
				{
					lazy_entry const* e = banned_peers_entry->list_at(i);
					if (e->type() != lazy_entry::dict_t) continue;
					std::string ip = e->dict_find_string_value("ip");
					int port = e->dict_find_int_value("port");
					if (ip.empty() || port == 0) continue;
					error_code ec;
					tcp::endpoint a(address::from_string(ip, ec), (unsigned short)port);
					if (ec) continue;
					policy::peer* p = m_policy.peer_from_tracker(a, id, peer_info::resume_data, 0);
					if (p) p->banned = true;
				}
			}
		}

		bool fastresume_rejected = !j.str.empty();
		
		if (fastresume_rejected && m_ses.m_alerts.should_post<fastresume_rejected_alert>())
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

			if (!fastresume_rejected && m_resume_entry.type() == lazy_entry::dict_t)
			{
				// parse have bitmask
				lazy_entry const* pieces = m_resume_entry.dict_find("pieces");
				if (pieces && pieces->type() == lazy_entry::string_t
					&& int(pieces->string_length()) == m_torrent_file->num_pieces())
				{
					char const* pieces_str = pieces->string_ptr();
					for (int i = 0, end(pieces->string_length()); i < end; ++i)
					{
						if ((pieces_str[i] & 1) == 0) continue;
						m_picker->we_have(i);
					}
				}
				else
				{
					lazy_entry const* slots = m_resume_entry.dict_find("slots");
					if (slots && slots->type() == lazy_entry::list_t)
					{
						for (int i = 0; i < slots->list_size(); ++i)
						{
							int piece = slots->list_int_value_at(i, -1);
							if (piece >= 0) m_picker->we_have(piece);
						}
					}
				}

				// parse unfinished pieces
				int num_blocks_per_piece =
					static_cast<int>(torrent_file().piece_length()) / block_size();

				if (lazy_entry const* unfinished_ent = m_resume_entry.dict_find_list("unfinished"))
				{
					for (int i = 0; i < unfinished_ent->list_size(); ++i)
					{
						lazy_entry const* e = unfinished_ent->list_at(i);
						if (e->type() != lazy_entry::dict_t) continue;
						int piece = e->dict_find_int_value("piece", -1);
						if (piece < 0 || piece > torrent_file().num_pieces()) continue;

						if (m_picker->have_piece(piece))
							m_picker->we_dont_have(piece);

						std::string bitmask = e->dict_find_string_value("bitmask");
						if (bitmask.empty()) continue;

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
									m_picker->mark_as_finished(piece_block(piece, bit), 0);
									if (m_picker->is_piece_finished(piece))
										async_verify_piece(piece, bind(&torrent::piece_finished
											, shared_from_this(), piece, _1));
								}
							}
						}
					}
				}
			}

			files_checked();
		}
		else
		{
			// either the fastresume data was rejected or there are
			// some files
			set_state(torrent_status::queued_for_checking);
			if (should_check_files())
				queue_torrent_check();
		}

		std::vector<char>().swap(m_resume_data);
		lazy_entry().swap(m_resume_entry);
	}

	void torrent::queue_torrent_check()
	{
		if (m_queued_for_checking) return;
		m_queued_for_checking = true;
		m_ses.check_torrent(shared_from_this());
	}

	void torrent::dequeue_torrent_check()
	{
		if (!m_queued_for_checking) return;
		m_queued_for_checking = false;
		m_ses.done_checking(shared_from_this());
	}

	void torrent::force_recheck()
	{
		// if the torrent is already queued to check its files
		// don't do anything
		if (should_check_files()
			|| m_state == torrent_status::checking_resume_data)
			return;

		disconnect_all();

		m_owning_storage->async_release_files();
		if (!m_picker) m_picker.reset(new piece_picker());
		m_picker->init(m_torrent_file->piece_length() / m_block_size
			, int((m_torrent_file->total_size()+m_block_size-1)/m_block_size));
		// assume that we don't have anything
		m_files_checked = false;
		set_state(torrent_status::checking_resume_data);

		m_policy.recalculate_connect_candidates();

		if (m_auto_managed)
			set_queue_position((std::numeric_limits<int>::max)());

		std::vector<char>().swap(m_resume_data);
		lazy_entry().swap(m_resume_entry);
		m_storage->async_check_fastresume(&m_resume_entry
			, bind(&torrent::on_force_recheck
			, shared_from_this(), _1, _2));
	}

	void torrent::on_force_recheck(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post<file_error_alert>())
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.str));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
				" error: " << j.str <<
				" torrent: " << torrent_file().name() <<
				" ]\n";
#endif
			set_error(j.str);
			pause();
			return;
		}
		if (ret == 0)
		{
			// if there are no files, just start
			files_checked();
		}
		else
		{
			set_state(torrent_status::queued_for_checking);
			if (should_check_files())
				queue_torrent_check();
		}
	}

	void torrent::start_checking()
	{
		TORRENT_ASSERT(should_check_files());
		set_state(torrent_status::checking_files);

		m_storage->async_check_files(bind(
			&torrent::on_piece_checked
			, shared_from_this(), _1, _2));
	}
	
	void torrent::on_piece_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		INVARIANT_CHECK;

		if (ret == piece_manager::disk_check_aborted)
		{
			pause();
			return;
		}
		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post<file_error_alert>())
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.str));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
				" error: " << j.str <<
				" torrent: " << torrent_file().name() <<
				" ]\n";
#endif
			set_error(j.str);
			pause();
			return;
		}

		m_progress = j.piece / float(torrent_file().num_pieces());

		TORRENT_ASSERT(m_picker);
		if (j.offset >= 0 && !m_picker->have_piece(j.offset))
			m_picker->we_have(j.offset);

		// we're not done checking yet
		// this handler will be called repeatedly until
		// we're done, or encounter a failure
		if (ret == piece_manager::need_full_check) return;

		dequeue_torrent_check();
		files_checked();
	}

	void torrent::use_interface(const char* net_interface)
	{
		INVARIANT_CHECK;

		error_code ec;
		address a(address::from_string(net_interface, ec));
		if (ec) return;
		m_net_interface = tcp::endpoint(a, 0);
	}

	void torrent::on_tracker_announce_disp(boost::weak_ptr<torrent> p
		, error_code const& e)
	{
		if (e) return;
		boost::shared_ptr<torrent> t = p.lock();
		if (!t) return;
		t->on_tracker_announce();
	}

	void torrent::on_tracker_announce()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
	
		if (m_abort) return;
		announce_with_tracker();
	}

	void torrent::on_lsd_announce_disp(boost::weak_ptr<torrent> p
		, error_code const& e)
	{
		if (e) return;
		boost::shared_ptr<torrent> t = p.lock();
		if (!t) return;
		t->on_lsd_announce();
	}

	void torrent::on_lsd_announce()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (m_abort) return;

		TORRENT_ASSERT(!m_torrent_file->priv());
		if (m_torrent_file->is_valid() && m_torrent_file->priv())
			return;

		if (is_paused()) return;

		boost::weak_ptr<torrent> self(shared_from_this());

		error_code ec;

		// announce on local network every 5 minutes
		m_lsd_announce_timer.expires_from_now(minutes(5), ec);
		m_lsd_announce_timer.async_wait(
			bind(&torrent::on_lsd_announce_disp, self, _1));

		// announce with the local discovery service
		m_ses.announce_lsd(m_torrent_file->info_hash());

#ifndef TORRENT_DISABLE_DHT
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

		if (m_ses.m_alerts.should_post<dht_reply_alert>())
		{
			m_ses.m_alerts.post_alert(dht_reply_alert(
				get_handle(), peers.size()));
		}
		std::for_each(peers.begin(), peers.end(), bind(
			&policy::peer_from_tracker, boost::ref(m_policy), _1, peer_id(0)
			, peer_info::dht, 0));
	}

#endif

	void torrent::announce_with_tracker(tracker_request::event_t e)
	{
		INVARIANT_CHECK;

		if (m_trackers.empty()) return;

		if (m_currently_trying_tracker < 0) m_currently_trying_tracker = 0;

		restart_tracker_timer(time_now() + seconds(tracker_retry_delay_max));

		if (m_abort) e = tracker_request::stopped;

		if (e == tracker_request::none)
		{
			if (!m_start_sent) e = tracker_request::started;
			if (!m_complete_sent && is_seed()) e = tracker_request::completed;
		}

		tracker_request req;
		req.info_hash = m_torrent_file->info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;
		req.event = e;
		error_code ec;
		tcp::endpoint ep;
		ep = m_ses.get_ipv6_interface();
		if (ep != tcp::endpoint()) req.ipv6 = ep.address().to_string(ec);
		ep = m_ses.get_ipv4_interface();
		if (ep != tcp::endpoint()) req.ipv4 = ep.address().to_string(ec);

		req.url = m_trackers[m_currently_trying_tracker].url;
		// if we are aborting. we don't want any new peers
		req.num_want = (req.event == tracker_request::stopped)
			?0:m_settings.num_want;

		req.listen_port = m_ses.m_listen_sockets.empty()
			?0:m_ses.m_listen_sockets.front().external_port;
		req.key = m_ses.m_key;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (m_abort)
		{
			boost::shared_ptr<aux::tracker_logger> tl(new aux::tracker_logger(m_ses));
			m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
				, tracker_login(), m_ses.m_listen_interface.address(), tl);
		}
		else
#endif
		m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
			, tracker_login(), m_ses.m_listen_interface.address()
			, m_abort?boost::shared_ptr<torrent>():shared_from_this());

		if (m_ses.m_alerts.should_post<tracker_announce_alert>())
		{
			m_ses.m_alerts.post_alert(
				tracker_announce_alert(get_handle(), req.url, req.event));
		}
	}

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

		m_last_scrape = time_now();
	}

	void torrent::tracker_warning(tracker_request const& req, std::string const& msg)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_ses.m_alerts.should_post<tracker_warning_alert>())
			m_ses.m_alerts.post_alert(tracker_warning_alert(get_handle(), req.url, msg));
	}
	
 	void torrent::tracker_scrape_response(tracker_request const& req
 		, int complete, int incomplete, int downloaded)
 	{
 		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
 
 		INVARIANT_CHECK;
		TORRENT_ASSERT(req.kind == tracker_request::scrape_request);
 
 		if (complete >= 0) m_complete = complete;
 		if (incomplete >= 0) m_incomplete = incomplete;
 
 		if (m_ses.m_alerts.should_post<scrape_reply_alert>())
 		{
 			m_ses.m_alerts.post_alert(scrape_reply_alert(
 				get_handle(), m_incomplete, m_complete, req.url));
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

		if (!m_start_sent && r.event == tracker_request::started)
			m_start_sent = true;
		if (!m_complete_sent && r.event == tracker_request::completed)
			m_complete_sent = true;

		m_failed_trackers = 0;

		if (interval < m_ses.settings().min_announce_interval)
			interval = m_ses.settings().min_announce_interval;

		m_last_working_tracker
			= prioritize_tracker(m_currently_trying_tracker);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		restart_tracker_timer(time_now() + seconds(m_duration));

		if (complete >= 0) m_complete = complete;
		if (incomplete >= 0) m_incomplete = incomplete;
		if (complete >= 0 && incomplete >= 0)
			m_last_scrape = time_now();

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

				tcp::resolver::query q(i->ip, to_string(i->port).elems);
				m_host_resolver.async_resolve(q,
					bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i->pid));
			}
			else
			{
				m_policy.peer_from_tracker(a, i->pid, peer_info::tracker, 0);
			}
		}

		if (m_ses.m_alerts.should_post<tracker_reply_alert>())
		{
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peer_list.size(), r.url));
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
			error_code ec;
			debug_log("blocked ip from tracker: " + host->endpoint().address().to_string(ec));
#endif
			if (m_ses.m_alerts.should_post<peer_blocked_alert>())
			{
				m_ses.m_alerts.post_alert(peer_blocked_alert(host->endpoint().address()));
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
			= size_type(num_have()) * m_torrent_file->piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_picker->have_piece(last_piece))
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

		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		size_type wanted_done = size_type(num_have() - m_picker->num_have_filtered())
			* piece_size;
		TORRENT_ASSERT(wanted_done >= 0);
		
		size_type total_done
			= size_type(num_have()) * piece_size;
		TORRENT_ASSERT(num_have() < m_torrent_file->num_pieces());

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_picker->have_piece(last_piece))
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

		const int blocks_per_piece = (piece_size + m_block_size - 1) / m_block_size;

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			if (m_picker->have_piece(index)) continue;
			TORRENT_ASSERT(i->finished <= m_picker->blocks_in_piece(index));

#ifdef TORRENT_DEBUG
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
				if (m_picker->have_piece(p->piece_index))
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
#ifdef TORRENT_DEBUG
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

		TORRENT_ASSERT(total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(wanted_done <= m_torrent_file->total_size());

#ifdef TORRENT_DEBUG

		if (total_done >= m_torrent_file->total_size())
		{
			// Thist happens when a piece has been downloaded completely
			// but not yet verified against the hash
			std::cerr << "num_have: " << num_have() << std::endl;
			
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

		TORRENT_ASSERT(valid_metadata());

		if (passed_hash_check == 0)
		{
			// the following call may cause picker to become invalid
			// in case we just became a seed
			piece_passed(index);
		}
		else if (passed_hash_check == -2)
		{
			// piece_failed() will restore the piece
			piece_failed(index);
		}
		else
		{
			TORRENT_ASSERT(passed_hash_check == -1);
			m_picker->restore_piece(index);
			restore_piece_state(index);
		}
	}

	void torrent::piece_passed(int index)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());

		if (m_ses.m_alerts.should_post<piece_finished_alert>())
		{
			m_ses.m_alerts.post_alert(piece_finished_alert(get_handle()
				, index));
		}

		bool was_finished = m_picker->num_filtered() + num_have()
			== torrent_file().num_pieces();

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		m_picker->we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end();)
		{
			peer_connection* p = *i;
			++i;
			p->announce_piece(index);
		}

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

		// since this piece just passed, we might have
		// become uninterested in some peers where this
		// was the last piece we were interested in
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			// if we're not interested already, no need to check
			if (!p->is_interesting()) continue;
			// if the peer doesn't have the piece we just got, it
			// wouldn't affect our interest
			if (!p->has_piece(index)) continue;
			p->update_interest();
		}

		if (!was_finished && is_finished())
		{
			// torrent finished
			// i.e. all the pieces we're interested in have
			// been downloaded. Release the files (they will open
			// in read only mode if needed)
			finished();
			// if we just became a seed, picker is now invalid, since it
			// is deallocated by the torrent once it starts seeding
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

		if (m_ses.m_alerts.should_post<hash_failed_alert>())
			m_ses.m_alerts.post_alert(hash_failed_alert(get_handle(), index));

		// increase the total amount of failed bytes
		add_failed_bytes(m_torrent_file->piece_size(index));

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#ifdef TORRENT_DEBUG
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
				if (m_ses.m_alerts.should_post<peer_ban_alert>())
				{
					peer_id pid(0);
					if (p->connection) pid = p->connection->pid();
					m_ses.m_alerts.post_alert(peer_ban_alert(
						get_handle(), p->ip(), pid));
				}

				// mark the peer as banned
				m_policy.ban_peer(p);

				if (p->connection)
				{
#ifdef TORRENT_LOGGING
					(*m_ses.m_logger) << time_now_string() << " *** BANNING PEER [ " << p->ip()
						<< " ] 'too many corrupt pieces'\n";
#endif
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
					(*p->connection->m_logger) << "*** BANNING PEER [ " << p->ip()
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
		restore_piece_state(index);
		TORRENT_ASSERT(m_storage);

		TORRENT_ASSERT(m_picker->have_piece(index) == false);

#ifdef TORRENT_DEBUG
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

	void torrent::restore_piece_state(int index)
	{
		TORRENT_ASSERT(has_picker());
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			std::deque<pending_block> const& dq = p->download_queue();
			std::deque<piece_block> const& rq = p->request_queue();
			for (std::deque<pending_block>::const_iterator k = dq.begin()
				, end(dq.end()); k != end; ++k)
			{
				if (k->block.piece_index != index) continue;
				m_picker->mark_as_downloading(k->block, p->peer_info_struct()
					, (piece_picker::piece_state_t)p->peer_speed());
			}
			for (std::deque<piece_block>::const_iterator k = rq.begin()
				, end(rq.end()); k != end; ++k)
			{
				if (k->piece_index != index) continue;
				m_picker->mark_as_downloading(*k, p->peer_info_struct()
					, (piece_picker::piece_state_t)p->peer_speed());
			}
		}
	}

	void torrent::abort()
	{
		INVARIANT_CHECK;

		if (m_abort) return;

		m_abort = true;
		// if the torrent is paused, it doesn't need
		// to announce with even=stopped again.
		if (!is_paused())
		{
			stop_announcing();
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*(*i)->m_logger) << "*** ABORTING TORRENT\n";
		}
#endif

		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all();
		if (m_owning_storage.get())
		{
			m_storage->async_release_files(
				bind(&torrent::on_files_released, shared_from_this(), _1, _2));
			m_storage->abort_disk_io();
		}
		
		dequeue_torrent_check();
		
		if (m_state == torrent_status::checking_files)
			set_state(torrent_status::queued_for_checking);

		m_owning_storage = 0;
		m_host_resolver.cancel();
	}

	void torrent::on_files_deleted(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret != 0)
		{
			if (alerts().should_post<torrent_delete_failed_alert>())
				alerts().post_alert(torrent_delete_failed_alert(get_handle(), j.str));
		}
		else
		{
			if (alerts().should_post<torrent_deleted_alert>())
				alerts().post_alert(torrent_deleted_alert(get_handle()));
		}
	}

	void torrent::on_files_released(int ret, disk_io_job const& j)
	{
/*
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post<torrent_paused_alert>())
		{
			alerts().post_alert(torrent_paused_alert(get_handle()));
		}
*/
	}

	void torrent::on_save_resume_data(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (!j.resume_data && alerts().should_post<save_resume_data_failed_alert>())
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle(), j.str));
			return;
		}

		if (j.resume_data && alerts().should_post<save_resume_data_alert>())
		{
			write_resume_data(*j.resume_data);
			alerts().post_alert(save_resume_data_alert(j.resume_data
				, get_handle()));
		}
	}

	void torrent::on_file_renamed(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		{
			if (ret == 0)
			{
				if (alerts().should_post<file_renamed_alert>())
					alerts().post_alert(file_renamed_alert(get_handle(), j.str, j.piece));
				m_torrent_file->rename_file(j.piece, j.str);
			}
			else
			{
				if (alerts().should_post<file_rename_failed_alert>())
					alerts().post_alert(file_rename_failed_alert(get_handle(), j.str, j.piece));
			}
		}
	}

	void torrent::on_torrent_paused(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (alerts().should_post<torrent_paused_alert>())
			alerts().post_alert(torrent_paused_alert(get_handle()));
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
		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
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
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
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
		
		if (m_torrent_file->num_pieces() == 0) return;

		std::copy(files.begin(), files.end(), m_file_priority.begin());
		update_piece_priorities();
	}

	void torrent::set_file_priority(int index, int prio)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(index < m_torrent_file->num_files());
		TORRENT_ASSERT(index >= 0);
		if (m_file_priority[index] == prio) return;
		m_file_priority[index] = prio;
		update_piece_priorities();
	}
	
	int torrent::file_priority(int index) const
	{
		TORRENT_ASSERT(index < m_torrent_file->num_files());
		TORRENT_ASSERT(index >= 0);
		return m_file_priority[index];
	}

	void torrent::file_priorities(std::vector<int>& files) const
	{
		INVARIANT_CHECK;
		files.resize(m_file_priority.size());
		std::copy(m_file_priority.begin(), m_file_priority.end(), files.begin());
	}

	void torrent::update_piece_priorities()
	{
		INVARIANT_CHECK;

		if (m_torrent_file->num_pieces() == 0) return;

		size_type position = 0;
		int piece_length = m_torrent_file->piece_length();
		// initialize the piece priorities to 0, then only allow
		// setting higher priorities
		std::vector<int> pieces(m_torrent_file->num_pieces(), 0);
		for (int i = 0; i < int(m_file_priority.size()); ++i)
		{
			size_type start = position;
			size_type size = m_torrent_file->files().at(i).size;
			if (size == 0) continue;
			position += size;
			if (m_file_priority[i] == 0) continue;

			// mark all pieces of the file with this file's priority
			// but only if the priority is higher than the pieces
			// already set (to avoid problems with overlapping pieces)
			int start_piece = int(start / piece_length);
			int last_piece = int((position - 1) / piece_length);
			TORRENT_ASSERT(last_piece < int(pieces.size()));
			// if one piece spans several files, we might
			// come here several times with the same start_piece, end_piece
			std::for_each(pieces.begin() + start_piece
				, pieces.begin() + last_piece + 1
				, bind(&set_if_greater, _1, m_file_priority[i]));
		}
		prioritize_pieces(pieces);
	}

	// this is called when piece priorities have been updated
	// updates the interested flag in peers
	void torrent::update_peer_interest(bool was_finished)
	{
		for (peer_iterator i = begin(); i != end();)
		{
			peer_connection* p = *i;
			// update_interest may disconnect the peer and
			// invalidate the iterator
			++i;
			p->update_interest();
		}

		// the torrent just became finished
		if (is_finished() && !was_finished)
		{
			finished();
		}
		else if (!is_finished() && was_finished)
		{
			// if we used to be finished, but we aren't anymore
			// we may need to connect to peers again
			resume_download();
		}
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
				position += m_torrent_file->files().at(i).size;
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
		m_trackers.clear();
		std::remove_copy_if(urls.begin(), urls.end(), back_inserter(m_trackers)
			, boost::bind(&std::string::empty, boost::bind(&announce_entry::url, _1)));

		if (m_currently_trying_tracker >= (int)m_trackers.size())
			m_currently_trying_tracker = (int)m_trackers.size()-1;
		m_last_working_tracker = -1;
		if (!m_trackers.empty()) start_announcing();
		else stop_announcing();
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
		if (!c.send_unchoke()) return false;
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
					m_picker->dec_refcount_all();
				}
			}
			else
			{
				if (m_picker.get())
				{
					bitfield const& pieces = p->get_bitfield();
					TORRENT_ASSERT(pieces.count() < int(pieces.size()));
					m_picker->dec_refcount(pieces);
				}
			}
		}

		if (!p->is_choked())
		{
			--m_num_uploads;
			m_ses.m_unchoke_time_scaler = 0;
		}

		if (p->peer_info_struct() && p->peer_info_struct()->optimistically_unchoked)
		{
			m_ses.m_optimistic_unchoke_time_scaler = 0;
		}

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
		char const* error;
		boost::tie(protocol, auth, hostname, port, path, error)
			= parse_url_components(url);

		if (error)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
			(*m_ses.m_logger) << time_now_string() << " failed to parse web seed url: " << error << "\n";
#endif
			// never try it again
			remove_url_seed(url);
			return;
		}
		
#ifdef TORRENT_USE_OPENSSL
		if (protocol != "http" && protocol != "https")
#else
		if (protocol != "http")
#endif
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
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
			if (m_ses.m_alerts.should_post<url_seed_alert>())
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
			if (m_ses.m_alerts.should_post<url_seed_alert>())
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
			tcp::resolver::query q(ps.hostname, to_string(ps.port).elems);
			m_host_resolver.async_resolve(q,
				bind(&torrent::on_proxy_name_lookup, shared_from_this(), _1, _2, url));
		}
		else
		{
			if (m_ses.m_port_filter.access(port) & port_filter::blocked)
			{
				if (m_ses.m_alerts.should_post<url_seed_alert>())
				{
					m_ses.m_alerts.post_alert(
						url_seed_alert(get_handle(), url, "port blocked by port-filter"));
				}
				// never try it again
				remove_url_seed(url);
				return;
			}

			tcp::resolver::query q(hostname, to_string(port).elems);
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

		if (m_abort) return;

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, e.message()));
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
		char const* error;
		boost::tie(ignore, ignore, hostname, port, ignore, error)
			= parse_url_components(url);

		if (error)
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), url, error));
			}
			remove_url_seed(url);
			return;
		}

		if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
		{
			if (m_ses.m_alerts.should_post<peer_blocked_alert>())
				m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()));
			return;
		}

		tcp::resolver::query q(hostname, to_string(port).elems);
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

		if (m_abort) return;

		std::set<std::string>::iterator i = m_resolving_web_seeds.find(url);
		if (i != m_resolving_web_seeds.end()) m_resolving_web_seeds.erase(i);

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
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
			if (m_ses.m_alerts.should_post<peer_blocked_alert>())
				m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()));
			return;
		}
		
		boost::shared_ptr<socket_type> s(new (std::nothrow) socket_type(m_ses.m_io_service));
		if (!s) return;
	
		bool ret = instantiate_connection(m_ses.m_io_service, m_ses.web_seed_proxy(), *s);
		(void)ret;
		TORRENT_ASSERT(ret);

		if (m_ses.web_seed_proxy().type == proxy_settings::http
			|| m_ses.web_seed_proxy().type == proxy_settings::http_pw)
		{
			// the web seed connection will talk immediately to
			// the proxy, without requiring CONNECT support
			s->get<http_stream>().set_no_connect(true);
		}

		boost::intrusive_ptr<peer_connection> c(new (std::nothrow) web_peer_connection(
			m_ses, shared_from_this(), s, a, url, 0));
		if (!c) return;
			
#ifdef TORRENT_DEBUG
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
			return (a >> 24) | ((a & 0xff0000) >> 8) | ((a & 0xff00) << 8) | ((a & 0xff) << 24);
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

		asio::ip::address_v4 reversed(swap_bytes(p->remote().address().to_v4().to_ulong()));
		error_code ec;
		tcp::resolver::query q(reversed.to_string(ec) + ".zz.countries.nerd.dk", "0");
		if (ec)
		{
			p->set_country("!!");
			return;
		}
		m_resolving_country = true;
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

		if (m_abort) return;

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

	void torrent::read_resume_data(lazy_entry const& rd)
	{
		m_total_uploaded = rd.dict_find_int_value("total_uploaded");
		m_total_downloaded = rd.dict_find_int_value("total_downloaded");
		m_active_time = seconds(rd.dict_find_int_value("active_time"));
		m_seeding_time = seconds(rd.dict_find_int_value("seeding_time"));
		m_complete = rd.dict_find_int_value("num_seeds", -1);
		m_incomplete = rd.dict_find_int_value("num_downloaders", -1);
		set_upload_limit(rd.dict_find_int_value("upload_rate_limit", -1));
		set_download_limit(rd.dict_find_int_value("download_rate_limit", -1));
		set_max_connections(rd.dict_find_int_value("max_connections", -1));
		set_max_uploads(rd.dict_find_int_value("max_uploads", -1));

		lazy_entry const* file_priority = rd.dict_find_list("file_priority");
		if (file_priority && file_priority->list_size()
			== m_torrent_file->num_files())
		{
			for (int i = 0; i < file_priority->list_size(); ++i)
				m_file_priority[i] = file_priority->list_int_value_at(i, 1);
			update_piece_priorities();
		}
		lazy_entry const* piece_priority = rd.dict_find_string("piece_priority");
		if (piece_priority && piece_priority->string_length()
			== m_torrent_file->num_pieces())
		{
			char const* p = piece_priority->string_ptr();
			for (int i = 0; i < piece_priority->string_length(); ++i)
				m_picker->set_piece_priority(i, p[i]);
		}

		int auto_managed_ = rd.dict_find_int_value("auto_managed", -1);
		if (auto_managed_ != -1) m_auto_managed = auto_managed_;

		int sequential_ = rd.dict_find_int_value("sequential_download", -1);
		if (sequential_ != -1) set_sequential_download(sequential_);

		int paused_ = rd.dict_find_int_value("paused", -1);
		if (paused_ != -1) m_paused = paused_;

		lazy_entry const* trackers = rd.dict_find_list("trackers");
		if (trackers)
		{
			int tier = 0;
			for (int i = 0; i < trackers->list_size(); ++i)
			{
				lazy_entry const* tier_list = trackers->list_at(i);
				if (tier_list == 0 || tier_list->type() != lazy_entry::list_t)
					continue;
				for (int j = 0; j < tier_list->list_size(); ++j)
				{
					announce_entry e(tier_list->list_string_value_at(j));
					if (std::find_if(m_trackers.begin(), m_trackers.end()
						, boost::bind(&announce_entry::url, _1) == e.url) != m_trackers.end())
						continue;
					e.tier = tier;
					m_trackers.push_back(e);
				}
				++tier;
			}
			std::sort(m_trackers.begin(), m_trackers.end(), boost::bind(&announce_entry::tier, _1)
				< boost::bind(&announce_entry::tier, _2));
		}

		lazy_entry const* mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files->list_size() == m_torrent_file->num_files())
		{
			for (int i = 0; i < m_torrent_file->num_files(); ++i)
			{
				std::string new_filename = mapped_files->list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_torrent_file->rename_file(i, new_filename);
			}
		}

		lazy_entry const* url_list = rd.dict_find_list("url-list");
		if (url_list)
		{
			for (int i = 0; i < url_list->list_size(); ++i)
			{
				std::string url = url_list->list_string_value_at(i);
				if (url.empty()) continue;
				m_web_seeds.insert(url);
			}
		}
	}
	
	void torrent::write_resume_data(entry& ret) const
	{
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;
		ret["libtorrent-version"] = LIBTORRENT_VERSION;

		ret["total_uploaded"] = m_total_uploaded;
		ret["total_downloaded"] = m_total_downloaded;

		ret["active_time"] = total_seconds(m_active_time);
		ret["seeding_time"] = total_seconds(m_seeding_time);

		int seeds = 0;
		int downloaders = 0;
		if (m_complete >= 0) seeds = m_complete;
		else seeds = m_policy.num_seeds();
		if (m_incomplete >= 0) downloaders = m_incomplete;
		else downloaders = m_policy.num_peers() - m_policy.num_seeds();

		ret["num_seeds"] = seeds;
		ret["num_downloaders"] = downloaders;

		ret["sequential_download"] = m_sequential_download;
		
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

		// save trackers
		if (!m_trackers.empty())
		{
			entry::list_type& tr_list = ret["trackers"].list();
			tr_list.push_back(entry::list_type());
			int tier = 0;
			for (std::vector<announce_entry>::const_iterator i = m_trackers.begin()
				, end(m_trackers.end()); i != end; ++i)
			{
				if (i->tier == tier)
				{
					tr_list.back().list().push_back(i->url);
				}
				else
				{
					tr_list.push_back(entry::list_t);
					tr_list.back().list().push_back(i->url);
					tier = i->tier;
				}
			}
		}

		// save web seeds
		if (!m_web_seeds.empty())
		{
			entry::list_type& url_list = ret["url-list"].list();
			for (std::set<std::string>::const_iterator i = m_web_seeds.begin()
				, end(m_web_seeds.end()); i != end; ++i)
			{
				url_list.push_back(*i);
			}
		}

		// write have bitmask
		entry::string_type& pieces = ret["pieces"].string();
		pieces.resize(m_torrent_file->num_pieces());
		if (is_seed())
		{
			std::memset(&pieces[0], 1, pieces.size());
		}
		else
		{
			for (int i = 0, end(pieces.size()); i < end; ++i)
				pieces[i] = m_picker->have_piece(i) ? 1 : 0;
		}

		// write renamed files
		if (&m_torrent_file->files() != &m_torrent_file->orig_files())
		{
			entry::list_type& fl = ret["mapped_files"].list();
			for (torrent_info::file_iterator i = m_torrent_file->begin_files()
				, end(m_torrent_file->end_files()); i != end; ++i)
			{
				fl.push_back(i->path.string());
			}
		}

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
				entry peer(entry::dictionary_t);
				peer["ip"] = i->second.addr.to_string(ec);
				if (ec) continue;
				peer["port"] = i->second.port;
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

			entry peer(entry::dictionary_t);
			peer["ip"] = i->second.addr.to_string(ec);
			if (ec) continue;
			peer["port"] = i->second.port;
			peer_list.push_back(peer);
		}

		ret["upload_rate_limit"] = upload_limit();
		ret["download_rate_limit"] = download_limit();
		ret["max_connections"] = max_connections();
		ret["max_uploads"] = max_uploads();
		ret["paused"] = m_paused;
		ret["auto_managed"] = m_auto_managed;

		// write piece priorities
		entry::string_type& piece_priority = ret["piece_priority"].string();
		piece_priority.resize(m_torrent_file->num_pieces());
		if (is_seed())
		{
			std::memset(&piece_priority[0], 1, pieces.size());
		}
		else
		{
			for (int i = 0, end(piece_priority.size()); i < end; ++i)
				piece_priority[i] = m_picker->piece_priority(i);
		}

		// write file priorities
		entry::list_type& file_priority = ret["file_priority"].list();
		file_priority.clear();
		for (int i = 0, end(m_file_priority.size()); i < end; ++i)
			file_priority.push_back(m_file_priority[i]);

	}

	void torrent::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		v.clear();
		v.reserve(m_policy.num_peers());
		for (policy::const_iterator i = m_policy.begin_peer();
			i != m_policy.end_peer(); ++i)
		{
			peer_list_entry e;
			e.ip = i->second.ip();
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
			int num_blocks = (std::min)(pi.blocks_in_piece, int(partial_piece_info::max_blocks_per_piece));
			for (int j = 0; j < num_blocks; ++j)
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
						bi.peer = p->ip();
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
#ifdef TORRENT_DEBUG
		// this asserts that we don't have duplicates in the policy's peer list
		peer_iterator i_ = std::find_if(m_connections.begin(), m_connections.end()
			, bind(&peer_connection::remote, _1) == peerinfo->ip());
		TORRENT_ASSERT(i_ == m_connections.end()
			|| dynamic_cast<bt_peer_connection*>(*i_) == 0);
#endif

		TORRENT_ASSERT(want_more_peers());
		TORRENT_ASSERT(m_ses.num_connections() < m_ses.max_connections());

		tcp::endpoint a(peerinfo->ip());
		TORRENT_ASSERT((m_ses.m_ip_filter.access(peerinfo->addr) & ip_filter::blocked) == 0);

		boost::shared_ptr<socket_type> s(new socket_type(m_ses.m_io_service));

		bool ret = instantiate_connection(m_ses.m_io_service, m_ses.peer_proxy(), *s);
		(void)ret;
		TORRENT_ASSERT(ret);

		boost::intrusive_ptr<peer_connection> c(new bt_peer_connection(
			m_ses, shared_from_this(), s, a, peerinfo));

#ifdef TORRENT_DEBUG
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

		return peerinfo->connection;
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

		if (m_ses.m_alerts.should_post<metadata_received_alert>())
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle()));
		}

		init();

		return true;
	}

	bool torrent::attach_peer(peer_connection* p)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(p != 0);
		TORRENT_ASSERT(!p->is_local());

		m_has_incoming = true;

		if ((m_state == torrent_status::queued_for_checking
			|| m_state == torrent_status::checking_files
			|| m_state == torrent_status::checking_resume_data)
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
#ifdef TORRENT_DEBUG
		error_code ec;
		TORRENT_ASSERT(p->remote() == p->get_socket()->remote_endpoint(ec) || ec);
#endif

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		m_policy.check_invariant();
#endif
		return true;
	}

	bool torrent::want_more_peers() const
	{
		return int(m_connections.size()) < m_max_connections
			&& !is_paused()
			&& m_state != torrent_status::checking_files
			&& m_state != torrent_status::checking_resume_data
			&& (m_state != torrent_status::queued_for_checking
				|| !valid_metadata())
			&& m_policy.num_connect_candidates() > 0
			&& !m_abort;
	}

	void torrent::disconnect_all()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

// doesn't work with the m_paused -> m_num_peers == 0 condition
//		INVARIANT_CHECK;

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
#ifdef TORRENT_DEBUG
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
		INVARIANT_CHECK;

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

		TORRENT_ASSERT(is_finished());
		TORRENT_ASSERT(m_state != torrent_status::finished && m_state != torrent_status::seeding);

		if (alerts().should_post<torrent_finished_alert>())
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()));
		}

		set_state(torrent_status::finished);
		set_queue_position(-1);

		// we have to call completed() before we start
		// disconnecting peers, since there's an assert
		// to make sure we're cleared the piece picker
		if (is_seed()) completed();

		// disconnect all seeds
		// TODO: should disconnect all peers that have the pieces we have
		// not just seeds
		std::vector<peer_connection*> seeds;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			peer_connection* p = *i;
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
			if (p->upload_only())
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*p->m_logger) << "*** SEED, CLOSING CONNECTION\n";
#endif
				seeds.push_back(p);
			}
		}
		std::for_each(seeds.begin(), seeds.end()
			, bind(&peer_connection::disconnect, _1, "torrent finished, disconnecting seed", 0));

		m_policy.recalculate_connect_candidates();

		TORRENT_ASSERT(m_storage);
		// we need to keep the object alive during this operation
		m_storage->async_release_files(
			bind(&torrent::on_files_released, shared_from_this(), _1, _2));
	}

	// this is called when we were finished, but some files were
	// marked for downloading, and we are no longer finished	
	void torrent::resume_download()
	{
		INVARIANT_CHECK;
	
		TORRENT_ASSERT(!is_finished());
		set_state(torrent_status::downloading);
		set_queue_position((std::numeric_limits<int>::max)());
		m_policy.recalculate_connect_candidates();
	}

	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
		m_picker.reset();

		set_state(torrent_status::seeding);
		if (!m_complete_sent && m_announcing) announce_with_tracker();
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

	void torrent::try_next_tracker(tracker_request const& req)
	{
		INVARIANT_CHECK;

		++m_currently_trying_tracker;

		if ((unsigned)m_currently_trying_tracker < m_trackers.size())
		{
			announce_with_tracker(req.event);
			return;
		}

		int delay = tracker_retry_delay_min
			+ (std::min)(int(m_failed_trackers), int(tracker_failed_max))
			* (tracker_retry_delay_max - tracker_retry_delay_min)
			/ tracker_failed_max;

		++m_failed_trackers;
		// if we've looped the tracker list, wait a bit before retrying
		m_currently_trying_tracker = 0;

		// if we're stopping, just give up. Don't bother retrying
		if (req.event == tracker_request::stopped)
			return;

		restart_tracker_timer(time_now() + seconds(delay));

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

	void torrent::files_checked()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		TORRENT_ASSERT(m_torrent_file->is_valid());

		if (m_abort) return;

		// we might be finished already, in which case we should
		// not switch to downloading mode.
		if (m_state != torrent_status::finished)
			set_state(torrent_status::downloading);

		INVARIANT_CHECK;

		if (m_ses.m_alerts.should_post<torrent_checked_alert>())
		{
			m_ses.m_alerts.post_alert(torrent_checked_alert(
				get_handle()));
		}
		
		if (!is_seed())
		{
			// if we just finished checking and we're not a seed, we are
			// likely to be unpaused
			if (m_ses.m_auto_manage_time_scaler > 1)
				m_ses.m_auto_manage_time_scaler = 1;

			if (is_finished() && m_state != torrent_status::finished) finished();
		}
		else
		{
			m_complete_sent = true;
			if (m_state != torrent_status::finished) finished();
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

		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			for (torrent::peer_iterator i = m_connections.begin();
				i != m_connections.end();)
			{
				peer_connection* pc = *i;
				++i;
				if (pc->is_disconnecting()) continue;
				pc->on_metadata_impl();
				if (pc->is_disconnecting()) continue;
				pc->init();
			}
		}

		m_files_checked = true;

		start_announcing();
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	fs::path torrent::save_path() const
	{
		return m_save_path;
	}

	bool torrent::rename_file(int index, std::string const& name)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_files());

		if (!m_owning_storage.get()) return false;

		m_owning_storage->async_rename_file(index, name
			, bind(&torrent::on_file_renamed, shared_from_this(), _1, _2));
		return true;
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
			if (alerts().should_post<storage_moved_alert>())
			{
				alerts().post_alert(storage_moved_alert(get_handle(), m_save_path.string()));
			}
		}
	}

	void torrent::on_storage_moved(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == 0)
		{
			if (alerts().should_post<storage_moved_alert>())
			{
				alerts().post_alert(storage_moved_alert(get_handle(), j.str));
			}
			m_save_path = j.str;
		}
		else
		{
			if (alerts().should_post<storage_moved_failed_alert>())
			{
				alerts().post_alert(storage_moved_failed_alert(get_handle(), j.error));
			}
		}
	}

	piece_manager& torrent::filesystem()
	{
		TORRENT_ASSERT(m_owning_storage.get());
		TORRENT_ASSERT(m_storage);
		return *m_storage;
	}


	torrent_handle torrent::get_handle()
	{
		return torrent_handle(shared_from_this());
	}

	session_settings const& torrent::settings() const
	{
		return m_ses.settings();
	}

#ifdef TORRENT_DEBUG
	void torrent::check_invariant() const
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (is_paused()) TORRENT_ASSERT(num_peers() == 0);

		if (!should_check_files())
			TORRENT_ASSERT(m_state != torrent_status::checking_files);
		else
			TORRENT_ASSERT(m_queued_for_checking);
  
		if (!m_ses.m_queued_for_checking.empty())
		{
			// if there are torrents waiting to be checked
			// assert that there's a torrent that is being
			// processed right now
			int found = 0;
			int found_active = 0;
			for (aux::session_impl::torrent_map::iterator i = m_ses.m_torrents.begin()
				, end(m_ses.m_torrents.end()); i != end; ++i)
				if (i->second->m_state == torrent_status::checking_files)
				{
					++found;
					if (i->second->should_check_files()) ++found_active;
				}
			// the case of 2 is in the special case where one switches over from
			// checking to complete
			TORRENT_ASSERT(found_active >= 1);
			TORRENT_ASSERT(found_active <= 2);
			TORRENT_ASSERT(found >= 1);
		}

		TORRENT_ASSERT(m_resume_entry.type() == lazy_entry::dict_t
			|| m_resume_entry.type() == lazy_entry::none_t);

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
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
#endif
			peer_connection const& p = *(*i);
			for (std::deque<piece_block>::const_iterator i = p.request_queue().begin()
				, end(p.request_queue().end()); i != end; ++i)
				++num_requests[*i];
			for (std::deque<pending_block>::const_iterator i = p.download_queue().begin()
				, end(p.download_queue().end()); i != end; ++i)
				++num_requests[i->block];
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
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		}

		if (valid_metadata())
		{
			TORRENT_ASSERT(m_abort || !m_picker || m_picker->num_pieces() == m_torrent_file->num_pieces());
		}
		else
		{
			TORRENT_ASSERT(m_abort || !m_picker || m_picker->num_pieces() == 0);
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		for (policy::const_iterator i = m_policy.begin_peer()
			, end(m_policy.end_peer()); i != end; ++i)
		{
			TORRENT_ASSERT(i->second.addr == i->first);
		}
#endif

		size_type total_done = quantized_bytes_done();
		if (m_torrent_file->is_valid())
		{
			if (is_seed())
				TORRENT_ASSERT(total_done == m_torrent_file->total_size());
			else
				TORRENT_ASSERT(total_done != m_torrent_file->total_size() || !m_files_checked);

			TORRENT_ASSERT(m_block_size <= m_torrent_file->piece_length());
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
		TORRENT_ASSERT(!valid_metadata() || m_block_size > 0);
		TORRENT_ASSERT(!valid_metadata() || (m_torrent_file->piece_length() % m_block_size) == 0);
//		if (is_seed()) TORRENT_ASSERT(m_picker.get() == 0);
	}
#endif

	void torrent::set_sequential_download(bool sd)
	{ m_sequential_download = sd; }

	void torrent::set_queue_position(int p)
	{
		TORRENT_ASSERT((p == -1) == is_finished()
			|| (!m_auto_managed && p == -1)
			|| (m_abort && p == -1));
		if (is_finished() && p != -1) return;
		if (p == m_sequence_number) return;

		session_impl::torrent_map& torrents = m_ses.m_torrents;
		if (p >= 0 && m_sequence_number == -1)
		{
			int max_seq = -1;
			for (session_impl::torrent_map::iterator i = torrents.begin()
				, end(torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t->m_sequence_number > max_seq) max_seq = t->m_sequence_number;
			}
			m_sequence_number = (std::min)(max_seq + 1, p);
		}
		else if (p < 0)
		{
			for (session_impl::torrent_map::iterator i = torrents.begin()
				, end(torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t == this) continue;
				if (t->m_sequence_number >= m_sequence_number
					&& t->m_sequence_number != -1)
					--t->m_sequence_number;
			}
			m_sequence_number = p;
		}
		else if (p < m_sequence_number)
		{
			for (session_impl::torrent_map::iterator i = torrents.begin()
				, end(torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				if (t == this) continue;
				if (t->m_sequence_number >= p 
					&& t->m_sequence_number < m_sequence_number
					&& t->m_sequence_number != -1)
					++t->m_sequence_number;
			}
			m_sequence_number = p;
		}
		else if (p > m_sequence_number)
		{
			int max_seq = 0;
			for (session_impl::torrent_map::iterator i = torrents.begin()
				, end(torrents.end()); i != end; ++i)
			{
				torrent* t = i->second.get();
				int pos = t->m_sequence_number;
				if (pos > max_seq) max_seq = pos;
				if (t == this) continue;

				if (pos <= p
						&& pos > m_sequence_number
						&& pos != -1)
					--t->m_sequence_number;

			}
			m_sequence_number = (std::min)(max_seq, p);
		}

		if (m_ses.m_auto_manage_time_scaler > 2)
			m_ses.m_auto_manage_time_scaler = 2;
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
		stop_announcing();

		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_delete_files(
				bind(&torrent::on_files_deleted, shared_from_this(), _1, _2));
		}
	}

	void torrent::clear_error()
	{
		if (m_error.empty()) return;
		bool checking_files = should_check_files();
		if (m_ses.m_auto_manage_time_scaler > 2)
			m_ses.m_auto_manage_time_scaler = 2;
		m_error.clear();
		if (!checking_files && should_check_files())
			queue_torrent_check();
	}

	void torrent::set_error(std::string const& msg)
	{
		bool checking_files = should_check_files();
		m_error = msg;
		if (checking_files && !should_check_files())
		{
			// stop checking
			m_storage->abort_disk_io();
			dequeue_torrent_check();
			set_state(torrent_status::queued_for_checking);
		}
	}

	void torrent::auto_managed(bool a)
	{
		INVARIANT_CHECK;

		if (m_auto_managed == a) return;
		bool checking_files = should_check_files();
		m_auto_managed = a;
		// recalculate which torrents should be
		// paused
		m_ses.m_auto_manage_time_scaler = 0;

		if (!checking_files && should_check_files())
		{
			queue_torrent_check();
		}
		else if (checking_files && !should_check_files())
		{
			// stop checking
			m_storage->abort_disk_io();
			dequeue_torrent_check();
			set_state(torrent_status::queued_for_checking);
		}
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
		// downloaded may be 0 if the torrent is 0-sized
		size_type downloaded = (std::max)(m_total_downloaded, m_torrent_file->total_size());
		if (seed_time < s.seed_time_limit
			&& (download_time > 1 && seed_time / download_time < s.seed_time_ratio_limit)
			&& downloaded > 0
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
			if (m_state == torrent_status::queued_for_checking
				|| m_state == torrent_status::checking_files
				|| m_state == torrent_status::checking_resume_data)
			{
				if (alerts().should_post<save_resume_data_alert>())
				{
					boost::shared_ptr<entry> rd(new entry);
					write_resume_data(*rd);
					alerts().post_alert(save_resume_data_alert(rd
						, get_handle()));
				}
			}
			else
			{
				m_storage->async_save_resume_data(
					bind(&torrent::on_save_resume_data, shared_from_this(), _1, _2));
			}
		}
		else
		{
			if (alerts().should_post<save_resume_data_failed_alert>())
			{
				alerts().post_alert(save_resume_data_failed_alert(get_handle()
					, "save resume data failed, torrent is being destructed"));
			}
		}
	}

	bool torrent::should_check_files() const
	{
		return (m_state == torrent_status::checking_files
			|| m_state == torrent_status::queued_for_checking)
			&& (!m_paused || m_auto_managed)
			&& m_error.empty()
			&& !m_abort;
	}

	bool torrent::is_paused() const
	{
		return m_paused || m_ses.is_paused();
	}

	void torrent::pause()
	{
		INVARIANT_CHECK;

		if (m_paused) return;
		bool checking_files = should_check_files();
		m_paused = true;
		if (!m_ses.is_paused())
			do_pause();
		if (checking_files && !should_check_files())
		{
			// stop checking
			m_storage->abort_disk_io();
			dequeue_torrent_check();
			set_state(torrent_status::queued_for_checking);
		}
	}

	void torrent::do_pause()
	{
		if (!is_paused()) return;

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

		// this will make the storage close all
		// files and flush all cached data
		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_release_files(
				bind(&torrent::on_torrent_paused, shared_from_this(), _1, _2));
			m_storage->async_clear_read_cache();
		}
		else
		{
			if (alerts().should_post<torrent_paused_alert>())
				alerts().post_alert(torrent_paused_alert(get_handle()));
		}

		disconnect_all();
		stop_announcing();
	}

	void torrent::resume()
	{
		INVARIANT_CHECK;

		if (!m_paused) return;
		bool checking_files = should_check_files();
		m_paused = false;
		do_resume();
		if (!checking_files && should_check_files())
			queue_torrent_check();
	}

	void torrent::do_resume()
	{
		if (is_paused()) return;

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

		if (alerts().should_post<torrent_resumed_alert>())
			alerts().post_alert(torrent_resumed_alert(get_handle()));

		m_started = time_now();
		m_error.clear();
		start_announcing();
	}

	void torrent::restart_tracker_timer(ptime announce_at)
	{
		if (!m_announcing) return;

		m_next_tracker_announce = announce_at;
		error_code ec;
		boost::weak_ptr<torrent> self(shared_from_this());
		m_tracker_timer.expires_at(m_next_tracker_announce, ec);
		m_tracker_timer.async_wait(bind(&torrent::on_tracker_announce_disp, self, _1));
	}

	void torrent::start_announcing()
	{
		if (is_paused()) return;
		// if we don't have metadata, we need to announce
		// before checking files, to get peers to
		// request the metadata from
		if (!m_files_checked && valid_metadata()) return;
		if (m_announcing) return;

		m_announcing = true;

		if (!m_trackers.empty())
		{
			// tell the tracker that we're back
			m_start_sent = false;
			m_stat.clear();
			announce_with_tracker();
		}

		// private torrents are never announced on LSD
		// or on DHT, we don't need this timer.
		if (!m_torrent_file->is_valid() || !m_torrent_file->priv())
		{
			error_code ec;
			boost::weak_ptr<torrent> self(shared_from_this());
			m_lsd_announce_timer.expires_from_now(seconds(1), ec);
			m_lsd_announce_timer.async_wait(
				bind(&torrent::on_lsd_announce_disp, self, _1));
		}
	}

	void torrent::stop_announcing()
	{
		if (!m_announcing) return;

		error_code ec;
		m_lsd_announce_timer.cancel(ec);
		m_tracker_timer.cancel(ec);

		m_announcing = false;

		if (!m_trackers.empty())
			announce_with_tracker(tracker_request::stopped);
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

		if (is_paused())
		{
			// let the stats fade out to 0
 			m_stat.second_tick(tick_interval);
			return;
		}

		time_duration since_last_tick = microsec(tick_interval * 1000000L);
		if (is_seed()) m_seeding_time += since_last_tick;
		m_active_time += since_last_tick;

		// ---- WEB SEEDS ----

		// re-insert urls that are to be retrieds into the m_web_seeds
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
		if (!is_finished() && !m_web_seeds.empty() && m_files_checked)
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
		TORRENT_ASSERT(piece_index < (int)m_picker->num_pieces());
#ifdef TORRENT_DEBUG
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
#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
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
			if (alerts().should_post<file_error_alert>())
				alerts().post_alert(file_error_alert(j.error_file, get_handle(), j.str));
			set_error(j.str);
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
		fp.clear();
		fp.resize(m_torrent_file->num_files(), 1.f);
		if (is_seed()) return;

		std::vector<size_type> progress;
		file_progress(progress);
		for (int i = 0; i < m_torrent_file->num_files(); ++i)
		{
			file_entry const& f = m_torrent_file->file_at(i);
			if (f.size == 0) fp[i] = 1.f;
			else fp[i] = float(progress[i]) / f.size;
		}
	}

	void torrent::file_progress(std::vector<size_type>& fp) const
	{
		TORRENT_ASSERT(valid_metadata());
	
		fp.resize(m_torrent_file->num_files(), 0);

		if (is_seed())
		{
			for (int i = 0; i < m_torrent_file->num_files(); ++i)
				fp[i] = m_torrent_file->files().at(i).size;
			return;
		}
		
		TORRENT_ASSERT(has_picker());

		for (int i = 0; i < m_torrent_file->num_files(); ++i)
		{
			peer_request ret = m_torrent_file->files().map_file(i, 0, 0);
			size_type size = m_torrent_file->files().at(i).size;

// zero sized files are considered
// 100% done all the time
			if (size == 0)
			{
				fp[i] = 0;
				continue;
			}

			size_type done = 0;
			while (size > 0)
			{
				size_type bytes_step = (std::min)(size_type(m_torrent_file->piece_size(ret.piece)
					- ret.start), size);
				if (m_picker->have_piece(ret.piece)) done += bytes_step;
				++ret.piece;
				ret.start = 0;
				size -= bytes_step;
			}
			TORRENT_ASSERT(size == 0);

			fp[i] = done;
		}

		const std::vector<piece_picker::downloading_piece>& q
			= m_picker->get_download_queue();

		for (std::vector<piece_picker::downloading_piece>::const_iterator
			i = q.begin(), end(q.end()); i != end; ++i)
		{
			size_type offset = size_type(i->index) * m_torrent_file->piece_length();
			torrent_info::file_iterator file = m_torrent_file->file_at_offset(offset);
			int file_index = file - m_torrent_file->begin_files();
			int num_blocks = m_picker->blocks_in_piece(i->index);
			piece_picker::block_info const* info = i->info;
			for (int k = 0; k < num_blocks; ++k)
			{
				TORRENT_ASSERT(file != m_torrent_file->end_files());
				TORRENT_ASSERT(offset == size_type(i->index) * m_torrent_file->piece_length()
					+ k * m_block_size);
				TORRENT_ASSERT(offset < m_torrent_file->total_size());
				while (offset >= file->offset + file->size)
				{
					++file;
					++file_index;
				}
				TORRENT_ASSERT(file != m_torrent_file->end_files());

				size_type block_size = m_block_size;

				if (info[k].state == piece_picker::block_info::state_none)
				{
					offset += m_block_size;
					continue;
				}

				if (info[k].state == piece_picker::block_info::state_requested)
				{
					block_size = 0;
					policy::peer* p = static_cast<policy::peer*>(info[k].peer);
					if (p && p->connection)
					{
						boost::optional<piece_block_progress> pbp
							= p->connection->downloading_piece_progress();
						if (pbp && pbp->piece_index == i->index && pbp->block_index == k)
							block_size = pbp->bytes_downloaded;
						TORRENT_ASSERT(block_size <= m_block_size);
					}

					if (block_size == 0)
					{
						offset += m_block_size;
						continue;
					}
				}

				if (offset + block_size > file->offset + file->size)
				{
					int left_over = m_block_size - block_size;
					// split the block on multiple files
					while (block_size > 0)
					{
						TORRENT_ASSERT(offset <= file->offset + file->size);
						size_type slice = (std::min)(file->offset + file->size - offset
							, block_size);
						fp[file_index] += slice;
						offset += slice;
						block_size -= slice;
						TORRENT_ASSERT(offset <= file->offset + file->size);
						if (offset == file->offset + file->size)
						{
							++file;
							++file_index;
							if (file == m_torrent_file->end_files())
							{
								offset += block_size;
								break;
							}
						}
					}
					offset += left_over;
					TORRENT_ASSERT(offset == size_type(i->index) * m_torrent_file->piece_length()
						+ (k+1) * m_block_size);
				}
				else
				{
					fp[file_index] += block_size;
					offset += m_block_size;
				}
				TORRENT_ASSERT(file_index <= m_torrent_file->num_files());
			}
		}
	}
	
	void torrent::set_state(torrent_status::state_t s)
	{
#ifdef TORRENT_DEBUG
		if (s != torrent_status::checking_files
			&& s != torrent_status::queued_for_checking)
		{
			// the only valid transition away from queued_for_checking
			// is to checking_files. One exception is to finished
			// in case all the files are marked with priority 0
			if (m_queued_for_checking)
			{
				std::vector<int> pieces;
				m_picker->piece_priorities(pieces);
				// make sure all pieces have priority 0
				TORRENT_ASSERT(std::count(pieces.begin(), pieces.end(), 0) == pieces.size());
			}
		}
		if (s == torrent_status::seeding)
			TORRENT_ASSERT(is_seed());
		if (s == torrent_status::finished)
			TORRENT_ASSERT(is_finished());
		if (s == torrent_status::downloading && m_state == torrent_status::finished)
			TORRENT_ASSERT(!is_finished());
#endif

		if (m_state == s) return;
		m_state = s;
		if (m_ses.m_alerts.should_post<state_changed_alert>())
			m_ses.m_alerts.post_alert(state_changed_alert(get_handle(), s));
	}

	torrent_status torrent::status() const
	{
		INVARIANT_CHECK;

		ptime now = time_now();

		torrent_status st;

		st.has_incoming = m_has_incoming;
		st.error = m_error;

		if (m_last_scrape == min_time())
		{
			st.last_scrape = -1;
		}
		else
		{
			st.last_scrape = total_seconds(now - m_last_scrape);
		}
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
			total_seconds(next_announce() - now));
		if (st.next_announce.is_negative() || is_paused())
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
		// for backwards compatibility
		if (st.state == torrent_status::checking_resume_data)
			st.state = torrent_status::queued_for_checking;

		if (!valid_metadata())
		{
			st.state = torrent_status::downloading_metadata;
			st.progress = m_progress;
			st.block_size = 0;
			return st;
		}

		st.block_size = block_size();

		// fill in status that depends on metadata

		st.total_wanted = m_torrent_file->total_size();
		TORRENT_ASSERT(st.total_wanted >= 0);
		TORRENT_ASSERT(st.total_wanted >= m_torrent_file->piece_length()
			* (m_torrent_file->num_pieces() - 1));

		if (m_picker.get() && (m_picker->num_filtered() > 0
			|| m_picker->num_have_filtered() > 0))
		{
			int num_filtered_pieces = m_picker->num_filtered()
				+ m_picker->num_have_filtered();
			int last_piece_index = m_torrent_file->num_pieces() - 1;
			if (m_picker->piece_priority(last_piece_index) == 0)
			{
				st.total_wanted -= m_torrent_file->piece_size(last_piece_index);
				--num_filtered_pieces;
			}
			
			st.total_wanted -= size_type(num_filtered_pieces) * m_torrent_file->piece_length();
		}

		TORRENT_ASSERT(st.total_wanted >= st.total_wanted_done);

		if (m_state == torrent_status::checking_files)
			st.progress = m_progress;
		else if (st.total_wanted == 0) st.progress = 1.f;
		else st.progress = st.total_wanted_done
			/ static_cast<float>(st.total_wanted);

		if (has_picker())
		{
			int num_pieces = m_picker->num_pieces();
			st.pieces.resize(num_pieces, false);
			for (int i = 0; i < num_pieces; ++i)
				if (m_picker->have_piece(i)) st.pieces.set_bit(i);
		}
		st.num_pieces = num_have();
		st.num_seeds = num_seeds();
		if (m_picker.get())
			st.distributed_copies = m_picker->distributed_copies();
		else
			st.distributed_copies = -1;
		return st;
	}

	void torrent::add_redundant_bytes(int b)
	{
		TORRENT_ASSERT(b > 0);
		m_total_redundant_bytes += b;
		m_ses.add_redundant_bytes(b);
	}

	void torrent::add_failed_bytes(int b)
	{
		TORRENT_ASSERT(b > 0);
		m_total_failed_bytes += b;
		m_ses.add_failed_bytes(b);
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

		if (r.kind == tracker_request::announce_request)
		{
			if (m_ses.m_alerts.should_post<tracker_error_alert>())
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, m_failed_trackers + 1, 0, r.url, "tracker timed out"));
			}
		}
		else if (r.kind == tracker_request::scrape_request)
		{
			if (m_ses.m_alerts.should_post<scrape_failed_alert>())
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle()
					, r.url, "tracker timed out"));
			}
		}

		if (r.kind == tracker_request::announce_request)
			try_next_tracker(r);
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
		if (r.kind == tracker_request::announce_request)
		{
			if (m_ses.m_alerts.should_post<tracker_error_alert>())
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, m_failed_trackers + 1, response_code, r.url, str));
			}
		}
		else if (r.kind == tracker_request::scrape_request)
		{
			if (m_ses.m_alerts.should_post<scrape_failed_alert>())
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle(), r.url, str));
			}
		}

		if (r.kind == tracker_request::announce_request)
			try_next_tracker(r);
	}


#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << time_now_string() << " " << line << "\n";
	}
#endif

}

