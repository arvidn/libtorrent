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

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/url_handler.hpp"
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
			return i.piece_length();
		}

		// if pieces are too large, adjust the block size
		if (i.piece_length() / default_block_size > piece_picker::max_blocks_per_piece)
		{
			return i.piece_length() / piece_picker::max_blocks_per_piece;
		}

		// otherwise, go with the default
		return default_block_size;
	}


	peer_entry extract_peer_info(const entry& e)
	{
		peer_entry ret;

		const entry::dictionary_type& info = e.dict();

		// extract peer id (if any)
		entry::dictionary_type::const_iterator i = info.find("peer id");
		if (i != info.end())
		{
			if (i->second.string().length() != 20) throw std::runtime_error("invalid response from tracker");
			std::copy(i->second.string().begin(), i->second.string().end(), ret.id.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.id.begin(), 20, 0);
		}

		// extract ip
		i = info.find("ip");
		if (i == info.end()) throw std::runtime_error("invalid response from tracker");
		ret.ip = i->second.string();

		// extract port
		i = info.find("port");
		if (i == info.end()) throw std::runtime_error("invalid response from tracker");
		ret.port = i->second.integer();

		return ret;
	}

/*
	struct find_peer_by_id
	{
		find_peer_by_id(const peer_id& i, const torrent* t): id(i), tor(t) {}
		
		bool operator()(const detail::session_impl::connection_map::value_type& c) const
		{
			if (c.second->get_peer_id() != id) return false;
			if (tor != c.second->associated_torrent()) return false;
			// have a special case for all zeros. We can have any number
			// of peers with that id, since it's used to indicate no id.
			if (std::count(id.begin(), id.end(), 0) == 20) return false;
			return true;
		}

		const peer_id& id;
		const torrent* tor;
	};
*/
	struct find_peer_by_ip
	{
		find_peer_by_ip(const address& a, const torrent* t): ip(a), tor(t) {}
		
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
	std::string unescape_string(std::string const& s)
	{
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
		{
			if (*i != '%') ret += *i;
			else
			{
				if (i == s.end())
					throw std::runtime_error("invalid escaped string");
				++i;

				int high = *i - '0';
				if (i == s.end())
					throw std::runtime_error("invalid escaped string");
				++i;

				int low = *i - '0';
				if (high >= 16 || low >= 16 || high < 0 || low < 0)
					throw std::runtime_error("invalid escaped string");

				ret += char(high * 16 + low);
			}
		}
		return ret;
	}


	std::string escape_string(const char* str, int len)
	{
		static const char special_chars[] = "$-_.+!*'(),";

		std::stringstream ret;
		ret << std::hex  << std::setfill('0');
		for (int i = 0; i < len; ++i)
		{
			if (std::isalnum(static_cast<unsigned char>(*str))
				|| std::count(
					special_chars
					, special_chars+sizeof(special_chars)-1
					, *str))
			{
				ret << *str;
			}
			else
			{
				ret << "%"
					<< std::setw(2)
					<< (int)static_cast<unsigned char>(*str);
			}
			++str;
		}
		return ret.str();
	}

	torrent::torrent(
		detail::session_impl& ses
		, const torrent_info& torrent_file
		, const boost::filesystem::path& save_path)
		: m_block_size(calculate_block_size(torrent_file))
		, m_abort(false)
		, m_event(event_started)
		, m_torrent_file(torrent_file)
		, m_storage(m_torrent_file, save_path)
		, m_next_request(boost::posix_time::second_clock::local_time())
		, m_duration(1800)
		, m_policy(new policy(this))
		, m_ses(ses)
		, m_picker(torrent_file.piece_length() / m_block_size,
			(torrent_file.total_size()+m_block_size-1)/m_block_size)
		, m_last_working_tracker(0)
		, m_currently_trying_tracker(0)
		, m_time_scaler(0)
		, m_priority(.5)
		, m_num_pieces(0)
		, m_got_tracker_response(false)
		, m_ratio(0.f)
	{
		assert(torrent_file.begin_files() != torrent_file.end_files());
		m_have_pieces.resize(torrent_file.num_pieces(), false);
	}

	torrent::~torrent()
	{
		if (m_ses.m_abort) m_abort = true;
	}

	void torrent::tracker_response(const entry& e)
	{
		std::vector<peer_entry> peer_list;
		try
		{
			// parse the response
			parse_response(e, peer_list);

			m_last_working_tracker
				= m_torrent_file.prioritize_tracker(m_currently_trying_tracker);
			m_next_request = boost::posix_time::second_clock::local_time()
				+ boost::posix_time::seconds(m_duration);
			m_currently_trying_tracker = 0;

			// connect to random peers from the list
			std::random_shuffle(peer_list.begin(), peer_list.end());


#ifndef NDEBUG
			std::stringstream s;
			s << "interval: " << m_duration << "\n";
			s << "peers:\n";
			for (std::vector<peer_entry>::const_iterator i = peer_list.begin();
				i != peer_list.end();
				++i)
			{
				s << "  " << std::setfill(' ') << std::setw(16) << i->ip
					<< " " << std::setw(5) << std::dec << i->port << "  "
					<< i->id << " " << identify_client(i->id) << "\n";
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

				address a(i->ip, i->port);

				m_policy->peer_from_tracker(a, i->id);
			}

		}
		catch(type_error& e)
		{
			tracker_request_error(-1, e.what());
		}
		catch(std::runtime_error& e)
		{
			tracker_request_error(-1, e.what());
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

		const int blocks_per_piece = m_torrent_file.piece_length() / m_block_size;

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

			if (p->second->trust_points() <= -5)
			{
				// we don't trust this peer anymore
				// ban it.
				m_policy->ban_peer(*p->second);
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

	std::string torrent::generate_tracker_request(int port)
	{
		m_duration = 1800;
		m_next_request = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(m_duration);

		std::vector<char> buffer;
		std::string request = m_torrent_file.trackers()[m_currently_trying_tracker].url;

		request += "?info_hash=";
		request += escape_string(reinterpret_cast<const char*>(m_torrent_file.info_hash().begin()), 20);

		request += "&peer_id=";
		request += escape_string(reinterpret_cast<const char*>(m_ses.get_peer_id().begin()), 20);

		request += "&port=";
		request += boost::lexical_cast<std::string>(port);

		request += "&uploaded=";
		request += boost::lexical_cast<std::string>(m_stat.total_payload_upload());

		request += "&downloaded=";
		request += boost::lexical_cast<std::string>(m_stat.total_payload_download());

		request += "&left=";
		request += boost::lexical_cast<std::string>(bytes_left());

		if (m_event != event_none)
		{
			const char* event_string[] = {"started", "stopped", "completed"};
			request += "&event=";
			request += event_string[m_event];
			m_event = event_none;
		}

		// extension that tells the tracker that
		// we don't need any peer_id's in the response
		request += "&no_peer_id=1";

		return request;
	}

	void torrent::parse_response(const entry& e, std::vector<peer_entry>& peer_list)
	{
		entry::dictionary_type::const_iterator i = e.dict().find("failure reason");
		if (i != e.dict().end())
		{
			throw std::runtime_error(i->second.string().c_str());
		}

		const entry::dictionary_type& msg = e.dict();
		i = msg.find("interval");
		if (i == msg.end()) throw std::runtime_error("invalid response from tracker (no interval)");

		m_duration = i->second.integer();

		i = msg.find("peers");
		if (i == msg.end()) throw std::runtime_error("invalid response from tracker (no peers)");

		peer_list.clear();

		const entry::list_type& l = i->second.list();
		for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
		{
			peer_entry p = extract_peer_info(*i);
			peer_list.push_back(p);
		}
	}

	void torrent::remove_peer(peer_connection* p)
	{
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
			if (*i) piece_list.push_back(i - pieces.begin());
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

	#ifndef NDEBUG
//		m_picker.integrity_check(this);
	#endif
	}

	peer_connection& torrent::connect_to_peer(const address& a)
	{
		boost::shared_ptr<socket> s(new socket(socket::tcp, false));
		s->connect(a);
		boost::shared_ptr<peer_connection> c(new peer_connection(
			m_ses
			, m_ses.m_selector
			, this
			, s));

		if (m_ses.m_upload_rate != -1) c->set_send_quota(0);

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
		assert(m_connections.find(p->get_socket()->sender()) == m_connections.end());
		assert(!p->is_local());

		m_connections.insert(std::make_pair(p->get_socket()->sender(), p));

		detail::session_impl::connection_map::iterator i
			= m_ses.m_connections.find(p->get_socket());
		assert(i != m_ses.m_connections.end());

		m_policy->new_connection(*i->second);
	}

	void torrent::close_all_connections()
	{
		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			assert(i->second->associated_torrent() == this);
			
			detail::session_impl::connection_map::iterator j =
				m_ses.m_connections.find(i->second->get_socket());

			assert(j != m_ses.m_connections.end());

			// in the destructor of the peer_connection
			// it will remove itself from this torrent
			// and from the list we're iterating over.
			// so we need to increment the iterator riht
			// away.
			++i;

			m_ses.m_connections.erase(j);
		}
	}


	void torrent::try_next_tracker()
	{
		m_currently_trying_tracker++;

		if (m_currently_trying_tracker >= m_torrent_file.trackers().size())
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
	}
#endif

	void torrent::second_tick()
	{
		m_time_scaler++;
		if (m_time_scaler >= 10)
		{
			m_time_scaler = 0;
			m_policy->pulse();
		}

		for (peer_iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			peer_connection* p = i->second;
			const stat& s = p->statistics();
			m_stat += s;
			p->second_tick();
		}

		m_stat.second_tick();
	}

	bool torrent::verify_piece(int piece_index)
	{
		size_type size = m_torrent_file.piece_size(piece_index);
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

	torrent_status torrent::status() const
	{
		assert(std::accumulate(
			m_have_pieces.begin()
			, m_have_pieces.end()
			, 0) == m_num_pieces);

		torrent_status st;

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
		st.announce_interval = boost::posix_time::seconds(m_duration);


		st.num_peers = m_connections.size();

		st.pieces = &m_have_pieces;

		if (m_got_tracker_response == false)
			st.state = torrent_status::connecting_to_tracker;
		else if (m_num_pieces == m_have_pieces.size())
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

	// TODO: this function should also take the
	// HTTP-response code as an argument.
	// with some codes, we should just consider
	// the tracker as a failure and not retry
	// it anymore
	void torrent::tracker_request_error(int response_code, const char* str)
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

