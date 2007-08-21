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
			return static_cast<int>(i.piece_length());
		}

		// otherwise, go with the default
		return default_block_size;
	}

	struct find_peer_by_ip
	{
		find_peer_by_ip(tcp::endpoint const& a, const torrent* t)
			: ip(a)
			, tor(t)
		{ assert(t != 0); }
		
		bool operator()(const session_impl::connection_map::value_type& c) const
		{
			tcp::endpoint sender = c.first->remote_endpoint();
			if (sender.address() != ip.address()) return false;
			if (tor != c.second->associated_torrent().lock().get()) return false;
			return true;
		}

		tcp::endpoint const& ip;
		torrent const* tor;
	};

	struct peer_by_id
	{
		peer_by_id(const peer_id& i): pid(i) {}
		
		bool operator()(const std::pair<tcp::endpoint, peer_connection*>& p) const
		{
			if (p.second->pid() != pid) return false;
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
		, aux::checker_impl& checker
		, torrent_info const& tf
		, fs::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size
		, session_settings const& s
		, storage_constructor_type sc)
		: m_torrent_file(tf)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
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
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_trackers(m_torrent_file.trackers())
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_sequenced_download_threshold(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_default_block_size(block_size)
		, m_connections_initialized(true)
		, m_settings(s)
		, m_storage_constructor(sc)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
	{
#ifndef NDEBUG
		m_initial_done = 0;
#endif
		m_policy.reset(new policy(this));
	}

	torrent::torrent(
		session_impl& ses
		, aux::checker_impl& checker
		, char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, fs::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size
		, session_settings const& s
		, storage_constructor_type sc)
		: m_torrent_file(info_hash)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
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
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_num_pieces(0)
		, m_sequenced_download_threshold(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(net_interface.address(), 0)
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_default_block_size(block_size)
		, m_connections_initialized(false)
		, m_settings(s)
		, m_storage_constructor(sc)
		, m_max_uploads((std::numeric_limits<int>::max)())
		, m_num_uploads(0)
		, m_max_connections((std::numeric_limits<int>::max)())
	{
#ifndef NDEBUG
		m_initial_done = 0;
#endif

		INVARIANT_CHECK;

		if (name) m_name.reset(new std::string(name));

		if (tracker_url)
		{
			m_trackers.push_back(announce_entry(tracker_url));
			m_torrent_file.add_tracker(tracker_url);
		}

		m_policy.reset(new policy(this));
	}

	void torrent::start()
	{
		boost::weak_ptr<torrent> self(shared_from_this());
		if (m_torrent_file.is_valid()) init();
		m_announce_timer.expires_from_now(seconds(1));
		m_announce_timer.async_wait(m_ses.m_strand.wrap(
			bind(&torrent::on_announce_disp, self, _1)));
	}

#ifndef TORRENT_DISABLE_DHT
	bool torrent::should_announce_dht() const
	{
		// don't announce private torrents
		if (m_torrent_file.is_valid() && m_torrent_file.priv()) return false;
	
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

		assert(m_connections.empty());
		
		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING)
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*i->second->m_logger) << "*** DESTRUCTING TORRENT\n";
		}
#endif

		assert(m_abort);
		if (!m_connections.empty())
			disconnect_all();
	}

	std::string torrent::name() const
	{
		if (valid_metadata()) return m_torrent_file.name();
		if (m_name) return *m_name;
		return "";
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void torrent::add_extension(boost::shared_ptr<torrent_plugin> ext)
	{
		m_extensions.push_back(ext);
	}
#endif

	// this may not be called from a constructor because of the call to
	// shared_from_this()
	void torrent::init()
	{
		assert(m_torrent_file.is_valid());
		assert(m_torrent_file.num_files() > 0);
		assert(m_torrent_file.total_size() >= 0);

		m_have_pieces.resize(m_torrent_file.num_pieces(), false);
		// the shared_from_this() will create an intentional
		// cycle of ownership, se the hpp file for description.
		m_owning_storage = new piece_manager(shared_from_this(), m_torrent_file
			, m_save_path, m_ses.m_files, m_ses.m_disk_thread, m_storage_constructor);
		m_storage = m_owning_storage.get();
		m_block_size = calculate_block_size(m_torrent_file, m_default_block_size);
		m_picker.reset(new piece_picker(
			static_cast<int>(m_torrent_file.piece_length() / m_block_size)
			, static_cast<int>((m_torrent_file.total_size()+m_block_size-1)/m_block_size)));

		std::vector<std::string> const& url_seeds = m_torrent_file.url_seeds();
		std::copy(url_seeds.begin(), url_seeds.end(), std::inserter(m_web_seeds
			, m_web_seeds.begin()));
	}

	void torrent::use_interface(const char* net_interface)
	{
		INVARIANT_CHECK;

		m_net_interface = tcp::endpoint(address::from_string(net_interface), 0);
	}

	void torrent::on_announce_disp(boost::weak_ptr<torrent> p
		, asio::error_code const& e)
	{
		if (e) return;
		boost::shared_ptr<torrent> t = p.lock();
		if (!t) return;
		t->on_announce();
	}

	void torrent::on_announce()
#ifndef NDEBUG
	try
#endif
	{
		boost::weak_ptr<torrent> self(shared_from_this());

		if (!m_torrent_file.priv())
		{
			// announce on local network every 5 minutes
			m_announce_timer.expires_from_now(minutes(5));
			m_announce_timer.async_wait(m_ses.m_strand.wrap(
				bind(&torrent::on_announce_disp, self, _1)));

			// announce with the local discovery service
			m_ses.announce_lsd(m_torrent_file.info_hash());
		}
		else
		{
			m_announce_timer.expires_from_now(minutes(15));
			m_announce_timer.async_wait(m_ses.m_strand.wrap(
				bind(&torrent::on_announce_disp, self, _1)));
		}

#ifndef TORRENT_DISABLE_DHT
		if (!m_ses.m_dht) return;
		ptime now = time_now();
		if (should_announce_dht() && now - m_last_dht_announce > minutes(14))
		{
			m_last_dht_announce = now;
			// TODO: There should be a way to abort an announce operation on the dht.
			// when the torrent is destructed
			assert(m_ses.m_external_listen_port > 0);
			m_ses.m_dht->announce(m_torrent_file.info_hash()
				, m_ses.m_external_listen_port
				, m_ses.m_strand.wrap(bind(&torrent::on_dht_announce_response_disp, self, _1)));
		}
#endif
	}
#ifndef NDEBUG
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		assert(false);
	};
#endif

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
				get_handle(), peers.size(), "Got peers from DHT"));
		}
		std::for_each(peers.begin(), peers.end(), bind(
			&policy::peer_from_tracker, boost::ref(m_policy), _1, peer_id(0)
			, peer_info::dht, 0));
	}

#endif

	// returns true if it is time for this torrent to make another
	// tracker request
	bool torrent::should_request()
	{
		INVARIANT_CHECK;
		
		if (m_torrent_file.trackers().empty()) return false;

		if (m_just_paused)
		{
			m_just_paused = false;
			return true;
		}
		return !m_paused && m_next_request < time_now();
	}

	void torrent::tracker_warning(std::string const& msg)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			m_ses.m_alerts.post_alert(tracker_warning_alert(get_handle(), msg));
		}
	}
	
	void torrent::tracker_response(
		tracker_request const&
		, std::vector<peer_entry>& peer_list
		, int interval
		, int complete
		, int incomplete)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

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

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
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
		debug_log(s.str());
#endif
		// for each of the peers we got from the tracker
		for (std::vector<peer_entry>::iterator i = peer_list.begin();
			i != peer_list.end(); ++i)
		{
			// don't make connections to ourself
			if (i->pid == m_ses.get_peer_id())
				continue;

			try
			{
				tcp::endpoint a(address::from_string(i->ip), i->port);

				if (m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked)
				{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
					debug_log("blocked ip from tracker: " + i->ip);
#endif
					if (m_ses.m_alerts.should_post(alert::info))
					{
						m_ses.m_alerts.post_alert(peer_blocked_alert(a.address()
							, "peer from tracker blocked by IP filter"));
					}

					continue;
				}
			
				m_policy->peer_from_tracker(a, i->pid, peer_info::tracker, 0);
			}
			catch (std::exception&)
			{
				// assume this is because we got a hostname instead of
				// an ip address from the tracker

				tcp::resolver::query q(i->ip, boost::lexical_cast<std::string>(i->port));
				m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
					bind(&torrent::on_peer_name_lookup, shared_from_this(), _1, _2, i->pid)));
			}	
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "Got response from tracker: "
				<< m_trackers[m_last_working_tracker].url;
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), peer_list.size(), s.str()));
		}
		m_got_tracker_response = true;
	}

	void torrent::on_peer_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, peer_id pid) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (e || host == tcp::resolver::iterator() ||
			m_ses.is_aborted()) return;

		if (m_ses.m_ip_filter.access(host->endpoint().address()) & ip_filter::blocked)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			debug_log("blocked ip from tracker: " + host->endpoint().address().to_string());
#endif
			if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(peer_blocked_alert(host->endpoint().address()
					, "peer from tracker blocked by IP filter"));
			}

			return;
		}
			
		m_policy->peer_from_tracker(*host, pid, peer_info::tracker, 0);
	}
	catch (std::exception&)
	{};

	size_type torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return -1;
		return m_torrent_file.total_size()
			- quantized_bytes_done();
	}

	size_type torrent::quantized_bytes_done() const
	{
//		INVARIANT_CHECK;

		if (!valid_metadata()) return 0;

		if (m_torrent_file.num_pieces() == 0)
			return 0;

		if (is_seed()) return m_torrent_file.total_size();

		const int last_piece = m_torrent_file.num_pieces() - 1;

		size_type total_done
			= m_num_pieces * m_torrent_file.piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			int corr = m_torrent_file.piece_size(last_piece)
				- m_torrent_file.piece_length();
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

		if (!valid_metadata() || m_torrent_file.num_pieces() == 0)
			return tuple<size_type, size_type>(0,0);

		const int last_piece = m_torrent_file.num_pieces() - 1;

		if (is_seed())
			return make_tuple(m_torrent_file.total_size()
				, m_torrent_file.total_size());

		size_type wanted_done = (m_num_pieces - m_picker->num_have_filtered())
			* m_torrent_file.piece_length();
		
		size_type total_done
			= m_num_pieces * m_torrent_file.piece_length();
		assert(m_num_pieces < m_torrent_file.num_pieces());

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			int corr = m_torrent_file.piece_size(last_piece)
				- m_torrent_file.piece_length();
			total_done += corr;
			if (m_picker->piece_priority(last_piece) != 0)
				wanted_done += corr;
		}

		assert(total_done <= m_torrent_file.total_size());
		assert(wanted_done <= m_torrent_file.total_size());

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = static_cast<int>(
			m_torrent_file.piece_length() / m_block_size);

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			int index = i->index;
			assert(!m_have_pieces[index]);
			assert(i->finished <= m_picker->blocks_in_piece(index));

#ifndef NDEBUG
			for (std::vector<piece_picker::downloading_piece>::const_iterator j = boost::next(i);
				j != dl_queue.end(); ++j)
			{
				assert(j->index != index);
			}
#endif

			for (int j = 0; j < blocks_per_piece; ++j)
			{
				assert(m_picker->is_finished(piece_block(index, j)) == (i->info[j].state == piece_picker::block_info::state_finished));
				corr += (i->info[j].state == piece_picker::block_info::state_finished) * m_block_size;
				assert(index != last_piece || j < m_picker->blocks_in_last_piece()
					|| i->info[j].state != piece_picker::block_info::state_finished);
			}

			// correction if this was the last piece
			// and if we have the last block
			if (i->index == last_piece
				&& i->info[m_picker->blocks_in_last_piece()-1].state
					== piece_picker::block_info::state_finished)
			{
				corr -= m_block_size;
				corr += m_torrent_file.piece_size(last_piece) % m_block_size;
			}
			total_done += corr;
			if (m_picker->piece_priority(index) != 0)
				wanted_done += corr;
		}

		assert(total_done <= m_torrent_file.total_size());
		assert(wanted_done <= m_torrent_file.total_size());

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection* pc = i->second;
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
				assert(p->bytes_downloaded <= p->full_block_bytes);
				int last_piece = m_torrent_file.num_pieces() - 1;
				if (p->piece_index == last_piece
					&& p->block_index == m_torrent_file.piece_size(last_piece) / block_size())
					assert(p->full_block_bytes == m_torrent_file.piece_size(last_piece) % block_size());
				else
					assert(p->full_block_bytes == block_size());
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

		if (total_done >= m_torrent_file.total_size())
		{
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

		assert(total_done <= m_torrent_file.total_size());
		assert(wanted_done <= m_torrent_file.total_size());

#endif

		assert(total_done >= wanted_done);
		return make_tuple(total_done, wanted_done);
	}

	void torrent::piece_finished(int index, bool passed_hash_check)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		bool was_seed = is_seed();
		bool was_finished = m_picker->num_filtered() + num_pieces()
			== torrent_file().num_pieces();

		if (passed_hash_check)
		{
                        if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(piece_finished_alert(get_handle()
					, index, "piece finished"));
			}
			// the following call may cause picker to become invalid
			// in case we just became a seed
			announce_piece(index);
			assert(valid_metadata());
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
				try { finished(); }
				catch (std::exception& e)
				{
#ifndef NDEBUG
					std::cerr << e.what() << std::endl;
					assert(false);
#endif
				}
			}
		}
		else
		{
			piece_failed(index);
		}

#ifndef NDEBUG
		try
		{
#endif

		m_policy->piece_finished(index, passed_hash_check);

#ifndef NDEBUG
		}
		catch (std::exception const& e)
		{
			std::cerr << e.what() << std::endl;
			assert(false);
		}
#endif

#ifndef NDEBUG
		try
		{
#endif

		if (!was_seed && is_seed())
		{
			assert(passed_hash_check);
			completed();
		}

#ifndef NDEBUG
		}
		catch (std::exception const& e)
		{
			std::cerr << e.what() << std::endl;
			assert(false);
		}
#endif

	}

	void torrent::piece_failed(int index)
	{
		// if the last piece fails the peer connection will still
		// think that it has received all of it until this function
		// resets the download queue. So, we cannot do the
		// invariant check here since it assumes:
		// (total_done == m_torrent_file.total_size()) => is_seed()
//		INVARIANT_CHECK;

		assert(m_storage);
		assert(m_storage->refcount() > 0);
		assert(m_picker.get());
		assert(index >= 0);
	  	assert(index < m_torrent_file.num_pieces());

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "hash for piece " << index << " failed";
			m_ses.m_alerts.post_alert(hash_failed_alert(get_handle(), index, s.str()));
		}
		// increase the total amount of failed bytes
		m_total_failed_bytes += m_torrent_file.piece_size(index);

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_piece_failed(index); } catch (std::exception&) {}
		}
#endif

		for (std::set<void*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			policy::peer* p = static_cast<policy::peer*>(*i);
			if (p == 0) continue;
#ifndef NDEBUG
			if (!settings().allow_multiple_connections_per_ip)
			{
				std::vector<peer_connection*> conns;
				connection_for(p->ip.address(), conns);
				assert(p->connection == 0
					|| std::find_if(conns.begin(), conns.end()
					, boost::bind(std::equal_to<peer_connection*>(), _1, p->connection))
					!= conns.end());
			}
#endif
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
#if defined(TORRENT_VERBOSE_LOGGING)
					(*p->connection->m_logger) << "*** BANNING PEER [ " << p->ip
						<< " ] 'too many corrupt pieces'\n";
#endif
					p->connection->disconnect();
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
		assert(m_storage);
		m_storage->mark_failed(index);

		assert(m_have_pieces[index] == false);
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

#if defined(TORRENT_VERBOSE_LOGGING)
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*i->second->m_logger) << "*** ABORTING TORRENT\n";
		}
#endif

		disconnect_all();
		if (m_owning_storage.get()) m_storage->async_release_files();
		m_owning_storage = 0;
	}

	void torrent::on_files_released(int ret, disk_io_job const& j)
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

		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		std::vector<void*> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<void*> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		if (!m_have_pieces[index])
			m_num_pieces++;
		m_have_pieces[index] = true;

		assert(std::accumulate(m_have_pieces.begin(), m_have_pieces.end(), 0)
			== m_num_pieces);

		m_picker->we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			try { i->second->announce_piece(index); } catch (std::exception&) {}

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
			try { (*i)->on_piece_pass(index); } catch (std::exception&) {}
		}
#endif
		if (is_seed())
		{
			m_picker.reset();
			m_torrent_file.seed_free();
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

		assert(valid_metadata());
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

		assert(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		bool filter_updated = m_picker->set_piece_priority(index, priority);
		if (filter_updated) update_peer_interest();
	}

	int torrent::piece_priority(int index) const
	{
//		INVARIANT_CHECK;

		assert(valid_metadata());
		if (is_seed()) return 1;

		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		return m_picker->piece_priority(index);
	}

	void torrent::prioritize_pieces(std::vector<int> const& pieces)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed()) return;

		assert(m_picker.get());

		int index = 0;
		bool filter_updated = false;
		for (std::vector<int>::const_iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i, ++index)
		{
			assert(*i >= 0);
			assert(*i <= 7);
			filter_updated |= m_picker->set_piece_priority(index, *i);
		}
		if (filter_updated) update_peer_interest();
	}

	void torrent::piece_priorities(std::vector<int>& pieces) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed())
		{
			pieces.clear();
			pieces.resize(m_torrent_file.num_pieces(), 1);
			return;
		}

		assert(m_picker.get());
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
		assert(int(files.size()) == m_torrent_file.num_files());
		
		size_type position = 0;

		if (m_torrent_file.num_pieces() == 0) return;

		int piece_length = m_torrent_file.piece_length();
		// initialize the piece priorities to 0, then only allow
		// setting higher priorities
		std::vector<int> pieces(m_torrent_file.num_pieces(), 0);
		for (int i = 0; i < int(files.size()); ++i)
		{
			size_type start = position;
			size_type size = m_torrent_file.file_at(i).size;
			if (size == 0) continue;
			position += size;
			// mark all pieces of the file with this file's priority
			// but only if the priority is higher than the pieces
			// already set (to avoid problems with overlapping pieces)
			int start_piece = int(start / piece_length);
			int last_piece = int((position - 1) / piece_length);
			assert(last_piece <= int(pieces.size()));
			// if one piece spans several files, we might
			// come here several times with the same start_piece, end_piece
			std::for_each(pieces.begin() + start_piece
				, pieces.begin() + last_piece + 1
				, bind(&set_if_greater, _1, files[i]));
		}
		prioritize_pieces(pieces);
		update_peer_interest();
	}

	// updates the interested flag in peers
	void torrent::update_peer_interest()
	{
		for (peer_iterator i = begin(); i != end(); ++i)
			i->second->update_interest();
	}

	void torrent::filter_piece(int index, bool filter)
	{
		INVARIANT_CHECK;

		assert(valid_metadata());
		if (is_seed()) return;

		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		m_picker->set_piece_priority(index, filter ? 1 : 0);
		update_peer_interest();
	}

	void torrent::filter_pieces(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed()) return;

		assert(m_picker.get());

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
		update_peer_interest();
	}

	bool torrent::is_piece_filtered(int index) const
	{
		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed()) return false;
		
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		return m_picker->piece_priority(index) == 0;
	}

	void torrent::filtered_pieces(std::vector<bool>& bitmask) const
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		assert(valid_metadata());
		if (is_seed())
		{
			bitmask.clear();
			bitmask.resize(m_torrent_file.num_pieces(), false);
			return;
		}

		assert(m_picker.get());
		m_picker->filtered_pieces(bitmask);
	}

	void torrent::filter_files(std::vector<bool> const& bitmask)
	{
		INVARIANT_CHECK;

		// this call is only valid on torrents with metadata
		if (!valid_metadata() || is_seed()) return;

		// the bitmask need to have exactly one bit for every file
		// in the torrent
		assert((int)bitmask.size() == m_torrent_file.num_files());
		
		size_type position = 0;

		if (m_torrent_file.num_pieces())
		{
			int piece_length = m_torrent_file.piece_length();
			// mark all pieces as filtered, then clear the bits for files
			// that should be downloaded
			std::vector<bool> piece_filter(m_torrent_file.num_pieces(), true);
			for (int i = 0; i < (int)bitmask.size(); ++i)
			{
				size_type start = position;
				position += m_torrent_file.file_at(i).size;
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
		assert(!urls.empty());
		m_trackers = urls;
		if (m_currently_trying_tracker >= (int)m_trackers.size())
			m_currently_trying_tracker = (int)m_trackers.size()-1;
		m_last_working_tracker = -1;
	}

	tracker_request torrent::generate_tracker_request()
	{
		INVARIANT_CHECK;

		assert(!m_trackers.empty());

		m_next_request = time_now() + seconds(tracker_retry_delay_max);

		tracker_request req;
		req.info_hash = m_torrent_file.info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 16*1024;
		req.event = m_event;

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

		assert(!c.is_choked());
		assert(m_num_uploads > 0);
		c.send_choke();
		--m_num_uploads;
	}
	
	bool torrent::unchoke_peer(peer_connection& c)
	{
		INVARIANT_CHECK;

		assert(c.is_choked());
		if (m_num_uploads >= m_max_uploads) return false;
		c.send_unchoke();
		++m_num_uploads;
		return true;
	}

	void torrent::cancel_block(piece_block block)
	{
		for (peer_iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			i->second->cancel_request(block);
		}
	}

	void torrent::remove_peer(peer_connection* p) try
	{
//		INVARIANT_CHECK;

		assert(p != 0);

		peer_iterator i = m_connections.find(p->remote());
		if (i == m_connections.end())
		{
			assert(false);
			return;
		}

		if (ready_for_connections())
		{
			assert(p->associated_torrent().lock().get() == this);

			if (p->is_seed())
			{
				if (m_picker.get())
				{
					assert(!is_seed());
					m_picker->dec_refcount_all();
				}
			}
			else
			{
				// if we're a seed, we don't keep track of piece availability
				if (!is_seed())
				{
					const std::vector<bool>& pieces = p->get_bitfield();

					for (std::vector<bool>::const_iterator i = pieces.begin();
						i != pieces.end(); ++i)
					{
						if (*i) peer_lost(static_cast<int>(i - pieces.begin()));
					}
				}
			}
		}

		if (!p->is_choked())
			--m_num_uploads;

		m_policy->connection_closed(*p);
		p->set_peer_info(0);
		m_connections.erase(i);
#ifndef NDEBUG
		m_policy->check_invariant();
#endif
	}
	catch (std::exception& e)
	{
#ifndef NDEBUG
		std::string err = e.what();
#endif
		assert(false);
	};

	void torrent::connect_to_url_seed(std::string const& url)
	{
		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << time_now_string() << " resolving: " << url << "\n";
#endif

		m_resolving_web_seeds.insert(url);
		proxy_settings const& ps = m_ses.web_seed_proxy();
		if (ps.type == proxy_settings::http
			|| ps.type == proxy_settings::http_pw)
		{
			// use proxy
			tcp::resolver::query q(ps.hostname
				, boost::lexical_cast<std::string>(ps.port));
			m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
				bind(&torrent::on_proxy_name_lookup, shared_from_this(), _1, _2, url)));
		}
		else
		{
			std::string protocol;
			std::string auth;
			std::string hostname;
			int port;
			std::string path;
			boost::tie(protocol, auth, hostname, port, path)
				= parse_url_components(url);

			// TODO: should auth be used here?

			tcp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
			m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
				bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url
					, tcp::endpoint())));
		}

	}

	void torrent::on_proxy_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, std::string url) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
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
		m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
			bind(&torrent::on_name_lookup, shared_from_this(), _1, _2, url, a)));
	}
	catch (std::exception& exc)
	{
		assert(false);
	};

	void torrent::on_name_lookup(asio::error_code const& e, tcp::resolver::iterator host
		, std::string url, tcp::endpoint proxy) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
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
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
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
		
		peer_iterator conn = m_connections.find(a);
		if (conn != m_connections.end())
		{
			if (dynamic_cast<web_peer_connection*>(conn->second) == 0
				|| conn->second->is_disconnecting()) conn->second->disconnect();
			else return;
		}

		boost::shared_ptr<socket_type> s
			= instantiate_connection(m_ses.m_io_service, m_ses.web_seed_proxy());
		if (m_ses.web_seed_proxy().type == proxy_settings::http
			|| m_ses.web_seed_proxy().type == proxy_settings::http_pw)
		{
			// the web seed connection will talk immediately to
			// the proxy, without requiring CONNECT support
			s->get<http_stream>().set_no_connect(true);
		}
		boost::intrusive_ptr<peer_connection> c(new web_peer_connection(
			m_ses, shared_from_this(), s, a, url, 0));
			
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

		try
		{
			assert(m_connections.find(a) == m_connections.end());

			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));
			m_ses.m_connections.insert(std::make_pair(s, c));

			m_ses.m_half_open.enqueue(
				bind(&peer_connection::connect, c, _1)
				, bind(&peer_connection::timed_out, c)
				, seconds(settings().peer_connect_timeout));
		}
		catch (std::exception& e)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_ses.m_logger) << " ** HOSTNAME LOOKUP FAILED!**: " << e.what() << "\n";
#endif

			// TODO: post an error alert!
			std::map<tcp::endpoint, peer_connection*>::iterator i = m_connections.find(a);
			if (i != m_connections.end()) m_connections.erase(i);
			m_ses.connection_failed(s, a, e.what());
			c->disconnect();
		}
	}
	catch (std::exception& exc)
	{
#ifndef NDEBUG
		std::cerr << exc.what() << std::endl;
#endif
		assert(false);
	};

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
		m_host_resolver.async_resolve(q, m_ses.m_strand.wrap(
			bind(&torrent::on_country_lookup, shared_from_this(), _1, _2, p)));
	}

	namespace
	{
		struct country_entry
		{
			int code;
			char const* name;
		};
	}

	void torrent::on_country_lookup(asio::error_code const& error, tcp::resolver::iterator i
		, intrusive_ptr<peer_connection> p) const
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;
		
		m_resolving_country = false;

		// must be ordered in increasing order
		country_entry country_map[] =
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
			country_entry* i =
				std::lower_bound(country_map, country_map + size, tmp
					, bind(&country_entry::code, _1) < bind(&country_entry::code, _2));
			if (i == country_map + size
				|| i->code != country)
			{
				// unknown country!
				p->set_country("!!");
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				(*m_ses.m_logger) << "IP " << p->remote().address() << " was mapped to unknown country: " << country << "\n";
#endif
				return;
			}
			
			p->set_country(i->name);
		}
	}
#endif

	peer_connection* torrent::connect_to_peer(policy::peer* peerinfo)
	{
		INVARIANT_CHECK;

		assert(peerinfo);
		assert(peerinfo->connection == 0);
#ifndef NDEBUG
		// this asserts that we don't have duplicates in the policy's peer list
		peer_iterator i_ = m_connections.find(peerinfo->ip);
		assert(i_ == m_connections.end()
			|| i_->second->is_disconnecting()
			|| dynamic_cast<bt_peer_connection*>(i_->second) == 0
			|| m_ses.settings().allow_multiple_connections_per_ip);
#endif

		assert(want_more_peers());

		tcp::endpoint const& a(peerinfo->ip);
		assert((m_ses.m_ip_filter.access(a.address()) & ip_filter::blocked) == 0);

		boost::shared_ptr<socket_type> s
			= instantiate_connection(m_ses.m_io_service, m_ses.peer_proxy());
		boost::intrusive_ptr<peer_connection> c(new bt_peer_connection(
			m_ses, shared_from_this(), s, a, peerinfo));

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

		try
		{
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));
			m_ses.m_connections.insert(std::make_pair(s, c));

			m_ses.m_half_open.enqueue(
				bind(&peer_connection::connect, c, _1)
				, bind(&peer_connection::timed_out, c)
				, seconds(settings().peer_connect_timeout));
		}
		catch (std::exception& e)
		{
			// TODO: post an error alert!
			std::map<tcp::endpoint, peer_connection*>::iterator i = m_connections.find(a);
			if (i != m_connections.end()) m_connections.erase(i);
			m_ses.connection_failed(s, a, e.what());
			c->disconnect();
			throw;
		}
		if (c->is_disconnecting()) throw protocol_error("failed to connect");
		return c.get();
	}

	void torrent::set_metadata(entry const& metadata)
	{
		INVARIANT_CHECK;

		assert(!m_torrent_file.is_valid());
		m_torrent_file.parse_info_section(metadata);

		init();

		boost::mutex::scoped_lock(m_checker.m_mutex);

		boost::shared_ptr<aux::piece_checker_data> d(
				new aux::piece_checker_data);
		d->torrent_ptr = shared_from_this();
		d->save_path = m_save_path;
		d->info_hash = m_torrent_file.info_hash();
		// add the torrent to the queue to be checked
		m_checker.m_torrents.push_back(d);
		typedef session_impl::torrent_map torrent_map;
		torrent_map::iterator i = m_ses.m_torrents.find(
			m_torrent_file.info_hash());
		assert(i != m_ses.m_torrents.end());
		m_ses.m_torrents.erase(i);
		// and notify the thread that it got another
		// job in its queue
		m_checker.m_cond.notify_one();

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle(), "metadata successfully received from swarm"));
		}
	}

	void torrent::attach_peer(peer_connection* p)
	{
//		INVARIANT_CHECK;

		assert(p != 0);
		assert(!p->is_local());

		std::map<tcp::endpoint, peer_connection*>::iterator c
			= m_connections.find(p->remote());
		if (c != m_connections.end())
		{
			// we already have a peer_connection to this ip.
			// It may currently be waiting for completing a
			// connection attempt that might fail. So,
			// prioritize this current connection since
			// it has already succeeded.
			if (!c->second->is_connecting())
			{
				throw protocol_error("already connected to peer");
			}
			c->second->disconnect();
		}

		if (m_ses.m_connections.find(p->get_socket())
			== m_ses.m_connections.end())
		{
			throw protocol_error("peer is not properly constructed");
		}

		if (m_ses.is_aborted())
		{
			throw protocol_error("session is closing");
		}

		peer_iterator ci = m_connections.insert(
			std::make_pair(p->remote(), p)).first;
		try
		{
			// if new_connection throws, we have to remove the
			// it from the list.

#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				boost::shared_ptr<peer_plugin> pp((*i)->new_connection(p));
				if (pp) p->add_extension(pp);
			}
#endif
			assert(connection_for(p->remote()) == p);
			assert(ci->second == p);
			m_policy->new_connection(*ci->second);
		}
		catch (std::exception& e)
		{
			m_connections.erase(ci);
			throw;
		}
		assert(p->remote() == p->get_socket()->remote_endpoint());

#ifndef NDEBUG
		m_policy->check_invariant();
#endif
	}

	bool torrent::want_more_peers() const
	{
		return int(m_connections.size()) < m_max_connections
			&& m_ses.m_half_open.free_slots()
			&& !m_paused;
	}

	void torrent::disconnect_all()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		while (!m_connections.empty())
		{
			peer_connection& p = *m_connections.begin()->second;
			assert(p.associated_torrent().lock().get() == this);

#if defined(TORRENT_VERBOSE_LOGGING)
			if (m_abort)
				(*p.m_logger) << "*** CLOSING CONNECTION 'aborting'\n";
			else
				(*p.m_logger) << "*** CLOSING CONNECTION 'pausing'\n";
#endif
#ifndef NDEBUG
			std::size_t size = m_connections.size();
#endif
			p.disconnect();
			assert(m_connections.size() <= size);
		}
	}

	int torrent::bandwidth_throttle(int channel) const
	{
		return m_bandwidth_limit[channel].throttle();
	}

	void torrent::request_bandwidth(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, bool non_prioritized)
	{
		int block_size = m_bandwidth_limit[channel].throttle() / 10;

		if (m_bandwidth_limit[channel].max_assignable() > 0)
		{
			perform_bandwidth_request(channel, p, block_size, non_prioritized);
		}
		else
		{
			// skip forward in the queue until we find a prioritized peer
			// or hit the front of it.
			queue_t::reverse_iterator i = m_bandwidth_queue[channel].rbegin();
			while (i != m_bandwidth_queue[channel].rend() && i->non_prioritized) ++i;
			m_bandwidth_queue[channel].insert(i.base(), bw_queue_entry<peer_connection>(
				p, block_size, non_prioritized));
		}
	}

	void torrent::expire_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		assert(amount > 0);
		m_bandwidth_limit[channel].expire(amount);
		
		while (!m_bandwidth_queue[channel].empty())
		{
			bw_queue_entry<peer_connection> qe = m_bandwidth_queue[channel].front();
			if (m_bandwidth_limit[channel].max_assignable() == 0)
				break;
			m_bandwidth_queue[channel].pop_front();
			perform_bandwidth_request(channel, qe.peer
				, qe.max_block_size, qe.non_prioritized);
		}
	}

	void torrent::perform_bandwidth_request(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, int block_size
		, bool non_prioritized)
	{
		m_ses.m_bandwidth_manager[channel]->request_bandwidth(p
			, block_size, non_prioritized);
		m_bandwidth_limit[channel].assign(block_size);
	}

	void torrent::assign_bandwidth(int channel, int amount, int blk)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		assert(amount > 0);
		assert(amount <= blk);
		if (amount < blk)
			expire_bandwidth(channel, blk - amount);
	}

	// called when torrent is finished (all interested pieces downloaded)
	void torrent::finished()
	{
		INVARIANT_CHECK;

		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()
				, "torrent has finished downloading"));
		}

	// disconnect all seeds
	// TODO: should disconnect all peers that have the pieces we have
	// not just seeds
		std::vector<peer_connection*> seeds;
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			assert(i->second->associated_torrent().lock().get() == this);
			if (i->second->is_seed())
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*i->second->m_logger) << "*** SEED, CLOSING CONNECTION\n";
#endif
				seeds.push_back(i->second);
			}
		}
		std::for_each(seeds.begin(), seeds.end()
			, bind(&peer_connection::disconnect, _1));

		assert(m_storage);
		m_storage->async_release_files();
	}
	
	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
		INVARIANT_CHECK;

		// make the next tracker request
		// be a completed-event
		m_event = tracker_request::completed;
		force_tracker_request();
	}

	// this will move the tracker with the given index
	// to a prioritized position in the list (move it towards
	// the begining) and return the new index to the tracker.
	int torrent::prioritize_tracker(int index)
	{
		INVARIANT_CHECK;

		assert(index >= 0);
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
			// only start the announce if we want to announce with the dht
			if (should_announce_dht())
			{
				// force the DHT to reannounce
				m_last_dht_announce = time_now() - minutes(15);
				boost::weak_ptr<torrent> self(shared_from_this());
				m_announce_timer.expires_from_now(seconds(1));
				m_announce_timer.async_wait(m_ses.m_strand.wrap(
					bind(&torrent::on_announce_disp, self, _1)));
			}
#endif

		}
		else
		{
			// don't delay before trying the next tracker
			m_next_request = time_now();
		}

	}

	bool torrent::check_fastresume(aux::piece_checker_data& data)
	{
		INVARIANT_CHECK;

		assert(valid_metadata());
		bool done = true;
		try
		{
			assert(m_storage);
			assert(m_owning_storage.get());
			done = m_storage->check_fastresume(data, m_have_pieces, m_num_pieces
				, m_compact_mode);
		}
		catch (std::exception& e)
		{
			// probably means file permission failure or invalid filename
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;

			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(
					file_error_alert(
						get_handle()
						, e.what()));
			}
			pause();
		}
#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
		return done;
	}
	
	std::pair<bool, float> torrent::check_files()
	{
		INVARIANT_CHECK;

		assert(m_owning_storage.get());

		std::pair<bool, float> progress(true, 1.f);
		try
		{
			assert(m_storage);
			progress = m_storage->check_files(m_have_pieces, m_num_pieces
				, m_ses.m_mutex);
		}
		catch (std::exception& e)
		{
			// probably means file permission failure or invalid filename
			std::fill(m_have_pieces.begin(), m_have_pieces.end(), false);
			m_num_pieces = 0;

			if (m_ses.m_alerts.should_post(alert::fatal))
			{
				m_ses.m_alerts.post_alert(
					file_error_alert(
						get_handle()
						, e.what()));
			}
			pause();
		}

#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
		return progress;
	}

	void torrent::files_checked(std::vector<piece_picker::downloading_piece> const&
		unfinished_pieces)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		INVARIANT_CHECK;

		if (!is_seed())
		{
			// this is filled in with pieces that needs to be checked
			// against its hashes.
			std::vector<int> verify_pieces;
			m_picker->files_checked(m_have_pieces, unfinished_pieces, verify_pieces);
			if (m_sequenced_download_threshold > 0)
				picker().set_sequenced_download_threshold(m_sequenced_download_threshold);
			while (!verify_pieces.empty())
			{
				int piece = verify_pieces.back();
				verify_pieces.pop_back();
				async_verify_piece(piece, bind(&torrent::piece_finished
					, shared_from_this(), piece, _1));
			}
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_files_checked(); } catch (std::exception&) {}
		}
#endif

		if (is_seed())
		{
			m_picker.reset();
			m_torrent_file.seed_free();
		}

		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			typedef std::map<tcp::endpoint, peer_connection*> conn_map;
			for (conn_map::iterator i = m_connections.begin()
				, end(m_connections.end()); i != end;)
			{
				try
				{
					i->second->on_metadata();
					i->second->init();
					++i;
				}
				catch (std::exception& e)
				{
					// the connection failed, close it
					conn_map::iterator j = i;
					++j;
					m_ses.connection_failed(i->second->get_socket()
						, i->first, e.what());
					i = j;
				}
			}
		}
#ifndef NDEBUG
		m_initial_done = boost::get<0>(bytes_done());
#endif
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	fs::path torrent::save_path() const
	{
		if (m_owning_storage.get())
			return m_owning_storage->save_path();
		else
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
		INVARIANT_CHECK;

		assert(m_owning_storage.get());
		return *m_owning_storage;
	}


	torrent_handle torrent::get_handle() const
	{
		return torrent_handle(&m_ses, &m_checker, m_torrent_file.info_hash());
	}

	session_settings const& torrent::settings() const
	{
		return m_ses.settings();
	}

#ifndef NDEBUG
	void torrent::check_invariant() const
	{
//		size_type download = m_stat.total_payload_download();
//		size_type done = boost::get<0>(bytes_done());
//		assert(download >= done - m_initial_done);
		int num_uploads = 0;
		std::map<piece_block, int> num_requests;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			peer_connection const& p = *i->second;
			for (std::deque<piece_block>::const_iterator i = p.request_queue().begin()
				, end(p.request_queue().end()); i != end; ++i)
				++num_requests[*i];
			for (std::deque<piece_block>::const_iterator i = p.download_queue().begin()
				, end(p.download_queue().end()); i != end; ++i)
				++num_requests[*i];
			if (!p.is_choked()) ++num_uploads;
			torrent* associated_torrent = p.associated_torrent().lock().get();
			if (associated_torrent != this)
				assert(false);
		}
		assert(num_uploads == m_num_uploads);

		if (has_picker())
		{
			for (std::map<piece_block, int>::iterator i = num_requests.begin()
				, end(num_requests.end()); i != end; ++i)
			{
				assert(m_picker->num_peers(i->first) == i->second);
			}
		}

		if (valid_metadata())
		{
			assert(m_abort || int(m_have_pieces.size()) == m_torrent_file.num_pieces());
		}
		else
		{
			assert(m_abort || m_have_pieces.empty());
		}

		size_type total_done = quantized_bytes_done();
		if (m_torrent_file.is_valid())
		{
			if (is_seed())
				assert(total_done == m_torrent_file.total_size());
			else
				assert(total_done != m_torrent_file.total_size());
		}
		else
		{
			assert(total_done == 0);
		}

// This check is very expensive.
		assert(m_num_pieces
			== std::count(m_have_pieces.begin(), m_have_pieces.end(), true));
		assert(!valid_metadata() || m_block_size > 0);
		assert(!valid_metadata() || (m_torrent_file.piece_length() % m_block_size) == 0);
//		if (is_seed()) assert(m_picker.get() == 0);
	}
#endif

	void torrent::set_sequenced_download_threshold(int threshold)
	{
		if (has_picker())
		{
			picker().set_sequenced_download_threshold(threshold);
		}
		else
		{
			m_sequenced_download_threshold = threshold;
		}
	}


	void torrent::set_max_uploads(int limit)
	{
		assert(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_uploads = limit;
	}

	void torrent::set_max_connections(int limit)
	{
		assert(limit >= -1);
		if (limit <= 0) limit = (std::numeric_limits<int>::max)();
		m_max_connections = limit;
	}

	void torrent::set_peer_upload_limit(tcp::endpoint ip, int limit)
	{
		assert(limit >= -1);
		peer_connection* p = connection_for(ip);
		if (p == 0) return;
		p->set_upload_limit(limit);
	}

	void torrent::set_peer_download_limit(tcp::endpoint ip, int limit)
	{
		assert(limit >= -1);
		peer_connection* p = connection_for(ip);
		if (p == 0) return;
		p->set_download_limit(limit);
	}

	void torrent::set_upload_limit(int limit)
	{
		assert(limit >= -1);
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
		assert(limit >= -1);
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

	void torrent::pause()
	{
		INVARIANT_CHECK;

		if (m_paused) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { if ((*i)->on_pause()) return; } catch (std::exception&) {}
		}
#endif

#if defined(TORRENT_VERBOSE_LOGGING)
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			(*i->second->m_logger) << "*** PAUSING TORRENT\n";
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
			assert(m_storage);
			// TOOD: add a callback which posts
			// an alert for the client to sync. with
			m_storage->async_release_files(
				bind(&torrent::on_files_released, shared_from_this(), _1, _2));
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
			try { if ((*i)->on_resume()) return; } catch (std::exception&) {}
		}
#endif

		m_paused = false;

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
			try { (*i)->tick(); } catch (std::exception&) {}
		}
#endif

		if (m_paused)
		{
			// let the stats fade out to 0
 			m_stat.second_tick(tick_interval);
			return;
		}

		// ---- WEB SEEDS ----

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
					= dynamic_cast<web_peer_connection*>(i->second);
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
			peer_connection* p = i->second;
			++i;
			m_stat += p->statistics();
			// updates the peer connection's ul/dl bandwidth
			// resource requests
			try
			{
				p->second_tick(tick_interval);
			}
			catch (std::exception& e)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*p->m_logger) << "**ERROR**: " << e.what() << "\n";
#endif
				p->set_failed();
				p->disconnect();
			}
		}
		accumulator += m_stat;
		m_stat.second_tick(tick_interval);

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;
			m_policy->pulse();
		}
	}

	bool torrent::try_connect_peer()
	{
		assert(want_more_peers());
		return m_policy->connect_one_peer();
	}

	void torrent::async_verify_piece(int piece_index, boost::function<void(bool)> const& f)
	{
		INVARIANT_CHECK;

		assert(m_storage);
		assert(m_storage->refcount() > 0);
		assert(piece_index >= 0);
		assert(piece_index < m_torrent_file.num_pieces());
		assert(piece_index < (int)m_have_pieces.size());

		m_storage->async_hash(piece_index, bind(&torrent::on_piece_verified
			, shared_from_this(), _1, _2, f));
	}

	void torrent::on_piece_verified(int ret, disk_io_job const& j
		, boost::function<void(bool)> f)
	{
		sha1_hash h(j.str);
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		f(m_torrent_file.hash_for_piece(j.piece) == h);
	}

	const tcp::endpoint& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	bool torrent::is_allocating() const
	{ return m_owning_storage.get() && m_owning_storage->is_allocating(); }
	
	void torrent::file_progress(std::vector<float>& fp) const
	{
		assert(valid_metadata());
	
		fp.clear();
		fp.resize(m_torrent_file.num_files(), 0.f);
		
		for (int i = 0; i < m_torrent_file.num_files(); ++i)
		{
			peer_request ret = m_torrent_file.map_file(i, 0, 0);
			size_type size = m_torrent_file.file_at(i).size;

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
				size_type bytes_step = (std::min)(m_torrent_file.piece_size(ret.piece)
					- ret.start, size);
				if (m_have_pieces[ret.piece]) done += bytes_step;
				++ret.piece;
				ret.start = 0;
				size -= bytes_step;
			}
			assert(size == 0);

			fp[i] = static_cast<float>(done) / m_torrent_file.file_at(i).size;
		}
	}
	
	torrent_status torrent::status() const
	{
		INVARIANT_CHECK;

		assert(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

		st.num_peers = (int)std::count_if(m_connections.begin(), m_connections.end(),
			!boost::bind(&peer_connection::is_connecting
			, boost::bind(&std::map<tcp::endpoint,peer_connection*>::value_type::second, _1)));

		st.compact_mode = m_compact_mode;

		st.num_complete = m_complete;
		st.num_incomplete = m_incomplete;
		st.paused = m_paused;
		boost::tie(st.total_done, st.total_wanted_done) = bytes_done();

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

		if (!valid_metadata())
		{
			if (m_got_tracker_response == false)
				st.state = torrent_status::connecting_to_tracker;
			else
				st.state = torrent_status::downloading_metadata;

// TODO: add a progress member to the torrent that will be used in this case
// and that may be set by a plugin
//			if (m_metadata_size == 0) st.progress = 0.f;
//			else st.progress = (std::min)(1.f, m_metadata_progress / (float)m_metadata_size);
			st.progress = 0.f;

			st.block_size = 0;

			return st;
		}

		st.block_size = block_size();

		// fill in status that depends on metadata

		st.total_wanted = m_torrent_file.total_size();

		if (m_picker.get() && (m_picker->num_filtered() > 0
			|| m_picker->num_have_filtered() > 0))
		{
			int filtered_pieces = m_picker->num_filtered()
				+ m_picker->num_have_filtered();
			int last_piece_index = m_torrent_file.num_pieces() - 1;
			if (m_picker->piece_priority(last_piece_index) == 0)
			{
				st.total_wanted -= m_torrent_file.piece_size(last_piece_index);
				--filtered_pieces;
			}
			
			st.total_wanted -= filtered_pieces * m_torrent_file.piece_length();
		}

		assert(st.total_wanted >= st.total_wanted_done);

		if (st.total_wanted == 0) st.progress = 1.f;
		else st.progress = st.total_wanted_done
			/ static_cast<double>(st.total_wanted);

		st.pieces = &m_have_pieces;
		st.num_pieces = m_num_pieces;

		if (m_got_tracker_response == false)
		{
			st.state = torrent_status::connecting_to_tracker;
		}
		else if (is_seed())
		{
			assert(st.total_done == m_torrent_file.total_size());
			st.state = torrent_status::seeding;
		}
		else if (st.total_wanted_done == st.total_wanted)
		{
			st.state = torrent_status::finished;
		}
		else
		{
			st.state = torrent_status::downloading;
		}

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
			, boost::bind(&peer_connection::is_seed
				, boost::bind(&std::map<tcp::endpoint
					, peer_connection*>::value_type::second, _1)));
	}

	void torrent::tracker_request_timed_out(
		tracker_request const&)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		debug_log("*** tracker timed out");
#endif

		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_trackers[m_currently_trying_tracker].url
				<< "\" timed out";
			m_ses.m_alerts.post_alert(tracker_alert(get_handle()
				, m_failed_trackers + 1, 0, s.str()));
		}
		try_next_tracker();
	}

	// TODO: with some response codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(tracker_request const&
		, int response_code, const std::string& str)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		debug_log(std::string("*** tracker error: ") + str);
#endif
		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_trackers[m_currently_trying_tracker].url
				<< "\" " << str;
			m_ses.m_alerts.post_alert(tracker_alert(get_handle()
				, m_failed_trackers + 1, response_code, s.str()));
		}

		try_next_tracker();
	}


#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << line << "\n";
	}
#endif

}

