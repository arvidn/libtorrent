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


namespace
{

	enum
	{
		// wait 60 seconds before retrying a failed tracker
		tracker_retry_delay = 60
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
		, const torrent_info& torrent_file
		, const boost::filesystem::path& save_path
		, address const& net_interface)
		: m_block_size(calculate_block_size(torrent_file))
		, m_abort(false)
		, m_paused(false)
		, m_event(tracker_request::started)
		, m_torrent_file(torrent_file)
		, m_storage(m_torrent_file, save_path)
		, m_next_request(boost::posix_time::second_clock::local_time())
		, m_duration(1800)
		, m_policy(new policy(this)) // warning: uses this in member init list
		, m_ses(ses)
		, m_picker(static_cast<int>(torrent_file.piece_length() / m_block_size),
			static_cast<int>((torrent_file.total_size()+m_block_size-1)/m_block_size))
		, m_last_working_tracker(-1)
		, m_currently_trying_tracker(0)
		, m_time_scaler(0)
		, m_priority(.5)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
		, m_net_interface(net_interface.ip(), address::any_port)
		, m_upload_bandwidth_limit(std::numeric_limits<int>::max())
		, m_download_bandwidth_limit(std::numeric_limits<int>::max())
	{
		assert(torrent_file.begin_files() != torrent_file.end_files());
		m_have_pieces.resize(torrent_file.num_pieces(), false);
	}

	torrent::~torrent()
	{
		assert(m_connections.empty());
		if (m_ses.m_abort) m_abort = true;
	}

	void torrent::use_interface(const char* net_interface)
	{
		m_net_interface = address(net_interface, address::any_port);
	}

	void torrent::tracker_response(
		std::vector<peer_entry>& peer_list
		, int interval)
	{
		// less than 60 seconds announce intervals
		// are insane.
		if (interval < 60) interval = 60;

		m_last_working_tracker
			= m_torrent_file.prioritize_tracker(m_currently_trying_tracker);
		m_next_request = boost::posix_time::second_clock::local_time()
			+ boost::posix_time::seconds(m_duration);
		m_currently_trying_tracker = 0;

		m_duration = interval;
		m_next_request = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(m_duration);

		// connect to random peers from the list
		std::random_shuffle(peer_list.begin(), peer_list.end());


#ifndef NDEBUG
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

		m_got_tracker_response = true;
	}
/*
	bool torrent::has_peer(const peer_id& id) const
	{
		assert(std::count_if(m_connections.begin()
			, m_connections.end()
			, peer_by_id(id)) <= 1);

		// pretend that we are connected to
		// ourself to avoid real connections
		// to ourself
		if (id == m_ses.m_peer_id) return true;

		return std::find_if(
			m_connections.begin()
			, m_connections.end()
			, peer_by_id(id))
			!= m_connections.end();
	}
*/

	size_type torrent::bytes_left() const
	{
		return m_torrent_file.total_size() - bytes_done();
	}

	size_type torrent::bytes_done() const
	{
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
			= m_picker.get_download_queue();

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
				&& i->finished_blocks[m_picker.blocks_in_last_piece()-1])
			{
				total_done -= m_block_size;
				total_done += m_torrent_file.piece_size(last_piece) % m_block_size;
			}
		}

		// TODO: may report too much if two peers are downloading
		// the same block
		for (const_peer_iterator i = begin();
			i != end();
			++i)
		{
			boost::optional<piece_block_progress> p
				= i->second->downloading_piece();
			if (p)
			{
				if (m_have_pieces[p->piece_index])
					continue;
				if (m_picker.is_finished(piece_block(p->piece_index, p->block_index)))
					continue;

				total_done += p->bytes_downloaded;
				assert(p->bytes_downloaded <= p->full_block_bytes);
			}
		}
		return total_done;
	}

	void torrent::piece_failed(int index)
	{
		assert(index >= 0);
	  	assert(index < m_torrent_file.num_pieces());

		if (m_ses.m_alerts.should_post(alert::info))
		{
			std::stringstream s;
			s << "hash for piece " << index << " failed";
			m_ses.m_alerts.post_alert(hash_failed_alert(get_handle(), index, s.str()));
		}
		std::vector<address> downloaders;
		m_picker.get_downloaders(downloaders, index);

		// decrease the trust point of all peers that sent
		// parts of this piece.
		for (std::vector<address>::iterator i = downloaders.begin();
			i != downloaders.end();
			++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_invalid_data();

			if (p->second->trust_points() <= -7)
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
		m_picker.restore_piece(index);
		m_storage.mark_failed(index);

		assert(m_have_pieces[index] == false);
	}


	void torrent::announce_piece(int index)
	{
		assert(index >= 0);
		assert(index < m_torrent_file.num_pieces());

		std::vector<address> downloaders;
		m_picker.get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		for (std::vector<address>::iterator i = downloaders.begin();
			i != downloaders.end();
			++i)
		{
			peer_iterator p = m_connections.find(*i);
			if (p == m_connections.end()) continue;
			p->second->received_valid_data();
		}

		m_picker.we_have(index);
		for (peer_iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			i->second->announce_piece(index);
	}

	std::string torrent::tracker_login() const
	{
		if (m_username.empty() && m_password.empty()) return "";
		return m_username + ":" + m_password;
	}

	tracker_request torrent::generate_tracker_request()
	{
		m_duration = 1800;
		m_next_request
			= boost::posix_time::second_clock::local_time()
			+ boost::posix_time::seconds(tracker_retry_delay);

		tracker_request req;
		req.info_hash = m_torrent_file.info_hash();
		req.id = m_ses.get_peer_id();
		req.downloaded = m_stat.total_payload_download();
		req.uploaded = m_stat.total_payload_upload();
		req.left = bytes_left();
		req.event = m_event;
		req.url = m_torrent_file.trackers()[m_currently_trying_tracker].url;
		req.num_want = (m_policy->get_max_connections()
			- m_policy->num_peers()) * 2;

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
			m_picker.abort_download(*i);
		}

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
				, "torrent is finished downloading"));
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

		// make the next tracker request
		// be a completed-event
		m_event = tracker_request::completed;
		force_tracker_request();
	}


	void torrent::try_next_tracker()
	{
		m_currently_trying_tracker++;

		if ((unsigned)m_currently_trying_tracker >= m_torrent_file.trackers().size())
		{
			// if we've looped the tracker list, wait a bit before retrying
			m_currently_trying_tracker = 0;
			m_next_request = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(tracker_retry_delay);
		}
		else
		{
			// don't delay before trying the next tracker
			m_next_request = boost::posix_time::second_clock::local_time();
		}
	}

	void torrent::check_files(detail::piece_checker_data& data,
		boost::mutex& mutex)
	{
 		m_storage.check_pieces(mutex, data, m_have_pieces);
		m_num_pieces = std::accumulate(
			m_have_pieces.begin()
		  , m_have_pieces.end()
		  , 0);

		m_picker.files_checked(m_have_pieces, data.unfinished_pieces);
#ifndef NDEBUG
		m_picker.integrity_check(this);
#endif
	}

	alert_manager& torrent::alerts() const
	{
		return m_ses.m_alerts;
	}

	torrent_handle torrent::get_handle() const
	{
		return torrent_handle(&m_ses, 0, m_torrent_file.info_hash());
	}



#ifndef NDEBUG
	void torrent::check_invariant()
	{
		assert(m_num_pieces
			== std::count(m_have_pieces.begin(), m_have_pieces.end(), true));
		assert(m_priority >= 0.f && m_priority < 1.f);
		assert(m_block_size > 0);
		assert((m_torrent_file.piece_length() % m_block_size) == 0);
	}
#endif

	void torrent::pause()
	{
		disconnect_all();
		// TODO: announce to tracker that we stopped!
		// possibly with some delay
		m_paused = true;
	}

	void torrent::resume()
	{
		m_paused = false;
		// TODO: announce to the tracker that we started.
		// possibly with some delay
		// make pulse be called as soon as possible
		m_time_scaler = 0;
	}

	void torrent::second_tick()
	{
		if (m_paused) return;

		m_time_scaler--;
		if (m_time_scaler <= 0)
		{
			m_time_scaler = 10;
			m_policy->pulse();
		}

		m_ul_bandwidth_quota.used = 0;
		m_ul_bandwidth_quota.max = 0;
		m_ul_bandwidth_quota.min = 0;

		m_dl_bandwidth_quota.used = 0;
		m_dl_bandwidth_quota.min = 0;
		m_dl_bandwidth_quota.max = 0;

		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
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

		m_stat.second_tick();
	}

	void torrent::distribute_resources()
	{
		// distribute allowed upload among the peers
		allocate_resources(m_ul_bandwidth_quota.given
			, m_connections
			, &peer_connection::m_ul_bandwidth_quota);

		// distribute allowed download among the peers
		allocate_resources(m_dl_bandwidth_quota.given
			, m_connections
			, &peer_connection::m_dl_bandwidth_quota);

		// tell all peers to reset their used quota. This is
		// a new second and they can again use up their quota
		for (std::map<address, peer_connection*>::iterator i = m_connections.begin();
			i != m_connections.end(); ++i)
		{
			i->second->reset_upload_quota();
		}
	}

	bool torrent::verify_piece(int piece_index)
	{
		assert(piece_index >= 0);
		assert(piece_index < m_torrent_file.num_pieces());

		int size = static_cast<int>(m_torrent_file.piece_size(piece_index));
		std::vector<char> buffer(size);
		assert(size > 0);
		m_storage.read(&buffer[0], piece_index, 0, size);

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

		if (m_last_working_tracker >= 0)
		{
            st.current_tracker
				= m_torrent_file.trackers()[m_last_working_tracker].url;
		}

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

		// transfer rate
		st.download_rate = m_stat.download_rate();
		st.upload_rate = m_stat.upload_rate();

		st.progress = st.total_done
			/ static_cast<float>(m_torrent_file.total_size());

		st.next_announce = next_announce()
			- boost::posix_time::second_clock::local_time();
		if (st.next_announce.is_negative()) st.next_announce
			= boost::posix_time::seconds(0);
		st.announce_interval = boost::posix_time::seconds(m_duration);

		st.num_peers = (int)m_connections.size();

		st.pieces = &m_have_pieces;

		if (m_got_tracker_response == false)
			st.state = torrent_status::connecting_to_tracker;
		else if (m_num_pieces == (int)m_have_pieces.size())
			st.state = torrent_status::seeding;
		else
			st.state = torrent_status::downloading;

		return st;
	}

	void torrent::tracker_request_timed_out()
	{
#ifndef NDEBUG
		debug_log("*** tracker timed out");
#endif
		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_torrent_file.trackers()[m_currently_trying_tracker].url
				<< "\" timed out";
			m_ses.m_alerts.post_alert(tracker_alert(get_handle(), s.str()));
		}
		// TODO: increase the retry_delay for
		// each failed attempt on the same tracker!
		// maybe we should add a counter that keeps
		// track of how many times a specific tracker
		// has timed out?
		try_next_tracker();
	}

	// TODO: with some response codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(int response_code, const std::string& str)
	{
#ifndef NDEBUG
		debug_log(std::string("*** tracker error: ") + str);
#endif
		if (m_ses.m_alerts.should_post(alert::warning))
		{
			std::stringstream s;
			s << "tracker: \""
				<< m_torrent_file.trackers()[m_currently_trying_tracker].url
				<< "\" " << str;
			m_ses.m_alerts.post_alert(tracker_alert(get_handle(), s.str()));
		}


		try_next_tracker();
	}


#ifndef NDEBUG
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << line << "\n";
	}
#endif

}

