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
		// TODO: if blocks_per_piece > 128 increase block-size
		return 16*1024;
	}


	peer extract_peer_info(const entry& e)
	{
		peer ret;

		const entry::dictionary_type& info = e.dict();

		// extract peer id
		entry::dictionary_type::const_iterator i = info.find("peer id");
		if (i == info.end()) throw std::runtime_error("invalid response from tracker");
		if (i->second.string().length() != 20) throw std::runtime_error("invalid response from tracker");
		std::copy(i->second.string().begin(), i->second.string().end(), ret.id.begin());

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

	std::string escape_string(const char* str, int len)
	{
		std::stringstream ret;
		ret << std::hex  << std::setfill('0');
		for (int i = 0; i < len; ++i)
		{
			// TODO: should alnum() be replaced with printable()?
			if (std::isalnum(static_cast<unsigned char>(*str))) ret << *str;
			else ret << "%" << std::setw(2) << (int)static_cast<unsigned char>(*str);
			++str;
		}
		return ret.str();
	}

	struct find_peer
	{
		find_peer(const peer_id& i, const torrent* t): id(i), tor(t) {}
		
		bool operator()(const detail::session_impl::connection_map::value_type& c) const
		{
			if (c.second->get_peer_id() != id) return false;
			if (tor != c.second->associated_torrent()) return false;
			return true;
		}

		bool operator()(const peer_connection* p) const
		{
			if (p->get_peer_id() != id) return false;
			if (tor != p->associated_torrent()) return false;
			return true;
		}

		const peer_id& id;
		const torrent* tor;
	};
}

namespace libtorrent
{

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
	{
		assert(torrent_file.begin_files() != torrent_file.end_files());
		m_have_pieces.resize(torrent_file.num_pieces(), false);
	}

	void torrent::tracker_response(const entry& e)
	{
		std::vector<peer> peer_list;
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


			std::cout << "interval: " << m_duration << "\n";
			std::cout << "peers:\n";
			for (std::vector<peer>::const_iterator i = peer_list.begin();
				i != peer_list.end();
				++i)
			{
				std::cout << "  " << std::setfill(' ') << std::setw(16) << i->ip
					<< " " << std::setw(5) << std::dec << i->port << "  "
					<< i->id << " " << extract_fingerprint(i->id) << "\n";
			}
			std::cout << std::setfill(' ');


			// for each of the peers we got from the tracker
			for (std::vector<peer>::iterator i = peer_list.begin();
				i != peer_list.end();
				++i)
			{
				// don't make connections to ourself
				if (i->id == m_ses.get_peer_id())
					continue;

				address a(i->ip, i->port);

				// if we aleady have a connection to the person, don't make another one
				if (std::find_if(m_ses.m_connections.begin(),
					m_ses.m_connections.end(),
					find_peer(i->id, this)) != m_ses.m_connections.end())
				{
					continue;
				}

				m_policy->peer_from_tracker(a, i->id);
			}

		}
		catch(type_error& e)
		{
			tracker_request_error(e.what());
		}
		catch(std::runtime_error& e)
		{
			tracker_request_error(e.what());
		}

	}

	bool torrent::has_peer(const peer_id& id) const
	{
		assert(std::count_if(m_connections.begin()
			, m_connections.end()
			, find_peer(id, this)) <= 1);

		return std::find_if(
			m_connections.begin()
			, m_connections.end()
			, find_peer(id, this))
			!= m_connections.end();
	}

	torrent::size_type torrent::bytes_left() const
	{
		size_type have_bytes = m_num_pieces * m_torrent_file.piece_length();
		int last_piece = m_torrent_file.num_pieces()-1;
		if (m_have_pieces[last_piece])
		{
			have_bytes -= m_torrent_file.piece_length()
				- m_torrent_file.piece_size(last_piece);
		}

		return m_torrent_file.total_size()
			- have_bytes;
	}


	void torrent::piece_failed(int index)
	{
		std::vector<peer_id> downloaders;
		m_picker.get_downloaders(downloaders, index);

#ifndef NDEBUG
		std::cout << "hash-test failed. Some of these peers sent invalid data:\n";
		std::copy(downloaders.begin(), downloaders.end(), std::ostream_iterator<peer_id>(std::cout, "\n"));
#endif

		// decrease the trust point of all peers that sent
		// parts of this piece.
		// TODO: implement this loop more efficient
		for (std::vector<peer_connection*>::iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			if (std::find(downloaders.begin(), downloaders.end(), (*i)->get_peer_id())
				!= downloaders.end())
			{
				(*i)->received_invalid_data();
				if ((*i)->trust_points() <= -5)
				{
					// we don't trust this peer anymore
					// ban it.
					m_policy->ban_peer(*(*i));
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
		m_picker.restore_piece(index);
	}


	void torrent::announce_piece(int index)
	{
		std::vector<peer_id> downloaders;
		m_picker.get_downloaders(downloaders, index);

		// increase the trust point of all peers that sent
		// parts of this piece.
		// TODO: implement this loop more efficient
		for (std::vector<peer_connection*>::iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			if (std::find(downloaders.begin(), downloaders.end(), (*i)->get_peer_id())
				!= downloaders.end())
			{
				(*i)->received_valid_data();
			}
		}


		m_picker.we_have(index);
		for (std::vector<peer_connection*>::iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			(*i)->announce_piece(index);
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
		request += boost::lexical_cast<std::string>(m_stat.total_upload());

		request += "&downloaded=";
		request += boost::lexical_cast<std::string>(m_stat.total_download());

		request += "&left=";
		request += boost::lexical_cast<std::string>(bytes_left());

		if (m_event != event_none)
		{
			const char* event_string[] = {"started", "stopped", "completed"};
			request += "&event=";
			request += event_string[m_event];
			m_event = event_none;
		}

		return request;
	}

	void torrent::parse_response(const entry& e, std::vector<peer>& peer_list)
	{
		entry::dictionary_type::const_iterator i = e.dict().find("failure reason");
		if (i != e.dict().end())
		{
			throw std::runtime_error(i->second.string().c_str());
		}

		const entry::dictionary_type& msg = e.dict();
		i = msg.find("interval");
		if (i == msg.end()) throw std::runtime_error("invalid response from tracker");

		m_duration = i->second.integer();

		i = msg.find("peers");
		if (i == msg.end()) throw std::runtime_error("invalid response from tracker");

		peer_list.clear();

		const entry::list_type& l = i->second.list();
		for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
		{
			peer p = extract_peer_info(*i);
			peer_list.push_back(p);
		}
	}

	void torrent::remove_peer(peer_connection* p)
	{
		std::vector<peer_connection*>::iterator i = std::find(m_connections.begin(), m_connections.end(), p);
		assert(i != m_connections.end());

		// if the peer_connection was downloading any pieces
		// abort them
		for (std::vector<piece_block>::const_iterator i = p->download_queue().begin();
			i != p->download_queue().end();
			++i)
		{
			m_picker.abort_download(*i);
		}

		for (std::size_t i = 0; i < torrent_file().num_pieces(); ++i)
		{
			if (p->has_piece(i)) peer_lost(i);
		}

//		std::cout << p->get_socket()->sender().as_string() << " *** DISCONNECT\n";

		m_policy->connection_closed(*p);
		m_connections.erase(i);

	#ifndef NDEBUG
		m_picker.integrity_check(this);
	#endif
	}

	boost::weak_ptr<peer_connection> torrent::connect_to_peer(const address& a, const peer_id& id)
	{
		boost::shared_ptr<socket> s(new socket(socket::tcp, false));
		// TODO: the send buffer size should be controllable from the outside
//		s->set_send_bufsize(2048);
		s->connect(a);
		boost::shared_ptr<peer_connection> c(new peer_connection(
			m_ses
			, m_ses.m_selector
			, this
			, s
			, id));
		if (m_ses.m_upload_rate != -1) c->set_send_quota(0);
		detail::session_impl::connection_map::iterator p =
			m_ses.m_connections.insert(std::make_pair(s, c)).first;

		// add the newly connected peer to this torrent's peer list
		assert(std::find(m_connections.begin()
			, m_connections.end()
			, boost::get_pointer(p->second))
			== m_connections.end());
		
		m_connections.push_back(boost::get_pointer(p->second));

		m_ses.m_selector.monitor_readability(s);
		m_ses.m_selector.monitor_errors(s);
//		std::cout << "connecting to: " << a.as_string() << ":" << a.port() << "\n";
		return c;
	}

	void torrent::attach_peer(peer_connection* p)
	{
		assert(std::find(m_connections.begin(), m_connections.end(), p) == m_connections.end());
		m_connections.push_back(p);
		detail::session_impl::connection_map::iterator i
			= m_ses.m_connections.find(p->get_socket());
		assert(i != m_ses.m_connections.end());

		if (!m_policy->new_connection(i->second)) throw network_error(0);
	}

	void torrent::close_all_connections()
	{
		for (detail::session_impl::connection_map::iterator i = m_ses.m_connections.begin();
			i != m_ses.m_connections.end();)
		{
			if (i->second->associated_torrent() == this)
			{
	#ifndef NDEBUG
				std::size_t num_connections = m_connections.size();
				peer_connection* pc = boost::get_pointer(i->second);
	#endif
				assert(std::find(m_connections.begin(), m_connections.end(), pc) != m_connections.end());
				detail::session_impl::connection_map::iterator j = i;
				++i;
				m_ses.m_connections.erase(j);
				assert(m_connections.size() + 1 == num_connections);
				assert(std::find(m_connections.begin(), m_connections.end(), pc) == m_connections.end());
			}
			else
			{
				assert(std::find(m_connections.begin(), m_connections.end(), boost::get_pointer(i->second)) == m_connections.end());
				++i;
			}
		}
		assert(m_connections.empty());
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

		m_picker.files_checked(m_have_pieces);
#ifndef NDEBUG
		m_picker.integrity_check(this);
#endif
	}

	void torrent::second_tick()
	{
		m_time_scaler++;
		if (m_time_scaler >= 10)
		{
			m_time_scaler = 0;
			m_policy->pulse();
		}

		for (std::vector<peer_connection*>::iterator i = m_connections.begin();
			i != m_connections.end();
			++i)
		{
			peer_connection* p = (*i);
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
		torrent_status st;

		const std::vector<bool>& p = m_have_pieces;
		assert(std::accumulate(p.begin(), p.end(), 0) == m_num_pieces);

		int total_blocks
			= (m_torrent_file.total_size()+m_block_size-1)/m_block_size;
		int blocks_per_piece
			= m_torrent_file.piece_length() / m_block_size;

		int unverified_blocks = m_picker.unverified_blocks();

		int blocks_we_have = m_num_pieces * blocks_per_piece;
		const int last_piece = m_torrent_file.num_pieces()-1;
		if (p[last_piece])
		{
			blocks_we_have += m_picker.blocks_in_piece(last_piece)
				- blocks_per_piece;
		}

		st.total_download = m_stat.total_download();
		st.total_upload = m_stat.total_upload();
		st.download_rate = m_stat.download_rate();
		st.upload_rate = m_stat.upload_rate();
		st.progress = (blocks_we_have + unverified_blocks)
			/ static_cast<float>(total_blocks);

		st.next_announce = next_announce()
			- boost::posix_time::second_clock::local_time();

		// TODO: this is not accurate because it assumes the last
		// block is m_block_size bytes
		// TODO: st.pieces could be a const pointer maybe?
		st.total_done = (blocks_we_have + unverified_blocks) * m_block_size;
		st.pieces = m_have_pieces;

		if (m_num_pieces == p.size())
			st.state = torrent_status::seeding;
		else
			st.state = torrent_status::downloading;

		return st;
	}

#ifndef NDEBUG
	void torrent::debug_log(const std::string& line)
	{
		(*m_ses.m_logger) << line << "\n";
	}
#endif

}

