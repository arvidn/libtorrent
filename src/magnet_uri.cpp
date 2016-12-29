/*

Copyright (c) 2007-2016, Arvid Norberg
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

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex
#include "libtorrent/socket_io.hpp"

namespace libtorrent {

	std::string make_magnet_uri(torrent_handle const& handle)
	{
		if (!handle.is_valid()) return "";

		std::string ret;
		sha1_hash const& ih = handle.info_hash();
		ret += "magnet:?xt=urn:btih:";
		ret += aux::to_hex(ih);

		torrent_status st = handle.status(torrent_handle::query_name);
		if (!st.name.empty())
		{
			ret += "&dn=";
			ret += escape_string(st.name);
		}

		for (auto const& tr : handle.trackers())
		{
			ret += "&tr=";
			ret += escape_string(tr.url);
		}

		for (auto const& s : handle.url_seeds())
		{
			ret += "&ws=";
			ret += escape_string(s);
		}

		return ret;
	}

	std::string make_magnet_uri(torrent_info const& info)
	{
		std::string ret;
		sha1_hash const& ih = info.info_hash();
		ret += "magnet:?xt=urn:btih:";
		ret += aux::to_hex(ih);

		std::string const& name = info.name();

		if (!name.empty())
		{
			ret += "&dn=";
			ret += escape_string(name);
		}

		for (auto const& tr : info.trackers())
		{
			ret += "&tr=";
			ret += escape_string(tr.url);
		}

		for (auto const& s : info.web_seeds())
		{
			if (s.type != web_seed_entry::url_seed) continue;

			ret += "&ws=";
			ret += escape_string(s.url);
		}

		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE

	namespace {
		torrent_handle add_magnet_uri_deprecated(session& ses, std::string const& uri
			, add_torrent_params p, error_code& ec)
		{
			parse_magnet_uri(uri, p, ec);
			if (ec) return torrent_handle();
			return ses.add_torrent(std::move(p), ec);
		}
	}

	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p, error_code& ec)
	{
		return add_magnet_uri_deprecated(ses, uri, std::move(p), ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, std::string const& save_path
		, storage_mode_t storage_mode
		, bool paused
		, void*)
	{
		add_torrent_params params;
		params.storage_mode = storage_mode;
		params.save_path = save_path;

		if (paused) params.flags |= add_torrent_params::flag_paused;
		else params.flags &= ~add_torrent_params::flag_paused;

		error_code ec;
		string_view display_name = url_has_argument(uri, "dn");
		if (!display_name.empty()) params.name = unescape_string(display_name, ec);
		string_view tracker_string = url_has_argument(uri, "tr");
		if (!tracker_string.empty()) params.trackers.push_back(unescape_string(tracker_string, ec));

		string_view btih = url_has_argument(uri, "xt");
		if (btih.empty()) return torrent_handle();

		if (btih.substr(0, 9) != "urn:btih:") return torrent_handle();

		if (btih.size() == 40 + 9) aux::from_hex({&btih[9], 40}, params.info_hash.data());
		else params.info_hash.assign(base32decode(btih.substr(9)).c_str());

		return ses.add_torrent(std::move(params));
	}

	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params p)
	{
		error_code ec;
		torrent_handle ret = add_magnet_uri_deprecated(ses, uri, std::move(p), ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}
#endif // BOOST_NO_EXCEPTIONS
#endif // TORRENT_NO_DEPRECATE

	void parse_magnet_uri(string_view uri, add_torrent_params& p, error_code& ec)
	{
		ec.clear();
		std::string name;

		{
			error_code e;
			string_view display_name = url_has_argument(uri, "dn");
			if (!display_name.empty()) name = unescape_string(display_name, e);
		}

		// parse trackers out of the magnet link
		auto pos = std::string::npos;
		string_view url = url_has_argument(uri, "tr", &pos);
		int tier = 0;
		while (pos != std::string::npos)
		{
			// since we're about to assign tiers to the trackers, make sure the two
			// vectors are aligned
			if (p.tracker_tiers.size() != p.trackers.size())
				p.tracker_tiers.resize(p.trackers.size(), 0);

			error_code e;
			std::string tracker = unescape_string(url, e);
			if (e) continue;
			p.trackers.push_back(std::move(tracker));
			p.tracker_tiers.push_back(tier++);
			pos = find(uri, "&tr=", pos);
			if (pos == std::string::npos) break;
			pos += 4;
			url = uri.substr(pos, find(uri, "&", pos) - pos);
		}

		// parse web seeds out of the magnet link
		pos = std::string::npos;
		url = url_has_argument(uri, "ws", &pos);
		while (pos != std::string::npos)
		{
			error_code e;
			std::string webseed = unescape_string(url, e);
			if (e) continue;
			p.url_seeds.push_back(std::move(webseed));
			pos = find(uri, "&ws=", pos);
			if (pos == std::string::npos) break;
			pos += 4;
			url = uri.substr(pos, find(uri, "&", pos) - pos);
		}

		string_view btih = url_has_argument(uri, "xt");
		std::string unescaped_btih;
		if (btih.empty())
		{
			ec = errors::missing_info_hash_in_uri;
			return;
		}
		if (btih.find('%') != string_view::npos)
		{
			unescaped_btih = unescape_string(btih, ec);
			if (ec) return;
			btih = unescaped_btih;
		}
		if (btih.substr(0, 9) != "urn:btih:")
		{
			ec = errors::missing_info_hash_in_uri;
			return;
		}

		std::string::size_type peer_pos = std::string::npos;
		string_view peer = url_has_argument(uri, "x.pe", &peer_pos);
		while (!peer.empty())
		{
			error_code e;
			tcp::endpoint endp = parse_endpoint(peer, e);
			if (!e)
				p.peers.push_back(endp);

			peer_pos = find(uri, "&x.pe=", peer_pos);
			if (peer_pos == std::string::npos) break;
			peer_pos += 6;
			peer = uri.substr(peer_pos, find(uri, "&", peer_pos) - peer_pos);
		}

#ifndef TORRENT_DISABLE_DHT
		std::string::size_type node_pos = std::string::npos;
		string_view node = url_has_argument(uri, "dht", &node_pos);
		while (!node.empty())
		{
			std::string::size_type divider = node.find_last_of(':');
			if (divider != std::string::npos)
			{
				int port = atoi(node.substr(divider + 1).to_string().c_str());
				if (port != 0)
					p.dht_nodes.push_back(std::make_pair(node.substr(0, divider).to_string(), port));
			}

			node_pos = find(uri, "&dht=", node_pos);
			if (node_pos == std::string::npos) break;
			node_pos += 5;
			node = uri.substr(node_pos, find(uri, "&", node_pos) - node_pos);
		}
#endif

		sha1_hash info_hash;
		if (btih.size() == 40 + 9) aux::from_hex({&btih[9], 40}, info_hash.data());
		else if (btih.size() == 32 + 9)
		{
			std::string ih = base32decode(btih.substr(9));
			if (ih.size() != 20)
			{
				ec = errors::invalid_info_hash;
				return;
			}
			info_hash.assign(ih);
		}
		else
		{
			ec = errors::invalid_info_hash;
			return;
		}

		p.info_hash = info_hash;
		if (!name.empty()) p.name = name;
	}
}
