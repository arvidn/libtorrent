/*

Copyright (c) 2025, libtorrent project
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

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_tracker_client.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include <cstdio>

// Include bdecode after escape_string so we get the right one
#include "libtorrent/bdecode.hpp"

namespace libtorrent { namespace aux {

namespace {

	bool extract_peer_info(bdecode_node const& info, peer_entry& ret, error_code& ec)
	{
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
			// Assign a random peer ID
			ret.pid = peer_id();
		}

		i = info.dict_find_string("ip");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.hostname = std::string(i.string_value());

		i = info.dict_find_int("port");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.port = std::uint16_t(i.int_value());

		return true;
	}
}

// Free function implementations (following http_tracker_connection pattern)
tracker_response parse_announce_response(
	span<char const> data,
	error_code& ec)
{
		tracker_response resp;

		bdecode_node e;
		int const res = bdecode(data.begin(), data.end(), e, ec);

		if (res != 0 || e.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_tracker_response;
			return resp;
		}

		bdecode_node const failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = std::string(failure.string_value());
			ec = errors::tracker_failure;
			return resp;
		}

		bdecode_node const warning = e.dict_find_string("warning message");
		if (warning)
		{
			resp.warning_message = std::string(warning.string_value());
		}

		resp.interval = seconds32{e.dict_find_int_value("interval", 1800)};
		resp.min_interval = seconds32{e.dict_find_int_value("min interval", 30)};

		bdecode_node const tracker_id = e.dict_find_string("tracker id");
		if (tracker_id)
		{
			resp.trackerid = std::string(tracker_id.string_value());
		}

		resp.complete = int(e.dict_find_int_value("complete", -1));
		resp.incomplete = int(e.dict_find_int_value("incomplete", -1));

		bdecode_node const peers_ent = e.dict_find("peers");
		if (peers_ent && peers_ent.type() == bdecode_node::string_t)
		{
			char const* peers = peers_ent.string_ptr();
			int const len = peers_ent.string_length();

			if (len % 6 != 0)
			{
				ec = errors::invalid_tracker_response;
				return resp;
			}

			for (int i = 0; i < len; i += 6)
			{
				peer_entry p;

				p.hostname = address_v4(read_uint32(peers)).to_string();
				p.port = read_uint16(peers);
				resp.peers.push_back(p);
			}
		}
		else if (peers_ent && peers_ent.type() == bdecode_node::list_t)
		{
			int const len = peers_ent.list_size();
			resp.peers.reserve(len);

			for (int i = 0; i < len; ++i)
			{
				peer_entry p;
				if (!extract_peer_info(peers_ent.list_at(i), p, ec))
					continue;
				resp.peers.push_back(p);
			}
		}

		bdecode_node const peers6_ent = e.dict_find_string("peers6");
		if (peers6_ent)
		{
			char const* peers = peers6_ent.string_ptr();
			int const len = peers6_ent.string_length();

			if (len % 18 != 0 || len < 0)
			{
				// Invalid IPv6 peer data - must be multiple of 18 bytes
				// Skip invalid data rather than failing entire response
				// This is more robust for partially corrupt responses
			}
			else if (len > 0)
			{
				// Pre-allocate for efficiency
				resp.peers.reserve(resp.peers.size() + len / 18);

				for (int i = 0; i < len; i += 18)
				{
					peer_entry p;
					address_v6::bytes_type addr_bytes;
					std::memcpy(addr_bytes.data(), peers + i, 16);
					p.hostname = address_v6(addr_bytes).to_string();

					// read_uint16 expects a mutable reference to advance the pointer
					// Create a local pointer that won't affect our iteration
					char const* port_ptr = peers + i + 16;
					p.port = read_uint16(port_ptr);
					// Note: port_ptr is now advanced by 2, but we don't use it again

					resp.peers.push_back(std::move(p));
				}
			}
		}

		bdecode_node const ip_ent = e.dict_find_string("external ip");
		if (ip_ent)
		{
			char const* ip_ptr = ip_ent.string_ptr();
			int const ip_len = ip_ent.string_length();

			if (ip_len == 4)
			{
				resp.external_ip = address_v4(read_uint32(ip_ptr));
			}
			else if (ip_len == 16)
			{
				address_v6::bytes_type addr_bytes;
				std::memcpy(addr_bytes.data(), ip_ptr, 16);
				resp.external_ip = address_v6(addr_bytes);
			}
		}

		return resp;
	}

tracker_response parse_scrape_response(
	span<char const> data,
	error_code& ec)
{
		tracker_response resp;

		bdecode_node e;
		int const res = bdecode(data.begin(), data.end(), e, ec);

		if (res != 0 || e.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_tracker_response;
			return resp;
		}

				bdecode_node const failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = std::string(failure.string_value());
			ec = errors::tracker_failure;
			return resp;
		}

		bdecode_node const files = e.dict_find_dict("files");
		if (!files)
		{
			ec = errors::invalid_tracker_response_length;
			return resp;
		}

		// We're looking for the first (and should be only) file entry
		// The key should be the info hash we scraped for
		if (files.dict_size() > 0)
		{
			// Since we don't have the actual info hash here, we'll just get the first entry
			for (int i = 0; i < files.dict_size(); ++i)
			{
				auto item = files.dict_at(i);
				if (item.second.type() == bdecode_node::dict_t)
				{
					bdecode_node const scrape_data = item.second;
					resp.complete = int(scrape_data.dict_find_int_value("complete", -1));
					resp.incomplete = int(scrape_data.dict_find_int_value("incomplete", -1));
					resp.downloaded = int(scrape_data.dict_find_int_value("downloaded", -1));
					break;
				}
			}
		}

		return resp;
	}

curl_tracker_client::curl_tracker_client(
	io_context& ios,
	std::string const& url,
	settings_pack const& settings,
	std::shared_ptr<curl_thread_manager> curl_mgr)
	: m_ios(ios)
	, m_tracker_url(url)
	, m_settings(settings)
	, m_curl_thread_manager(std::move(curl_mgr))
{
}

curl_tracker_client::~curl_tracker_client()
{
	close();
}

void curl_tracker_client::announce(
	tracker_request const& req,
	std::function<void(error_code const&, tracker_response const&)> handler)
{
	std::string url = build_announce_url(req);

	m_curl_thread_manager->add_request(url,
		[this, handler](error_code const& ec, std::vector<char> const& data) {
			if (ec) {
				tracker_response resp;
				resp.failure_reason = ec.message();
				handler(ec, resp);
				return;
			}

			error_code parse_ec;
			tracker_response resp = parse_announce_response(
				span<char const>(data.data(), data.size()), parse_ec);
			handler(parse_ec, resp);
		});
}

void curl_tracker_client::scrape(
	tracker_request const& req,
	std::function<void(error_code const&, tracker_response const&)> handler)
{
	std::string url = build_scrape_url(req);

	m_curl_thread_manager->add_request(url,
		[this, handler](error_code const& ec, std::vector<char> const& data) {
			if (ec) {
				tracker_response resp;
				resp.failure_reason = ec.message();
				handler(ec, resp);
				return;
			}

			error_code parse_ec;
			tracker_response resp = parse_scrape_response(
				span<char const>(data.data(), data.size()), parse_ec);
			handler(parse_ec, resp);
		});
}

void curl_tracker_client::close()
{
	// No longer tracking pending requests since we don't support cancellation
	// The curl_thread_manager will handle cleanup on shutdown
}

std::string curl_tracker_client::build_announce_url(tracker_request const& req) const
{
	return m_tracker_url + "?" + build_tracker_query(req, false);
}

std::string curl_tracker_client::build_scrape_url(tracker_request const& req) const
{
	std::string scrape_url = scrape_url_from_announce(m_tracker_url);
	return scrape_url + "?" + build_tracker_query(req, true);
}

std::string curl_tracker_client::build_tracker_query(tracker_request const& req, bool scrape) const
{
	std::string query;

	// Pre-allocate space to avoid reallocations (optimize string building)
	// Estimated size: info_hash(60) + peer_id(60) + numbers(100) + extras(100) = ~320 bytes
	query.reserve(scrape ? 70 : 400);

	query += "info_hash=";
	query += libtorrent::escape_string({req.info_hash.data(), 20});

	if (scrape) {
		return query;
	}

	query += "&peer_id=";
	query += libtorrent::escape_string({req.pid.data(), 20});
	query += "&port=" + std::to_string(req.listen_port);
	query += "&uploaded=" + std::to_string(req.uploaded);
	query += "&downloaded=" + std::to_string(req.downloaded);
	query += "&left=" + std::to_string(req.left);
	query += "&corrupt=" + std::to_string(req.corrupt);

	if (req.event != event_t::none) {
		// BEP-3 compliant events only (removed non-standard "paused")
		// Only add event parameter for valid BEP-3 events (1=completed, 2=started, 3=stopped)
		// Skip event_t::paused (4) as it's non-standard
		if (static_cast<int>(req.event) >= 1 && static_cast<int>(req.event) <= 3) {
			const char* event_str[] = {"empty", "completed", "started", "stopped"};
			query += "&event=";
			query += event_str[static_cast<int>(req.event)];
		}
	}

	// BEP-23: Compact Peer Lists - Request compact response format
	query += "&compact=1";      // Prefer compact response format
	query += "&no_peer_id=1";   // Don't need peer IDs in response

	if (req.key != 0) {
		char key_str[20];
		std::snprintf(key_str, sizeof(key_str), "%x", req.key);
		query += "&key=";
		query += key_str;
	}

	query += "&numwant=" + std::to_string(req.num_want);

	// BEP-7: IPv6 Tracker Extension - Support both IPv4 and IPv6
	// Add IP if specified (tracker usually knows the client IP)
	if (!req.ipv4.empty()) {
		query += "&ip=";
		query += libtorrent::escape_string(req.ipv4[0].to_string());
	}

	if (!req.ipv6.empty()) {
		query += "&ipv6=";
		query += libtorrent::escape_string(req.ipv6[0].to_string());
	}

	return query;
}


std::string curl_tracker_client::scrape_url_from_announce(std::string const& announce) const
{
	std::string scrape_url = announce;

	std::size_t pos = scrape_url.rfind("/announce");
	if (pos != std::string::npos)
	{
		scrape_url.replace(pos, 9, "/scrape");
	}
	else
	{
		pos = scrape_url.rfind('/');
		if (pos != std::string::npos)
		{
			scrape_url = scrape_url.substr(0, pos) + "/scrape";
		}
	}

	return scrape_url;
}

}} // namespace libtorrent::aux

#endif // TORRENT_USE_LIBCURL