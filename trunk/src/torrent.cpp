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
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::isalnum;
};
#endif

using namespace libtorrent;
using namespace boost::posix_time;


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

	int calculate_block_size(const torrent_info& i)
	{
		const int default_block_size = 16 * 1024;

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
		find_peer_by_ip(const address& a, const torrent* t)
			: ip(a)
			, tor(t)
		{ assert(t != 0); }
		
		bool operator()(const detail::session_impl::connection_map::value_type& c) const
		{
			if (c.first->sender().ip() != ip.ip()) return false;
			if (tor != c.second->associated_torrent()) return false;
			return true;
		}

		const address& ip;
		const torrent* tor;
	};

	struct peer_by_id
	{
		peer_by_id(const peer_id& i): id(i) {}
		
		bool operator()(const std::pair<address, peer_connection*>& p) const
		{
			if (p.second->get_peer_id() != id) return false;
			// have a special case for all zeros. We can have any number
			// of peers with that id, since it's used to indicate no id.
			if (std::count(id.begin(), id.end(), 0) == 20) return false;
			return true;
		}

		const peer_id& id;
	};

}

namespace libtorrent
{
	torrent::torrent(
		detail::session_impl& ses
		, entry const& metadata
		, boost::filesystem::path const& save_path
		, address const& net_interface
		, bool compact_mode)
		: m_torrent_file(metadata)
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
		, m_net_interface(net_interface.ip(), address::any_port)
		, m_upload_bandwidth_limit(std::numeric_limits<int>::max())
		, m_download_bandwidth_limit(std::numeric_limits<int>::max())
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
	{
		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();

		m_policy.reset(new policy(this));
		bencode(std::back_inserter(m_metadata), metadata["info"]);
		init();
	}

	torrent::torrent(
		detail::session_impl& ses
		, char const* tracker_url
		, sha1_hash const& info_hash
		, boost::filesystem::path const& save_path
		, address const& net_interface
		, bool compact_mode)
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
		, m_net_interface(net_interface.ip(), address::any_port)
		, m_upload_bandwidth_limit(std::numeric_limits<int>::max())
		, m_download_bandwidth_limit(std::numeric_limits<int>::max())
		, m_save_path(complete(save_path))
		, m_compact_mode(compact_mode)
	{
		m_uploads_quota.min = 2;
		m_connections_quota.min = 2;
		// this will be corrected the next time the main session
		// distributes resources, i.e. on average in 0.5 seconds
		m_connections_quota.given = 100;
		m_uploads_quota.max = std::numeric_limits<int>::max();
		m_connections_quota.max = std::numeric_limits<int>::max();

		m_trackers.push_back(announce_entry(tracker_url));
		m_requested_metadata.resize(256, 0);
		m_policy.reset(new policy(this));
		m_torrent_file.add_tracker(tracker_url);
	}

	torrent::~torrent()
	{
		assert(m_connections.empty());
		if (m_ses.m_abort) m_abort = true;
	}

	void torrent::init()
	{
		assert(m_torrent_file.is_valid());
		assert(m_torrent_file.num_files() > 0);
		assert(m_torrent_file.total_size() > 0);

		m_have_pieces.resize(m_torrent_file.num_pieces(), false);
		m_storage = std::auto_ptr<piece_manager>(new piece_manager(m_torrent_file, m_save_path));
		m_block_size = calculate_block_size(m_torrent_file);
		m_picker = std::auto_ptr<piece_picker>(new piece_picker(
				static_cast<int>(m_torrent_file.piece_length() / m_block_size)
				, static_cast<int>((m_torrent_file.total_size()+m_block_size-1)/m_block_size)));
	}

	void torrent::use_interface(const char* net_interface)
	{
		m_net_interface = address(net_interface, address::any_port);
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

	void torrent::tracker_response(
		tracker_request const&
		, std::vector<peer_entry>& peer_list
		, int interval
		, int complete
		, int incomplete)
	{
		m_failed_trackers = 0;
		// less than 5 minutes announce intervals
		// are insane.
		if (interval < 60 * 5) interval = 60 * 5;

		m_last_working_tracker
			= prioritize_tracker(m_currently_trying_tracker);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		if (peer_list.empty())
		{
			// if the peer list is empty, we should contact the
			// tracker soon again to see if there are any peers
			m_next_request = second_clock::universal_time() + boost::posix_time::minutes(2);
		}
		else
		{
			m_next_request = second_clock::universal_time() + boost::posix_time::seconds(m_duration);
		}

		if (complete >= 0) m_complete = complete;
		if (incomplete >= 0) m_incomplete = incomplete;
		
		// connect to random peers from the list
		std::random_shuffle(peer_list.begin(), peer_list.end());


#ifdef TORRENT_VERBOSE_LOGGING
		std::stringstream s;
		s << "TRACKER RESPONSE:\n"
			"interval: " << m_duration << "\n"
			"peers:\n";
		for (std::vector<peer_entry>::const_iterator i = peer_list.begin();
			i != peer_list.end();
			++i)
		{
			s << "  " << std::setfill(' ') << std::setw(16) << i->ip
				<< " " << std::setw(5) << std::dec << i->port << "  ";
			if (!i->id.is_all_zeros()) s << " " << i->id << " " << identify_client(i->id);
			s << "\n";
		}
		debug_log(s.str());
#endif
		// for each of the peers we got from the tracker
		for (std::vector<peer_entry>::iterator i = peer_list.begin();
			i != peer_list.end();
			++i)
		{
			// don't make connections to ourself
			if (i->id == m_ses.get_peer_id())
				continue;

			address a(i->ip.c_str(), i->port);

			m_policy->peer_from_tracker(a, i->id);
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
		return m_torrent_file.total_size() - bytes_done();
	}

	size_type torrent::bytes_done() const
	{
		if (!valid_metadata()) return 0;

		assert(m_picker.get());
		const int last_piece = m_torrent_file.num_pieces()-1;

		size_type total_done
			= m_num_pieces * m_torrent_file.piece_length();

		// if we have the last piece, we have to correct
		// the amount we have, since the first calculation
		// assumed all pieces were of equal size
		if (m_have_pieces[last_piece])
		{
			total_done -= m_torrent_file.piece_length();
			total_done += m_torrent_file.piece_size(last_piece);
		}

		const std::vector<piece_picker::downloading_piece>& dl_queue
			= m_picker->get_download_queue();

		const int blocks_per_piece = static_cast<int>(m_torrent_file.piece_length() / m_block_size);

		for (std::vector<piece_picker::downloading_piece>::const_iterator i =
			dl_queue.begin();
			i != dl_queue.end();
			++i)
		{
			assert(!m_have_pieces[i->index]);

			for (int j = 0; j < blocks_per_piece; ++j)
			{
				total_done += (i->finished_blocks[j]) * m_block_size;
			}

			// correction if this was the last piece
			// and if we have the last block
			if (i->index == last_piece
				&& i->finished_blocks[m_picker->blocks_in_last_piece()-1])
			{
				total_done -= m_block_size;
				total_done += m_torrent_file.piece_size(last_piece) % m_block_size;
			}
		}

		std::map<piece_block, int> downloading_piece;
		for (const_peer_iterator i = begin(); i != end(); ++i)
		{
			boost::optional<piece_block_progress> p
				= i->second->downloading_piece();
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
			total_done += i->second;
		return total_done;
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

		std::vector<address> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// first, build a set of all peers that participated
		std::set<address> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		for (std::set<address>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_invalid_data();

			// either, we have received too many failed hashes
			// or this was the only peer that sent us this piece.
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
		m_event = tracker_request::stopped;
		// disconnect all peers and close all
		// files belonging to the torrent
		disconnect_all();
		if (m_storage.get()) m_storage->release_files();
	}

	void torrent::announce_piece(int index)
	{
		assert(m_picker.get());
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		std::vector<address> downloaders;
		m_picker->get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		std::set<address> peers;
		std::copy(downloaders.begin(), downloaders.end(), std::inserter(peers, peers.begin()));

		for (std::set<address>::iterator i = peers.begin()
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

		if (filter) m_picker->mark_as_filtered(index);
		else m_picker->mark_as_unfiltered(index);
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
		req.id = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		if (req.left == -1) req.left = 1000;
		req.event = m_event;
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

		peer_iterator i = m_connections.find(p->get_socket()->sender());
		assert(i != m_connections.end());

		// if the peer_connection was downloading any pieces
		// abort them
		for (std::deque<piece_block>::const_iterator i = p->download_queue().begin();
			i != p->download_queue().end();
			++i)
		{
			m_picker->abort_download(*i);
		}

		if (valid_metadata())
		{
			std::vector<int> piece_list;
			const std::vector<bool>& pieces = p->get_bitfield();

			for (std::vector<bool>::const_iterator i = pieces.begin();
				i != pieces.end();
				++i)
			{
				if (*i) piece_list.push_back(static_cast<int>(i - pieces.begin()));
			}

			std::random_shuffle(piece_list.begin(), piece_list.end());

			for (std::vector<int>::iterator i = piece_list.begin();
				i != piece_list.end();
				++i)
			{
				peer_lost(*i);
			}
		}

		m_policy->connection_closed(*p);
		m_connections.erase(i);
	}

	peer_connection& torrent::connect_to_peer(const address& a)
	{
		boost::shared_ptr<socket> s(new socket(socket::tcp, false));
		s->connect(a, m_net_interface);
		boost::shared_ptr<peer_connection> c(new peer_connection(
			m_ses
			, m_ses.m_selector
			, this
			, s));

		detail::session_impl::connection_map::iterator p =
			m_ses.m_connections.insert(std::make_pair(s, c)).first;

		// add the newly connected peer to this torrent's peer list
		assert(m_connections.find(p->second->get_socket()->sender())
			== m_connections.end());
		
		m_connections.insert(
			std::make_pair(
				p->second->get_socket()->sender()
				, boost::get_pointer(p->second)));

		m_ses.m_selector.monitor_readability(s);
		m_ses.m_selector.monitor_errors(s);
		return *c;
	}

	void torrent::attach_peer(peer_connection* p)
	{
		assert(p != 0);
		assert(m_connections.find(p->get_socket()->sender()) == m_connections.end());
		assert(!p->is_local());

		m_connections.insert(std::make_pair(p->get_socket()->sender(), p));

		detail::session_impl::connection_map::iterator i
			= m_ses.m_connections.find(p->get_socket());
		assert(i != m_ses.m_connections.end());

		m_policy->new_connection(*i->second);
	}

	void torrent::disconnect_all()
	{
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			assert(i->second->associated_torrent() == this);
			i->second->disconnect();
		}
	}

	void torrent::completed()
	{
		if (alerts().should_post(alert::info))
		{
			alerts().post_alert(torrent_finished_alert(
				get_handle()
				, "torrent has finished downloading"));
		}


	// disconnect all seeds
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			assert(i->second->associated_torrent() == this);
			if (i->second->is_seed())
				i->second->disconnect();
		}

		m_storage->release_files();

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

	void torrent::check_files(detail::piece_checker_data& data,
		boost::mutex& mutex, bool lock_session)
	{
		assert(m_storage.get());
 		m_storage->check_pieces(mutex, data, m_have_pieces, m_compact_mode);

		// TODO: temporary solution. This function should only
		// be called from the checker thread, and then this
		// hack can be removed (because the session should always
		// be locked then)
		boost::mutex temp;
		boost::mutex* m = &temp;
		if (lock_session) m = &m_ses.m_mutex;
		
		boost::mutex::scoped_lock l(mutex);
		boost::mutex::scoped_lock l2(*m);
		m_num_pieces = std::count(
			m_have_pieces.begin()
		  , m_have_pieces.end()
		  , true);

		m_picker->files_checked(m_have_pieces, data.unfinished_pieces);
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

		m_dl_bandwidth_quota.max
			= std::min(m_dl_bandwidth_quota.max, m_download_bandwidth_limit);

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

		for (std::map<address, peer_connection*>::iterator i
			= m_connections.begin(); i != m_connections.end(); ++i)
		{
			i->second->reset_upload_quota();
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

	const address& torrent::current_tracker() const
	{
		return m_tracker_address;
	}

	torrent_status torrent::status() const
	{
		assert(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

		st.block_size = block_size();
		st.num_peers = num_peers();
		st.num_complete = m_complete;
		st.num_incomplete = m_incomplete;
		st.paused = m_paused;
		st.total_done = bytes_done();

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

			st.progress = std::count(
				m_have_metadata.begin()
				, m_have_metadata.end()
				, true) / 256.f;

			return st;
		}

		// fill in status that depends on metadata

		st.progress = st.total_done
			/ static_cast<float>(m_torrent_file.total_size());

		st.pieces = &m_have_pieces;

		if (m_got_tracker_response == false)
			st.state = torrent_status::connecting_to_tracker;
		else if (m_num_pieces == (int)m_have_pieces.size())
			st.state = torrent_status::seeding;
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
				boost::bind(&std::map<address,peer_connection*>::value_type::second, _1)));
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
			return false;
		}

		m_torrent_file.parse_info_section(bdecode(m_metadata.begin(), m_metadata.end()));

		init();

		boost::mutex m;
		detail::piece_checker_data d;
		d.abort = false;
		// TODO: this check should be moved to the checker thread
		// not really a high priority, since no files would usually
		// be available if the metadata wasn't available.
		check_files(d, m, false);

		if (m_ses.m_alerts.should_post(alert::info))
		{
			m_ses.m_alerts.post_alert(metadata_received_alert(
				get_handle(), "metadata successfully received from swarm"));
		}

		// all peer connections have to initialize themselves now that the metadata
		// is available
		typedef std::map<address, peer_connection*> conn_map;
		for (conn_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			i->second->init();
		}

#ifndef NDEBUG
		m_picker->integrity_check(this);
#endif

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
		typedef std::map<address, peer_connection*> conn_map;
		for (conn_map::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			if (!i->second->supports_extension(
				peer_connection::extended_metadata_message))
				continue;
			if (!i->second->has_metadata())
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
#ifdef TORRENT_VERBOSE_LOGGING
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
#ifdef TORRENT_VERBOSE_LOGGING
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


#ifdef TORRENT_VERBOSE_LOGGING
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << line << "\n";
	}
#endif

}

