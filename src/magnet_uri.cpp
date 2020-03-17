/*

Copyright (c) 2007-2018, Arvid Norberg
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

#if TORRENT_ABI_VERSION == 1

	namespace {
		torrent_handle add_magnet_uri_deprecated(session& ses, std::string const& uri
			, add_torrent_params const& p, error_code& ec)
		{
			add_torrent_params params(p);
			parse_magnet_uri(uri, params, ec);
			if (ec) return torrent_handle();
			return ses.add_torrent(std::move(params), ec);
		}
	}

	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params const& p, error_code& ec)
	{
		return add_magnet_uri_deprecated(ses, uri, p, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, std::string const& save_path
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		add_torrent_params params(std::move(sc));
		error_code ec;
		parse_magnet_uri(uri, params, ec);
		params.storage_mode = storage_mode;
		params.userdata = userdata;
		params.save_path = save_path;

		if (paused) params.flags |= add_torrent_params::flag_paused;
		else params.flags &= ~add_torrent_params::flag_paused;

		return ses.add_torrent(std::move(params));
	}

	torrent_handle add_magnet_uri(session& ses, std::string const& uri
		, add_torrent_params const& p)
	{
		error_code ec;
		torrent_handle ret = add_magnet_uri_deprecated(ses, uri, p, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}
#endif // BOOST_NO_EXCEPTIONS
#endif // TORRENT_ABI_VERSION

	add_torrent_params parse_magnet_uri(string_view uri, error_code& ec)
	{
		add_torrent_params ret;
		parse_magnet_uri(uri, ret, ec);
		return ret;
	}

	void parse_magnet_uri(string_view uri, add_torrent_params& p, error_code& ec)
	{
		ec.clear();
		std::string display_name;

		string_view sv(uri);
		if (sv.substr(0, 8) != "magnet:?"_sv)
		{
			ec = errors::unsupported_url_protocol;
			return;
		}
		sv = sv.substr(8);

		int tier = 0;
		bool has_ih = false;
		while (!sv.empty())
		{
			string_view name;
			std::tie(name, sv) = split_string(sv, '=');
			string_view value;
			std::tie(value, sv) = split_string(sv, '&');

			// parameter names are allowed to have a .<number>-suffix.
			// the number has no meaning, just strip it
			// if the characters after the period are not digits, don't strip
			// anything
			string_view number;
			string_view stripped_name;
			std::tie(stripped_name, number) = split_string(name, '.');
			if (std::all_of(number.begin(), number.end(), [](char const c) { return is_digit(c); } ))
				name = stripped_name;

			if (name == "dn"_sv) // display name
			{
				error_code e;
				display_name = unescape_string(value, e);
			}
			else if (name == "tr"_sv) // tracker
			{
				// since we're about to assign tiers to the trackers, make sure the two
				// vectors are aligned
				if (p.tracker_tiers.size() != p.trackers.size())
					p.tracker_tiers.resize(p.trackers.size(), 0);
				error_code e;
				std::string tracker = unescape_string(value, e);
				if (!e)
				{
					p.trackers.push_back(std::move(tracker));
					p.tracker_tiers.push_back(tier++);
				}
			}
			else if (name == "ws"_sv) // web seed
			{
				error_code e;
				std::string webseed = unescape_string(value, e);
				if (!e) p.url_seeds.push_back(std::move(webseed));
			}
			else if (name == "xt"_sv)
			{
				std::string unescaped_btih;
				if (value.find('%') != string_view::npos)
				{
					unescaped_btih = unescape_string(value, ec);
					if (ec) return;
					value = unescaped_btih;
				}

				if (value.substr(0, 9) != "urn:btih:") continue;
				value = value.substr(9);

				sha1_hash info_hash;
				if (value.size() == 40) aux::from_hex({value.data(), 40}, info_hash.data());
				else if (value.size() == 32)
				{
					std::string const ih = base32decode(value);
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
				has_ih = true;
			}
			else if (name == "so"_sv) // select-only (files)
			{
				// accept only digits, '-' and ','
				if (std::any_of(value.begin(), value.end(), [](char c)
					{ return !is_digit(c) && c != '-' && c != ','; }))
					continue;

				do
				{
					string_view token;
					std::tie(token, value) = split_string(value, ',');

					if (token.empty()) continue;

					int idx1, idx2;
					// TODO: what's the right number here?
					constexpr int max_index = 10000; // can't risk out of memory

					auto const divider = token.find_first_of('-');
					if (divider != std::string::npos) // it's a range
					{
						if (divider == 0) // no start index
							continue;
						if (divider == token.size() - 1) // no end index
							continue;

						idx1 = std::atoi(token.substr(0, divider).to_string().c_str());
						if (idx1 < 0 || idx1 > max_index) // invalid index
							continue;
						idx2 = std::atoi(token.substr(divider + 1).to_string().c_str());
						if (idx2 < 0 || idx2 > max_index) // invalid index
							continue;

						if (idx1 > idx2) // wrong range limits
							continue;
					}
					else // it's an index
					{
						idx1 = std::atoi(token.to_string().c_str());
						if (idx1 < 0 || idx1 > max_index) // invalid index
							continue;
						idx2 = idx1;
					}

					if (int(p.file_priorities.size()) <= idx2)
						p.file_priorities.resize(static_cast<std::size_t>(idx2) + 1, dont_download);

					for (int i = idx1; i <= idx2; i++)
						p.file_priorities[std::size_t(i)] = default_priority;

				} while (!value.empty());
			}
			else if (name == "x.pe")
			{
				error_code e;
				tcp::endpoint endp = parse_endpoint(value, e);
				if (!e) p.peers.push_back(std::move(endp));
			}
#ifndef TORRENT_DISABLE_DHT
			else if (name == "dht"_sv)
			{
				auto const divider = value.find_last_of(':');
				if (divider != std::string::npos)
				{
					int const port = std::atoi(value.substr(divider + 1).to_string().c_str());
					if (port > 0 && port < int(std::numeric_limits<std::uint16_t>::max()))
						p.dht_nodes.emplace_back(value.substr(0, divider).to_string(), port);
				}
			}
#endif
		}

		if (!has_ih)
		{
			ec = errors::missing_info_hash_in_uri;
			return;
		}

		if (!display_name.empty()) p.name = display_name;
	}

	add_torrent_params parse_magnet_uri(string_view uri)
	{
		error_code ec;
		add_torrent_params ret;
		parse_magnet_uri(uri, ret, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}
}
