/*

Copyright (c) 2003-2014, Arvid Norberg
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

#include <vector>
#include <list>
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
#include "libtorrent/broadcast_socket.hpp" // for is_local
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/ip_filter.hpp"

using namespace libtorrent;

namespace libtorrent
{
#if TORRENT_USE_I2P	
	// defined in torrent_info.cpp
	bool is_i2p_url(std::string const& url);
#endif

	http_tracker_connection::http_tracker_connection(
		io_service& ios
		, tracker_manager& man
		, tracker_request const& req
		, boost::weak_ptr<request_callback> c)
		: tracker_connection(man, req, ios, c)
		, m_man(man)
	{}

	void http_tracker_connection::start()
	{
		std::string url = tracker_req().url;

		if (tracker_req().kind == tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = url.find("announce");
			if (pos == std::string::npos)
			{
				tracker_connection::fail(error_code(errors::scrape_not_available));
				return;
			}
			url.replace(pos, 8, "scrape");
		}
		
#if TORRENT_USE_I2P
		bool i2p = is_i2p_url(url);
#else
		static const bool i2p = false;
#endif

		aux::session_settings const& settings = m_man.settings();

		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		size_t arguments_start = url.find('?');
		if (arguments_start != std::string::npos)
			url += "&";
		else
			url += "?";
		
		if (tracker_req().kind == tracker_request::announce_request)
		{
			const char* event_string[] = {"completed", "started", "stopped", "paused"};

			char str[1024];
			const bool stats = tracker_req().send_stats;
			snprintf(str, sizeof(str)
				, "info_hash=%s"
				"&peer_id=%s"
				"&port=%d"
				"&uploaded=%" PRId64
				"&downloaded=%" PRId64
				"&left=%" PRId64
				"&corrupt=%" PRId64
				"&key=%08X"
				"%s%s" // event
				"&numwant=%d"
				"&compact=1"
				"&no_peer_id=1"
				, escape_string((const char*)&tracker_req().info_hash[0], 20).c_str()
				, escape_string((const char*)&tracker_req().pid[0], 20).c_str()
				// the i2p tracker seems to verify that the port is not 0,
				// even though it ignores it otherwise
				, i2p ? 1 : tracker_req().listen_port
				, stats ? tracker_req().uploaded : 0
				, stats ? tracker_req().downloaded : 0
				, stats ? tracker_req().left : 0
				, stats ? tracker_req().corrupt : 0
				, tracker_req().key
				, (tracker_req().event != tracker_request::none) ? "&event=" : ""
				, (tracker_req().event != tracker_request::none) ? event_string[tracker_req().event - 1] : ""
				, tracker_req().num_want);
			url += str;
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			if (settings.get_int(settings_pack::in_enc_policy) != settings_pack::pe_disabled
				&& settings.get_bool(settings_pack::announce_crypto_support))
				url += "&supportcrypto=1";
#endif
			if (stats && settings.get_bool(settings_pack::report_redundant_bytes))
			{
				url += "&redundant=";
				url += to_string(tracker_req().redundant).elems;
			}
			if (!tracker_req().trackerid.empty())
			{
				std::string id = tracker_req().trackerid;
				url += "&trackerid=";
				url += escape_string(id.c_str(), id.length());
			}

#if TORRENT_USE_I2P
			if (i2p && tracker_req().i2pconn)
			{
				url += "&ip=";
				url += escape_string(tracker_req().i2pconn->local_endpoint().c_str()
					, tracker_req().i2pconn->local_endpoint().size());
				url += ".i2p";
			}
			else
#endif
			if (!settings.get_bool(settings_pack::anonymous_mode))
			{
				std::string announce_ip = settings.get_str(settings_pack::announce_ip);
				if (!announce_ip.empty())
				{
					url += "&ip=" + escape_string(announce_ip.c_str(), announce_ip.size());
				}
// TODO: support this somehow
/*				else if (settings.get_bool(settings_pack::announce_double_nat)
					&& is_local(m_ses.listen_address()))
				{
					// only use the global external listen address here
					// if it turned out to be on a local network
					// since otherwise the tracker should use our
					// source IP to determine our origin
					url += "&ip=" + print_address(m_ses.listen_address());
				}
*/
			}
		}

		m_tracker_connection.reset(new http_connection(get_io_service(), m_man.host_resolver()
			, boost::bind(&http_tracker_connection::on_response, shared_from_this(), _1, _2, _3, _4)
			, true, settings.get_int(settings_pack::max_http_recv_buffer_size)
			, boost::bind(&http_tracker_connection::on_connect, shared_from_this(), _1)
			, boost::bind(&http_tracker_connection::on_filter, shared_from_this(), _1, _2)
#ifdef TORRENT_USE_OPENSSL
			, tracker_req().ssl_ctx
#endif
			));

		int timeout = tracker_req().event==tracker_request::stopped
			?settings.get_int(settings_pack::stop_tracker_timeout)
			:settings.get_int(settings_pack::tracker_completion_timeout);

		// when sending stopped requests, prefer the cached DNS entry
		// to avoid being blocked for slow or failing responses. Chances
		// are that we're shutting down, and this should be a best-effort
		// attempt. It's not worth stalling shutdown.
		proxy_settings ps(settings);
		m_tracker_connection->get(url, seconds(timeout)
			, tracker_req().event == tracker_request::stopped ? 2 : 1
			, &ps, 5, settings.get_bool(settings_pack::anonymous_mode)
				? "" : settings.get_str(settings_pack::user_agent)
			, bind_interface()
			, tracker_req().event == tracker_request::stopped
				? resolver_interface::prefer_cache
				: resolver_interface::abort_on_shutdown
			, tracker_req().auth
#if TORRENT_USE_I2P
			, tracker_req().i2pconn
#endif
			);

		// the url + 100 estimated header size
		sent_bytes(url.size() + 100);

#if defined TORRENT_LOGGING

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

	void http_tracker_connection::on_filter(http_connection& c, std::vector<tcp::endpoint>& endpoints)
	{
		if (tracker_req().apply_ip_filter == false) return;

		// remove endpoints that are filtered by the IP filter
		for (std::vector<tcp::endpoint>::iterator i = endpoints.begin();
			i != endpoints.end();)
		{
			if (m_man.ip_filter().access(i->address()) == ip_filter::blocked) 
				i = endpoints.erase(i);
			else
				++i;
		}

#if defined TORRENT_LOGGING
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
	}

	void http_tracker_connection::on_response(error_code const& ec
		, http_parser const& parser, char const* data, int size)
	{
		// keep this alive
		boost::shared_ptr<http_tracker_connection> me(shared_from_this());

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
		error_code ecode;

		boost::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			close();
			return;
		}

		tracker_response resp = parse_tracker_response(data, size, ecode
			, tracker_req().kind == tracker_request::scrape_request
			, tracker_req().info_hash);

		if (!resp.warning_message.empty())
			cb->tracker_warning(tracker_req(), resp.warning_message);

		if (ecode)
		{
			fail(ecode, parser.status_code());
			close();
			return;
		}

		if (!resp.failure_reason.empty())
		{
			fail(error_code(errors::tracker_failure), parser.status_code()
				, resp.failure_reason.c_str(), resp.interval, resp.min_interval);
			close();
			return;
		}

		// do slightly different things for scrape requests
		if (tracker_req().kind == tracker_request::scrape_request)
		{
			cb->tracker_scrape_response(tracker_req(), resp.complete
				, resp.incomplete, resp.downloaded, resp.downloaders);
		}
		else
		{
			std::list<address> ip_list;
			if (m_tracker_connection)
			{
				error_code ec;
				ip_list.push_back(
					m_tracker_connection->socket().remote_endpoint(ec).address());
				std::vector<tcp::endpoint> const& epts = m_tracker_connection->endpoints();
				for (std::vector<tcp::endpoint>::const_iterator i = epts.begin()
					, end(epts.end()); i != end; ++i)
				{
					ip_list.push_back(i->address());
				}
			}

			cb->tracker_response(tracker_req(), m_tracker_ip, ip_list, resp);
		}
		close();
	}

	bool extract_peer_info(bdecode_node const& info, peer_entry& ret, error_code& ec)
	{
		// extract peer id (if any)
		if (info.type() != bdecode_node::dict_t)
		{
			ec.assign(errors::invalid_peer_dict, get_libtorrent_category());
			return false;
		}
		bdecode_node i = info.dict_find_string("peer id");
		if (i && i.string_length() == 20)
		{
			std::copy(i.string_ptr(), i.string_ptr()+20, ret.pid.begin());
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
			ec.assign(errors::invalid_tracker_response, get_libtorrent_category());
			return false;
		}
		ret.hostname = i.string_value();

		// extract port
		i = info.dict_find_int("port");
		if (i == 0)
		{
			ec.assign(errors::invalid_tracker_response, get_libtorrent_category());
			return false;
		}
		ret.port = (unsigned short)i.int_value();

		return true;
	}

	tracker_response parse_tracker_response(char const* data, int size, error_code& ec
		, bool scrape_request, sha1_hash scrape_ih)
	{
		tracker_response resp;

		bdecode_node e;
		int res = bdecode(data, data + size, e, ec);

		if (ec) return resp;

		if (res != 0 || e.type() != bdecode_node::dict_t)
		{
			ec.assign(errors::invalid_tracker_response, get_libtorrent_category());
			return resp;
		}

		int interval = int(e.dict_find_int_value("interval", 0));
		// if no interval is specified, default to 30 minutes
		if (interval == 0) interval = 1800;
		int min_interval = int(e.dict_find_int_value("min interval", 30));

		resp.interval = interval;
		resp.min_interval = min_interval;

		bdecode_node tracker_id = e.dict_find_string("tracker id");
		if (tracker_id)
			resp.trackerid = tracker_id.string_value();

		// parse the response
		bdecode_node failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = failure.string_value();
			ec.assign(errors::tracker_failure, get_libtorrent_category());
			return resp;
		}

		bdecode_node warning = e.dict_find_string("warning message");
		if (warning)
			resp.warning_message = warning.string_value();

		if (scrape_request)
		{
			bdecode_node files = e.dict_find_dict("files");
			if (!files)
			{
				ec.assign(errors::invalid_files_entry, get_libtorrent_category());
				return resp;
			}

			bdecode_node scrape_data = files.dict_find_dict(
				scrape_ih.to_string());

			if (!scrape_data)
			{
				ec.assign(errors::invalid_hash_entry, get_libtorrent_category());
				return resp;
			}

			resp.complete = int(scrape_data.dict_find_int_value("complete", -1));
			resp.incomplete = int(scrape_data.dict_find_int_value("incomplete", -1));
			resp.downloaded = int(scrape_data.dict_find_int_value("downloaded", -1));
			resp.downloaders = int(scrape_data.dict_find_int_value("downloaders", -1));

			return resp;
		}

		// look for optional scrape info
		resp.complete = int(e.dict_find_int_value("complete", -1));
		resp.incomplete = int(e.dict_find_int_value("incomplete", -1));
		resp.downloaded = int(e.dict_find_int_value("downloaded", -1));

		bdecode_node peers_ent = e.dict_find("peers");
		if (peers_ent && peers_ent.type() == bdecode_node::string_t)
		{
			char const* peers = peers_ent.string_ptr();
			int len = peers_ent.string_length();
			resp.peers4.reserve(len / 6);
			for (int i = 0; i < len; i += 6)
			{
				if (len - i < 6) break;

				ipv4_peer_entry p;
				error_code ec;
				p.ip = detail::read_v4_address(peers).to_v4().to_bytes();
				p.port = detail::read_uint16(peers);
				resp.peers4.push_back(p);
			}
		}
		else if (peers_ent && peers_ent.type() == bdecode_node::list_t)
		{
			int len = peers_ent.list_size();
			resp.peers.reserve(len);
			error_code parse_error;
			for (int i = 0; i < len; ++i)
			{
				peer_entry p;
				if (!extract_peer_info(peers_ent.list_at(i), p, parse_error))
					continue;
				resp.peers.push_back(p);
			}

			// only report an error if all peer entries are invalid
			if (resp.peers.empty() && parse_error)
			{
				ec = parse_error;
				return resp;
			}
		}
		else
		{
			peers_ent.clear();
		}

#if TORRENT_USE_IPV6
		bdecode_node ipv6_peers = e.dict_find_string("peers6");
		if (ipv6_peers)
		{
			char const* peers = ipv6_peers.string_ptr();
			int len = ipv6_peers.string_length();
			resp.peers6.reserve(len / 18);
			for (int i = 0; i < len; i += 18)
			{
				if (len - i < 18) break;

				ipv6_peer_entry p;
				p.ip = detail::read_v6_address(peers).to_v6().to_bytes();
				p.port = detail::read_uint16(peers);
				resp.peers6.push_back(p);
			}
		}
		else
		{
			ipv6_peers.clear();
		}
#else
		bdecode_node ipv6_peers;
#endif
/*
		// if we didn't receive any peers. We don't care if we're stopping anyway
		if (peers_ent == 0 && ipv6_peers == 0
			&& tracker_req().event != tracker_request::stopped)
		{
			ec.assign(errors::invalid_peers_entry, get_libtorrent_category());
			return resp;
		}
*/
		bdecode_node ip_ent = e.dict_find_string("external ip");
		if (ip_ent)
		{
			char const* p = ip_ent.string_ptr();
			if (ip_ent.string_length() == int(address_v4::bytes_type().size()))
				resp.external_ip = detail::read_v4_address(p);
#if TORRENT_USE_IPV6
			else if (ip_ent.string_length() == int(address_v6::bytes_type().size()))
				resp.external_ip = detail::read_v6_address(p);
#endif
		}
		
		return resp;
	}
}

