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

using namespace libtorrent;
using namespace boost::posix_time;
using boost::tuples::tuple;
using boost::tuples::get;
using boost::tuples::make_tuple;
using boost::filesystem::complete;
using boost::bind;
using boost::mutex;
using libtorrent::detail::session_impl;

// PROFILING CODE

#ifdef TORRENT_PROFILE
#include <boost/date_time/posix_time/ptime.hpp>

namespace libtorrent
{
	namespace
	{
		using boost::posix_time::ptime;
		using boost::posix_time::time_duration;
		using boost::posix_time::microsec_clock;
		std::vector<std::pair<ptime, std::string> > checkpoints;
	}

	void add_checkpoint(std::string const& str)
	{
		checkpoints.push_back(std::make_pair(microsec_clock::universal_time(), str));
	}

	void print_checkpoints()
	{
		for (std::vector<std::pair<ptime, std::string> >::iterator i
			= checkpoints.begin(); i != checkpoints.end(); ++i)
		{
			ptime cur = i->first;
			if (i + 1 != checkpoints.end())
			{
				time_duration diff = (i + 1)->first - cur;
				std::cout << diff.total_microseconds() << " " << i->second << "\n";
			}
			else
			{
				std::cout << "    " << i->second << "\n";
			}
		}
	}
}

#endif

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

		// if pieces are too large, adjust the block size
		if (i.piece_length() / default_block_size > piece_picker::max_blocks_per_piece)
		{
			return static_cast<int>(i.piece_length() / piece_picker::max_blocks_per_piece);
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
		
		bool operator()(const detail::session_impl::connection_map::value_type& c) const
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
		detail::session_impl& ses
		, detail::checker_impl& checker
		, torrent_info const& tf
		, boost::filesystem::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size)
		: m_torrent_file(tf)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(second_clock::universal_time())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_trackers(m_torrent_file.trackers())
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_priority(.5)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(0, net_interface.address())
		, m_upload_bandwidth_limit(std::numeric_limits<int>::max())
		, m_download_bandwidth_limit(std::numeric_limits<int>::max())
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_metadata_progress(0)
		, m_metadata_size(0)
		, m_default_block_size(block_size)
		, m_connections_initialized(true)
	{
		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();

		m_dl_bandwidth_quota.min = 100;
		m_dl_bandwidth_quota.max = resource_request::inf;

		if (m_ses.m_download_rate == -1)
		{
			m_dl_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			m_dl_bandwidth_quota.given = 400;
		}

		m_ul_bandwidth_quota.min = 100;
		m_ul_bandwidth_quota.max = resource_request::inf;

		if (m_ses.m_upload_rate == -1)
		{
			m_ul_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			m_ul_bandwidth_quota.given = 400;
		}

		m_policy.reset(new policy(this));
		init();
	}

	torrent::torrent(
		detail::session_impl& ses
		, detail::checker_impl& checker
		, char const* tracker_url
		, sha1_hash const& info_hash
		, boost::filesystem::path const& save_path
		, tcp::endpoint const& net_interface
		, bool compact_mode
		, int block_size)
		: m_torrent_file(info_hash)
		, m_abort(false)
		, m_paused(false)
		, m_just_paused(false)
		, m_event(tracker_request::started)
		, m_block_size(0)
		, m_storage(0)
		, m_next_request(second_clock::universal_time())
		, m_duration(1800)
		, m_complete(-1)
		, m_incomplete(-1)
		, m_policy()
		, m_ses(ses)
		, m_checker(checker)
		, m_picker(0)
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_failed_trackers(0)
		, m_time_scaler(0)
		, m_priority(.5)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_total_failed_bytes(0)
		, m_total_redundant_bytes(0)
		, m_net_interface(0, net_interface.address())
		, m_upload_bandwidth_limit(std::numeric_limits<int>::max())
		, m_download_bandwidth_limit(std::numeric_limits<int>::max())
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
		, m_metadata_progress(0)
		, m_metadata_size(0)
		, m_default_block_size(block_size)
		, m_connections_initialized(false)
	{
		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();

		m_dl_bandwidth_quota.min = 100;
		m_dl_bandwidth_quota.max = resource_request::inf;

		if (m_ses.m_download_rate == -1)
		{
			m_dl_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			m_dl_bandwidth_quota.given = 400;
		}

		m_ul_bandwidth_quota.min = 100;
		m_ul_bandwidth_quota.max = resource_request::inf;


		if (m_ses.m_upload_rate == -1)
		{
			m_ul_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			m_ul_bandwidth_quota.given = 400;
		}

		m_trackers.push_back(announce_entry(tracker_url));
		m_requested_metadata.resize(256, 0);

		m_policy.reset(new policy(this));
		m_torrent_file.add_tracker(tracker_url);
	}

	torrent::~torrent()
	{
		if (m_ses.m_abort)
			m_abort = true;
		if (!m_connections.empty())
			disconnect_all();
	}

	void torrent::init()
	{
		assert(m_torrent_file.is_valid());
		assert(m_torrent_file.num_files() > 0);
		assert(m_torrent_file.total_size() >= 0);

		m_have_pieces.resize(m_torrent_file.num_pieces(), false);
		m_storage.reset(new piece_manager(m_torrent_file, m_save_path));
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
		m_net_interface = tcp::endpoint(0, net_interface);
	}

	// returns true if it is time for this torrent to make another
	// tracker request
	bool torrent::should_request()
	{
		if (m_just_paused)
		{
			m_just_paused = false;
			return true;
		}
		return !m_paused &&
			m_next_request < second_clock::universal_time();
	}

	void torrent::tracker_warning(std::string const& msg)
	{
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

		m_failed_trackers = 0;
		// less than 5 minutes announce intervals
		// are insane.
		if (interval < 60 * 5) interval = 60 * 5;

		m_last_working_tracker
			= prioritize_tracker(m_currently_trying_tracker);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		m_next_request = second_clock::universal_time() + boost::posix_time::seconds(m_duration);

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

			tcp::endpoint a(i->port, i->ip.c_str());

			if (m_ses.m_ip_filter.access(a.address()) == ip_filter::blocked)
			{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				debug_log("blocked ip from tracker: " + i->ip);
#endif
				continue;
			}
			
			m_policy->peer_from_tracker(a, i->pid);
		}

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "Got response from tracker: "
				<< m_trackers[m_last_working_tracker].url;
			m_ses.m_alerts.post_alert(tracker_reply_alert(
				get_handle(), s.str()));
		}
		m_got_tracker_response = true;
	}

	size_type torrent::bytes_left() const
	{
		// if we don't have the metadata yet, we
		// cannot tell how big the torrent is.
		if (!valid_metadata()) return -1;
		return m_torrent_file.total_size()
			- boost::tuples::get<0>(bytes_done());
	}

	// the first value is the total number of bytes downloaded
	// the second value is the number of bytes of those that haven't
	// been filtered as not wanted we have downloaded
	tuple<size_type, size_type> torrent::bytes_done() const
	{
		if (!valid_metadata()) return tuple<size_type, size_type>(0,0);

		assert(m_picker.get());

		if (m_torrent_file.num_pieces() == 0)
			return tuple<size_type, size_type>(0,0);
		const int last_piece = m_torrent_file.num_pieces() - 1;

		size_type wanted_done = (m_num_pieces - m_picker->num_have_filtered())
			* m_torrent_file.piece_length();
		
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
			if (!m_picker->is_filtered(last_piece))
				wanted_done += corr;
		}

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = static_cast<int>(
			m_torrent_file.piece_length() / m_block_size);

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin(); i != dl_queue.end(); ++i)
		{
			int corr = 0;
			assert(!m_have_pieces[i->index]);

			for (int j = 0; j < blocks_per_piece; ++j)
			{
				corr += (i->finished_blocks[j]) * m_block_size;
			}

			// correction if this was the last piece
			// and if we have the last block
			if (i->index == last_piece
				&& i->finished_blocks[m_picker->blocks_in_last_piece()-1])
			{
				corr -= m_block_size;
				corr += m_torrent_file.piece_size(last_piece) % m_block_size;
			}
			total_done += corr;
			if (!m_picker->is_filtered(i->index))
				wanted_done += corr;
		}

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
				assert(p->bytes_downloaded <= p->full_block_bytes);
			}
		}
		for (std::map<piece_block, int>::iterator i = downloading_piece.begin();
			i != downloading_piece.end(); ++i)
		{
			total_done += i->second;
			if (!m_picker->is_filtered(i->first.piece_index))
				wanted_done += i->second;
		}
		return make_tuple(total_done, wanted_done);
	}

	void torrent::piece_failed(int index)
	{
		assert(m_storage.get());
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

		std::vector<tcp::endpoint> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<tcp::endpoint> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		for (std::set<tcp::endpoint>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_invalid_data();

			// either, we have received too many failed hashes
			// or this was the only peer that sent us this piece.
			// TODO: make this a changable setting
			if (p->second->trust_points() <= -7 || peers.size() == 1)
			{
				// we don't trust this peer anymore
				// ban it.
				if (m_ses.m_alerts.should_post(alert::info))
				{
					m_ses.m_alerts.post_alert(peer_ban_alert(
						p->first
						, get_handle()
						, "banning peer because of too many corrupt pieces"));
				}
				m_policy->ban_peer(*p->second);

#if defined(TORRENT_VERBOSE_LOGGING)
				(*p->second->m_logger) << "*** BANNING PEER 'too many corrupt pieces'\n";
#endif
				p->second->disconnect();
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
		m_storage->mark_failed(index);

		assert(m_have_pieces[index] == false);
	}

	void torrent::abort()
	{
		m_abort = true;
		// if the torrent is paused, it doesn't need
		// to announce with even=stopped again.
		if (!m_paused)
			m_event = tracker_request::stopped;
		// disconnect all peers and close all
		// files belonging to the torrents
		disconnect_all();
		if (m_storage.get()) m_storage->release_files();
	}

	void torrent::announce_piece(int index)
	{
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		std::vector<tcp::endpoint> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<tcp::endpoint> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		for (std::set<tcp::endpoint>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_valid_data();
		}

		m_picker->we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			i->second->announce_piece(index);
	}

	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}

	void torrent::filter_piece(int index, bool filter)
	{
		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		// TODO: update peer's interesting-bit
		
		if (filter) m_picker->mark_as_filtered(index);
		else m_picker->mark_as_unfiltered(index);
	}

	void torrent::filter_pieces(std::vector<bool> const& bitmask)
	{
		// this call is only valid on torrents with metadata
		assert(m_picker.get());

		// TODO: update peer's interesting-bit
		
		std::vector<int> state;
		state.reserve(100);
		int index = 0;
		for (std::vector<bool>::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if (m_picker->is_filtered(index) == *i) continue;
			if (*i)
				m_picker->mark_as_filtered(index);
			else
				state.push_back(index);
		}
		std::random_shuffle(state.begin(), state.end());
		for (std::vector<int>::iterator i = state.begin();
			i != state.end(); ++i)
		{
			m_picker->mark_as_unfiltered(*i);
		}
	}

	bool torrent::is_piece_filtered(int index) const
	{
		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		return m_picker->is_filtered(index);
	}

	void torrent::filtered_pieces(std::vector<bool>& bitmask) const
	{
		// this call is only valid on torrents with metadata
		assert(m_picker.get());
		m_picker->filtered_pieces(bitmask);
	}


	
	//idea from Arvid and MooPolice
	//todo refactoring and improving the function body
	void torrent::filter_file(int index, bool filter)
	{
		// this call is only valid on torrents with metadata
		if (!valid_metadata()) return;

		assert(index < m_torrent_file.num_files());
		assert(index >= 0);

		size_type start_position = 0;
		int start_piece_index = 0;
		int end_piece_index = 0;
		int piece_length = m_torrent_file.piece_length();

		for (int i = 0; i < index; ++i)
			start_position += m_torrent_file.file_at(i).size;

		start_piece_index = start_position / piece_length;
		// make the end piece index be rounded upwards
		end_piece_index = (start_position + m_torrent_file.file_at(index).size
			+ piece_length - 1) / piece_length;

		for(int i = start_piece_index; i <= end_piece_index; ++i)
			filter_piece(i, filter);
	}

	void torrent::filter_files(std::vector<bool> const& bitmask)
	{
		// this call is only valid on torrents with metadata
		if (!valid_metadata()) return;

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
		m_next_request
			= second_clock::universal_time()
			+ boost::posix_time::seconds(tracker_retry_delay_max);

		tracker_request req;
		req.info_hash = m_torrent_file.info_hash();
		req.pid = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 1000;
		req.event = m_event;

		if (m_event != tracker_request::stopped)
			m_event = tracker_request::none;
		req.url = m_trackers[m_currently_trying_tracker].url;
		assert(m_connections_quota.given > 0);
		req.num_want = std::max(
			(m_connections_quota.given
			- m_policy->num_peers()), 10);
		// if we are aborting. we don't want any new peers
		if (req.event == tracker_request::stopped)
			req.num_want = 0;

		// default initialize, these should be set by caller
		// before passing the request to the tracker_manager
		req.listen_port = 0;
		req.key = 0;

		return req;
	}

	void torrent::remove_peer(peer_connection* p)
	{
		assert(p != 0);

		peer_iterator i = m_connections.find(p->remote());
		if (i == m_connections.end()) return;

		if (ready_for_connections())
		{
			assert(p->associated_torrent().lock().get() == this);

			std::vector<int> piece_list;
			const std::vector<bool>& pieces = p->get_bitfield();

			for (std::vector<bool>::const_iterator i = pieces.begin();
				i != pieces.end(); ++i)
			{
				if (*i) piece_list.push_back(static_cast<int>(i - pieces.begin()));
			}

			std::random_shuffle(piece_list.begin(), piece_list.end());

			for (std::vector<int>::iterator i = piece_list.begin();
				i != piece_list.end(); ++i)
			{
				peer_lost(*i);
			}
		}

		m_policy->connection_closed(*p);
		m_connections.erase(i);
#ifndef NDEBUG
		m_policy->check_invariant();
#endif
	}

	peer_connection& torrent::connect_to_url_seed(std::string const& url)
	{
		// TODO: should be non-blocking!!
		host_resolver resolver(m_ses.m_selector);
		host h;
		
		std::string protocol;
		std::string hostname;
		int port;
		std::string path;
		boost::tie(protocol, hostname, port, path)
			= parse_url_components(url);

		resolver.by_name(h, hostname);
		tcp::endpoint a(port, h.address(0));

		boost::shared_ptr<stream_socket> s(new stream_socket(m_ses.m_selector));
		boost::intrusive_ptr<peer_connection> c(new web_peer_connection(
			m_ses, shared_from_this(), s, a, url));

		try
		{
			m_ses.m_connection_queue.push_back(c);

			assert(m_connections.find(a) == m_connections.end());

#ifndef NDEBUG
			m_policy->check_invariant();
#endif
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));

#ifndef NDEBUG
			m_policy->check_invariant();
#endif

			m_ses.process_connection_queue();
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
		return *c;
	}

	peer_connection& torrent::connect_to_peer(const tcp::endpoint& a)
	{
		if (m_connections.find(a) != m_connections.end())
			throw protocol_error("already connected to peer");

		boost::shared_ptr<stream_socket> s(new stream_socket(m_ses.m_selector));
		boost::intrusive_ptr<peer_connection> c(new bt_peer_connection(
			m_ses, shared_from_this(), s, a));

		try
		{
			m_ses.m_connection_queue.push_back(c);

			assert(m_connections.find(a) == m_connections.end());

#ifndef NDEBUG
			m_policy->check_invariant();
#endif
			// add the newly connected peer to this torrent's peer list
			m_connections.insert(
				std::make_pair(a, boost::get_pointer(c)));

#ifndef NDEBUG
			m_policy->check_invariant();
#endif

			m_ses.process_connection_queue();
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
		return *c;
	}

	void torrent::attach_peer(peer_connection* p)
	{
		assert(p != 0);
		assert(!p->is_local());

		if (m_connections.find(p->remote()) != m_connections.end())
			throw protocol_error("already connected to peer");

		detail::session_impl::connection_map::iterator i
			= m_ses.m_connections.find(p->get_socket());
		if (i == m_ses.m_connections.end())
		{
			throw protocol_error("peer is not properly constructed");
		}

		// it's important that we call new_connection before
		// the connection is added to the torrent's list.
		// because if this fails, it will throw, and if this throws
		// m_attatched_to_torrent won't be set in the peer_connections
		// and the destructor won't remove the entry from the torrent's
		// connection list.
		m_policy->new_connection(*i->second);

#ifndef NDEBUG
		assert(p->remote() == p->get_socket()->remote_endpoint());
#endif
		m_connections.insert(std::make_pair(p->remote(), p));

#ifndef NDEBUG
		m_policy->check_invariant();
#endif

	}

	void torrent::disconnect_all()
	{
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
			assert(m_connections.size() < size);
		}
	}

	// called when torrent is finished (all interested pieces downloaded)
	void torrent::finished()
	{
		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()
				, "torrent has finished downloading"));
		}

	// disconnect all seeds
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

		m_storage->release_files();
	}
	
	// called when torrent is complete (all pieces downloaded)
	void torrent::completed()
	{
/*
		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_complete_alert(
				get_handle()
				, "torrent is complete"));
		}
*/
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
		using namespace boost::posix_time;
		++m_currently_trying_tracker;

		if ((unsigned)m_currently_trying_tracker >= m_trackers.size())
		{
			int delay = tracker_retry_delay_min
				+ std::min(m_failed_trackers, (int)tracker_failed_max)
				* (tracker_retry_delay_max - tracker_retry_delay_min)
				/ tracker_failed_max;

			++m_failed_trackers;
			// if we've looped the tracker list, wait a bit before retrying
			m_currently_trying_tracker = 0;
			m_next_request = second_clock::universal_time() + seconds(delay);
		}
		else
		{
			// don't delay before trying the next tracker
			m_next_request = second_clock::universal_time();
		}

	}

	bool torrent::check_fastresume(detail::piece_checker_data& data)
	{
		if (!m_storage.get())
		{
			// this means we have received the metadata through the
			// metadata extension, and we have to initialize
			init();
		}

		assert(m_storage.get());
		return m_storage->check_fastresume(data, m_have_pieces, m_compact_mode);
	}
	
	std::pair<bool, float> torrent::check_files()
	{
		assert(m_storage.get());
		return m_storage->check_files(m_have_pieces);
	}

	void torrent::files_checked(std::vector<piece_picker::downloading_piece> const&
		unfinished_pieces)
	{
		m_num_pieces = std::count(
			m_have_pieces.begin()
		  , m_have_pieces.end()
		  , true);

		m_picker->files_checked(m_have_pieces, unfinished_pieces);
		if (!m_connections_initialized)
		{
			m_connections_initialized = true;
			// all peer connections have to initialize themselves now that the metadata
			// is available
			typedef std::map<tcp::endpoint, peer_connection*> conn_map;
			for (conn_map::iterator i = m_connections.begin()
					, end(m_connections.end()); i != end; ++i)
			{
				try { i->second->init(); }
				catch (std::exception&e)
				{
					// TODO: close the connection
					assert(false);
				}
			}
		}
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	boost::filesystem::path torrent::save_path() const
	{
		return m_save_path;
	}

	bool torrent::move_storage(boost::filesystem::path const& save_path)
	{
		bool ret = true;
		if (m_storage.get())
		{
			ret = m_storage->move_storage(save_path);
			m_save_path = m_storage->save_path();
		}
		else
		{
			m_save_path = save_path;
		}
		return ret;
	}

	piece_manager& torrent::filesystem()
	{
		assert(m_storage.get());
		return *m_storage;
	}


	torrent_handle torrent::get_handle() const
	{
		return torrent_handle(&m_ses, 0, m_torrent_file.info_hash());
	}



#ifndef NDEBUG
	void torrent::check_invariant() const
	{
		for (const_peer_iterator i = begin(); i != end(); ++i)
			assert(i->second->associated_torrent().lock().get() == this);

		assert(m_num_pieces
			== std::count(m_have_pieces.begin(), m_have_pieces.end(), true));
		assert(m_priority >= 0.f && m_priority < 1.f);
		assert(!valid_metadata() || m_block_size > 0);
		assert(!valid_metadata() || (m_torrent_file.piece_length() % m_block_size) == 0);
	}
#endif

	void torrent::set_max_uploads(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		m_uploads_quota.max = std::max(m_uploads_quota.min, limit);
	}

	void torrent::set_max_connections(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		m_connections_quota.max = std::max(m_connections_quota.min, limit);
	}

	void torrent::set_upload_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_upload_bandwidth_limit = limit;
	}

	void torrent::set_download_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = std::numeric_limits<int>::max();
		if (limit < num_peers() * 10) limit = num_peers() * 10;
		m_download_bandwidth_limit = limit;
	}

	void torrent::pause()
	{
		if (m_paused) return;
		disconnect_all();
		m_paused = true;
		// tell the tracker that we stopped
		m_event = tracker_request::stopped;
		m_just_paused = true;
		// this will make the storage close all
		// files and flush all cached data
		if (m_storage.get()) m_storage->release_files();
	}

	void torrent::resume()
	{
		if (!m_paused) return;
		m_paused = false;

		// tell the tracker that we're back
		m_event = tracker_request::started;
		force_tracker_request();

		// make pulse be called as soon as possible
		m_time_scaler = 0;
	}

	void torrent::second_tick(stat& accumulator)
	{
		m_connections_quota.used = (int)m_connections.size();
		m_uploads_quota.used = m_policy->num_uploads();

		m_ul_bandwidth_quota.used = 0;
		m_ul_bandwidth_quota.max = 0;
		m_ul_bandwidth_quota.min = 0;

		m_dl_bandwidth_quota.used = 0;
		m_dl_bandwidth_quota.min = 0;
		m_dl_bandwidth_quota.max = 0;

		if (m_paused)
		{
			// let the stats fade out to 0
 			m_stat.second_tick();
			return;
		}

		// ---- WEB SEEDS ----

		// if we're a seed, we don't need to connect to any web-seed
		if (!is_seed())
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
			i != m_connections.end(); ++i)
		{
			peer_connection* p = i->second;
			m_stat += p->statistics();
			// updates the peer connection's ul/dl bandwidth
			// resource requests
			p->second_tick();

			m_ul_bandwidth_quota.used += p->m_ul_bandwidth_quota.used;
			m_ul_bandwidth_quota.min += p->m_ul_bandwidth_quota.min;
			m_dl_bandwidth_quota.used += p->m_dl_bandwidth_quota.used;
			m_dl_bandwidth_quota.min += p->m_dl_bandwidth_quota.min;

			m_ul_bandwidth_quota.max = saturated_add(
				m_ul_bandwidth_quota.max
				, p->m_ul_bandwidth_quota.max);

			m_dl_bandwidth_quota.max = saturated_add(
				m_dl_bandwidth_quota.max
				, p->m_dl_bandwidth_quota.max);
		}

		m_ul_bandwidth_quota.max
			= std::min(m_ul_bandwidth_quota.max, m_upload_bandwidth_limit);

		if (m_upload_bandwidth_limit == resource_request::inf)
			m_ul_bandwidth_quota.max = resource_request::inf;

		m_dl_bandwidth_quota.max
			= std::min(m_dl_bandwidth_quota.max, m_download_bandwidth_limit);

		if (m_download_bandwidth_limit == resource_request::inf)
			m_dl_bandwidth_quota.max = resource_request::inf;

		accumulator += m_stat;
		m_stat.second_tick();
	}

	void torrent::distribute_resources()
	{
		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;
			m_policy->pulse();
		}

		// distribute allowed upload among the peers
		allocate_resources(m_ul_bandwidth_quota.given
			, m_connections
			, &peer_connection::m_ul_bandwidth_quota);

		// distribute allowed download among the peers
		allocate_resources(m_dl_bandwidth_quota.given
			, m_connections
			, &peer_connection::m_dl_bandwidth_quota);
	
		using boost::bind;

		// tell all peers to reset their used quota. This is
		// a new second and they can again use up their quota

		for (std::map<tcp::endpoint, peer_connection*>::iterator i
			= m_connections.begin(); i != m_connections.end(); ++i)
		{
			i->second->reset_upload_quota();
			assert(i->second->m_dl_bandwidth_quota.used
				<= i->second->m_dl_bandwidth_quota.given);
		}
	}

	bool torrent::verify_piece(int piece_index)
	{
		assert(m_storage.get());
		assert(piece_index >= 0);
		assert(piece_index < m_torrent_file.num_pieces());
		assert(piece_index < (int)m_have_pieces.size());

		int size = static_cast<int>(m_torrent_file.piece_size(piece_index));
		std::vector<char> buffer(size);
		assert(size > 0);
		m_storage->read(&buffer[0], piece_index, 0, size);

		hasher h;
		h.update(&buffer[0], size);
		sha1_hash digest = h.final();

		if (m_torrent_file.hash_for_piece(piece_index) != digest)
			return false;

		if (!m_have_pieces[piece_index])
			m_num_pieces++;
		m_have_pieces[piece_index] = true;

		assert(std::accumulate(m_have_pieces.begin(), m_have_pieces.end(), 0)
			== m_num_pieces);
		return true;
	}

	const tcp::endpoint& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	bool torrent::is_allocating() const
	{ return m_storage.get() && m_storage->is_allocating(); }
	
	std::vector<char> const& torrent::metadata() const
	{
		if (m_metadata.empty())
		{
			bencode(std::back_inserter(m_metadata)
				, m_torrent_file.create_info_metadata());
			assert(hasher(&m_metadata[0], m_metadata.size()).final()
				== m_torrent_file.info_hash());
		}
		assert(!m_metadata.empty());
		return m_metadata;
	}
	
	torrent_status torrent::status() const
	{
		assert(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

		st.block_size = block_size();

		
		st.num_peers = (int)std::count_if(m_connections.begin(),	m_connections.end(),
			boost::bind<bool>(std::logical_not<bool>(), boost::bind(&peer_connection::is_connecting,
			boost::bind(&std::map<tcp::endpoint,peer_connection*>::value_type::second, _1))));

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

		st.next_announce = next_announce()
			- second_clock::universal_time();
		if (st.next_announce.is_negative()) st.next_announce
			= boost::posix_time::seconds(0);
		st.announce_interval = boost::posix_time::seconds(m_duration);

		if (m_last_working_tracker >= 0)
		{
			st.current_tracker
				= m_trackers[m_last_working_tracker].url;
		}

		// if we don't have any metadata, stop here

		if (!valid_metadata())
		{
			if (m_got_tracker_response == false)
				st.state = torrent_status::connecting_to_tracker;
			else
				st.state = torrent_status::downloading_metadata;

			if (m_metadata_size == 0) st.progress = 0.f;
			else st.progress = std::min(1.f, m_metadata_progress / (float)m_metadata_size);

			return st;
		}

		// fill in status that depends on metadata

		st.total_wanted = m_torrent_file.total_size();

		if (m_picker.get() && (m_picker->num_filtered() > 0
			|| m_picker->num_have_filtered() > 0))
		{
			int filtered_pieces = m_picker->num_filtered()
				+ m_picker->num_have_filtered();
			int last_piece_index = m_torrent_file.num_pieces() - 1;
			if (m_picker->is_filtered(last_piece_index))
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

		if (m_got_tracker_response == false)
			st.state = torrent_status::connecting_to_tracker;
		else if (m_num_pieces == (int)m_have_pieces.size())
			st.state = torrent_status::seeding;
		else if (st.total_wanted_done == st.total_wanted)
			st.state = torrent_status::finished;
		else
			st.state = torrent_status::downloading;

		st.num_seeds = num_seeds();
		st.distributed_copies = m_picker->distributed_copies();
		return st;
	}

	int torrent::num_seeds() const
	{
		return (int)std::count_if(m_connections.begin(),	m_connections.end(),
			boost::bind(&peer_connection::is_seed,
				boost::bind(&std::map<tcp::endpoint,peer_connection*>::value_type::second, _1)));
	}

	int div_round_up(int numerator, int denominator)
	{
		return (numerator + denominator - 1) / denominator;
	}

	std::pair<int, int> req_to_offset(std::pair<int, int> req, int total_size)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.second <= 256);
		assert(req.first + req.second <= 256);

		int start = div_round_up(req.first * total_size, 256);
		int size = div_round_up((req.first + req.second) * total_size, 256) - start;
		return std::make_pair(start, size);
	}

	std::pair<int, int> offset_to_req(std::pair<int, int> offset, int total_size)
	{
		int start = offset.first * 256 / total_size;
		int size = (offset.first + offset.second) * 256 / total_size - start;

		std::pair<int, int> ret(start, size);
	
		assert(start >= 0);
		assert(size > 0);
		assert(start <= 256);
		assert(start + size <= 256);

		// assert the identity of this function
#ifndef NDEBUG
		std::pair<int, int> identity = req_to_offset(ret, total_size);
		assert(offset == identity);
#endif
		return ret;
	}

	bool torrent::received_metadata(char const* buf, int size, int offset, int total_size)
	{
		INVARIANT_CHECK;

		if (valid_metadata()) return false;

		if ((int)m_metadata.size() < total_size)
			m_metadata.resize(total_size);

		std::copy(
			buf
			, buf + size
			, &m_metadata[offset]);

		if (m_have_metadata.empty())
			m_have_metadata.resize(256, false);

		std::pair<int, int> req = offset_to_req(std::make_pair(offset, size)
			, total_size);

		assert(req.first + req.second <= (int)m_have_metadata.size());

		std::fill(
			m_have_metadata.begin() + req.first
			, m_have_metadata.begin() + req.first + req.second
			, true);
	
		bool have_all = std::count(
			m_have_metadata.begin()
			, m_have_metadata.end()
			, true) == 256;

		if (!have_all) return false;

		hasher h;
		h.update(&m_metadata[0], (int)m_metadata.size());
		sha1_hash info_hash = h.final();

		if (info_hash != m_torrent_file.info_hash())
		{
			std::fill(
				m_have_metadata.begin()
				, m_have_metadata.begin() + req.first + req.second
				, false);
			m_metadata_progress = 0;
			m_metadata_size = 0;
			if (m_ses.m_alerts.should_post(alert::info))
			{
				m_ses.m_alerts.post_alert(metadata_failed_alert(
					get_handle(), "invalid metadata received from swarm"));
			}

			return false;
		}

		entry metadata = bdecode(m_metadata.begin(), m_metadata.end());
		m_torrent_file.parse_info_section(metadata);

		{
			boost::mutex::scoped_lock(m_checker.m_mutex);

			boost::shared_ptr<detail::piece_checker_data> d(
					new detail::piece_checker_data);
			d->torrent_ptr = shared_from_this();
			d->save_path = m_save_path;
			d->info_hash = m_torrent_file.info_hash();
			// add the torrent to the queue to be checked
			m_checker.m_torrents.push_back(d);
			typedef detail::session_impl::torrent_map torrent_map;
			torrent_map::iterator i = m_ses.m_torrents.find(
				m_torrent_file.info_hash());
			assert(i != m_ses.m_torrents.end());
			m_ses.m_torrents.erase(i);
			// and notify the thread that it got another
			// job in its queue
			m_checker.m_cond.notify_one();
		}
		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle(), "metadata successfully received from swarm"));
		}

		// clear the storage for the bitfield
		std::vector<bool>().swap(m_have_metadata);
		std::vector<int>().swap(m_requested_metadata);

		return true;
	}

	std::pair<int, int> torrent::metadata_request()
	{
		// count the number of peers that supports the
		// extension and that has metadata
		int peers = 0;
		typedef std::map<tcp::endpoint, peer_connection*> conn_map;
		for (conn_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			bt_peer_connection* c = dynamic_cast<bt_peer_connection*>(i->second);
			if (c == 0) continue;
			if (!c->supports_extension(
				extended_metadata_message))
				continue;
			if (!c->has_metadata())
				continue;
			++peers;
		}

		// the number of blocks to request
		int num_blocks = 256 / (peers + 1);
		if (num_blocks < 1) num_blocks = 1;
		assert(num_blocks <= 128);

		int min_element = std::numeric_limits<int>::max();
		int best_index = 0;
		for (int i = 0; i < 256 - num_blocks + 1; ++i)
		{
			int min = *std::min_element(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks);
			min += std::accumulate(m_requested_metadata.begin() + i
				, m_requested_metadata.begin() + i + num_blocks, (int)0);

			if (min_element > min)
			{
				best_index = i;
				min_element = min;
			}
		}

		std::pair<int, int> ret(best_index, num_blocks);
		for (int i = ret.first; i < ret.first + ret.second; ++i)
			m_requested_metadata[i]++;

		assert(ret.first >= 0);
		assert(ret.second > 0);
		assert(ret.second <= 256);
		assert(ret.first + ret.second <= 256);

		return ret;
	}

	void torrent::cancel_metadata_request(std::pair<int, int> req)
	{
		for (int i = req.first; i < req.first + req.second; ++i)
		{
            assert(m_requested_metadata[i] > 0);
			if (m_requested_metadata[i] > 0)
				--m_requested_metadata[i];
		}
	}

	void torrent::tracker_request_timed_out(
		tracker_request const&)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
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

void torrent::metadata_progress(int total_size, int received)
{
	m_metadata_progress += received;
	m_metadata_size = total_size;
}

