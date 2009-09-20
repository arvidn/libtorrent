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
#include <cctype>
#include <algorithm>

#include "libtorrent/config.hpp"
#include "libtorrent/gzip.hpp"
#include "libtorrent/socket_io.hpp"

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
#include "libtorrent/aux_/session_impl.hpp"

using namespace libtorrent;
using boost::bind;

namespace libtorrent
{
#if TORRENT_USE_I2P	
	// defined in torrent_info.cpp
	bool is_i2p_url(std::string const& url);
#endif

	http_tracker_connection::http_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, boost::weak_ptr<request_callback> c
		, aux::session_impl const& ses
		, proxy_settings const& ps
		, std::string const& auth
#if TORRENT_USE_I2P
		, i2p_connection* i2p_conn
#endif
		)
		: tracker_connection(man, req, ios, c)
		, m_man(man)
		, m_ses(ses)
		, m_ps(ps)
		, m_cc(cc)
		, m_ios(ios)
#if TORRENT_USE_I2P
		, m_i2p_conn(i2p_conn)
#endif
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
				m_ios.post(boost::bind(&http_tracker_connection::fail_disp, self()
					, -1, "scrape is not available on url: '" + tracker_req().url +"'"));
				return;
			}
			url.replace(pos, 8, "scrape");
		}
		
#if TORRENT_USE_I2P
		bool i2p = is_i2p_url(url);
#else
		static const bool i2p = false;
#endif

		session_settings const& settings = m_ses.settings();

		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		size_t arguments_start = url.find('?');
		if (arguments_start != std::string::npos)
			url += "&";
		else
			url += "?";

		url += "info_hash=";
		url += escape_string((const char*)&tracker_req().info_hash[0], 20);
		
		if (tracker_req().kind == tracker_request::announce_request)
		{
			char str[1024];
			snprintf(str, sizeof(str), "&peer_id=%s&port=%d&uploaded=%"PRId64
				"&downloaded=%"PRId64"&left=%"PRId64"&compact=1&numwant=%d&key=%x&no_peer_id=1"
				, escape_string((const char*)&tracker_req().pid[0], 20).c_str()
				// the i2p tracker seems to verify that the port is not 0,
				// even though it ignores it otherwise
				, i2p ? 1 : tracker_req().listen_port
				, tracker_req().uploaded
				, tracker_req().downloaded
				, tracker_req().left
				, tracker_req().num_want
				, tracker_req().key);
			url += str;
#ifndef TORRENT_DISABLE_ENCRYPTION
			if (m_ses.get_pe_settings().in_enc_policy != pe_settings::disabled)
				url += "&supportcrypto=1";
#endif

			if (tracker_req().event != tracker_request::none)
			{
				const char* event_string[] = {"completed", "started", "stopped"};
				url += "&event=";
				url += event_string[tracker_req().event - 1];
			}

#if TORRENT_USE_I2P
			if (i2p)
			{
				url += "&ip=";
				url += escape_string(m_i2p_conn->local_endpoint().c_str()
					, m_i2p_conn->local_endpoint().size());
				url += ".i2p";
			}
			else
#endif
			if (settings.announce_ip != address())
			{
				error_code ec;
				std::string ip = settings.announce_ip.to_string(ec);
				if (!ec) url += "&ip=" + ip;
			}

			if (!tracker_req().ipv6.empty() && !i2p)
			{
				url += "&ipv6=";
				url += tracker_req().ipv6;
			}

			if (!tracker_req().ipv4.empty() && !i2p)
			{
				url += "&ipv4=";
				url += tracker_req().ipv4;
			}
		}

		m_tracker_connection.reset(new http_connection(m_ios, m_cc
			, boost::bind(&http_tracker_connection::on_response, self(), _1, _2, _3, _4)
			, true
			, boost::bind(&http_tracker_connection::on_connect, self(), _1)
			, boost::bind(&http_tracker_connection::on_filter, self(), _1, _2)));

		int timeout = tracker_req().event==tracker_request::stopped
			?settings.stop_tracker_timeout
			:settings.tracker_completion_timeout;

		m_tracker_connection->get(url, seconds(timeout)
			, 1, &m_ps, 5, settings.user_agent, bind_interface()
#if TORRENT_USE_I2P
			, m_i2p_conn
#endif
			);

		// the url + 100 estimated header size
		sent_bytes(url.size() + 100);

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

	void http_tracker_connection::on_filter(http_connection& c, std::list<tcp::endpoint>& endpoints)
	{
		// remove endpoints that are filtered by the IP filter
		for (std::list<tcp::endpoint>::iterator i = endpoints.begin();
			i != endpoints.end();)
		{
			if (m_ses.m_ip_filter.access(i->address()) == ip_filter::blocked) 
				i = endpoints.erase(i);
			else
				++i;
		}

		if (endpoints.empty())
			fail(-1, "blocked by IP filter");
	}

	void http_tracker_connection::on_connect(http_connection& c)
	{
    	error_code ec;
		tcp::endpoint ep = c.socket().remote_endpoint(ec);
		m_tracker_ip = ep.address();
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->m_tracker_address = ep;
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
		
		received_bytes(size + parser.body_start());

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
				else
				{
					char val[30];
					snprintf(val, sizeof(val), "0x%02x ", *i);
					error_str += val;
				}
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
				if (peers.end() - i < 6) break;

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

#if TORRENT_USE_IPV6
		entry const* ipv6_peers = e.find_key("peers6");
		if (ipv6_peers && ipv6_peers->type() == entry::string_t)
		{
			std::string const& peers = ipv6_peers->string();
			for (std::string::const_iterator i = peers.begin();
				i != peers.end();)
			{
				if (peers.end() - i < 18) break;

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
#else
		entry const* ipv6_peers = 0;
#endif

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
#if TORRENT_USE_IPV6
			else if (ip.size() == address_v6::bytes_type::static_size)
				external_ip = detail::read_v6_address(p);
#endif
		}
		
		entry const* complete_ent = e.find_key("complete");
		if (complete_ent && complete_ent->type() == entry::int_t)
			complete = int(complete_ent->integer());

		entry const* incomplete_ent = e.find_key("incomplete");
		if (incomplete_ent && incomplete_ent->type() == entry::int_t)
			incomplete = int(incomplete_ent->integer());

		std::list<address> ip_list;
		if (m_tracker_connection)
		{
			std::list<tcp::endpoint> const& epts = m_tracker_connection->endpoints();
			for (std::list<tcp::endpoint>::const_iterator i = epts.begin()
				, end(epts.end()); i != end; ++i)
			{
				ip_list.push_back(i->address());
			}
		}

		cb->tracker_response(tracker_req(), m_tracker_ip, ip_list, peer_list
			, interval->integer(), complete, incomplete, external_ip);
	}

}

