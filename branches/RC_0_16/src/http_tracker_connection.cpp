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
#include "libtorrent/broadcast_socket.hpp" // for is_local

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
					, error_code(errors::scrape_not_available)));
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
			const bool stats = tracker_req().send_stats;
			snprintf(str, sizeof(str), "&peer_id=%s&port=%d&uploaded=%" PRId64
				"&downloaded=%" PRId64 "&left=%" PRId64 "&corrupt=%" PRId64 "&redundant=%" PRId64
				"&compact=1&numwant=%d&key=%x&no_peer_id=1"
				, escape_string((const char*)&tracker_req().pid[0], 20).c_str()
				// the i2p tracker seems to verify that the port is not 0,
				// even though it ignores it otherwise
				, i2p ? 1 : tracker_req().listen_port
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
			if (!tracker_req().trackerid.empty())
			{
				std::string id = tracker_req().trackerid;
				url += "&trackerid=";
				url += escape_string(id.c_str(), id.length());
			}

			if (tracker_req().event != tracker_request::none)
			{
				const char* event_string[] = {"completed", "started", "stopped", "paused"};
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
			if (!m_ses.settings().anonymous_mode)
			{
				if (!settings.announce_ip.empty())
				{
					url += "&ip=" + escape_string(
						settings.announce_ip.c_str(), settings.announce_ip.size());
				}
				else if (m_ses.settings().announce_double_nat
					&& is_local(m_ses.listen_address()))
				{
					// only use the global external listen address here
					// if it turned out to be on a local network
					// since otherwise the tracker should use our
					// source IP to determine our origin
					url += "&ip=" + print_address(m_ses.listen_address());
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
		}

		m_tracker_connection.reset(new http_connection(m_ios, m_cc
			, boost::bind(&http_tracker_connection::on_response, self(), _1, _2, _3, _4)
			, true
			, boost::bind(&http_tracker_connection::on_connect, self(), _1)
			, boost::bind(&http_tracker_connection::on_filter, self(), _1, _2)
#ifdef TORRENT_USE_OPENSSL
			, tracker_req().ssl_ctx
#endif
			));

		int timeout = tracker_req().event==tracker_request::stopped
			?settings.stop_tracker_timeout
			:settings.tracker_completion_timeout;

		m_tracker_connection->get(url, seconds(timeout)
			, tracker_req().event == tracker_request::stopped ? 2 : 1
			, &m_ps, 5, settings.anonymous_mode ? "" : settings.user_agent
			, bind_interface()
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
			cb->debug_log("==> TRACKER_REQUEST [ url: %s ]", url.c_str());
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
		if (tracker_req().apply_ip_filter == false) return;

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
			fail(error_code(errors::banned_by_ip_filter));
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
			fail(ec);
			return;
		}
		
		if (!parser.header_finished())
		{
			fail(asio::error::eof);
			return;
		}

		if (parser.status_code() != 200)
		{
			fail(error_code(parser.status_code(), get_http_category())
				, parser.status_code(), parser.message().c_str());
			return;
		}
	
		if (ec && ec != asio::error::eof)
		{
			fail(ec, parser.status_code());
			return;
		}
		
		received_bytes(size + parser.body_start());

		// handle tracker response
		lazy_entry e;
		error_code ecode;
		int res = lazy_bdecode(data, data + size, e, ecode);

		if (res == 0 && e.type() == lazy_entry::dict_t)
		{
			parse(parser.status_code(), e);
		}
		else
		{
			fail(ecode, parser.status_code());
		}
		close();
	}

	bool http_tracker_connection::extract_peer_info(lazy_entry const& info, peer_entry& ret)
	{
		// extract peer id (if any)
		if (info.type() != lazy_entry::dict_t)
		{
			fail(error_code(errors::invalid_peer_dict));
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
			fail(error_code(errors::invalid_tracker_response));
			return false;
		}
		ret.ip = i->string_value();

		// extract port
		i = info.dict_find_int("port");
		if (i == 0)
		{
			fail(error_code(errors::invalid_tracker_response));
			return false;
		}
		ret.port = (unsigned short)i->int_value();

		return true;
	}

	void http_tracker_connection::parse(int status_code, lazy_entry const& e)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (!cb) return;

		int interval = int(e.dict_find_int_value("interval", 0));
		int min_interval = int(e.dict_find_int_value("min interval", 30));

		// if no interval is specified, default to 30 minutes
		if (interval == 0) interval = 1800;
		
		std::string trackerid;
		lazy_entry const* tracker_id = e.dict_find_string("tracker id");
		if (tracker_id)
			trackerid = tracker_id->string_value();
		// parse the response
		lazy_entry const* failure = e.dict_find_string("failure reason");
		if (failure)
		{
			fail(error_code(errors::tracker_failure), status_code
				, failure->string_value().c_str(), interval, min_interval);
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
				fail(error_code(errors::invalid_files_entry), -1, ""
					, interval, min_interval);
				return;
			}

			lazy_entry const* scrape_data = files->dict_find_dict(ih.c_str());
			if (scrape_data == 0)
			{
				fail(error_code(errors::invalid_hash_entry), -1, ""
					, interval, min_interval);
				return;
			}

			int complete = int(scrape_data->dict_find_int_value("complete", -1));
			int incomplete = int(scrape_data->dict_find_int_value("incomplete", -1));
			int downloaded = int(scrape_data->dict_find_int_value("downloaded", -1));
			int downloaders = int(scrape_data->dict_find_int_value("downloaders", -1));
			cb->tracker_scrape_response(tracker_req(), complete
				, incomplete, downloaded, downloaders);
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
			fail(error_code(errors::invalid_peers_entry), -1, ""
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
		
		int complete = int(e.dict_find_int_value("complete", -1));
		int incomplete = int(e.dict_find_int_value("incomplete", -1));

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
			, interval, min_interval, complete, incomplete, external_ip, trackerid);
	}

}

