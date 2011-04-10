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
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <numeric>

#ifdef TORRENT_DEBUG
#include <iostream>
#endif

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/convenience.hpp>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/http_seed_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"

#if TORRENT_USE_IOSTREAM
#include <iostream>
#endif

using namespace libtorrent;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;
using libtorrent::aux::session_impl;

namespace
{
	size_type collect_free_download(
		torrent::peer_iterator start
		, torrent::peer_iterator end)
	{
		size_type accumulator = 0;
		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			// if the peer is interested in us, it means it may
			// want to trade it's surplus uploads for downloads itself
			// (and we should not consider it free). If the share diff is
			// negative, there's no free download to get from this peer.
			size_type diff = (*i)->share_diff();
			TORRENT_ASSERT(diff < (std::numeric_limits<size_type>::max)());
			if ((*i)->is_peer_interested() || diff <= 0)
				continue;

			TORRENT_ASSERT(diff > 0);
			(*i)->add_free_upload(-diff);
			accumulator += diff;
			TORRENT_ASSERT(accumulator > 0);
		}
		TORRENT_ASSERT(accumulator >= 0);
		return accumulator;
	}

	// returns the amount of free upload left after
	// it has been distributed to the peers
	size_type distribute_free_upload(
		torrent::peer_iterator start
		, torrent::peer_iterator end
		, size_type free_upload)
	{
		if (free_upload <= 0) return free_upload;
		int num_peers = 0;
		size_type total_diff = 0;
		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			size_type d = (*i)->share_diff();
			TORRENT_ASSERT(d < (std::numeric_limits<size_type>::max)());
			total_diff += d;
			if (!(*i)->is_peer_interested() || (*i)->share_diff() >= 0) continue;
			++num_peers;
		}

		if (num_peers == 0) return free_upload;
		size_type upload_share;
		if (total_diff >= 0)
		{
			upload_share = (std::min)(free_upload, total_diff) / num_peers;
		}
		else
		{
			upload_share = (free_upload + total_diff) / num_peers;
		}
		if (upload_share < 0) return free_upload;

		for (torrent::peer_iterator i = start; i != end; ++i)
		{
			peer_connection* p = *i;
			if (!p->is_peer_interested() || p->share_diff() >= 0) continue;
			p->add_free_upload(upload_share);
			free_upload -= upload_share;
		}
		return free_upload;
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
			if (pid.is_all_zeros()) return false;
			return true;
		}

		peer_id const& pid;
	};
}

namespace libtorrent
{

	torrent::torrent(
		session_impl& ses
		, tcp::endpoint const& net_interface
		, int block_size
		, int seq
		, add_torrent_params const& p)
		: m_policy(this)
		, m_active_time(seconds(0))
		, m_finished_time(seconds(0))
		, m_seeding_time(seconds(0))
		, m_total_uploaded(0)
		, m_total_downloaded(0)
		, m_started(time_now())
		, m_last_scrape(min_time())
		, m_upload_mode_time(time_now())
		, m_torrent_file(p.ti ? p.ti : new torrent_info(p.info_hash))
		, m_storage(0)
		, m_host_resolver(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_dht_announce_timer(ses.m_io_service)
#endif
		, m_tracker_timer(ses.m_io_service)
#ifndef TORRENT_DISABLE_DHT
		, m_last_dht_announce(time_now() - minutes(15))
#endif
		, m_ses(ses)
		, m_trackers(m_torrent_file->trackers())
		, m_average_piece_time(seconds(0))
		, m_piece_time_deviation(seconds(0))
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_padding(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(p.save_path))
		, m_num_verified(0)
		, m_available_free_upload(0)
		, m_storage_mode(p.storage_mode)
		, m_state(torrent_status::checking_resume_data)
		, m_settings(ses.settings())
		, m_storage_constructor(p.storage)
		, m_progress_ppm(0)
		, m_ratio(0.f)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
		, m_block_size(p.ti ? (std::min)(block_size, m_torrent_file->piece_length()) : block_size)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_deficit_counter(0)
		, m_sequence_number(seq)
		, m_last_working_tracker(-1)
		, m_time_scaler(0)
		, m_priority(0)
		, m_abort(false)
		, m_paused(p.paused)
		, m_upload_mode(p.upload_mode)
		, m_auto_managed(p.auto_managed)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		, m_resolving_country(false)
		, m_resolve_countries(false)
#endif
		, m_sequential_download(false)
		, m_got_tracker_response(false)
		, m_connections_initialized(p.ti)
		, m_super_seeding(false)
		, m_has_incoming(false)
		, m_files_checked(false)
		, m_queued_for_checking(false)
		, m_announcing(false)
		, m_waiting_tracker(false)
		, m_seed_mode(p.seed_mode && m_torrent_file->is_valid())
		, m_override_resume_data(p.override_resume_data)
	{
		if (m_seed_mode)
			m_verified.resize(m_torrent_file->num_pieces(), false);

		if (p.resume_data) m_resume_data.swap(*p.resume_data);

		if (m_settings.prefer_udp_trackers)
			prioritize_udp_trackers();

#ifndef TORRENT_DISABLE_ENCRYPTION
		hasher h;
		h.update("req2", 4);
		h.update((char*)&m_torrent_file->info_hash()[0], 20);
		m_obfuscated_hash = h.final();
#endif

#ifdef TORRENT_DEBUG
		m_files_checked = false;
#endif
		INVARIANT_CHECK;

		if (p.name && !p.ti) m_name.reset(new std::string(p.name));

		if (p.tracker_url && std::strlen(p.tracker_url) > 0)
		{
			m_trackers.push_back(announce_entry(p.tracker_url));
			m_trackers.back().fail_limit = 0;
			m_trackers.back().source = announce_entry::source_magnet_link;
			m_torrent_file->add_tracker(p.tracker_url);
		}

	}

	void torrent::start()
	{
		TORRENT_ASSERT(!m_picker);

		if (!m_seed_mode)
		{
			m_picker.reset(new piece_picker());
			std::fill(m_file_progress.begin(), m_file_progress.end(), 0);

			if (!m_resume_data.empty())
			{
				if (lazy_bdecode(&m_resume_data[0], &m_resume_data[0]
					+ m_resume_data.size(), m_resume_entry) != 0)
				{
					std::vector<char>().swap(m_resume_data);
					if (m_ses.m_alerts.should_post<fastresume_rejected_alert>())
					{
						error_code ec(errors::parse_failed);
						m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle(), ec));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
						(*m_ses.m_logger) << "fastresume data for "
							<< torrent_file().name() << " rejected: " << ec.message() << "\n";
#endif
					}
				}
			}
		}

		// we need to start announcing since we don't have any
		// metadata. To receive peers to ask for it.
		if (m_torrent_file->is_valid())
		{
			init();
		}
		else
		{
			set_state(torrent_status::downloading_metadata);
			start_announcing();
		}
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
		if (!m_settings.use_dht_as_fallback) return true;

		int verified_trackers = 0;
		for (std::vector<announce_entry>::const_iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
			if (i->verified) ++verified_trackers;
			
		return verified_trackers == 0;
	}

	void torrent::force_dht_announce()
	{
		m_last_dht_announce = min_time();
		// DHT announces are done on the local service
		// discovery timer. Trigger it.
		error_code ec;
		m_dht_announce_timer.expires_from_now(seconds(1), ec);
		m_dht_announce_timer.async_wait(
			boost::bind(&torrent::on_dht_announce, shared_from_this(), _1));
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
			disconnect_all(errors::torrent_aborted);
	}

	void torrent::read_piece(int piece)
	{
		TORRENT_ASSERT(piece >= 0 && piece < m_torrent_file->num_pieces());
		int piece_size = m_torrent_file->piece_size(piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		read_piece_struct* rp = new read_piece_struct;
		rp->piece_data.reset(new (std::nothrow) char[piece_size]);
		rp->blocks_left = 0;
		rp->fail = false;

		peer_request r;
		r.piece = piece;
		r.start = 0;
		for (int i = 0; i < blocks_in_piece; ++i, r.start += m_block_size)
		{
			r.length = (std::min)(piece_size - r.start, m_block_size);
			filesystem().async_read(r, boost::bind(&torrent::on_disk_read_complete
				, shared_from_this(), _1, _2, r, rp));
			++rp->blocks_left;
		}
	}

	void torrent::send_upload_only()
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		for (std::set<peer_connection*>::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			bt_peer_connection* p = dynamic_cast<bt_peer_connection*>(*i);
			if (p == 0) continue;
			p->write_upload_only();
		}
#endif
	}

	void torrent::set_upload_mode(bool b)
	{
		if (b == m_upload_mode) return;

		m_upload_mode = b;

		send_upload_only();

		if (m_upload_mode)
		{
			// clear request queues of all peers
			for (std::set<peer_connection*>::iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = (*i);
				p->cancel_all_requests();
			}
			// this is used to try leaving upload only mode periodically
			m_upload_mode_time = time_now();
		}
		else
		{
			// reset last_connected, to force fast reconnect after leaving upload mode
			for (policy::iterator i = m_policy.begin_peer()
				, end(m_policy.end_peer()); i != end; ++i)
			{
				(*i)->last_connected = 0;
			}

			// send_block_requests on all peers
			for (std::set<peer_connection*>::iterator i = m_connections.begin()
				, end(m_connections.end()); i != end; ++i)
			{
				peer_connection* p = (*i);
				p->send_block_requests();
			}
		}
	}

	void torrent::handle_disk_error(disk_io_job const& j, peer_connection* c)
	{
		if (!j.error) return;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		(*m_ses.m_logger) << "disk error: '" << j.error.message()
			<< " in file " << j.error_file
			<< " in torrent " << torrent_file().name()
			<< "\n";
#endif

		TORRENT_ASSERT(j.piece >= 0);

		piece_block block_finished(j.piece, j.offset / block_size());

		if (j.action == disk_io_job::write)
		{
			// we failed to write j.piece to disk tell the piece picker
			if (has_picker() && j.piece >= 0) picker().write_failed(block_finished);
		}

		if (j.error ==
#if BOOST_VERSION == 103500
			error_code(boost::system::posix_error::not_enough_memory, get_posix_category())
#elif BOOST_VERSION > 103500
			error_code(boost::system::errc::not_enough_memory, get_posix_category())
#else
			asio::error::no_memory
#endif
			)
		{
			if (alerts().should_post<file_error_alert>())
				alerts().post_alert(file_error_alert(j.error_file, get_handle(), j.error));
			if (c) c->disconnect(errors::no_memory);
			return;
		}

		// notify the user of the error
		if (alerts().should_post<file_error_alert>())
			alerts().post_alert(file_error_alert(j.error_file, get_handle(), j.error));

		if (j.action == disk_io_job::write)
		{
			// if we failed to write, stop downloading and just
			// keep seeding.
			// TODO: make this depend on the error and on the filesystem the
			// files are being downloaded to. If the error is no_space_left_on_device
			// and the filesystem doesn't support sparse files, only zero the priorities
			// of the pieces that are at the tails of all files, leaving everything
			// up to the highest written piece in each file
			set_upload_mode(true);
			return;
		}

		// put the torrent in an error-state
		set_error(j.error, j.error_file);
		pause();
	}

	void torrent::on_disk_read_complete(int ret, disk_io_job const& j, peer_request r, read_piece_struct* rp)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		disk_buffer_holder buffer(m_ses, j.buffer);

		--rp->blocks_left;
		if (ret != r.length)
		{
			rp->fail = true;
			handle_disk_error(j);
		}
		else
		{
			std::memcpy(rp->piece_data.get() + r.start, j.buffer, r.length);
		}

		if (rp->blocks_left == 0)
		{
			int size = m_torrent_file->piece_size(r.piece);
			if (rp->fail)
			{
				rp->piece_data.reset();
				size = 0;
			}

			if (m_ses.m_alerts.should_post<read_piece_alert>())
			{
				m_ses.m_alerts.post_alert(read_piece_alert(
					get_handle(), r.piece, rp->piece_data, size));
			}
			delete rp;
		}
	}

	void torrent::add_piece(int piece, char const* data, int flags)
	{
		TORRENT_ASSERT(piece >= 0 && piece < m_torrent_file->num_pieces());
		int piece_size = m_torrent_file->piece_size(piece);
		int blocks_in_piece = (piece_size + m_block_size - 1) / m_block_size;

		// avoid crash trying to access the picker when there is none
		if (is_seed()) return;

		if (picker().have_piece(piece)
			&& (flags & torrent::overwrite_existing) == 0)
			return;

		peer_request p;
		p.piece = piece;
		p.start = 0;
		picker().inc_refcount(piece);
		for (int i = 0; i < blocks_in_piece; ++i, p.start += m_block_size)
		{
			if (picker().is_finished(piece_block(piece, i))
				&& (flags & torrent::overwrite_existing) == 0)
				continue;

			p.length = (std::min)(piece_size - p.start, m_block_size);
			char* buffer = m_ses.allocate_disk_buffer("add piece");
			// out of memory
			if (buffer == 0)
			{
				picker().dec_refcount(piece);
				return;
			}
			disk_buffer_holder holder(m_ses, buffer);
			std::memcpy(buffer, data + p.start, p.length);
			filesystem().async_write(p, holder, boost::bind(&torrent::on_disk_write_complete
				, shared_from_this(), _1, _2, p));
			piece_block block(piece, i);
			picker().mark_as_downloading(block, 0, piece_picker::fast);
			picker().mark_as_writing(block, 0);
		}
		async_verify_piece(piece, boost::bind(&torrent::piece_finished
			, shared_from_this(), piece, _1));
		picker().dec_refcount(piece);
	}

	void torrent::on_disk_write_complete(int ret, disk_io_job const& j
		, peer_request p)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
	
		if (is_seed()) return;

		if (m_abort)
		{
			piece_block block_finished(p.piece, p.start / m_block_size);
			return;
		}

		piece_block block_finished(p.piece, p.start / m_block_size);

		if (ret == -1)
		{
			handle_disk_error(j);
			return;
		}

		// if we already have this block, just ignore it.
		// this can happen if the same block is passed in through
		// add_piece() multiple times
		if (picker().is_finished(block_finished)) return;

		picker().mark_as_finished(block_finished, 0);
	}

	bool torrent::add_merkle_nodes(std::map<int, sha1_hash> const& nodes, int piece)
	{
		return m_torrent_file->add_merkle_nodes(nodes, piece);
	}

	peer_request torrent::to_req(piece_block const& p) const
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

		m_file_priority.resize(m_torrent_file->num_files(), 1);
		m_file_progress.resize(m_torrent_file->num_files(), 0);

		m_block_size = (std::min)(m_block_size, m_torrent_file->piece_length());

		if (m_torrent_file->num_pieces() > piece_picker::max_pieces)
		{
			set_error(errors::too_many_pieces_in_torrent, "");
			pause();
			return;
		}

		if (m_torrent_file->num_pieces() == 0)
		{
			set_error(errors::torrent_invalid_length, "");
			pause();
			return;
		}

		// the shared_from_this() will create an intentional
		// cycle of ownership, se the hpp file for description.
		m_owning_storage = new piece_manager(shared_from_this(), m_torrent_file
			, m_save_path, m_ses.m_files, m_ses.m_disk_thread, m_storage_constructor
			, m_storage_mode);
		m_storage = m_owning_storage.get();

		if (has_picker())
		{
			int blocks_per_piece = (m_torrent_file->piece_length() + block_size() - 1) / block_size();
			int blocks_in_last_piece = ((m_torrent_file->total_size() % m_torrent_file->piece_length())
				+ block_size() - 1) / block_size();
			m_picker->init(blocks_per_piece, blocks_in_last_piece, m_torrent_file->num_pieces());
		}

		std::vector<std::string> const& url_seeds = m_torrent_file->url_seeds();
		for (std::vector<std::string>::const_iterator i = url_seeds.begin()
			, end(url_seeds.end()); i != end; ++i)
			add_web_seed(*i, web_seed_entry::url_seed);

		std::vector<std::string> const& http_seeds = m_torrent_file->http_seeds();
		for (std::vector<std::string>::const_iterator i = http_seeds.begin()
			, end(http_seeds.end()); i != end; ++i)
			add_web_seed(*i, web_seed_entry::http_seed);

		if (m_seed_mode)
		{
			m_ses.m_io_service.post(boost::bind(&torrent::files_checked_lock, shared_from_this()));
			std::vector<char>().swap(m_resume_data);
			lazy_entry().swap(m_resume_entry);
			return;
		}

		set_state(torrent_status::checking_resume_data);

		if (m_resume_entry.type() == lazy_entry::dict_t)
		{
			int ev = 0;
			if (m_resume_entry.dict_find_string_value("file-format") != "libtorrent resume file")
				ev = errors::invalid_file_tag;
	
			std::string info_hash = m_resume_entry.dict_find_string_value("info-hash");
			if (!ev && info_hash.empty())
				ev = errors::missing_info_hash;

			if (!ev && sha1_hash(info_hash) != m_torrent_file->info_hash())
				ev = errors::mismatching_info_hash;

			if (ev && m_ses.m_alerts.should_post<fastresume_rejected_alert>())
			{
				m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle()
					, error_code(ev, get_libtorrent_category())));
			}

			if (ev)
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_ses.m_logger) << "fastresume data for "
					<< torrent_file().name() << " rejected: "
					<< error_code(ev, get_libtorrent_category()).message() << "\n";
#endif
				std::vector<char>().swap(m_resume_data);
				lazy_entry().swap(m_resume_entry);
			}
			else
			{
				read_resume_data(m_resume_entry);
			}
		}
	
		TORRENT_ASSERT(m_block_size > 0);
		int file = 0;
		for (file_storage::iterator i = m_torrent_file->files().begin()
			, end(m_torrent_file->files().end()); i != end; ++i, ++file)
		{
			if (!i->pad_file || i->size == 0) continue;
			m_padding += i->size;
			
			peer_request pr = m_torrent_file->map_file(file, 0, m_torrent_file->file_at(file).size);
			int off = pr.start & (m_block_size-1);
			if (off != 0) { pr.length -= m_block_size - off; pr.start += m_block_size - off; }
			TORRENT_ASSERT((pr.start & (m_block_size-1)) == 0);

			int blocks_per_piece = m_torrent_file->piece_length() / m_block_size;
			piece_block pb(pr.piece, pr.start / m_block_size);
			for (; pr.length >= m_block_size; pr.length -= m_block_size, ++pb.block_index)
			{
				if (pb.block_index == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
				m_picker->mark_as_finished(pb, 0);
			}
			// ugly edge case where padfiles are not used they way they're
			// supposed to be. i.e. added back-to back or at the end
			if (pb.block_index == blocks_per_piece) { pb.block_index = 0; ++pb.piece_index; }
			if (pr.length > 0 && ((boost::next(i) != end && boost::next(i)->pad_file)
				|| boost::next(i) == end))
			{
				m_picker->mark_as_finished(pb, 0);
			}
		}

		if (m_padding > 0)
		{
			// if we marked an entire piece as finished, we actually
			// need to consider it finished

			std::vector<piece_picker::downloading_piece> const& dq
				= m_picker->get_download_queue();

			std::vector<int> have_pieces;

			for (std::vector<piece_picker::downloading_piece>::const_iterator i
				= dq.begin(); i != dq.end(); ++i)
			{
				int num_blocks = m_picker->blocks_in_piece(i->index);
				if (i->finished < num_blocks) continue;
				have_pieces.push_back(i->index);
			}

			for (std::vector<int>::iterator i = have_pieces.begin();
				i != have_pieces.end(); ++i)
			{
				we_have(*i);
			}
		}

		m_storage->async_check_fastresume(&m_resume_entry
			, boost::bind(&torrent::on_resume_data_checked
			, shared_from_this(), _1, _2));
	}

	void torrent::on_resume_data_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			handle_disk_error(j);
			set_state(torrent_status::queued_for_checking);
			std::vector<char>().swap(m_resume_data);
			lazy_entry().swap(m_resume_entry);
			return;
		}

		if (m_resume_entry.type() == lazy_entry::dict_t)
		{
			using namespace libtorrent::detail; // for read_*_endpoint()
			peer_id id(0);

			if (lazy_entry const* peers_entry = m_resume_entry.dict_find_string("peers"))
			{
				int num_peers = peers_entry->string_length() / (sizeof(address_v4::bytes_type) + 2);
				char const* ptr = peers_entry->string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					m_policy.add_peer(read_v4_endpoint<tcp::endpoint>(ptr)
						, id, peer_info::resume_data, 0);
				}
			}

			if (lazy_entry const* banned_peers_entry = m_resume_entry.dict_find_string("banned_peers"))
			{
				int num_peers = banned_peers_entry->string_length() / (sizeof(address_v4::bytes_type) + 2);
				char const* ptr = banned_peers_entry->string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					policy::peer* p = m_policy.add_peer(read_v4_endpoint<tcp::endpoint>(ptr)
						, id, peer_info::resume_data, 0);
					if (p) m_policy.ban_peer(p);
				}
			}

#if TORRENT_USE_IPV6
			if (lazy_entry const* peers6_entry = m_resume_entry.dict_find_string("peers6"))
			{
				int num_peers = peers6_entry->string_length() / (sizeof(address_v6::bytes_type) + 2);
				char const* ptr = peers6_entry->string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					m_policy.add_peer(read_v6_endpoint<tcp::endpoint>(ptr)
						, id, peer_info::resume_data, 0);
				}
			}

			if (lazy_entry const* banned_peers6_entry = m_resume_entry.dict_find_string("banned_peers6"))
			{
				int num_peers = banned_peers6_entry->string_length() / (sizeof(address_v6::bytes_type) + 2);
				char const* ptr = banned_peers6_entry->string_ptr();
				for (int i = 0; i < num_peers; ++i)
				{
					policy::peer* p = m_policy.add_peer(read_v6_endpoint<tcp::endpoint>(ptr)
						, id, peer_info::resume_data, 0);
					if (p) m_policy.ban_peer(p);
				}
			}
#endif

			// parse out "peers" from the resume data and add them to the peer list
			if (lazy_entry const* peers_entry = m_resume_entry.dict_find_list("peers"))
			{
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
					m_policy.add_peer(a, id, peer_info::resume_data, 0);
				}
			}

			// parse out "banned_peers" and add them as banned
			if (lazy_entry const* banned_peers_entry = m_resume_entry.dict_find_list("banned_peers"))
			{	
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
					policy::peer* p = m_policy.add_peer(a, id, peer_info::resume_data, 0);
					if (p) m_policy.ban_peer(p);
				}
			}
		}

		// only report this error if the user actually provided resume data
		if ((j.error || ret != 0) && !m_resume_data.empty()
			&& m_ses.m_alerts.should_post<fastresume_rejected_alert>())
		{
			m_ses.m_alerts.post_alert(fastresume_rejected_alert(get_handle(), j.error));
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		(*m_ses.m_logger) << "fastresume data for "
			<< torrent_file().name() << " rejected: "
			<< j.error.message() << " ret:" << ret << "\n";
#endif

		// if ret != 0, it means we need a full check. We don't necessarily need
		// that when the resume data check fails. For instance, if the resume data
		// is incorrect, but we don't have any files, we skip the check and initialize
		// the storage to not have anything.
		if (ret == 0)
		{
			// there are either no files for this torrent
			// or the resume_data was accepted

			if (!j.error && m_resume_entry.type() == lazy_entry::dict_t)
			{
				// parse have bitmask
				lazy_entry const* pieces = m_resume_entry.dict_find("pieces");
				if (pieces && pieces->type() == lazy_entry::string_t
					&& int(pieces->string_length()) == m_torrent_file->num_pieces())
				{
					char const* pieces_str = pieces->string_ptr();
					for (int i = 0, end(pieces->string_length()); i < end; ++i)
					{
						if (pieces_str[i] & 1) we_have(i);
						if (m_seed_mode && (pieces_str[i] & 2)) m_verified.set_bit(i);
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
							if (piece >= 0) we_have(piece);
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
										async_verify_piece(piece, boost::bind(&torrent::piece_finished
											, shared_from_this(), piece, _1));
								}
							}
						}
					}
				}
			}

			files_checked(l);
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
		m_ses.queue_check_torrent(shared_from_this());
	}

	void torrent::dequeue_torrent_check()
	{
		if (!m_queued_for_checking) return;
		m_queued_for_checking = false;
		m_ses.dequeue_check_torrent(shared_from_this());
	}

	void torrent::force_recheck()
	{
		if (!valid_metadata()) return;

		// if the torrent is already queued to check its files
		// don't do anything
		if (should_check_files()
			|| m_state == torrent_status::checking_resume_data)
			return;

		clear_error();

		disconnect_all(errors::stopping_torrent);
		stop_announcing();

		m_owning_storage->async_release_files();
		if (!m_picker) m_picker.reset(new piece_picker());
		std::fill(m_file_progress.begin(), m_file_progress.end(), 0);

		int blocks_per_piece = (m_torrent_file->piece_length() + block_size() - 1) / block_size();
		int blocks_in_last_piece = ((m_torrent_file->total_size() % m_torrent_file->piece_length())
			+ block_size() - 1) / block_size();
		m_picker->init(blocks_per_piece, blocks_in_last_piece, m_torrent_file->num_pieces());
		// assume that we don't have anything
		TORRENT_ASSERT(m_picker->num_have() == 0);
		m_files_checked = false;
		set_state(torrent_status::checking_resume_data);

		m_policy.recalculate_connect_candidates();

		if (m_auto_managed && !is_finished())
			set_queue_position((std::numeric_limits<int>::max)());

		std::vector<char>().swap(m_resume_data);
		lazy_entry().swap(m_resume_entry);
		m_storage->async_check_fastresume(&m_resume_entry
			, boost::bind(&torrent::on_force_recheck
			, shared_from_this(), _1, _2));
	}

	void torrent::on_force_recheck(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret == piece_manager::fatal_disk_error)
		{
			handle_disk_error(j);
			return;
		}
		if (ret == 0)
		{
			// if there are no files, just start
			files_checked(l);
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

		m_storage->async_check_files(boost::bind(
			&torrent::on_piece_checked
			, shared_from_this(), _1, _2));
	}
	
	void torrent::on_piece_checked(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		INVARIANT_CHECK;

		if (ret == piece_manager::disk_check_aborted)
		{
			dequeue_torrent_check();
			pause();
			return;
		}
		if (ret == piece_manager::fatal_disk_error)
		{
			if (m_ses.m_alerts.should_post<file_error_alert>())
			{
				m_ses.m_alerts.post_alert(file_error_alert(j.error_file, get_handle(), j.error));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << time_now_string() << ": fatal disk error ["
				" error: " << j.error.message() <<
				" torrent: " << torrent_file().name() <<
				" ]\n";
#endif
			pause();
			set_error(j.error, j.error_file);
			return;
		}

		m_progress_ppm = size_type(j.piece) * 1000000 / torrent_file().num_pieces();

		TORRENT_ASSERT(m_picker);
		if (j.offset >= 0 && !m_picker->have_piece(j.offset))
			we_have(j.offset);

		remove_time_critical_piece(j.piece);

		// we're not done checking yet
		// this handler will be called repeatedly until
		// we're done, or encounter a failure
		if (ret == piece_manager::need_full_check) return;

		dequeue_torrent_check();
		files_checked(l);
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
		m_waiting_tracker = false;	
		if (m_abort) return;
		announce_with_tracker();
	}

	void torrent::lsd_announce()
	{
		if (m_abort) return;
		
		// if the files haven't been checked yet, we're
		// not ready for peers
		if (!m_files_checked) return;

		if (m_torrent_file->is_valid() && m_torrent_file->priv())
			return;

		if (is_paused()) return;

		// announce with the local discovery service
		m_ses.announce_lsd(m_torrent_file->info_hash());
	}

#ifndef TORRENT_DISABLE_DHT

	void torrent::on_dht_announce(error_code const& e)
	{
		if (e) return;

		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		if (m_abort) return;

		error_code ec;
		m_dht_announce_timer.expires_from_now(minutes(15), ec);
		m_dht_announce_timer.async_wait(
			boost::bind(&torrent::on_dht_announce, shared_from_this(), _1));

		if (m_torrent_file->is_valid() && m_torrent_file->priv())
			return;

		if (is_paused()) return;
		if (!m_ses.m_dht) return;
		if (!should_announce_dht()) return;

		m_last_dht_announce = time_now();
		boost::weak_ptr<torrent> self(shared_from_this());
		m_ses.m_dht->announce(m_torrent_file->info_hash()
			, m_ses.listen_port()
			, boost::bind(&torrent::on_dht_announce_post, self, _1));
	}

	void torrent::on_dht_announce_post(boost::weak_ptr<libtorrent::torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		boost::shared_ptr<libtorrent::torrent> tor = t.lock();
		if (!tor) return;
		tor->session().m_io_service.post(boost::bind(&torrent::on_dht_announce_response_disp, t, peers));
	}

	void torrent::on_dht_announce_response_disp(boost::weak_ptr<libtorrent::torrent> t
		, std::vector<tcp::endpoint> const& peers)
	{
		boost::shared_ptr<libtorrent::torrent> tor = t.lock();
		if (!tor) return;
		session_impl::mutex_t::scoped_lock l(tor->session().m_mutex);
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
		std::for_each(peers.begin(), peers.end(), boost::bind(
			&policy::add_peer, boost::ref(m_policy), _1, peer_id(0)
			, peer_info::dht, 0));
	}

#endif

	void torrent::announce_with_tracker(tracker_request::event_t e
		, address const& bind_interface)
	{
		INVARIANT_CHECK;

		if (m_trackers.empty()) return;

		if (m_abort) e = tracker_request::stopped;

		tracker_request req;
		req.info_hash = m_torrent_file->info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download() - m_total_failed_bytes;
		req.uploaded = m_stat.total_payload_upload();
		req.corrupt = m_total_failed_bytes;
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;

		// exclude redundant bytes if we should
		if (!settings().report_true_downloaded)
			req.downloaded -= m_total_redundant_bytes;
		if (req.downloaded < 0) req.downloaded = 0;

		req.event = e;
		error_code ec;
		tcp::endpoint ep;
		ep = m_ses.get_ipv6_interface();
		if (ep != tcp::endpoint()) req.ipv6 = ep.address().to_string(ec);
		ep = m_ses.get_ipv4_interface();
		if (ep != tcp::endpoint()) req.ipv4 = ep.address().to_string(ec);

		// if we are aborting. we don't want any new peers
		req.num_want = (req.event == tracker_request::stopped)
			?0:m_settings.num_want;

		req.listen_port = m_ses.listen_port();
		req.key = m_ses.m_key;

		ptime now = time_now_hires();

		// the tier is kept as INT_MAX until we find the first
		// tracker that works, then it's set to that tracker's
		// tier.
		int tier = INT_MAX;

		// have we sent an announce in this tier yet?
		bool sent_announce = false;

		for (int i = 0; i < int(m_trackers.size()); ++i)
		{
			announce_entry& ae = m_trackers[i];
			if (m_settings.announce_to_all_tiers
				&& !m_settings.announce_to_all_trackers
				&& sent_announce
				&& ae.tier <= tier
				&& tier != INT_MAX)
				continue;

			if (ae.tier > tier && !m_settings.announce_to_all_tiers) break;
			if (ae.is_working()) { tier = ae.tier; sent_announce = false; }
			if (!ae.can_announce(now, is_seed()))
			{
				if (ae.is_working())
				{
					sent_announce = true; // this counts

					if (!m_settings.announce_to_all_trackers
						&& !m_settings.announce_to_all_tiers)
						break;
				}
				continue;
			}
			
			req.url = ae.url;
			req.event = e;
			if (req.event == tracker_request::none)
			{
				if (!ae.start_sent) req.event = tracker_request::started;
				else if (!ae.complete_sent && is_seed()) req.event = tracker_request::completed;
			}

			if (!is_any(bind_interface)) req.bind_ip = bind_interface;
			else req.bind_ip = m_ses.m_listen_interface.address();

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
			(*m_ses.m_logger) << time_now_string() << " ==> TACKER REQUEST " << req.url
				<< " event=" << (req.event==tracker_request::stopped?"stopped"
					:req.event==tracker_request::started?"started":"")
				<< " abort=" << m_abort << "\n";
			if (m_abort)
			{
				boost::shared_ptr<aux::tracker_logger> tl(new aux::tracker_logger(m_ses));
				m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
					, tracker_login(), tl);
			}
			else
#endif
				m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
					, tracker_login() , shared_from_this());
			ae.updating = true;
			ae.next_announce = now + seconds(20);
			ae.min_announce = now + seconds(10);

			if (m_ses.m_alerts.should_post<tracker_announce_alert>())
			{
				m_ses.m_alerts.post_alert(
					tracker_announce_alert(get_handle(), req.url, req.event));
			}

			sent_announce = true;
			if (ae.is_working()
				&& !m_settings.announce_to_all_trackers
				&& !m_settings.announce_to_all_tiers)
				break;
		}
		update_tracker_timer(now);
	}

	void torrent::scrape_tracker()
	{
		m_last_scrape = time_now();

		if (m_trackers.empty()) return;

		int i = m_last_working_tracker;
		if (i == -1) i = 0;
		
		tracker_request req;
		req.info_hash = m_torrent_file->info_hash();
		req.kind = tracker_request::scrape_request;
		req.url = m_trackers[i].url;
		req.bind_ip = m_ses.m_listen_interface.address();
		m_ses.m_tracker_manager.queue_request(m_ses.m_io_service, m_ses.m_half_open, req
			, tracker_login(), shared_from_this());
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
		, address const& tracker_ip // this is the IP we connected to
		, std::list<address> const& tracker_ips // these are all the IPs it resolved to
		, std::vector<peer_entry>& peer_list
		, int interval
		, int min_interval
		, int complete
		, int incomplete
		, address const& external_ip)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		TORRENT_ASSERT(r.kind == tracker_request::announce_request);

		if (external_ip != address())
			m_ses.set_external_address(external_ip);

		ptime now = time_now();

		if (interval < m_settings.min_announce_interval)
			interval = m_settings.min_announce_interval;

		announce_entry* ae = find_tracker(r);
		if (ae)
		{
			if (!ae->start_sent && r.event == tracker_request::started)
				ae->start_sent = true;
			if (!ae->complete_sent && r.event == tracker_request::completed)
				ae->complete_sent = true;
			ae->verified = true;
			ae->updating = false;
			ae->fails = 0;
			ae->next_announce = now + seconds(interval);
			ae->min_announce = now + seconds(min_interval);
			int tracker_index = ae - &m_trackers[0];
			m_last_working_tracker = prioritize_tracker(tracker_index);
		}
		update_tracker_timer(now);

		if (complete >= 0) m_complete = complete;
		if (incomplete >= 0) m_incomplete = incomplete;
		if (complete >= 0 && incomplete >= 0)
			m_last_scrape = now;

#if (defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING) && TORRENT_USE_IOSTREAM
		std::stringstream s;
		s << "TRACKER RESPONSE:\n"
			"interval: " << interval << "\n"
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
		s << "tracker ips: ";
		std::copy(tracker_ips.begin(), tracker_ips.end(), std::ostream_iterator<address>(s, " "));
		s << "\n";
		s << "we connected to: " << tracker_ip << "\n";
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
					boost::bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i->pid));
			}
			else
			{
				// ignore local addresses from the tracker (unless the tracker is local too)
				if (is_local(a.address()) && !is_local(tracker_ip)) continue;
				m_policy.add_peer(a, i->pid, peer_info::tracker, 0);
			}
		}

		if (m_ses.m_alerts.should_post<tracker_reply_alert>())
		{
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peer_list.size(), r.url));
		}
		m_got_tracker_response = true;

		// we're listening on an interface type that was not used
		// when talking to the tracker. If there is a matching interface
		// type in the tracker IP list, make another tracker request
		// using that interface
		// in order to avoid triggering this case over and over, don't
		// do it if the bind IP for the tracker request that just completed
		// matches one of the listen interfaces, since that means this
		// announce was the second one
		// don't connect twice just to tell it we're stopping

		if (((!is_any(m_ses.m_ipv6_interface.address()) && tracker_ip.is_v4())
			|| (!is_any(m_ses.m_ipv4_interface.address()) && tracker_ip.is_v6()))
			&& r.bind_ip != m_ses.m_ipv4_interface.address()
			&& r.bind_ip != m_ses.m_ipv6_interface.address()
			&& r.event != tracker_request::stopped)
		{
			std::list<address>::const_iterator i = std::find_if(tracker_ips.begin()
				, tracker_ips.end(), boost::bind(&address::is_v4, _1) != tracker_ip.is_v4());
			if (i != tracker_ips.end())
			{
				// the tracker did resolve to a different type if address, so announce
				// to that as well

				// tell the tracker to bind to the opposite protocol type
				address bind_interface = tracker_ip.is_v4()
					?m_ses.m_ipv6_interface.address()
					:m_ses.m_ipv4_interface.address();
				announce_with_tracker(r.event, bind_interface);
#if (defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING) && TORRENT_USE_IOSTREAM
				debug_log("announce again using " + print_address(bind_interface)
					+ " as the bind interface");
#endif
			}
		}
	}

	ptime torrent::next_announce() const
	{
		return m_waiting_tracker?m_tracker_timer.expires_at():min_time();
	}

	void torrent::force_tracker_request()
	{
		force_tracker_request(time_now_hires());
	}

	void torrent::force_tracker_request(ptime t)
	{
		if (is_paused()) return;
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
			i->next_announce = (std::max)(t, i->min_announce) + seconds(1);
		update_tracker_timer(time_now_hires());
	}

	void torrent::set_tracker_login(
		std::string const& name
		, std::string const& pw)
	{
		m_username = name;
		m_password = pw;
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
			
		m_policy.add_peer(*host, pid, peer_info::tracker, 0);
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

	// returns the number of bytes we are interested
	// in for the given block. This returns m_block_size
	// for all blocks except the last one (if it's smaller
	// than m_block_size) and blocks that overlap a padding
	// file
	int torrent::block_bytes_wanted(piece_block const& p) const
	{
		file_storage const& fs = m_torrent_file->files();
		int piece_size = m_torrent_file->piece_size(p.piece_index);
		int offset = p.block_index * m_block_size;
		if (m_padding == 0) return (std::min)(piece_size - offset, m_block_size);

		std::vector<file_slice> files = fs.map_block(
			p.piece_index, offset, (std::min)(piece_size - offset, m_block_size));
		int ret = 0;
		for (std::vector<file_slice>::iterator i = files.begin()
			, end(files.end()); i != end; ++i)
		{
			file_entry const& fe = fs.at(i->file_index);
			if (fe.pad_file) continue;
			ret += i->size;
		}
		TORRENT_ASSERT(ret <= (std::min)(piece_size - offset, m_block_size));
		return ret;
	}

	// fills in total_wanted, total_wanted_done and total_done
	void torrent::bytes_done(torrent_status& st) const
	{
		INVARIANT_CHECK;

		st.total_done = 0;
		st.total_wanted_done = 0;
		st.total_wanted = m_torrent_file->total_size();

		TORRENT_ASSERT(st.total_wanted >= m_padding);
		TORRENT_ASSERT(st.total_wanted >= 0);

		if (!valid_metadata() || m_torrent_file->num_pieces() == 0)
			return;

		TORRENT_ASSERT(st.total_wanted >= m_torrent_file->piece_length()
			* (m_torrent_file->num_pieces() - 1));

		const int last_piece = m_torrent_file->num_pieces() - 1;
		const int piece_size = m_torrent_file->piece_length();

		if (is_seed())
		{
			st.total_done = m_torrent_file->total_size() - m_padding;
			st.total_wanted_done = st.total_done;
			st.total_wanted = st.total_done;
			return;
		}

		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		st.total_wanted_done = size_type(num_have() - m_picker->num_have_filtered())
			* piece_size;
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		
		st.total_done = size_type(num_have()) * piece_size;
		TORRENT_ASSERT(num_have() < m_torrent_file->num_pieces());

		int num_filtered_pieces = m_picker->num_filtered()
			+ m_picker->num_have_filtered();
		int last_piece_index = m_torrent_file->num_pieces() - 1;
		if (m_picker->piece_priority(last_piece_index) == 0)
		{
			st.total_wanted -= m_torrent_file->piece_size(last_piece_index);
			--num_filtered_pieces;
		}
		st.total_wanted -= size_type(num_filtered_pieces) * piece_size;
	
		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_picker->have_piece(last_piece))
		{
			TORRENT_ASSERT(st.total_done >= piece_size);
			int corr = m_torrent_file->piece_size(last_piece)
				- piece_size;
			TORRENT_ASSERT(corr <= 0);
			TORRENT_ASSERT(corr > -piece_size);
			st.total_done += corr;
			if (m_picker->piece_priority(last_piece) != 0)
			{
				TORRENT_ASSERT(st.total_wanted_done >= piece_size);
				st.total_wanted_done += corr;
			}
		}
		TORRENT_ASSERT(st.total_wanted >= st.total_wanted_done);

		// subtract padding files
		if (m_padding > 0)
		{
			file_storage const& files = m_torrent_file->files();
			int fileno = 0;
			for (file_storage::iterator i = files.begin()
					, end(files.end()); i != end; ++i, ++fileno)
			{
				if (!i->pad_file) continue;
				peer_request p = files.map_file(fileno, 0, i->size);
				for (int j = p.piece; p.length > 0; ++j, p.length -= piece_size)
				{
					int deduction = (std::min)(p.length, piece_size);
					bool done = m_picker->have_piece(j);
					bool wanted = m_picker->piece_priority(j) > 0;
					if (done) st.total_done -= deduction;
					if (wanted) st.total_wanted -= deduction;
					if (wanted && done) st.total_wanted_done -= deduction;
				}
			}
		}

		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = (piece_size + m_block_size - 1) / m_block_size;

		// look at all unfinished pieces and add the completed
		// blocks to our 'done' counter
		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			// completed pieces are already accounted for
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
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
				TORRENT_ASSERT(m_picker->is_finished(piece_block(index, j))
					== (i->info[j].state == piece_picker::block_info::state_finished));
#endif
				if (i->info[j].state == piece_picker::block_info::state_finished)
				{
					corr += block_bytes_wanted(piece_block(index, j));
				}
				TORRENT_ASSERT(corr >= 0);
				TORRENT_ASSERT(index != last_piece || j < m_picker->blocks_in_last_piece()
					|| i->info[j].state != piece_picker::block_info::state_finished);
			}

			st.total_done += corr;
			if (m_picker->piece_priority(index) > 0)
				st.total_wanted_done += corr;
		}

		TORRENT_ASSERT(st.total_wanted <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done >= 0);
		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection* pc = *i;
			boost::optional<piece_block_progress> p
				= pc->downloading_piece_progress();
			if (!p) continue;

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
			TORRENT_ASSERT(p->full_block_bytes == to_req(piece_block(
				p->piece_index, p->block_index)).length);
#endif
		}
		for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
			i != downloading_piece.end(); ++i)
		{
			int done = (std::min)(block_bytes_wanted(i->first), i->second);
			st.total_done += done;
			if (m_picker->piece_priority(i->first.piece_index) != 0)
				st.total_wanted_done += done;
		}

		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size() - m_padding);
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size() - m_padding);

#ifdef TORRENT_DEBUG

		if (st.total_done >= m_torrent_file->total_size())
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

		TORRENT_ASSERT(st.total_done <= m_torrent_file->total_size());
		TORRENT_ASSERT(st.total_wanted_done <= m_torrent_file->total_size());

#endif

		TORRENT_ASSERT(st.total_done >= st.total_wanted_done);
	}

	// passed_hash_check
	// 0: success, piece passed check
	// -1: disk failure
	// -2: piece failed check
	void torrent::piece_finished(int index, int passed_hash_check)
	{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " *** PIECE_FINISHED [ p: "
			<< index << " chk: " << ((passed_hash_check == 0)
				?"passed":passed_hash_check == -1
				?"disk failed":"failed") << " ]\n";
#endif

		TORRENT_ASSERT(valid_metadata());

		TORRENT_ASSERT(!m_picker->have_piece(index));

		// even though the piece passed the hash-check
		// it might still have failed being written to disk
		// if so, piece_picker::write_failed() has been
		// called, and the piece is no longer finished.
		// in this case, we have to ignore the fact that
		// it passed the check
		if (!m_picker->is_piece_finished(index)) return;

		if (passed_hash_check == 0)
		{
			// the following call may cause picker to become invalid
			// in case we just became a seed
			piece_passed(index);
			// if we're in seed mode, we just acquired this piece
			// mark it as verified
			if (m_seed_mode) verified(index);
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

	void torrent::update_sparse_piece_prio(int i, int start, int end)
	{
		TORRENT_ASSERT(m_picker);
		if (m_picker->have_piece(i) || m_picker->piece_priority(i) == 0)
			return;
		bool have_before = i == 0 || m_picker->have_piece(i - 1);
		bool have_after = i == end - 1 || m_picker->have_piece(i + 1);
		if (have_after && have_before)
			m_picker->set_piece_priority(i, 7);
		else if (have_after || have_before)
			m_picker->set_piece_priority(i, 6);
	}

	void torrent::we_have(int index)
	{
		// update m_file_progress
		TORRENT_ASSERT(m_picker);
		TORRENT_ASSERT(!have_piece(index));
		TORRENT_ASSERT(!m_picker->have_piece(index));

		const int piece_size = m_torrent_file->piece_length();
		size_type off = size_type(index) * piece_size;
		file_storage::iterator f = m_torrent_file->files().file_at_offset(off);
		int size = m_torrent_file->piece_size(index);
		int file_index = f - m_torrent_file->files().begin();
		for (; size > 0; ++f, ++file_index)
		{
			size_type file_offset = off - f->offset;
			TORRENT_ASSERT(f != m_torrent_file->files().end());
			TORRENT_ASSERT(file_offset <= f->size);
			int add = (std::min)(f->size - file_offset, (size_type)size);
			m_file_progress[file_index] += add;

			TORRENT_ASSERT(m_file_progress[file_index]
				<= m_torrent_file->files().at(file_index).size);

			if (m_file_progress[file_index] >= m_torrent_file->files().at(file_index).size)
			{
				if (m_ses.m_alerts.should_post<piece_finished_alert>())
				{
					// this file just completed, post alert
					m_ses.m_alerts.post_alert(file_completed_alert(get_handle()
						, file_index));
				}
			}
			size -= add;
			off += add;
			TORRENT_ASSERT(size >= 0);
		}

		m_picker->we_have(index);
	}

	void torrent::piece_passed(int index)
	{
//		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < m_torrent_file->num_pieces());
#ifdef TORRENT_DEBUG
		// make sure all blocks were successfully written before we
		// declare the piece has "we have".
		piece_picker::downloading_piece dp;
		m_picker->piece_info(index, dp);
		int blocks_in_piece = m_picker->blocks_in_piece(index);
		TORRENT_ASSERT(dp.finished == blocks_in_piece);
		TORRENT_ASSERT(dp.writing == 0);
		TORRENT_ASSERT(dp.requested == 0);
		TORRENT_ASSERT(dp.index == index);
#endif

		if (m_ses.m_alerts.should_post<piece_finished_alert>())
		{
			m_ses.m_alerts.post_alert(piece_finished_alert(get_handle()
				, index));
		}

		remove_time_critical_piece(index, true);

		bool was_finished = m_picker->num_filtered() + num_have()
			== torrent_file().num_pieces();

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		we_have(index);

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
			int trust_points = p->trust_points;
			++trust_points;
			if (trust_points > 8) trust_points = 8;
			p->trust_points = trust_points;
			if (p->connection) p->connection->received_valid_data(index);
		}

		if (settings().max_sparse_regions > 0
			&& m_picker->sparse_regions() > settings().max_sparse_regions)
		{
			// we have too many sparse regions. Prioritize pieces
			// that won't introduce new sparse regions
			// prioritize pieces that will reduce the number of sparse
			// regions even higher
			int start = m_picker->cursor();
			int end = m_picker->reverse_cursor();
			if (index > start) update_sparse_piece_prio(index - 1, start, end);
			if (index < end - 1) update_sparse_piece_prio(index + 1, start, end);
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
					p->connection->disconnect(errors::too_many_corrupt_pieces);
				}
			}
		}

		// we have to let the piece_picker know that
		// this piece failed the check as it can restore it
		// and mark it as being interesting for download
		m_picker->restore_piece(index);

		// we might still have outstanding requests to this
		// piece that hasn't been received yet. If this is the
		// case, we need to re-open the piece and mark any
		// blocks we're still waiting for as requested
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
			std::vector<pending_block> const& dq = p->download_queue();
			std::vector<pending_block> const& rq = p->request_queue();
			for (std::vector<pending_block>::const_iterator k = dq.begin()
				, end(dq.end()); k != end; ++k)
			{
				if (k->timed_out || k->not_wanted) continue;
				if (k->block.piece_index != index) continue;
				m_picker->mark_as_downloading(k->block, p->peer_info_struct()
					, (piece_picker::piece_state_t)p->peer_speed());
			}
			for (std::vector<pending_block>::const_iterator k = rq.begin()
				, end(rq.end()); k != end; ++k)
			{
				if (k->block.piece_index != index) continue;
				m_picker->mark_as_downloading(k->block, p->peer_info_struct()
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
			(*(*i)->m_logger) << time_now_string() << " *** ABORTING TORRENT\n";
		}
#endif

		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all(errors::torrent_aborted);

		// post a message to the main thread to destruct
		// the torrent object from there
		if (m_owning_storage.get())
		{
			m_storage->abort_disk_io();
			m_storage->async_release_files(
				boost::bind(&torrent::on_torrent_aborted, shared_from_this(), _1, _2));
		}
		
		dequeue_torrent_check();

		if (m_state == torrent_status::checking_files)
			set_state(torrent_status::queued_for_checking);

		m_owning_storage = 0;
		m_host_resolver.cancel();
	}

	void torrent::super_seeding(bool on)
	{
		if (on == m_super_seeding) return;

		// don't turn on super seeding if we're not a seed
		TORRENT_ASSERT(!on || is_seed() || !m_files_checked);
		if (on && !is_seed() && m_files_checked) return;
		m_super_seeding = on;

		if (m_super_seeding) return;

		// disable super seeding for all peers
		for (peer_iterator i = begin(); i != end(); ++i)
		{
			(*i)->superseed_piece(-1);
		}
	}

	int torrent::get_piece_to_super_seed(bitfield const& bits)
	{
		// return a piece with low availability that is not in
		// the bitfield and that is not currently being super
		// seeded by any peer
		TORRENT_ASSERT(m_super_seeding);
		
		// do a linear search from the first piece
		int min_availability = 9999;
		std::vector<int> avail_vec;
		for (int i = 0; i < m_torrent_file->num_pieces(); ++i)
		{
			if (bits[i]) continue;

			int availability = 0;
			for (const_peer_iterator j = begin(); j != end(); ++j)
			{
				if ((*j)->superseed_piece() == i)
				{
					// avoid superseeding the same piece to more than one
					// peer if we can avoid it. Do this by artificially
					// increase the availability
					availability = 999;
					break;
				}
				if ((*j)->has_piece(i)) ++availability;
			}
			if (availability > min_availability) continue;
			if (availability == min_availability)
			{
				avail_vec.push_back(i);
				continue;
			}
			TORRENT_ASSERT(availability < min_availability);
			min_availability = availability;
			avail_vec.clear();
			avail_vec.push_back(i);
		}

		if (min_availability > 1)
		{
			// if the minimum availability is 2 or more,
			// we shouldn't be super seeding any more
			super_seeding(false);
			return -1;
		}

		return avail_vec[rand() % avail_vec.size()];
	}

	void torrent::on_files_deleted(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (ret != 0)
		{
			if (alerts().should_post<torrent_delete_failed_alert>())
				alerts().post_alert(torrent_delete_failed_alert(get_handle(), j.error));
		}
		else
		{
			if (alerts().should_post<torrent_deleted_alert>())
				alerts().post_alert(torrent_deleted_alert(get_handle(), m_torrent_file->info_hash()));
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

	void torrent::on_torrent_aborted(int ret, disk_io_job const& j)
	{
		// the torrent should be completely shut down now, and the
		// destructor has to be called from the main thread
	}

	void torrent::on_save_resume_data(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		if (!j.resume_data)
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle(), j.error));
		}
		else
		{
			write_resume_data(*j.resume_data);
			alerts().post_alert(save_resume_data_alert(j.resume_data
				, get_handle()));
		}
	}

	void torrent::on_file_renamed(int ret, disk_io_job const& j)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		if (ret == 0)
		{
			if (alerts().should_post<file_renamed_alert>())
				alerts().post_alert(file_renamed_alert(get_handle(), j.str, j.piece));
			m_torrent_file->rename_file(j.piece, j.str);
		}
		else
		{
			if (alerts().should_post<file_rename_failed_alert>())
				alerts().post_alert(file_rename_failed_alert(get_handle()
					, j.piece, j.error));
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

	void torrent::set_piece_deadline(int piece, int t, int flags)
	{
		ptime deadline = time_now() + milliseconds(t);

		if (is_seed() || m_picker->have_piece(piece))
		{
			if (flags & torrent_handle::alert_when_available)
				read_piece(piece);
			return;
		}

		for (std::list<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			if (i->piece != piece) continue;
			i->deadline = deadline;
			i->flags = flags;

			// resort i since deadline might have changed
			while (boost::next(i) != m_time_critical_pieces.end() && i->deadline > boost::next(i)->deadline)
			{
				std::iter_swap(i, boost::next(i));
				++i;
			}
			while (i != m_time_critical_pieces.begin() && i->deadline < boost::prior(i)->deadline)
			{
				std::iter_swap(i, boost::next(i));
				--i;
			}
			return;
		}

		time_critical_piece p;
		p.first_requested = min_time();
		p.last_requested = min_time();
		p.flags = flags;
		p.deadline = deadline;
		p.peers = 0;
		p.piece = piece;
		std::list<time_critical_piece>::iterator i = std::upper_bound(m_time_critical_pieces.begin()
			, m_time_critical_pieces.end(), p);
		m_time_critical_pieces.insert(i, p);
	}

	void torrent::remove_time_critical_piece(int piece, bool finished)
	{
		for (std::list<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			if (i->piece != piece) continue;
			if (finished)
			{
				if (i->flags & torrent_handle::alert_when_available)
				{
					read_piece(i->piece);
				}

				// update the average download time and average
				// download time deviation
				time_duration dl_time = time_now() - i->first_requested;

				if (m_average_piece_time == seconds(0))
				{
					m_average_piece_time = dl_time;
				}
				else
				{
					time_duration diff = dl_time > m_average_piece_time
						? dl_time - m_average_piece_time
						: m_average_piece_time - dl_time;
					if (m_piece_time_deviation == seconds(0)) m_piece_time_deviation = diff;
					else m_piece_time_deviation = (m_piece_time_deviation * 6 + diff * 4) / 10;

					m_average_piece_time = (m_average_piece_time * 6 + dl_time * 4) / 10;
				}
			}
			m_time_critical_pieces.erase(i);
			return;
		}
	}

	// remove time critical pieces where priority is 0
	void torrent::remove_time_critical_pieces(std::vector<int> const& priority)
	{
		for (std::list<time_critical_piece>::iterator i = m_time_critical_pieces.begin();
			i != m_time_critical_pieces.end();)
		{
			if (priority[i->piece] == 0)
			{
				i = m_time_critical_pieces.erase(i);
				continue;
			}
			++i;
		}
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
		if (index < 0 || index >= m_torrent_file->num_pieces()) return;

		bool was_finished = is_finished();
		bool filter_updated = m_picker->set_piece_priority(index, priority);
		TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		if (filter_updated)
		{
			update_peer_interest(was_finished);
			if (priority == 0) remove_time_critical_piece(index);
		}

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
		if (index < 0 || index >= m_torrent_file->num_pieces()) return 0;

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
		if (filter_updated)
		{
			update_peer_interest(was_finished);
			remove_time_critical_pieces(pieces);
		}
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

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		TORRENT_ASSERT(index < m_torrent_file->num_files());
		TORRENT_ASSERT(index >= 0);
		if (index < 0 || index >= m_torrent_file->num_files()) return;
		if (m_file_priority[index] == prio) return;
		m_file_priority[index] = prio;
		update_piece_priorities();
	}
	
	int torrent::file_priority(int index) const
	{
		// this call is only valid on torrents with metadata
		if (!valid_metadata()) return 1;

		TORRENT_ASSERT(index < m_torrent_file->num_files());
		TORRENT_ASSERT(index >= 0);
		if (index < 0 || index >= m_torrent_file->num_files()) return 0;
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
				, boost::bind(&set_if_greater, _1, m_file_priority[i]));
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

		if (index < 0 || index >= m_torrent_file->num_pieces()) return;

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

		if (index < 0 || index >= m_torrent_file->num_pieces()) return true;

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
		TORRENT_ASSERT(int(bitmask.size()) == m_torrent_file->num_files());

		if (int(bitmask.size()) != m_torrent_file->num_files()) return;
		
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

		m_last_working_tracker = -1;
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
			if (i->source == 0) i->source = announce_entry::source_client;

		if (m_settings.prefer_udp_trackers)
			prioritize_udp_trackers();

		if (!m_trackers.empty()) announce_with_tracker();
	}

	void torrent::prioritize_udp_trackers()
	{
		// look for udp-trackers
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (i->url.substr(0, 6) != "udp://") continue;
			// now, look for trackers with the same hostname
			// that is has higher priority than this one
			// if we find one, swap with the udp-tracker
			error_code ec;
			std::string udp_hostname;
			using boost::tuples::ignore;
			boost::tie(ignore, ignore, udp_hostname, ignore, ignore)
				= parse_url_components(i->url, ec);
			for (std::vector<announce_entry>::iterator j = m_trackers.begin();
				j != i; ++j)
			{
				std::string hostname;
				boost::tie(ignore, ignore, hostname, ignore, ignore)
					= parse_url_components(j->url, ec);
				if (hostname != udp_hostname) continue;
				if (j->url.substr(0, 6) == "udp://") continue;
				using std::swap;
				using std::iter_swap;
				swap(i->tier, j->tier);
				iter_swap(i, j);
				break;
			}
		}
	}

	void torrent::add_tracker(announce_entry const& url)
	{
		std::vector<announce_entry>::iterator k = std::find_if(m_trackers.begin()
			, m_trackers.end(), boost::bind(&announce_entry::url, _1) == url.url);
		if (k != m_trackers.end()) 
		{
			k->source |= url.source;
			return;
		}
		k = std::upper_bound(m_trackers.begin(), m_trackers.end(), url
			, boost::bind(&announce_entry::tier, _1) < boost::bind(&announce_entry::tier, _2));
		if (k - m_trackers.begin() < m_last_working_tracker) ++m_last_working_tracker;
		k = m_trackers.insert(k, url);
		if (k->source == 0) k->source = announce_entry::source_client;
		if (!m_trackers.empty()) announce_with_tracker();
	}

	bool torrent::choke_peer(peer_connection& c)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!c.is_choked());
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		TORRENT_ASSERT(m_num_uploads > 0);
		if (!c.send_choke()) return false;
		--m_num_uploads;
		return true;
	}
	
	bool torrent::unchoke_peer(peer_connection& c, bool optimistic)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(c.is_choked());
		TORRENT_ASSERT(!c.ignore_unchoke_slots());
		// when we're unchoking the optimistic slots, we might
		// exceed the limit temporarily while we're iterating
		// over the peers
		if (m_num_uploads >= m_max_uploads && !optimistic) return false;
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

		if (!p->is_choked() && !p->ignore_unchoke_slots())
		{
			--m_num_uploads;
			m_ses.m_unchoke_time_scaler = 0;
		}

		policy::peer* pp = p->peer_info_struct();
		if (pp)
		{
			if (pp->optimistically_unchoked)
				m_ses.m_optimistic_unchoke_time_scaler = 0;

			// if the share ratio is 0 (infinite), the
			// m_available_free_upload isn't used,
			// because it isn't necessary.
			if (ratio() != 0.f)
			{
				TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
				TORRENT_ASSERT(p->share_diff() < (std::numeric_limits<size_type>::max)());
				m_available_free_upload += p->share_diff();
			}
			TORRENT_ASSERT(pp->prev_amount_upload == 0);
			TORRENT_ASSERT(pp->prev_amount_download == 0);
			pp->prev_amount_download += p->statistics().total_payload_download();
			pp->prev_amount_upload += p->statistics().total_payload_upload();
		}

		m_policy.connection_closed(*p, m_ses.session_time());
		p->set_peer_info(0);
		TORRENT_ASSERT(i != m_connections.end());
		m_connections.erase(i);
	}

	void torrent::connect_to_url_seed(web_seed_entry const& web)
	{
		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " resolving web seed: " << web.url << "\n";
#endif

		std::string protocol;
		std::string auth;
		std::string hostname;
		int port;
		std::string path;
		error_code ec;
		boost::tie(protocol, auth, hostname, port, path)
			= parse_url_components(web.url, ec);

		if (ec)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
			(*m_ses.m_logger) << time_now_string() << " failed to parse web seed url: " << ec.message() << "\n";
#endif
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, ec));
			}
			// never try it again
			m_web_seeds.erase(web);
			return;
		}
		
		if (protocol != "http")
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, errors::unsupported_url_protocol));
			}
			// never try it again
			m_web_seeds.erase(web);
			return;
		}

		if (hostname.empty())
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, errors::invalid_hostname));
			}
			// never try it again
			m_web_seeds.erase(web);
			return;
		}

		if (port == 0)
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, errors::invalid_port));
			}
			// never try it again
			m_web_seeds.erase(web);
			return;
		}

		m_resolving_web_seeds.insert(web);
		proxy_settings const& ps = m_ses.web_seed_proxy();
		if (ps.type == proxy_settings::http
			|| ps.type == proxy_settings::http_pw)
		{
			// use proxy
			tcp::resolver::query q(ps.hostname, to_string(ps.port).elems);
			m_host_resolver.async_resolve(q,
				boost::bind(&torrent::on_proxy_name_lookup, shared_from_this(), _1, _2, web));
		}
		else
		{
			if (m_ses.m_port_filter.access(port) & port_filter::blocked)
			{
				if (m_ses.m_alerts.should_post<url_seed_alert>())
				{
					m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, errors::port_blocked));
				}
				// never try it again
				m_web_seeds.erase(web);
				return;
			}

			tcp::resolver::query q(hostname, to_string(port).elems);
			m_host_resolver.async_resolve(q,
				boost::bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, web
					, tcp::endpoint()));
		}

	}

	void torrent::on_proxy_name_lookup(error_code const& e, tcp::resolver::iterator host
		, web_seed_entry web)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " completed resolve proxy hostname for: " << web.url << "\n";
#endif

		if (m_abort) return;

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, e));
			}

			// the name lookup failed for the http host. Don't try
			// this host again
			m_web_seeds.erase(web);
			return;
		}

		if (m_ses.is_aborted()) return;

		tcp::endpoint a(host->endpoint());

		using boost::tuples::ignore;
		std::string hostname;
		int port;
		error_code ec;
		boost::tie(ignore, ignore, hostname, port, ignore)
			= parse_url_components(web.url, ec);

		if (ec)
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(
					url_seed_alert(get_handle(), web.url, ec));
			}
			m_web_seeds.erase(web);
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
			boost::bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, web, a));
	}

	void torrent::on_name_lookup(error_code const& e, tcp::resolver::iterator host
		, web_seed_entry web, tcp::endpoint proxy)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " completed resolve: " << web.url << "\n";
#endif

		if (m_abort) return;

		std::set<web_seed_entry>::iterator i = m_resolving_web_seeds.find(web);
		if (i != m_resolving_web_seeds.end()) m_resolving_web_seeds.erase(i);

		if (is_paused())
		{
			m_web_seeds.insert(web);
			return;
		}

		if (e || host == tcp::resolver::iterator())
		{
			if (m_ses.m_alerts.should_post<url_seed_alert>())
			{
				m_ses.m_alerts.post_alert(url_seed_alert(get_handle(), web.url, e));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << web.url
				<< " " << e.message() << "\n";
#endif

			// unavailable, retry in 30 minutes
			retry_web_seed(web.url, web.type, 60 * 30);
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
			s->get<http_stream>()->set_no_connect(true);
		}

		boost::intrusive_ptr<peer_connection> c;
		if (web.type == web_seed_entry::url_seed)
		{
			c = new (std::nothrow) web_peer_connection(
				m_ses, shared_from_this(), s, a, web.url, 0);
		}
		else if (web.type == web_seed_entry::http_seed)
		{
			c = new (std::nothrow) http_seed_connection(
				m_ses, shared_from_this(), s, a, web.url, 0);
		}
		if (!c) return;
			
#ifdef TORRENT_DEBUG
		c->m_in_constructor = false;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			boost::shared_ptr<peer_plugin>
				pp((*i)->new_connection(c.get()));
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

#if defined TORRENT_VERBOSE_LOGGING 
			(*m_ses.m_logger) << time_now_string() << " web seed connection started " << web.url << "\n";
#endif
			m_ses.m_half_open.enqueue(
				boost::bind(&peer_connection::on_connect, c, _1)
				, boost::bind(&peer_connection::on_timeout, c)
				, seconds(settings().peer_connect_timeout));
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << e.what() << "\n";
#endif
			c->disconnect(errors::no_error, 1);
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
			|| is_local(p->remote().address())
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
			boost::bind(&torrent::on_country_lookup, shared_from_this(), _1, _2, p));
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
					, boost::bind(&country_entry::code, _1) < boost::bind(&country_entry::code, _2));
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
		m_finished_time = seconds(rd.dict_find_int_value("finished_time"));
		m_seeding_time = seconds(rd.dict_find_int_value("seeding_time"));
		m_complete = rd.dict_find_int_value("num_seeds", -1);
		m_incomplete = rd.dict_find_int_value("num_downloaders", -1);
		set_upload_limit(rd.dict_find_int_value("upload_rate_limit", -1));
		set_download_limit(rd.dict_find_int_value("download_rate_limit", -1));
		set_max_connections(rd.dict_find_int_value("max_connections", -1));
		set_max_uploads(rd.dict_find_int_value("max_uploads", -1));
		m_seed_mode = rd.dict_find_int_value("seed_mode", 0) && m_torrent_file->is_valid();
		if (m_seed_mode) m_verified.resize(m_torrent_file->num_pieces(), false);
		super_seeding(rd.dict_find_int_value("super_seeding", 0));

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
			m_policy.recalculate_connect_candidates();
		}

		if (!m_override_resume_data)
		{
			int auto_managed_ = rd.dict_find_int_value("auto_managed", -1);
			if (auto_managed_ != -1) m_auto_managed = auto_managed_;
		}

		int sequential_ = rd.dict_find_int_value("sequential_download", -1);
		if (sequential_ != -1) set_sequential_download(sequential_);

		if (!m_override_resume_data)
		{
			int paused_ = rd.dict_find_int_value("paused", -1);
			if (paused_ != -1) m_paused = paused_;
		}

		lazy_entry const* trackers = rd.dict_find_list("trackers");
		if (trackers)
		{
			m_trackers.clear();
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
					e.fail_limit = 0;
					m_trackers.push_back(e);
				}
				++tier;
			}
			std::sort(m_trackers.begin(), m_trackers.end(), boost::bind(&announce_entry::tier, _1)
				< boost::bind(&announce_entry::tier, _2));

			if (m_settings.prefer_udp_trackers)
				prioritize_udp_trackers();
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
				add_web_seed(url, web_seed_entry::url_seed);
			}
		}

		lazy_entry const* httpseeds = rd.dict_find_list("httpseeds");
		if (httpseeds)
		{
			for (int i = 0; i < httpseeds->list_size(); ++i)
			{
				std::string url = httpseeds->list_string_value_at(i);
				if (url.empty()) continue;
				add_web_seed(url, web_seed_entry::http_seed);
			}
		}

		if (m_torrent_file->is_merkle_torrent())
		{
			lazy_entry const* mt = rd.dict_find_string("merkle tree");
			if (mt)
			{
				std::vector<sha1_hash> tree;
				tree.resize(m_torrent_file->merkle_tree().size());
				std::memcpy(&tree[0], mt->string_ptr()
					, (std::min)(mt->string_length(), int(tree.size()) * 20));
				if (mt->string_length() < int(tree.size()) * 20)
					std::memset(&tree[0] + mt->string_length() / 20, 0
						, tree.size() - mt->string_length() / 20);
				m_torrent_file->set_merkle_tree(tree);
			}
			else
			{
				// TODO: if this is a merkle torrent and we can't
				// restore the tree, we need to wipe all the
				// bits in the have array, but not necessarily
				// we might want to do a full check to see if we have
				// all the pieces
				TORRENT_ASSERT(false);
			}
		}
	}
	
	void torrent::write_resume_data(entry& ret) const
	{
		using namespace libtorrent::detail; // for write_*_endpoint()
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;
		ret["libtorrent-version"] = LIBTORRENT_VERSION;

		ret["total_uploaded"] = m_total_uploaded;
		ret["total_downloaded"] = m_total_downloaded;

		ret["active_time"] = total_seconds(m_active_time);
		ret["finished_time"] = total_seconds(m_finished_time);
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

		ret["seed_mode"] = m_seed_mode;
		ret["super_seeding"] = m_super_seeding;
		
		const sha1_hash& info_hash = torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

		// blocks per piece
		int num_blocks_per_piece =
			static_cast<int>(torrent_file().piece_length()) / block_size();
		ret["blocks per piece"] = num_blocks_per_piece;

		if (m_torrent_file->is_merkle_torrent())
		{
			// we need to save the whole merkle hash tree
			// in order to resume
			std::string& tree_str = ret["merkle tree"].string();
			std::vector<sha1_hash> const& tree = m_torrent_file->merkle_tree();
			tree_str.resize(tree.size() * 20);
			std::memcpy(&tree_str[0], &tree[0], tree.size() * 20);
		}

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
				// don't save trackers we can't trust
				// TODO: save the send_stats state instead
				if (i->send_stats == false) continue;
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
			for (std::set<web_seed_entry>::const_iterator i = m_web_seeds.begin()
				, end(m_web_seeds.end()); i != end; ++i)
			{
				if (i->type != web_seed_entry::url_seed) continue;
				url_list.push_back(i->url);
			}

			entry::list_type& httpseed_list = ret["httpseeds"].list();
			for (std::set<web_seed_entry>::const_iterator i = m_web_seeds.begin()
				, end(m_web_seeds.end()); i != end; ++i)
			{
				if (i->type != web_seed_entry::http_seed) continue;
				httpseed_list.push_back(i->url);
			}
		}

		// write have bitmask
		// the pieces string has one byte per piece. Each
		// byte is a bitmask representing different properties
		// for the piece
		// bit 0: set if we have the piece
		// bit 1: set if we have verified the piece (in seed mode)
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

		if (m_seed_mode)
		{
			TORRENT_ASSERT(m_verified.size() == pieces.size());
			for (int i = 0, end(pieces.size()); i < end; ++i)
				pieces[i] |= m_verified[i] ? 2 : 0;
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

		std::back_insert_iterator<entry::string_type> peers(ret["peers"].string());
		std::back_insert_iterator<entry::string_type> banned_peers(ret["banned_peers"].string());
#if TORRENT_USE_IPV6
		std::back_insert_iterator<entry::string_type> peers6(ret["peers6"].string());
		std::back_insert_iterator<entry::string_type> banned_peers6(ret["banned_peers6"].string());
#endif

		// failcount is a 5 bit value
		int max_failcount = (std::min)(m_ses.m_settings.max_failcount, 31);

		for (policy::const_iterator i = m_policy.begin_peer()
			, end(m_policy.end_peer()); i != end; ++i)
		{
			error_code ec;
			policy::peer const* p = *i;
			address addr = p->address();
			if (p->banned)
			{
#if TORRENT_USE_IPV6
				if (addr.is_v6())
					write_endpoint(tcp::endpoint(addr, p->port), banned_peers6);
				else
#endif
					write_endpoint(tcp::endpoint(addr, p->port), banned_peers);
				continue;
			}

			// we cannot save remote connection
			// since we don't know their listen port
			// unless they gave us their listen port
			// through the extension handshake
			// so, if the peer is not connectable (i.e. we
			// don't know its listen port) or if it has
			// been banned, don't save it.
			if (!p->connectable) continue;

			// don't save peers that don't work
			if (int(p->failcount) >= max_failcount) continue;

#if TORRENT_USE_IPV6
			if (addr.is_v6())
				write_endpoint(tcp::endpoint(addr, p->port), peers6);
			else
#endif
				write_endpoint(tcp::endpoint(addr, p->port), peers);
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
			e.ip = (*i)->ip();
			e.flags = (*i)->banned ? peer_list_entry::banned : 0;
			e.failcount = (*i)->failcount;
			e.source = (*i)->source;
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
		std::vector<block_info>& blk = m_ses.m_block_info_storage;
		blk.clear();

		if (!valid_metadata() || is_seed()) return;
		piece_picker const& p = picker();
		std::vector<piece_picker::downloading_piece> const& q
			= p.get_download_queue();

		const int blocks_per_piece = m_picker->blocks_in_piece(0);
		blk.resize(q.size() * blocks_per_piece);

		int counter = 0;
		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin(); i != q.end(); ++i, ++counter)
		{
			partial_piece_info pi;
			pi.piece_state = (partial_piece_info::state_t)i->state;
			pi.blocks_in_piece = p.blocks_in_piece(i->index);
			pi.finished = (int)i->finished;
			pi.writing = (int)i->writing;
			pi.requested = (int)i->requested;
			pi.blocks = &blk[counter * blocks_per_piece];
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
					bi.set_peer(tcp::endpoint());
					bi.bytes_progress = complete ? bi.block_size : 0;
				}
				else
				{
					policy::peer* p = static_cast<policy::peer*>(i->info[j].peer);
					if (p->connection)
					{
						bi.set_peer(p->connection->remote());
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
						bi.set_peer(p->ip());
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

		peerinfo->last_connected = m_ses.session_time();
#ifdef TORRENT_DEBUG
		// this asserts that we don't have duplicates in the policy's peer list
		peer_iterator i_ = std::find_if(m_connections.begin(), m_connections.end()
			, boost::bind(&peer_connection::remote, _1) == peerinfo->ip());
		TORRENT_ASSERT(i_ == m_connections.end()
			|| dynamic_cast<bt_peer_connection*>(*i_) == 0);
#endif

		TORRENT_ASSERT(want_more_peers());
		TORRENT_ASSERT(m_ses.num_connections() < m_ses.max_connections());

		tcp::endpoint a(peerinfo->ip());
		TORRENT_ASSERT((m_ses.m_ip_filter.access(peerinfo->address()) & ip_filter::blocked) == 0);

		boost::shared_ptr<socket_type> s(new socket_type(m_ses.m_io_service));

		bool ret = instantiate_connection(m_ses.m_io_service, m_ses.peer_proxy(), *s);
		(void)ret;
		TORRENT_ASSERT(ret);

		m_ses.setup_socket_buffers(*s);

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
		m_policy.set_connection(peerinfo, c.get());
		c->start();

		int timeout = settings().peer_connect_timeout;
		if (peerinfo) timeout += 3 * peerinfo->failcount;

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			m_ses.m_half_open.enqueue(
				boost::bind(&peer_connection::on_connect, c, _1)
				, boost::bind(&peer_connection::on_timeout, c)
				, seconds(timeout));
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
			std::set<peer_connection*>::iterator i
				= m_connections.find(boost::get_pointer(c));
			if (i != m_connections.end()) m_connections.erase(i);
			c->disconnect(errors::no_error, 1);
			return false;
		}
#endif

		return peerinfo->connection;
	}

	bool torrent::set_metadata(char const* metadata_buf, int metadata_size)
	{
		INVARIANT_CHECK;

		if (m_torrent_file->is_valid()) return false;

		hasher h;
		h.update(metadata_buf, metadata_size);
		sha1_hash info_hash = h.final();

		if (info_hash != m_torrent_file->info_hash())
		{
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().post_alert(metadata_failed_alert(get_handle()));
			}
			return false;
		}

		lazy_entry metadata;
		int ret = lazy_bdecode(metadata_buf, metadata_buf + metadata_size, metadata);
		error_code ec;
		if (ret != 0 || !m_torrent_file->parse_info_section(metadata, ec))
		{
			// this means the metadata is correct, since we
			// verified it against the info-hash, but we
			// failed to parse it. Pause the torrent
			if (alerts().should_post<metadata_failed_alert>())
			{
				alerts().post_alert(metadata_failed_alert(get_handle()));
			}
			set_error(errors::invalid_swarm_metadata, "");
			pause();
			return false;
		}

		if (m_ses.m_alerts.should_post<metadata_received_alert>())
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle()));
		}

		// this makes the resume data "paused" and
		// "auto_managed" fields be ignored. If the paused
		// field is not ignored, the invariant check will fail
		// since we will be paused but without having disconnected
		// any of the peers.
		m_override_resume_data = true;

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
			p->disconnect(errors::torrent_not_ready);
			return false;
		}
		
		if (m_ses.m_connections.find(p) == m_ses.m_connections.end())
		{
			p->disconnect(errors::peer_not_constructed);
			return false;
		}

		if (m_ses.is_aborted())
		{
			p->disconnect(errors::session_closing);
			return false;
		}

		if (int(m_connections.size()) >= m_max_connections)
		{
			p->disconnect(errors::too_many_connections);
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
			if (!m_policy.new_connection(*p, m_ses.session_time()))
				return false;
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
#if defined TORRENT_LOGGING
			(*m_ses.m_logger) << time_now_string() << " CLOSING CONNECTION "
				<< p->remote() << " policy::new_connection threw: " << e.what() << "\n";
#endif
			p->disconnect(errors::no_error);
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
			&& ((m_state != torrent_status::checking_files
			&& m_state != torrent_status::checking_resume_data
			&& m_state != torrent_status::queued_for_checking)
				|| !valid_metadata())
			&& m_policy.num_connect_candidates() > 0
			&& !m_abort;
	}

	void torrent::disconnect_all(error_code const& ec)
	{
// doesn't work with the m_paused -> m_num_peers == 0 condition
//		INVARIANT_CHECK;

		while (!m_connections.empty())
		{
			peer_connection* p = *m_connections.begin();
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*p->m_logger) << "*** CLOSING CONNECTION: " << ec.message() << "\n";
#endif
#ifdef TORRENT_DEBUG
			std::size_t size = m_connections.size();
#endif
			if (p->is_disconnecting())
				m_connections.erase(m_connections.begin());
			else
				p->disconnect(ec);
			TORRENT_ASSERT(m_connections.size() <= size);
		}
	}

	namespace
	{
		// this returns true if lhs is a better disconnect candidate than rhs
		bool compare_disconnect_peer(peer_connection const* lhs, peer_connection const* rhs)
		{
			// prefer to disconnect peers that are already disconnecting
			if (lhs->is_disconnecting() != rhs->is_disconnecting())
				return lhs->is_disconnecting();

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

			ptime now = time_now();
			size_type lhs_time_connected = total_seconds(now - lhs->connected_time());
			size_type rhs_time_connected = total_seconds(now - rhs->connected_time());

			lhs_transferred /= lhs_time_connected + 1;
			rhs_transferred /= (rhs_time_connected + 1);
			if (lhs_transferred != rhs_transferred)	
				return lhs_transferred < rhs_transferred;

			// prefer to disconnect peers that chokes us
			if (lhs->is_choked() != rhs->is_choked())
				return lhs->is_choked();

			return lhs->last_received() < rhs->last_received();
		}
	}

	int torrent::disconnect_peers(int num)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
		for (std::set<peer_connection*>::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
		}
#endif
		int ret = 0;
		while (ret < num && !m_connections.empty())
		{
			std::set<peer_connection*>::iterator i = std::min_element(
				m_connections.begin(), m_connections.end(), compare_disconnect_peer);

			peer_connection* p = *i;
			++ret;
			TORRENT_ASSERT(p->associated_torrent().lock().get() == this);
#ifdef TORRENT_DEBUG
			int num_conns = m_connections.size();
#endif
			p->disconnect(errors::optimistic_disconnect);
			TORRENT_ASSERT(int(m_connections.size()) == num_conns - 1);
		}

		return ret;
	}

	int torrent::bandwidth_throttle(int channel) const
	{
		return m_bandwidth_channel[channel].throttle();
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

		send_upload_only();

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
			, boost::bind(&peer_connection::disconnect, _1, errors::torrent_finished, 0));

		if (m_abort) return;

		m_policy.recalculate_connect_candidates();

		TORRENT_ASSERT(m_storage);
		// we need to keep the object alive during this operation
		m_storage->async_release_files(
			boost::bind(&torrent::on_files_released, shared_from_this(), _1, _2));
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

		send_upload_only();
	}

	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
		m_picker.reset();

		set_state(torrent_status::seeding);
		if (!m_announcing) return;

		ptime now = time_now();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (i->complete_sent) continue;
			i->next_announce = now;
			i->min_announce = now;
		}
		announce_with_tracker();
	}

	// this will move the tracker with the given index
	// to a prioritized position in the list (move it towards
	// the begining) and return the new index to the tracker.
	int torrent::prioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_trackers.size()));
		if (index >= (int)m_trackers.size()) return -1;

		while (index > 0 && m_trackers[index].tier == m_trackers[index-1].tier)
		{
			using std::swap;
			swap(m_trackers[index], m_trackers[index-1]);
			if (m_last_working_tracker == index) --m_last_working_tracker;
			else if (m_last_working_tracker == index - 1) ++m_last_working_tracker;
			--index;
		}
		return index;
	}

	int torrent::deprioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_trackers.size()));
		if (index >= (int)m_trackers.size()) return -1;

		while (index < int(m_trackers.size()) - 1 && m_trackers[index].tier == m_trackers[index + 1].tier)
		{
			using std::swap;
			swap(m_trackers[index], m_trackers[index + 1]);
			if (m_last_working_tracker == index) ++m_last_working_tracker;
			else if (m_last_working_tracker == index + 1) --m_last_working_tracker;
			++index;
		}
		return index;
	}

	void torrent::files_checked_lock()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		files_checked(l);
	}

	void torrent::files_checked(session_impl::mutex_t::scoped_lock const& l)
	{
		TORRENT_ASSERT(m_torrent_file->is_valid());

		if (m_abort) return;

		// we might be finished already, in which case we should
		// not switch to downloading mode. If all files are
		// filtered, we're finished when we start.
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
			// turn off super seeding if we're not a seed
			if (m_super_seeding) m_super_seeding = false;

			// if we just finished checking and we're not a seed, we are
			// likely to be unpaused
			if (m_ses.m_auto_manage_time_scaler > 1)
				m_ses.m_auto_manage_time_scaler = 1;

			if (is_finished() && m_state != torrent_status::finished)
				finished();
		}
		else
		{
			for (std::vector<announce_entry>::iterator i = m_trackers.begin()
				, end(m_trackers.end()); i != end; ++i)
				i->complete_sent = true;

			if (m_state != torrent_status::finished)
				finished();
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
			, boost::bind(&torrent::on_file_renamed, shared_from_this(), _1, _2));
		return true;
	}

	void torrent::move_storage(fs::path const& save_path)
	{
		INVARIANT_CHECK;

		if (m_owning_storage.get())
		{
			m_owning_storage->async_move_storage(save_path
				, boost::bind(&torrent::on_storage_moved, shared_from_this(), _1, _2));
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
			// checking to complete.
			TORRENT_ASSERT(found_active >= 1);
			TORRENT_ASSERT(found_active <= 2);
			TORRENT_ASSERT(found >= 1);
		}

		TORRENT_ASSERT(m_resume_entry.type() == lazy_entry::dict_t
			|| m_resume_entry.type() == lazy_entry::none_t);

		int num_uploads = 0;
		std::map<piece_block, int> num_requests;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			// make sure this peer is not a dangling pointer
			TORRENT_ASSERT(m_ses.has_peer(*i));
#endif
			peer_connection const& p = *(*i);
			for (std::vector<pending_block>::const_iterator i = p.request_queue().begin()
				, end(p.request_queue().end()); i != end; ++i)
				++num_requests[i->block];
			for (std::vector<pending_block>::const_iterator i = p.download_queue().begin()
				, end(p.download_queue().end()); i != end; ++i)
				if (!i->not_wanted && !i->timed_out) ++num_requests[i->block];
			if (!p.is_choked() && !p.ignore_unchoke_slots()) ++num_uploads;
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
				piece_block b = i->first;
				int count = i->second;
				int picker_count = m_picker->num_peers(b);
				if (!m_picker->is_downloaded(b))
					TORRENT_ASSERT(picker_count == count);
			}
			TORRENT_ASSERT(num_have() >= m_picker->num_have_filtered());
		}

		if (valid_metadata())
		{
			TORRENT_ASSERT(m_abort || m_error || !m_picker || m_picker->num_pieces() == m_torrent_file->num_pieces());
		}
		else
		{
			TORRENT_ASSERT(m_abort || m_error || !m_picker || m_picker->num_pieces() == 0);
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		// make sure we haven't modified the peer object
		// in a way that breaks the sort order
		if (m_policy.begin_peer() != m_policy.end_peer())
		{
			policy::const_iterator i = m_policy.begin_peer();
			policy::const_iterator prev = i++;
			policy::const_iterator end(m_policy.end_peer());
			policy::peer_address_compare cmp;
			for (; i != end; ++i, ++prev)
			{
				TORRENT_ASSERT(!cmp(*i, *prev));
			}
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
			
		if (m_files_checked && valid_metadata())
		{
			TORRENT_ASSERT(m_block_size > 0);
		}
//		if (is_seed()) TORRENT_ASSERT(m_picker.get() == 0);


		for (std::vector<size_type>::const_iterator i = m_file_progress.begin()
			, end(m_file_progress.end()); i != end; ++i)
		{
			int index = i - m_file_progress.begin();
			TORRENT_ASSERT(*i <= m_torrent_file->files().at(index).size);
		}
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
		const_peer_iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, boost::bind(&peer_connection::remote, _1) == ip);
		if (i == m_connections.end()) return;
		(*i)->set_upload_limit(limit);
	}

	void torrent::set_peer_download_limit(tcp::endpoint ip, int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		const_peer_iterator i = std::find_if(m_connections.begin(), m_connections.end()
			, boost::bind(&peer_connection::remote, _1) == ip);
		if (i == m_connections.end()) return;
		(*i)->set_download_limit(limit);
	}

	void torrent::set_upload_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = 0;
		m_bandwidth_channel[peer_connection::upload_channel].throttle(limit);
	}

	int torrent::upload_limit() const
	{
		int limit = m_bandwidth_channel[peer_connection::upload_channel].throttle();
		if (limit == (std::numeric_limits<int>::max)()) limit = -1;
		return limit;
	}

	void torrent::set_download_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit <= 0) limit = 0;
		m_bandwidth_channel[peer_connection::download_channel].throttle(limit);
	}

	int torrent::download_limit() const
	{
		int limit = m_bandwidth_channel[peer_connection::download_channel].throttle();
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

		disconnect_all(errors::torrent_removed);
		stop_announcing();

		if (m_owning_storage.get())
		{
			TORRENT_ASSERT(m_storage);
			m_storage->async_delete_files(
				boost::bind(&torrent::on_files_deleted, shared_from_this(), _1, _2));
		}
	}

	void torrent::clear_error()
	{
		if (!m_error) return;
		bool checking_files = should_check_files();
		if (m_ses.m_auto_manage_time_scaler > 2)
			m_ses.m_auto_manage_time_scaler = 2;
		m_error = error_code();
		m_error_file.clear();
		// if the error happened during initialization, try again now
		if (!m_storage) init();
		if (!checking_files && should_check_files())
			queue_torrent_check();
	}

	void torrent::set_error(error_code const& ec, std::string const& error_file)
	{
		bool checking_files = should_check_files();
		m_error = ec;
		m_error_file = error_file;
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

		if (!is_finished()) return 0;

		int scale = 100;
		if (!is_seed()) scale = 50;

		int ret = 0;

		ptime now = time_now();

		int finished_time = total_seconds(m_finished_time);
		int download_time = total_seconds(m_active_time) - finished_time;

		// if we haven't yet met the seed limits, set the seed_ratio_not_met
		// flag. That will make this seed prioritized
		// downloaded may be 0 if the torrent is 0-sized
		size_type downloaded = (std::max)(m_total_downloaded, m_torrent_file->total_size());
		if (finished_time < s.seed_time_limit
			&& (download_time > 1 && finished_time / download_time < s.seed_time_ratio_limit)
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
			ret |= (downloaders * scale / seeds) & prio_mask;
		}

		return ret;
	}

	// this is an async operation triggered by the client	
	void torrent::save_resume_data()
	{
		INVARIANT_CHECK;
	
		if (!m_owning_storage.get())
		{
			alerts().post_alert(save_resume_data_failed_alert(get_handle()
				, errors::destructing_torrent));
			return;
		}

		TORRENT_ASSERT(m_storage);
		if (m_state == torrent_status::queued_for_checking
			|| m_state == torrent_status::checking_files
			|| m_state == torrent_status::checking_resume_data)
		{
			boost::shared_ptr<entry> rd(new entry);
			write_resume_data(*rd);
			alerts().post_alert(save_resume_data_alert(rd
				, get_handle()));
			return;
		}
		m_storage->async_save_resume_data(
			boost::bind(&torrent::on_save_resume_data, shared_from_this(), _1, _2));
	}
	
	bool torrent::should_check_files() const
	{
		return (m_state == torrent_status::checking_files
			|| m_state == torrent_status::queued_for_checking)
			&& (!m_paused || m_auto_managed)
			&& !has_error()
			&& !m_abort;
	}

	void torrent::flush_cache()
	{
		m_storage->async_release_files(
			boost::bind(&torrent::on_cache_flushed, shared_from_this(), _1, _2));
	}

	void torrent::on_cache_flushed(int ret, disk_io_job const& j)
	{
		if (alerts().should_post<cache_flushed_alert>())
			alerts().post_alert(cache_flushed_alert(get_handle()));
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
				boost::bind(&torrent::on_torrent_paused, shared_from_this(), _1, _2));
			m_storage->async_clear_read_cache();
		}
		else
		{
			if (alerts().should_post<torrent_paused_alert>())
				alerts().post_alert(torrent_paused_alert(get_handle()));
		}

		disconnect_all(errors::torrent_paused);
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
		clear_error();
		start_announcing();
	}

	void torrent::update_tracker_timer(ptime now)
	{
		if (!m_announcing) return;

		ptime next_announce = max_time();
		int tier = INT_MAX;

		bool found_working = false;

		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			if (m_settings.announce_to_all_tiers
				&& found_working
				&& i->tier <= tier
				&& tier != INT_MAX)
				continue;

			if (i->tier > tier && !m_settings.announce_to_all_tiers) break;
			if (i->is_working()) { tier = i->tier; found_working = false; }
			if (i->fails >= i->fail_limit && i->fail_limit != 0) continue;
			if (i->updating) { found_working = true; continue; }
			ptime next_tracker_announce = (std::max)(i->next_announce, i->min_announce);
			if (!i->updating
				&& next_tracker_announce < next_announce
				&& (!found_working || i->is_working()))
				next_announce = next_tracker_announce;
			if (i->is_working()) found_working = true;
			if (!m_settings.announce_to_all_trackers
				&& !m_settings.announce_to_all_tiers) break;
		}

		if (next_announce <= now) return;

		m_waiting_tracker = true;
		error_code ec;
		boost::weak_ptr<torrent> self(shared_from_this());

		// since we don't know if we have to re-issue the async_wait or not
		// always do it
//		if (m_tracker_timer.expires_at() <= next_announce) return;

		m_tracker_timer.expires_at(next_announce, ec);
		m_tracker_timer.async_wait(boost::bind(&torrent::on_tracker_announce_disp, self, _1));
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
			std::for_each(m_trackers.begin(), m_trackers.end()
				, boost::bind(&announce_entry::reset, _1));
		}

		// reset the stats, since from the tracker's
		// point of view, this is a new session
		m_total_failed_bytes = 0;
		m_total_redundant_bytes = 0;
		m_stat.clear();

		announce_with_tracker();

		// private torrents are never announced on LSD
		// or on DHT, we don't need this timer.
		if (!m_torrent_file->is_valid() || !m_torrent_file->priv())
		{
			if (m_ses.m_lsd) lsd_announce();

#ifndef TORRENT_DISABLE_DHT
			error_code ec;
			m_dht_announce_timer.expires_from_now(seconds(1), ec);
			m_dht_announce_timer.async_wait(
				boost::bind(&torrent::on_dht_announce, shared_from_this(), _1));
#endif
		}
	}

	void torrent::stop_announcing()
	{
		if (!m_announcing) return;

		error_code ec;
#ifndef TORRENT_DISABLE_DHT
		m_dht_announce_timer.cancel(ec);
#endif
		if (m_ses.m_lsd)
			m_ses.m_lsd_announce_timer.cancel(ec);
		m_tracker_timer.cancel(ec);

		m_announcing = false;

		ptime now = time_now();
		for (std::vector<announce_entry>::iterator i = m_trackers.begin()
			, end(m_trackers.end()); i != end; ++i)
		{
			i->next_announce = now;
			i->min_announce = now;
		}
		announce_with_tracker(tracker_request::stopped);
	}

	void torrent::second_tick(stat& accumulator, int tick_interval_ms)
	{
		INVARIANT_CHECK;

		ptime now = time_now();

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

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;

			if (settings().max_sparse_regions > 0
				&& m_picker
				&& m_picker->sparse_regions() > settings().max_sparse_regions)
			{
				// we have too many sparse regions. Prioritize pieces
				// that won't introduce new sparse regions
				// prioritize pieces that will reduce the number of sparse
				// regions even higher
				int start = m_picker->cursor();
				int end = m_picker->reverse_cursor();
				for (int i = start; i < end; ++i)
					update_sparse_piece_prio(i, start, end);
			}

			// ------------------------
			// upload shift
			// ------------------------

			// this part will shift downloads
			// from peers that are seeds and peers
			// that don't want to download from us
			// to peers that cannot upload anything
			// to us. The shifting will make sure
			// that the torrent's share ratio
			// will be maintained

			// if the share ratio is 0 (infinite)
			// m_available_free_upload isn't used
			// because it isn't necessary
			if (ratio() != 0.f)
			{
				// accumulate all the free download we get
				// and add it to the available free upload
				m_available_free_upload += collect_free_download(
					this->begin(), this->end());

				// distribute the free upload among the peers
				m_available_free_upload = distribute_free_upload(
					this->begin(), this->end(), m_available_free_upload);
			}

			m_policy.pulse();
		}

		// if we're in upload only mode and we're auto-managed
		// leave upload mode every 10 minutes hoping that the error
		// condition has been fixed
		if (m_upload_mode && m_auto_managed && now - m_upload_mode_time
			> seconds(m_settings.optimistic_disk_retry))
		{
			set_upload_mode(false);
		}

		if (is_paused())
		{
			// let the stats fade out to 0
			accumulator += m_stat;
 			m_stat.second_tick(tick_interval_ms);
			return;
		}

		if (m_settings.rate_limit_ip_overhead)
		{
			int up_limit = m_bandwidth_channel[peer_connection::upload_channel].throttle();
			int down_limit = m_bandwidth_channel[peer_connection::download_channel].throttle();

			if (down_limit > 0
				&& m_stat.download_ip_overhead() >= down_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().post_alert(performance_alert(get_handle()
					, performance_alert::download_limit_too_low));
			}

			if (up_limit > 0
				&& m_stat.upload_ip_overhead() >= up_limit
				&& alerts().should_post<performance_alert>())
			{
				alerts().post_alert(performance_alert(get_handle()
					, performance_alert::upload_limit_too_low));
			}
		}

		time_duration since_last_tick = milliseconds(tick_interval_ms);
		if (is_seed()) m_seeding_time += since_last_tick;
		if (is_finished()) m_finished_time += since_last_tick;
		m_active_time += since_last_tick;

		// ---- TIME CRITICAL PIECES ----

		if (!m_time_critical_pieces.empty())
		{
			request_time_critical_pieces();
		}

		// ---- WEB SEEDS ----

		// re-insert urls that are to be retried into the m_web_seeds
		typedef std::map<web_seed_entry, ptime>::iterator iter_t;
		for (iter_t i = m_web_seeds_next_retry.begin(); i != m_web_seeds_next_retry.end();)
		{
			iter_t erase_element = i++;
			if (erase_element->second <= now)
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
			std::set<web_seed_entry> web_seeds;
			for (peer_iterator i = m_connections.begin();
				i != m_connections.end(); ++i)
			{
				web_peer_connection* p = dynamic_cast<web_peer_connection*>(*i);
				if (p) web_seeds.insert(web_seed_entry(p->url(), web_seed_entry::url_seed));
				http_seed_connection* s = dynamic_cast<http_seed_connection*>(*i);
				if (s) web_seeds.insert(web_seed_entry(s->url(), web_seed_entry::http_seed));
			}

			for (std::set<web_seed_entry>::iterator i = m_resolving_web_seeds.begin()
				, end(m_resolving_web_seeds.end()); i != end; ++i)
				web_seeds.insert(web_seeds.begin(), *i);

			// from the list of available web seeds, subtract the ones we are
			// already connected to.
			std::vector<web_seed_entry> not_connected_web_seeds;
			std::set_difference(m_web_seeds.begin(), m_web_seeds.end(), web_seeds.begin()
				, web_seeds.end(), std::back_inserter(not_connected_web_seeds));

			// connect to all of those that we aren't connected to
			std::for_each(not_connected_web_seeds.begin(), not_connected_web_seeds.end()
				, boost::bind(&torrent::connect_to_url_seed, this, _1));
		}
		
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			peer_connection* p = *i;
			++i;
			m_stat += p->statistics();
			// updates the peer connection's ul/dl bandwidth
			// resource requests
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				p->second_tick(tick_interval_ms);
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*p->m_logger) << "**ERROR**: " << e.what() << "\n";
#endif
				p->disconnect(errors::no_error, 1);
			}
#endif
		}
		if (m_ses.m_alerts.should_post<stats_alert>())
			m_ses.m_alerts.post_alert(stats_alert(get_handle(), tick_interval_ms, m_stat));

		accumulator += m_stat;
		m_total_uploaded += m_stat.last_payload_uploaded();
		m_total_downloaded += m_stat.last_payload_downloaded();
		m_stat.second_tick(tick_interval_ms);
	}

	void torrent::add_stats(stat const& s)
	{
		// these stats are propagated to the session
		// stats the next time second_tick is called
		m_stat += s;
	}

	void torrent::request_time_critical_pieces()
	{
		// build a list of peers and sort it by download_queue_time
		std::vector<peer_connection*> peers;
		peers.reserve(m_connections.size());
		std::remove_copy_if(m_connections.begin(), m_connections.end()
			, std::back_inserter(peers), !boost::bind(&peer_connection::can_request_time_critical, _1));
		std::sort(peers.begin(), peers.end()
			, boost::bind(&peer_connection::download_queue_time, _1, 16*1024)
			< boost::bind(&peer_connection::download_queue_time, _2, 16*1024));

		std::set<peer_connection*> peers_with_requests;

		std::vector<piece_block> interesting_blocks;
		std::vector<piece_block> backup1;
		std::vector<piece_block> backup2;
		std::vector<int> ignore;

		ptime now = time_now();

		for (std::list<time_critical_piece>::iterator i = m_time_critical_pieces.begin()
			, end(m_time_critical_pieces.end()); i != end; ++i)
		{
			if (i != m_time_critical_pieces.begin() && i->deadline > now
				+ m_average_piece_time + m_piece_time_deviation * 4)
			{
				// don't request pieces whose deadline is too far in the future
				break;
			}

			// loop until every block has been requested from
			do
			{
				// pick the peer with the lowest download_queue_time that has i->piece
				std::vector<peer_connection*>::iterator p = std::find_if(peers.begin(), peers.end()
					, boost::bind(&peer_connection::has_piece, _1, i->piece));

				if (p == peers.end()) break;
				peer_connection& c = **p;

				interesting_blocks.clear();
				backup1.clear();
				backup2.clear();
				// specifically request blocks with no affinity towards fast or slow
				// pieces. If we would, the picked block might end up in one of
				// the backup lists
				m_picker->add_blocks(i->piece, c.get_bitfield(), interesting_blocks
					, backup1, backup2, 1, 0, c.peer_info_struct()
					, ignore, piece_picker::none, 0);

				std::vector<pending_block> const& rq = c.request_queue();

				bool added_request = false;

				if (!interesting_blocks.empty() && std::find_if(rq.begin(), rq.end()
					, has_block(interesting_blocks.front())) != rq.end())
				{
					c.make_time_critical(interesting_blocks.front());
					added_request = true;
				}
				else if (!interesting_blocks.empty())
				{
					c.add_request(interesting_blocks.front(), peer_connection::req_time_critical);
					added_request = true;
				}

				// TODO: if there's been long enough since we requested something
				// from this piece, request one of the backup blocks (the one with
				// the least number of requests to it) and update the last request
				// timestamp

				if (added_request)
				{
					peers_with_requests.insert(peers_with_requests.begin(), &c);
					if (i->first_requested == min_time()) i->first_requested = now;

					if (!c.can_request_time_critical())
					{
						peers.erase(p);
					}
					else
					{
						// resort p, since it will have a higher download_queue_time now
						while (p != peers.end()-1 && (*p)->download_queue_time() > (*(p+1))->download_queue_time())
						{
							std::iter_swap(p, p+1);
							++p;
						}
					}
				}

			} while (!interesting_blocks.empty());
		}

		// commit all the time critical requests
		for (std::set<peer_connection*>::iterator i = peers_with_requests.begin()
			, end(peers_with_requests.end()); i != end; ++i)
		{
			(*i)->send_block_requests();
		}
	}

	std::set<std::string> torrent::web_seeds(web_seed_entry::type_t type) const
	{
		std::set<std::string> ret;
		for (std::set<web_seed_entry>::const_iterator i = m_web_seeds.begin()
			, end(m_web_seeds.end()); i != end; ++i)
		{
			if (i->type != type) continue;
			ret.insert(i->url);
		}
		return ret;
	}

	void torrent::retry_web_seed(std::string const& url, web_seed_entry::type_t type, int retry)
	{
		if (retry == 0) retry = m_ses.settings().urlseed_wait_retry;
		m_web_seeds_next_retry[web_seed_entry(url, type)] = time_now() + seconds(retry);
	}

	bool torrent::try_connect_peer()
	{
		TORRENT_ASSERT(want_more_peers());
		if (m_deficit_counter < 100) return false;
		m_deficit_counter -= 100;
		bool ret = m_policy.connect_one_peer(m_ses.session_time());
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
		TORRENT_ASSERT(!m_picker || !m_picker->have_piece(piece_index));
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

		m_storage->async_hash(piece_index, boost::bind(&torrent::on_piece_verified
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

		if (ret == -1) handle_disk_error(j);
		f(ret);
	}

	const tcp::endpoint& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	announce_entry* torrent::find_tracker(tracker_request const& r)
	{
		std::vector<announce_entry>::iterator i = std::find_if(
			m_trackers.begin(), m_trackers.end()
			, boost::bind(&announce_entry::url, _1) == r.url);
		if (i == m_trackers.end()) return 0;
		return &*i;
	}

#if !TORRENT_NO_FPU
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
#endif

	void torrent::file_progress(std::vector<size_type>& fp, int flags) const
	{
		TORRENT_ASSERT(valid_metadata());
	
		fp.resize(m_torrent_file->num_files(), 0);

		if (flags & torrent_handle::piece_granularity)
		{
			std::copy(m_file_progress.begin(), m_file_progress.end(), fp.begin());
			return;
		}

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
				TORRENT_ASSERT(std::accumulate(pieces.begin(), pieces.end(), 0) == 0);
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
		if (m_ses.m_alerts.should_post<state_changed_alert>())
			m_ses.m_alerts.post_alert(state_changed_alert(get_handle(), s, m_state));
		m_state = s;
	}

	torrent_status torrent::status() const
	{
		INVARIANT_CHECK;

		ptime now = time_now();

		torrent_status st;

		st.has_incoming = m_has_incoming;
		if (m_error) st.error = m_error.message() + ": " + m_error_file;
		st.seed_mode = m_seed_mode;

		if (m_last_scrape == min_time())
		{
			st.last_scrape = -1;
		}
		else
		{
			st.last_scrape = total_seconds(now - m_last_scrape);
		}
		st.upload_mode = m_upload_mode;
		st.up_bandwidth_queue = 0;
		st.down_bandwidth_queue = 0;
		st.priority = m_priority;

		st.num_peers = (int)std::count_if(m_connections.begin(), m_connections.end()
			, !boost::bind(&peer_connection::is_connecting, _1));

		st.list_peers = m_policy.num_peers();
		st.list_seeds = m_policy.num_seeds();
		st.connect_candidates = m_policy.num_connect_candidates();
		st.seed_rank = seed_rank(m_ses.m_settings);

		st.all_time_upload = m_total_uploaded;
		st.all_time_download = m_total_downloaded;

		st.active_time = total_seconds(m_active_time);
		st.active_time = total_seconds(m_active_time);
		st.seeding_time = total_seconds(m_seeding_time);

		st.storage_mode = m_storage_mode;

		st.num_complete = m_complete;
		st.num_incomplete = m_incomplete;
		st.paused = m_paused;
		bytes_done(st);
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

		if (m_waiting_tracker && !is_paused())
			st.next_announce = boost::posix_time::seconds(
				total_seconds(next_announce() - now));
		else
			st.next_announce = boost::posix_time::seconds(0);

		if (st.next_announce.is_negative())
			st.next_announce = boost::posix_time::seconds(0);

		st.announce_interval = boost::posix_time::seconds(0);

		st.current_tracker.clear();
		if (m_last_working_tracker >= 0)
		{
			TORRENT_ASSERT(m_last_working_tracker < int(m_trackers.size()));
			st.current_tracker = m_trackers[m_last_working_tracker].url;
		}
		else
		{
			std::vector<announce_entry>::const_iterator i;
			for (i = m_trackers.begin(); i != m_trackers.end(); ++i)
				if (i->updating) break;
			if (i != m_trackers.end()) st.current_tracker = i->url;
		}

		st.num_uploads = m_num_uploads;
		st.uploads_limit = m_max_uploads;
		st.num_connections = int(m_connections.size());
		st.connections_limit = m_max_connections;
		// if we don't have any metadata, stop here

		st.state = m_state;

		if (!valid_metadata())
		{
			st.state = torrent_status::downloading_metadata;
			st.progress_ppm = m_progress_ppm;
#if !TORRENT_NO_FPU
			st.progress = m_progress_ppm / 1000000.f;
#endif
			st.block_size = 0;
			return st;
		}

		st.block_size = block_size();

		if (m_state == torrent_status::checking_files)
		{
			st.progress_ppm = m_progress_ppm;
#if !TORRENT_NO_FPU
			st.progress = m_progress_ppm / 1000000.f;
#endif
		}
		else if (st.total_wanted == 0)
		{
			st.progress_ppm = 1000000;
			st.progress = 1.f;
		}
		else
		{
			st.progress_ppm = st.total_wanted_done * 1000000
				/ st.total_wanted;
#if !TORRENT_NO_FPU
			st.progress = st.progress_ppm / 1000000.f;
#endif
		}

		if (has_picker())
		{
			st.sparse_regions = m_picker->sparse_regions();
			int num_pieces = m_picker->num_pieces();
			st.pieces.resize(num_pieces, false);
			for (int i = 0; i < num_pieces; ++i)
				if (m_picker->have_piece(i)) st.pieces.set_bit(i);
		}
		st.num_pieces = num_have();
		st.num_seeds = num_seeds();
		if (m_picker.get())
		{
			boost::tie(st.distributed_full_copies, st.distributed_fraction) =
				m_picker->distributed_copies();
#if TORRENT_NO_FPU
			st.distributed_copies = -1.f;
#else
			st.distributed_copies = st.distributed_full_copies
				+ float(st.distributed_fraction) / 1000;
#endif
		}
		else
		{
			st.distributed_full_copies = -1;
			st.distributed_fraction = -1;
			st.distributed_copies = -1.f;
		}
		return st;
	}

	void torrent::add_redundant_bytes(int b)
	{
		TORRENT_ASSERT(b > 0);
		m_total_redundant_bytes += b;
		m_ses.add_redundant_bytes(b);
		TORRENT_ASSERT(m_total_redundant_bytes + m_total_failed_bytes
			<= m_stat.total_payload_download());
	}

	void torrent::add_failed_bytes(int b)
	{
		TORRENT_ASSERT(b > 0);
		m_total_failed_bytes += b;
		m_ses.add_failed_bytes(b);
//		TORRENT_ASSERT(m_total_redundant_bytes + m_total_failed_bytes
//			<= m_stat.total_payload_download());
	}

	int torrent::num_seeds() const
	{
		INVARIANT_CHECK;

		int ret = 0;
		for (std::set<peer_connection*>::const_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
			if ((*i)->is_seed()) ++ret;
		return ret;
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
			announce_entry* ae = find_tracker(r);
			if (ae)
			{
				ae->failed();
				int tracker_index = ae - &m_trackers[0];
				deprioritize_tracker(tracker_index);
			}
			if (m_ses.m_alerts.should_post<tracker_error_alert>())
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, ae?ae->fails:0, 0, r.url
					, errors::timed_out));
			}
		}
		else if (r.kind == tracker_request::scrape_request)
		{
			if (m_ses.m_alerts.should_post<scrape_failed_alert>())
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle()
					, r.url, errors::timed_out));
			}
		}
		update_tracker_timer(time_now());
	}

	// TODO: with some response codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(tracker_request const& r
		, int response_code, const std::string& str
		, int retry_interval)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		debug_log(std::string("*** tracker error: ") + str);
#endif
		if (r.kind == tracker_request::announce_request)
		{
			announce_entry* ae = find_tracker(r);
			if (ae)
			{
				ae->failed(retry_interval);
				int tracker_index = ae - &m_trackers[0];
				deprioritize_tracker(tracker_index);
			}
			if (m_ses.m_alerts.should_post<tracker_error_alert>())
			{
				m_ses.m_alerts.post_alert(tracker_error_alert(get_handle()
					, ae?ae->fails:0, response_code, r.url, str));
			}
		}
		else if (r.kind == tracker_request::scrape_request)
		{
			if (m_ses.m_alerts.should_post<scrape_failed_alert>())
			{
				m_ses.m_alerts.post_alert(scrape_failed_alert(get_handle(), r.url, str));
			}
		}
		// announce to the next working tracker
		if (!m_abort) announce_with_tracker();
		update_tracker_timer(time_now());
	}


#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << time_now_string() << " " << line << "\n";
	}
#endif

}

