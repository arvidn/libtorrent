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

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>

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
		// TODO: if blocks_per_piece > 64 increase block-size
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

		const peer_id& id;
		const torrent* tor;
	};
}

namespace libtorrent
{

	torrent::torrent(detail::session_impl* ses, const torrent_info& torrent_file)
		: m_block_size(calculate_block_size(torrent_file))
		, m_abort(false)
		, m_event(event_started)
		, m_bytes_uploaded(0)
		, m_bytes_downloaded(0)
		, m_torrent_file(torrent_file)
		, m_next_request(boost::posix_time::second_clock::local_time())
		, m_duration(1800)
		, m_policy(new policy(this))
		, m_ses(ses)
		, m_picker(torrent_file.piece_length() / m_block_size,
			(torrent_file.total_size()+m_block_size-1)/m_block_size)
		, m_last_working_tracker(0)
		, m_currently_trying_tracker(0)
	{
	}

	void torrent::tracker_response(const entry& e)
	{
		try
		{
			// parse the response
			parse_response(e);
		}
		catch(type_error& e)
		{
			tracker_request_error(e.what());
		}
		catch(std::runtime_error& e)
		{
			tracker_request_error(e.what());
		}

		m_last_working_tracker = m_torrent_file.prioritize_tracker(m_currently_trying_tracker);
		m_next_request = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(m_duration);
		m_currently_trying_tracker = 0;

		// connect to random peers from the list
		std::random_shuffle(m_peer_list.begin(), m_peer_list.end());

		print(std::cout);

		// for each of the peers we got from the tracker
		for (std::vector<peer>::iterator i = m_peer_list.begin(); i != m_peer_list.end(); ++i)
		{
			// don't make connections to ourself
			if (i->id == m_ses->get_peer_id())
				continue;

			address a(i->ip, i->port);

			// if we aleady have a connection to the person, don't make another one
			if (std::find_if(m_ses->m_connections.begin(), m_ses->m_connections.end(), find_peer(i->id, this)) != m_ses->m_connections.end())
				continue;

			m_policy->peer_from_tracker(a, i->id);
		}
	}

	int torrent::num_connections(const peer_id& id) const
	{
		int num = 0;
		for (detail::session_impl::connection_map::const_iterator i = m_ses->m_connections.begin();
			i != m_ses->m_connections.end();
			++i)
		{
			if (i->second->get_peer_id() == id && i->second->associated_torrent() == this) ++num;
		}
		return num;
	}

	void torrent::announce_piece(int index)
	{
		m_picker.we_have(index);
		for (std::vector<peer_connection*>::iterator i = m_connections.begin(); i != m_connections.end(); ++i)
			(*i)->announce_piece(index);

	#ifndef NDEBUG
		m_picker.integrity_check(this);
	#endif
	}

	std::string torrent::generate_tracker_request(int port)
	{
		m_duration = 1800;
		m_next_request = boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(m_duration);

		std::vector<char> buffer;
		// TODO: temporary! support multi-tracker
		std::string request = m_torrent_file.trackers()[m_currently_trying_tracker].url;

		request += "?info_hash=";
		request += escape_string(reinterpret_cast<const char*>(m_torrent_file.info_hash().begin()), 20);

		request += "&peer_id=";
		request += escape_string(reinterpret_cast<const char*>(m_ses->get_peer_id().begin()), 20);

		request += "&port=";
		request += boost::lexical_cast<std::string>(port);

		request += "&uploaded=";
		request += boost::lexical_cast<std::string>(m_bytes_uploaded);

		request += "&downloaded=";
		request += boost::lexical_cast<std::string>(m_bytes_downloaded);

		request += "&left=";
		request += boost::lexical_cast<std::string>(m_storage.bytes_left());

		if (m_event != event_none)
		{
			const char* event_string[] = {"started", "stopped", "completed"};
			request += "&event=";
			request += event_string[m_event];
			m_event = event_none;
		}

		return request;
	}

	void torrent::parse_response(const entry& e)
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

		m_peer_list.clear();

		const entry::list_type& peer_list = i->second.list();
		for(entry::list_type::const_iterator i = peer_list.begin(); i != peer_list.end(); ++i)
		{
			peer p = extract_peer_info(*i);
			m_peer_list.push_back(p);
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

		std::cout << p->get_socket()->sender().as_string() << " *** DISCONNECT\n";

		m_policy->connection_closed(*p);
		m_connections.erase(i);

	#ifndef NDEBUG
		m_picker.integrity_check(this);
	#endif
	}

	void torrent::connect_to_peer(const address& a, const peer_id& id)
	{
		boost::shared_ptr<socket> s(new socket(socket::tcp, false));
		// TODO: the send buffer size should be controllable from the outside
		s->set_send_bufsize(2048);
		s->connect(a);
		boost::shared_ptr<peer_connection> c(new peer_connection(m_ses, this, s, id));
		detail::session_impl::connection_map::iterator p =
			m_ses->m_connections.insert(std::make_pair(s, c)).first;
		attach_peer(boost::get_pointer(p->second));
		m_ses->m_selector.monitor_writability(s);
		m_ses->m_selector.monitor_readability(s);
		m_ses->m_selector.monitor_errors(s);
		std::cout << "connecting to: " << a.as_string() << ":" << a.port() << "\n";
	}

	void torrent::print(std::ostream& os) const
	{
		os << "interval: " << m_duration << "\n";
		os << "peers:\n";
		for (std::vector<peer>::const_iterator i = m_peer_list.begin(); i != m_peer_list.end(); ++i)
		{
			os << "  " << std::setfill(' ') << std::setw(16) << i->ip << " " << std::setw(5) << std::dec << i->port << "  ";
			for (const unsigned char* j = i->id.begin(); j != i->id.end(); ++j)
				os << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(*j);
			os << "\n";
		}
		os << std::dec << std::setfill(' ');
	}

#ifndef NDEBUG
	logger* torrent::spawn_logger(const char* title)
	{
		return m_ses->m_log_spawner->create_logger(title);
	}
#endif

	void torrent::close_all_connections()
	{
		for (detail::session_impl::connection_map::iterator i = m_ses->m_connections.begin();
			i != m_ses->m_connections.end();)
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
				m_ses->m_connections.erase(j);
				assert(m_connections.size() + 1 == num_connections);
				assert(std::find(m_connections.begin(), m_connections.end(), pc) == m_connections.end());
			}
			else
			{
				++i;
				assert(std::find(m_connections.begin(), m_connections.end(), boost::get_pointer(i->second)) == m_connections.end());
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

}