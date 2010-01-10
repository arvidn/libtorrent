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

#include <vector>
#include <iostream>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "libtorrent/config.hpp"
#include "libtorrent/gzip.hpp"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/socket.hpp"

using namespace libtorrent;
using boost::bind;

namespace libtorrent
{
	
	http_tracker_connection::http_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, address bind_infc
		, boost::weak_ptr<request_callback> c
		, session_settings const& stn
		, proxy_settings const& ps
		, std::string const& auth)
		: tracker_connection(man, req, ios, bind_infc, c)
		, m_man(man)
		, m_settings(stn)
		, m_bind_iface(bind_infc)
		, m_ps(ps)
		, m_cc(cc)
		, m_ios(ios)
	{}

	void http_tracker_connection::start()
	{
		// TODO: authentication
		std::string url = tracker_req().url;

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = url.find("announce");
			if (pos == std::string::npos)
			{
				fail(-1, ("scrape is not available on url: '"
					+ tracker_req().url +"'").c_str());
				return;
			}
			url.replace(pos, 8, "scrape");
		}
		
		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		size_t arguments_start = url.find('?');
		if (arguments_start != std::string::npos)
			url += "&";
		else
			url += "?";

		url += "info_hash=";
		url += escape_string(
			reinterpret_cast<const char*>(tracker_req().info_hash.begin()), 20);
		
		if (tracker_req().kind == tracker_request::announce_request)
		{
			url += "&peer_id=";
			url += escape_string(
				reinterpret_cast<const char*>(tracker_req().pid.begin()), 20);

			url += "&port=";
			url += to_string(tracker_req().listen_port).elems;

			url += "&uploaded=";
			url += to_string(tracker_req().uploaded).elems;

			url += "&downloaded=";
			url += to_string(tracker_req().downloaded).elems;

			url += "&left=";
			url += to_string(tracker_req().left).elems;

			if (tracker_req().event != tracker_request::none)
			{
				const char* event_string[] = {"completed", "started", "stopped"};
				url += "&event=";
				url += event_string[tracker_req().event - 1];
			}

			url += "&key=";
			std::stringstream key_string;
			key_string << std::hex << tracker_req().key;
			url += key_string.str();

			url += "&compact=1";

			url += "&numwant=";
			url += to_string((std::min)(tracker_req().num_want, 999)).elems;

			if (m_settings.announce_ip != address())
			{
				error_code ec;
				std::string ip = m_settings.announce_ip.to_string(ec);
				if (!ec) url += "&ip=" + ip;
			}

#ifndef TORRENT_DISABLE_ENCRYPTION
			url += "&supportcrypto=1";
#endif
			if (!tracker_req().ipv6.empty())
			{
				url += "&ipv6=";
				url += tracker_req().ipv6;
			}

			if (!tracker_req().ipv4.empty())
			{
				url += "&ipv4=";
				url += tracker_req().ipv4;
			}

			// extension that tells the tracker that
			// we don't need any peer_id's in the response
			url += "&no_peer_id=1";
		}

		m_tracker_connection.reset(new http_connection(m_ios, m_cc
			, boost::bind(&http_tracker_connection::on_response, self(), _1, _2, _3, _4)));

		int timeout = tracker_req().event==tracker_request::stopped
			?m_settings.stop_tracker_timeout
			:m_settings.tracker_completion_timeout;

		m_tracker_connection->get(url, seconds(timeout)
			, 1, &m_ps, 5, m_settings.user_agent, m_bind_iface);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)

		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("==> TRACKER_REQUEST [ url: " + url + " ]");
		}
#endif
	}

	void http_tracker_connection::close()
	{
		if (m_tracker_connection)
		{
			m_tracker_connection->close();
			m_tracker_connection.reset();
		}
		tracker_connection::close();
	}

	void http_tracker_connection::on_response(error_code const& ec
		, http_parser const& parser, char const* data, int size)
	{
		// keep this alive
		boost::intrusive_ptr<http_tracker_connection> me(this);

		if (ec && ec != asio::error::eof)
		{
			fail(-1, ec.message().c_str());
			return;
		}
		
		if (!parser.header_finished())
		{
			fail(-1, "premature end of file");
			return;
		}

		if (parser.status_code() != 200)
		{
			fail(parser.status_code(), parser.message().c_str());
			return;
		}
	
		if (ec && ec != asio::error::eof)
		{
			fail(parser.status_code(), ec.message().c_str());
			return;
		}
		
		// handle tracker response
		entry e;
		e = bdecode(data, data + size);

		if (e.type() == entry::dictionary_t)
		{
			parse(parser.status_code(), e);
		}
		else
		{
			std::string error_str("invalid bencoding of tracker response: \"");
			for (char const* i = data, *end(data + size); i != end; ++i)
			{
				if (*i >= ' ' && *i <= '~') error_str += *i;
				else error_str += "0x" + boost::lexical_cast<std::string>((unsigned int)*i) + " ";
			}
			error_str += "\"";
			fail(parser.status_code(), error_str.c_str());
		}
		close();
	}

	bool http_tracker_connection::extract_peer_info(const entry& info, peer_entry& ret)
	{
		// extract peer id (if any)
		if (info.type() != entry::dictionary_t)
		{
			fail(-1, "invalid response from tracker (invalid peer entry)");
			return false;
		}
		entry const* i = info.find_key("peer id");
		if (i != 0)
		{
			if (i->type() != entry::string_t || i->string().length() != 20)
			{
				fail(-1, "invalid response from tracker (invalid peer id)");
				return false;
			}
			std::copy(i->string().begin(), i->string().end(), ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.pid.begin(), 20, 0);
		}

		// extract ip
		i = info.find_key("ip");
		if (i == 0 || i->type() != entry::string_t)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.ip = i->string();

		// extract port
		i = info.find_key("port");
		if (i == 0 || i->type() != entry::int_t)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.port = (unsigned short)i->integer();

		return true;
	}

	void http_tracker_connection::parse(int status_code, entry const& e)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (!cb) return;

		// parse the response
		entry const* failure = e.find_key("failure reason");
		if (failure && failure->type() == entry::string_t)
		{
			fail(status_code, failure->string().c_str());
			return;
		}

		entry const* warning = e.find_key("warning message");
		if (warning && warning->type() == entry::string_t)
		{
			cb->tracker_warning(tracker_req(), warning->string());
		}

		std::vector<peer_entry> peer_list;

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			std::string ih = tracker_req().info_hash.to_string();

			entry const* files = e.find_key("files");
			if (files == 0 || files->type() != entry::dictionary_t)
			{
				fail(-1, "invalid or missing 'files' entry in scrape response");
				return;
			}

			entry const* scrape_data = files->find_key(ih);
			if (scrape_data == 0 || scrape_data->type() != entry::dictionary_t)
			{
				fail(-1, "missing or invalid info-hash entry in scrape response");
				return;
			}
			entry const* complete = scrape_data->find_key("complete");
			entry const* incomplete = scrape_data->find_key("incomplete");
			entry const* downloaded = scrape_data->find_key("downloaded");
			if (complete == 0 || incomplete == 0 || downloaded == 0
				|| complete->type() != entry::int_t
				|| incomplete->type() != entry::int_t
				|| downloaded->type() != entry::int_t)
			{
				fail(-1, "missing 'complete' or 'incomplete' entries in scrape response");
				return;
			}
			cb->tracker_scrape_response(tracker_req(), int(complete->integer())
				, int(incomplete->integer()), int(downloaded->integer()));
			return;
		}

		entry const* interval = e.find_key("interval");
		if (interval == 0 || interval->type() != entry::int_t)
		{
			fail(-1, "missing or invalid 'interval' entry in tracker response");
			return;
		}

		entry const* peers_ent = e.find_key("peers");
		if (peers_ent && peers_ent->type() == entry::string_t)
		{
			std::string const& peers = peers_ent->string();
			for (std::string::const_iterator i = peers.begin();
				i != peers.end();)
			{
				if (std::distance(i, peers.end()) < 6) break;

				peer_entry p;
				p.pid.clear();
				error_code ec;
				p.ip = detail::read_v4_address(i).to_string(ec);
				if (ec) continue;
				p.port = detail::read_uint16(i);
				peer_list.push_back(p);
			}
		}
		else if (peers_ent && peers_ent->type() == entry::list_t)
		{
			entry::list_type const& l = peers_ent->list();
			for(entry::list_type::const_iterator i = l.begin(); i != l.end(); ++i)
			{
				peer_entry p;
				if (!extract_peer_info(*i, p)) return;
				peer_list.push_back(p);
			}
		}
		else
		{
			peers_ent = 0;
		}

		entry const* ipv6_peers = e.find_key("peers6");
		if (ipv6_peers && ipv6_peers->type() == entry::string_t)
		{
			std::string const& peers = ipv6_peers->string();
			for (std::string::const_iterator i = peers.begin();
				i != peers.end();)
			{
				if (std::distance(i, peers.end()) < 18) break;

				peer_entry p;
				p.pid.clear();
				error_code ec;
				p.ip = detail::read_v6_address(i).to_string(ec);
				if (ec) continue;
				p.port = detail::read_uint16(i);
				peer_list.push_back(p);
			}
		}
		else
		{
			ipv6_peers = 0;
		}

		if (peers_ent == 0 && ipv6_peers == 0)
		{
			fail(-1, "missing 'peers' and 'peers6' entry in tracker response");
			return;
		}


		// look for optional scrape info
		int complete = -1;
		int incomplete = -1;
		address external_ip;

		entry const* ip_ent = e.find_key("external ip");
		if (ip_ent && ip_ent->type() == entry::string_t)
		{
			std::string const& ip = ip_ent->string();
			char const* p = &ip[0];
			if (ip.size() == address_v4::bytes_type::static_size)
				external_ip = detail::read_v4_address(p);
			else if (ip.size() == address_v6::bytes_type::static_size)
				external_ip = detail::read_v6_address(p);
		}
		
		entry const* complete_ent = e.find_key("complete");
		if (complete_ent && complete_ent->type() == entry::int_t)
			complete = int(complete_ent->integer());

		entry const* incomplete_ent = e.find_key("incomplete");
		if (incomplete_ent && incomplete_ent->type() == entry::int_t)
			incomplete = int(incomplete_ent->integer());

		cb->tracker_response(tracker_req(), peer_list, interval->integer(), complete
			, incomplete, external_ip);
	}

}

