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
		, std::string const& auth)
		: tracker_connection(man, req, ios, c)
		, m_man(man)
		, m_ses(ses)
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
				m_ios.post(boost::bind(&http_tracker_connection::fail_disp, self()
					, -1, "scrape is not available on url: '" + tracker_req().url +"'"));
				return;
			}
			url.replace(pos, 8, "scrape");
		}
		
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
			const bool stats = tracker_req().send_stats;
			snprintf(str, sizeof(str), "&peer_id=%s&port=%d&uploaded=%"PRId64
				"&downloaded=%"PRId64"&left=%"PRId64"&corrupt=%"PRId64"&redundant=%"PRId64
				"&compact=1&numwant=%d&key=%x&no_peer_id=1"
				, escape_string((const char*)&tracker_req().pid[0], 20).c_str()
				, tracker_req().listen_port
				, stats ? tracker_req().uploaded : 0
				, stats ? tracker_req().downloaded : 0
				, stats ? tracker_req().left : 0
				, stats ? tracker_req().corrupt : 0
				, stats ? tracker_req().redundant: 0
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

			if (settings.announce_ip != address())
			{
				error_code ec;
				std::string ip = settings.announce_ip.to_string(ec);
				if (!ec) url += "&ip=" + ip;
			}

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
			, tracker_req().event == tracker_request::stopped ? 2 : 1
			, &m_ps, 5, settings.user_agent, bind_interface());

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

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("*** TRACKER_FILTER");
		}
#endif
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
		lazy_entry e;
		int res = lazy_bdecode(data, data + size, e);

		if (res == 0 && e.type() == lazy_entry::dict_t)
		{
			parse(parser.status_code(), e);
		}
		else
		{
			std::string error_str("invalid encoding of tracker response: \"");
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

	bool http_tracker_connection::extract_peer_info(lazy_entry const& info, peer_entry& ret)
	{
		// extract peer id (if any)
		if (info.type() != lazy_entry::dict_t)
		{
			fail(-1, "invalid response from tracker (invalid peer entry)");
			return false;
		}
		lazy_entry const* i = info.dict_find_string("peer id");
		if (i != 0 && i->string_length() == 20)
		{
			std::copy(i->string_ptr(), i->string_ptr()+20, ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			std::fill_n(ret.pid.begin(), 20, 0);
		}

		// extract ip
		i = info.dict_find_string("ip");
		if (i == 0)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.ip = i->string_value();

		// extract port
		i = info.dict_find_int("port");
		if (i == 0)
		{
			fail(-1, "invalid response from tracker");
			return false;
		}
		ret.port = (unsigned short)i->int_value();

		return true;
	}

	void http_tracker_connection::parse(int status_code, lazy_entry const& e)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (!cb) return;

		int interval = e.dict_find_int_value("interval", 1800);
		int min_interval = e.dict_find_int_value("min interval", 60);

		// parse the response
		lazy_entry const* failure = e.dict_find_string("failure reason");
		if (failure)
		{
			fail(status_code, failure->string_value().c_str(), interval, min_interval);
			return;
		}

		lazy_entry const* warning = e.dict_find_string("warning message");
		if (warning)
			cb->tracker_warning(tracker_req(), warning->string_value());

		std::vector<peer_entry> peer_list;

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			std::string ih = tracker_req().info_hash.to_string();

			lazy_entry const* files = e.dict_find_dict("files");
			if (files == 0)
			{
				fail(-1, "invalid or missing 'files' entry in scrape response"
					, interval, min_interval);
				return;
			}

			lazy_entry const* scrape_data = files->dict_find_dict(ih.c_str());
			if (scrape_data == 0)
			{
				fail(-1, "missing or invalid info-hash entry in scrape response"
					, interval, min_interval);
				return;
			}
			int complete = scrape_data->dict_find_int_value("complete", -1);
			int incomplete = scrape_data->dict_find_int_value("incomplete", -1);
			int downloaded = scrape_data->dict_find_int_value("downloaded", -1);
			cb->tracker_scrape_response(tracker_req(), complete
				, incomplete, downloaded);
			return;
		}

		lazy_entry const* peers_ent = e.dict_find("peers");
		if (peers_ent && peers_ent->type() == lazy_entry::string_t)
		{
			char const* peers = peers_ent->string_ptr();
			int len = peers_ent->string_length();
			for (int i = 0; i < len; i += 6)
			{
				if (len - i < 6) break;

				peer_entry p;
				p.pid.clear();
				error_code ec;
				p.ip = detail::read_v4_address(peers).to_string(ec);
				p.port = detail::read_uint16(peers);
				if (ec) continue;
				peer_list.push_back(p);
			}
		}
		else if (peers_ent && peers_ent->type() == lazy_entry::list_t)
		{
			int len = peers_ent->list_size();
			for (int i = 0; i < len; ++i)
			{
				peer_entry p;
				if (!extract_peer_info(*peers_ent->list_at(i), p)) return;
				peer_list.push_back(p);
			}
		}
		else
		{
			peers_ent = 0;
		}

#if TORRENT_USE_IPV6
		lazy_entry const* ipv6_peers = e.dict_find_string("peers6");
		if (ipv6_peers)
		{
			char const* peers = ipv6_peers->string_ptr();
			int len = ipv6_peers->string_length();
			for (int i = 0; i < len; i += 18)
			{
				if (len - i < 18) break;

				peer_entry p;
				p.pid.clear();
				error_code ec;
				p.ip = detail::read_v6_address(peers).to_string(ec);
				p.port = detail::read_uint16(peers);
				if (ec) continue;
				peer_list.push_back(p);
			}
		}
		else
		{
			ipv6_peers = 0;
		}
#else
		lazy_entry const* ipv6_peers = 0;
#endif

		// if we didn't receive any peers. We don't care if we're stopping anyway
		if (peers_ent == 0 && ipv6_peers == 0
			&& tracker_req().event != tracker_request::stopped)
		{
			fail(-1, "missing 'peers' and 'peers6' entry in tracker response"
				, interval, min_interval);
			return;
		}


		// look for optional scrape info
		address external_ip;

		lazy_entry const* ip_ent = e.dict_find_string("external ip");
		if (ip_ent)
		{
			char const* p = ip_ent->string_ptr();
			if (ip_ent->string_length() == address_v4::bytes_type().size())
				external_ip = detail::read_v4_address(p);
#if TORRENT_USE_IPV6
			else if (ip_ent->string_length() == address_v6::bytes_type().size())
				external_ip = detail::read_v6_address(p);
#endif
		}
		
		int complete = e.dict_find_int_value("complete", -1);
		int incomplete = e.dict_find_int_value("incomplete", -1);

		std::list<address> ip_list;
		if (m_tracker_connection)
		{
			error_code ec;
			ip_list.push_back(m_tracker_connection->socket().remote_endpoint(ec).address());
			std::list<tcp::endpoint> const& epts = m_tracker_connection->endpoints();
			for (std::list<tcp::endpoint>::const_iterator i = epts.begin()
				, end(epts.end()); i != end; ++i)
			{
				ip_list.push_back(i->address());
			}
		}

		cb->tracker_response(tracker_req(), m_tracker_ip, ip_list, peer_list
			, interval, min_interval, complete, incomplete, external_ip);
	}

}

