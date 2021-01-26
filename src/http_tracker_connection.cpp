/*

Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2004-2020, Arvid Norberg
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2018, 2020, Alden Torres
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2020, Paul-Louis Ageneau
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

#include "libtorrent/config.hpp"
#include "libtorrent/socket_io.hpp"

#include <string>
#include <functional>
#include <vector>
#include <list>
#include <cctype>
#include <algorithm>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/string_util.hpp" // for is_i2p_url
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/aux_/array.hpp"

namespace libtorrent {

	http_tracker_connection::http_tracker_connection(
		io_context& ios
		, tracker_manager& man
		, tracker_request req
		, std::weak_ptr<request_callback> c)
		: tracker_connection(man, std::move(req), ios, std::move(c))
		, m_ioc(ios)
	{}

	void http_tracker_connection::start()
	{
		std::string url = tracker_req().url;

		if (tracker_req().kind & tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = url.find("announce");
			if (pos == std::string::npos)
			{
				fail(errors::scrape_not_available, operation_t::bittorrent);
				return;
			}
			url.replace(pos, 8, "scrape");
		}

#if TORRENT_USE_I2P
		bool const i2p = is_i2p_url(url);
#else
		static const bool i2p = false;
#endif

		aux::session_settings const& settings = m_man.settings();

		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		auto const arguments_start = url.find('?');
		if (arguments_start != std::string::npos)
		{
			// tracker URLs that come pre-baked with query string arguments will be
			// rejected when SSRF-mitigation is enabled
			bool const ssrf_mitigation = settings.get_bool(settings_pack::ssrf_mitigation);
			if (ssrf_mitigation && has_tracker_query_string(string_view(url).substr(arguments_start + 1)))
			{
				fail(errors::ssrf_mitigation, operation_t::bittorrent);
				return;
			}
			url += "&";
		}
		else
		{
			url += "?";
		}

		url += "info_hash=";
		url += escape_string({tracker_req().info_hash.data(), 20});

		if (!(tracker_req().kind & tracker_request::scrape_request))
		{
			static aux::array<const char*, 4> const event_string{{{"completed", "started", "stopped", "paused"}}};

			char str[1024];
			std::snprintf(str, sizeof(str)
				, "&peer_id=%s"
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
				, escape_string({tracker_req().pid.data(), 20}).c_str()
				// the i2p tracker seems to verify that the port is not 0,
				// even though it ignores it otherwise
				, tracker_req().listen_port
				, tracker_req().uploaded
				, tracker_req().downloaded
				, tracker_req().left
				, tracker_req().corrupt
				, tracker_req().key
				, (tracker_req().event != event_t::none) ? "&event=" : ""
				, (tracker_req().event != event_t::none) ? event_string[static_cast<int>(tracker_req().event) - 1] : ""
				, tracker_req().num_want);
			url += str;
#if !defined TORRENT_DISABLE_ENCRYPTION
			if (settings.get_int(settings_pack::in_enc_policy) != settings_pack::pe_disabled
				&& settings.get_bool(settings_pack::announce_crypto_support))
				url += "&supportcrypto=1";
#endif
			if (settings.get_bool(settings_pack::report_redundant_bytes))
			{
				url += "&redundant=";
				url += to_string(tracker_req().redundant).data();
			}
			if (!tracker_req().trackerid.empty())
			{
				url += "&trackerid=";
				url += escape_string(tracker_req().trackerid);
			}

#if TORRENT_USE_I2P
			if (i2p && tracker_req().i2pconn)
			{
				if (tracker_req().i2pconn->local_endpoint().empty())
				{
					fail(errors::no_i2p_endpoint, operation_t::bittorrent
						, "Waiting for i2p acceptor from SAM bridge", seconds32(5));
					return;
				}
				else
				{
					url += "&ip=" + tracker_req().i2pconn->local_endpoint () + ".i2p";
				}
			}
			else
#endif
			if (!settings.get_bool(settings_pack::anonymous_mode))
			{
				std::string const& announce_ip = settings.get_str(settings_pack::announce_ip);
				if (!announce_ip.empty())
				{
					url += "&ip=" + escape_string(announce_ip);
				}
			}
		}

		if (!tracker_req().ipv4.empty() && !i2p)
		{
			for (auto const& v4 : tracker_req().ipv4)
			{
				std::string const ip = v4.to_string();
				url += "&ipv4=";
				url += escape_string(ip);
			}
		}
		if (!tracker_req().ipv6.empty() && !i2p)
		{
			for (auto const& v6 : tracker_req().ipv6)
			{
				std::string const ip = v6.to_string();
				url += "&ipv6=";
				url += escape_string(ip);
			}
		}

		if (!tracker_req().outgoing_socket)
		{
			fail(errors::invalid_listen_socket, operation_t::get_interface
				, "outgoing socket was closed");
			return;
		}

		using namespace std::placeholders;
		m_tracker_connection = std::make_shared<http_connection>(m_ioc, m_man.host_resolver()
			, std::bind(&http_tracker_connection::on_response, shared_from_this(), _1, _2, _3)
			, true, settings.get_int(settings_pack::max_http_recv_buffer_size)
			, std::bind(&http_tracker_connection::on_connect, shared_from_this(), _1)
			, std::bind(&http_tracker_connection::on_filter, shared_from_this(), _1, _2)
			, std::bind(&http_tracker_connection::on_filter_hostname, shared_from_this(), _1, _2)
#if TORRENT_USE_SSL
			, tracker_req().ssl_ctx
#endif
			);

		int const timeout = tracker_req().event == event_t::stopped
			? settings.get_int(settings_pack::stop_tracker_timeout)
			: settings.get_int(settings_pack::tracker_completion_timeout);

		// in anonymous mode we omit the user agent to mitigate fingerprinting of
		// the client. Private torrents is an exception because some private
		// trackers may require the user agent
		std::string const user_agent = settings.get_bool(settings_pack::anonymous_mode)
			&& !tracker_req().private_torrent ? "" : settings.get_str(settings_pack::user_agent);

		// when sending stopped requests, prefer the cached DNS entry
		// to avoid being blocked for slow or failing responses. Chances
		// are that we're shutting down, and this should be a best-effort
		// attempt. It's not worth stalling shutdown.
		aux::proxy_settings ps(settings);
		m_tracker_connection->get(url, seconds(timeout)
			, tracker_req().event == event_t::stopped ? 2 : 1
			, ps.proxy_tracker_connections ? &ps : nullptr
			, 5, user_agent, bind_interface()
			, (tracker_req().event == event_t::stopped
				? aux::resolver_interface::cache_only : aux::resolver_flags{})
				| aux::resolver_interface::abort_on_shutdown
#if TORRENT_ABI_VERSION == 1
			, tracker_req().auth
#else
			, ""
#endif
#if TORRENT_USE_I2P
			, tracker_req().i2pconn
#endif
			);

		// the url + 100 estimated header size
		sent_bytes(int(url.size()) + 100);

#ifndef TORRENT_DISABLE_LOGGING

		std::shared_ptr<request_callback> cb = requester();
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
		cancel();
		m_man.remove_request(this);
	}

	// endpoints is an in-out parameter
	void http_tracker_connection::on_filter(http_connection& c
		, std::vector<tcp::endpoint>& endpoints)
	{
		// filter all endpoints we cannot reach from this listen socket, which may
		// be all of them, in which case we should not announce this listen socket
		// to this tracker
		auto const ls = bind_socket();
		endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end()
			, [&](tcp::endpoint const& ep) { return !ls.can_route(ep.address()); })
			, endpoints.end());

		if (endpoints.empty())
		{
			fail(lt::errors::announce_skipped, operation_t::get_interface);
			return;
		}

		aux::session_settings const& settings = m_man.settings();
		bool const ssrf_mitigation = settings.get_bool(settings_pack::ssrf_mitigation);
		if (ssrf_mitigation && std::find_if(endpoints.begin(), endpoints.end()
			, [](tcp::endpoint const& ep) { return ep.address().is_loopback(); }) != endpoints.end())
		{
			// there is at least one loopback address in here. If the request
			// path for this tracker is not /announce. filter all loopback
			// addresses.
			std::string path;

			error_code ec;
			std::tie(std::ignore, std::ignore, std::ignore, std::ignore, path)
				= parse_url_components(c.url(), ec);
			if (ec)
			{
				fail(ec, operation_t::parse_address);
				return;
			}

			// this is mitigation for Server Side request forgery. Any tracker
			// announce to localhost need to look like a standard BitTorrent
			// announce
			if (path.substr(0, 9) != "/announce")
			{
				for (auto i = endpoints.begin(); i != endpoints.end();)
				{
					if (i->address().is_loopback())
						i = endpoints.erase(i);
					else
						++i;
				}
			}

			if (endpoints.empty())
			{
				fail(errors::ssrf_mitigation, operation_t::bittorrent);
				return;
			}
		}

		TORRENT_UNUSED(c);
		if (!tracker_req().filter) return;

		// remove endpoints that are filtered by the IP filter
		for (auto i = endpoints.begin(); i != endpoints.end();)
		{
			if (tracker_req().filter->access(i->address()) == ip_filter::blocked)
				i = endpoints.erase(i);
			else
				++i;
		}

#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("*** TRACKER_FILTER");
		}
#endif
		if (endpoints.empty())
			fail(errors::banned_by_ip_filter, operation_t::bittorrent);
	}

	// returns true if the hostname is allowed
	bool http_tracker_connection::on_filter_hostname(http_connection&
		, string_view hostname)
	{
		aux::session_settings const& settings = m_man.settings();
		if (settings.get_bool(settings_pack::allow_idna)) return true;
		return !is_idna(hostname);
	}

	void http_tracker_connection::on_connect(http_connection& c)
	{
		error_code ec;
		tcp::endpoint ep = c.socket().remote_endpoint(ec);
		m_tracker_ip = ep.address();
	}

	void http_tracker_connection::on_response(error_code const& ec
		, http_parser const& parser, span<char const> data)
	{
		// keep this alive
		std::shared_ptr<http_tracker_connection> me(shared_from_this());

		if (ec && ec != boost::asio::error::eof)
		{
			fail(ec, operation_t::sock_read);
			return;
		}

		if (!parser.header_finished())
		{
			fail(boost::asio::error::eof, operation_t::sock_read);
			return;
		}

		if (parser.status_code() != 200)
		{
			fail(error_code(parser.status_code(), http_category())
				, operation_t::bittorrent
				, parser.message().c_str());
			return;
		}

		received_bytes(static_cast<int>(data.size()) + parser.body_start());

		// handle tracker response
		error_code ecode;

		std::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			close();
			return;
		}

		tracker_response resp = parse_tracker_response(data, ecode
			, tracker_req().kind, tracker_req().info_hash);

		if (!resp.warning_message.empty())
			cb->tracker_warning(tracker_req(), resp.warning_message);

		if (ecode)
		{
			fail(ecode, operation_t::bittorrent
				, resp.failure_reason.c_str()
				, resp.interval, resp.min_interval);
			close();
			return;
		}

		// do slightly different things for scrape requests
		if (tracker_req().kind & tracker_request::scrape_request)
		{
			cb->tracker_scrape_response(tracker_req(), resp.complete
				, resp.incomplete, resp.downloaded, resp.downloaders);
		}
		else
		{
			std::list<address> ip_list;
			if (m_tracker_connection)
			{
				for (auto const& endp : m_tracker_connection->endpoints())
				{
					ip_list.push_back(endp.address());
				}
			}

			cb->tracker_response(tracker_req(), m_tracker_ip, ip_list, resp);
		}
		close();
	}

	// TODO: 2 returning a bool here is redundant. Instead this function should
	// return the peer_entry
	bool extract_peer_info(bdecode_node const& info, peer_entry& ret, error_code& ec)
	{
		// extract peer id (if any)
		if (info.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_peer_dict;
			return false;
		}
		bdecode_node i = info.dict_find_string("peer id");
		if (i && i.string_length() == 20)
		{
			std::copy(i.string_ptr(), i.string_ptr() + 20, ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			ret.pid.clear();
		}

		// extract ip
		i = info.dict_find_string("ip");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.hostname = i.string_value().to_string();

		// extract port
		i = info.dict_find_int("port");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.port = std::uint16_t(i.int_value());

		return true;
	}

	tracker_response parse_tracker_response(span<char const> const data, error_code& ec
		, tracker_request_flags_t const flags, sha1_hash const& scrape_ih)
	{
		tracker_response resp;

		bdecode_node e;
		int const res = bdecode(data.begin(), data.end(), e, ec);

		if (ec) return resp;

		if (res != 0 || e.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_tracker_response;
			return resp;
		}

		// if no interval is specified, default to 30 minutes
		resp.interval = seconds32{e.dict_find_int_value("interval", 1800)};
		resp.min_interval = seconds32{e.dict_find_int_value("min interval", 30)};

		bdecode_node const tracker_id = e.dict_find_string("tracker id");
		if (tracker_id)
			resp.trackerid = tracker_id.string_value().to_string();

		// parse the response
		bdecode_node const failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = failure.string_value().to_string();
			ec = errors::tracker_failure;
			return resp;
		}

		bdecode_node const warning = e.dict_find_string("warning message");
		if (warning)
			resp.warning_message = warning.string_value().to_string();

		if (flags & tracker_request::scrape_request)
		{
			bdecode_node const files = e.dict_find_dict("files");
			if (!files)
			{
				ec = errors::invalid_files_entry;
				return resp;
			}

			bdecode_node const scrape_data = files.dict_find_dict(
				scrape_ih.to_string());

			if (!scrape_data)
			{
				ec = errors::invalid_hash_entry;
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
			int const len = peers_ent.string_length();
#if TORRENT_USE_I2P
			if (flags & tracker_request::i2p)
			{
				for (int i = 0; i < len; i += 32)
				{
					if (len - i < 32) break;
					peer_entry p;
					p.hostname = base32encode(std::string(peers + i, 32), string::i2p);
					p.hostname += ".b32.i2p";
					p.port = 6881;
					resp.peers.push_back(p);
				}
			}
			else
#endif
			{
				resp.peers4.reserve(std::size_t(len / 6));
				for (int i = 0; i < len; i += 6)
				{
					if (len - i < 6) break;

					ipv4_peer_entry p;
					p.ip = aux::read_v4_address(peers).to_bytes();
					p.port = aux::read_uint16(peers);
					resp.peers4.push_back(p);
				}
			}
		}
		else if (peers_ent && peers_ent.type() == bdecode_node::list_t)
		{
			int const len = peers_ent.list_size();
			resp.peers.reserve(std::size_t(len));
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

		bdecode_node ipv6_peers = e.dict_find_string("peers6");
		if (ipv6_peers)
		{
			char const* peers = ipv6_peers.string_ptr();
			int const len = ipv6_peers.string_length();
			resp.peers6.reserve(std::size_t(len / 18));
			for (int i = 0; i < len; i += 18)
			{
				if (len - i < 18) break;

				ipv6_peer_entry p;
				p.ip = aux::read_v6_address(peers).to_bytes();
				p.port = aux::read_uint16(peers);
				resp.peers6.push_back(p);
			}
		}
		else
		{
			ipv6_peers.clear();
		}
/*
		// if we didn't receive any peers. We don't care if we're stopping anyway
		if (peers_ent == 0 && ipv6_peers == 0
			&& tracker_req().event != event_t::stopped)
		{
			ec = errors::invalid_peers_entry;
			return resp;
		}
*/
		bdecode_node const ip_ent = e.dict_find_string("external ip");
		if (ip_ent)
		{
			char const* p = ip_ent.string_ptr();
			if (ip_ent.string_length() == std::tuple_size<address_v4::bytes_type>::value)
				resp.external_ip = aux::read_v4_address(p);
			else if (ip_ent.string_length() == std::tuple_size<address_v6::bytes_type>::value)
				resp.external_ip = aux::read_v6_address(p);
		}

		return resp;
	}
}
