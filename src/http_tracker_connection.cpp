/*

Copyright (c) 2004-2026, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/socket_io.hpp"

#include <string>
#include <functional>
#include <vector>
#include <list>
#include <cctype>
#include <algorithm>

#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/aux_/http_tracker_request.hpp"
#include "libtorrent/aux_/http_connection.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/string_util.hpp" // for is_i2p_url
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"

namespace libtorrent::aux {

	http_tracker_connection::http_tracker_connection(
		io_context& ios
		, tracker_manager& man
		, tracker_request req
		, std::weak_ptr<request_callback> c)
		: tracker_connection(man, std::move(req), ios, std::move(c))
		, m_ioc(ios)
	{}

	void http_tracker_connection::fail_error(http_tracker_request::error_type& error)
	{
		fail(error.code, error.op, std::move(error.failure_reason), error.interval, error.min_interval);
	}

	void http_tracker_connection::start()
	{
#if TORRENT_USE_I2P
		bool const i2p = is_i2p_url(tracker_req().url);
#else
		static const bool i2p = false;
#endif

		auto& settings = m_man.settings();
		http_tracker_request common{tracker_req(), settings};

		http_tracker_request::error_type error;
		std::string url = common.build_tracker_url(i2p, error);
		if (error)
		{
			fail_error(error);
			return;
		}

		error = common.validate_socket(i2p);
		if (error)
		{
			fail_error(error);
			return;
		}

		using namespace std::placeholders;
		m_tracker_connection = std::make_shared<aux::http_connection>(m_ioc, m_man.host_resolver()
			, std::bind(&http_tracker_connection::on_response, shared_from_this(), _1, _2, _3)
			, true, settings.get_int(settings_pack::max_http_recv_buffer_size)
			, std::bind(&http_tracker_connection::on_connect, shared_from_this(), _1)
			, std::bind(&http_tracker_connection::on_filter, shared_from_this(), _1, _2)
			, std::bind(&http_tracker_connection::on_filter_hostname, shared_from_this(), _1, _2)
#if TORRENT_USE_SSL
			, tracker_req().ssl_ctx
#endif
			);

		const auto timeout = common.get_timeout();
		const auto user_agent = common.get_user_agent();

		auto const ls = bind_socket();
		bind_info_t bi = [&ls](){
			if (ls.get() == nullptr)
				return bind_info_t{};
			else
				return bind_info_t{ls.device(), ls.get_local_endpoint().address()};
		}();

		// when sending stopped requests, prefer the cached DNS entry
		// to avoid being blocked for slow or failing responses. Chances
		// are that we're shutting down, and this should be a best-effort
		// attempt. It's not worth stalling shutdown.
		aux::proxy_settings ps(settings);
		m_tracker_connection->get(url, seconds(timeout)
			, ps.proxy_tracker_connections ? &ps : nullptr
			, http_tracker_request::max_redirects, user_agent, bi
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
		if (auto cb = requester())
		{
			common.log_request(*cb, url);
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
	void http_tracker_connection::on_filter(aux::http_connection& c
		, std::vector<tcp::endpoint>& endpoints)
	{
		// filter all endpoints we cannot reach from this listen socket, which may
		// be all of them, in which case we should not announce this listen socket
		// to this tracker
		auto const ls = bind_socket();
		if (ls.get() != nullptr)
		{
			endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end()
				, [&](tcp::endpoint const& ep) { return !ls.can_route(ep.address()); })
				, endpoints.end());
		}

		if (endpoints.empty())
		{
			fail(lt::errors::announce_skipped, operation_t::get_interface);
			return;
		}

		http_tracker_request common{tracker_req(), m_man.settings()};
		if (auto error = common.filter(m_requester, endpoints, c.url()))
		{
			fail_error(error);
			return;
		}
	}

	// returns true if the hostname is allowed
	bool http_tracker_connection::on_filter_hostname(aux::http_connection&
		, string_view hostname)
	{
		http_tracker_request common{tracker_req(), m_man.settings()};
		return common.allowed_by_idna(hostname);
	}

	void http_tracker_connection::on_connect(aux::http_connection& c)
	{
		error_code ec;
		tcp::endpoint ep = c.socket().remote_endpoint(ec);
		m_tracker_ip = ep.address();
	}

	void http_tracker_connection::on_response(error_code const& ec
		, aux::http_parser const& parser, span<char const> data)
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
				, parser.message());
			return;
		}

		received_bytes(static_cast<int>(data.size()) + parser.body_start());

		std::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			close();
			return;
		}

		std::list<address> ip_list;
		if (m_tracker_connection)
		{
			for (auto const& endp : m_tracker_connection->endpoints())
			{
				ip_list.push_back(endp.address());
			}
		}

		http_tracker_request common{tracker_req(), m_man.settings()};
		auto error = common.process_response(*cb, m_tracker_ip, ip_list, data);
		if (error)
		{
			fail_error(error);
			return;
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
		ret.hostname = i.string_value();

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
			resp.trackerid = tracker_id.string_value();

		// parse the response
		bdecode_node const failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = failure.string_value();
			ec = errors::tracker_failure;
			return resp;
		}

		bdecode_node const warning = e.dict_find_string("warning message");
		if (warning)
			resp.warning_message = warning.string_value();

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
					i2p_peer_entry p;
					std::memcpy(p.destination.data(), peers + i, 32);
					resp.i2p_peers.push_back(p);
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
