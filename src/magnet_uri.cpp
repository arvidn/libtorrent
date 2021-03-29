/*

Copyright (c) 2007-2010, 2012-2020, Arvid Norberg
Copyright (c) 2016-2017, 2020-2021, Alden Torres
Copyright (c) 2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex
#include "libtorrent/aux_/socket_io.hpp"

namespace lt {

	std::string make_magnet_uri(torrent_handle const& handle)
	{
		if (!handle.is_valid()) return "";

		std::string ret = "magnet:?";

		if (handle.info_hashes().has_v1())
		{
			sha1_hash const& ih = handle.info_hashes().v1;
			ret += "xt=urn:btih:";
			ret += aux::to_hex(ih);
		}

		if (handle.info_hashes().has_v2())
		{
			if (handle.info_hashes().has_v1()) ret += '&';
			sha256_hash const& ih = handle.info_hashes().v2;
			ret += "xt=urn:btmh:1220";
			ret += aux::to_hex(ih);
		}

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
		std::string ret = "magnet:?";

		if (info.info_hashes().has_v1())
		{
			sha1_hash const& ih = info.info_hashes().v1;
			ret += "xt=urn:btih:";
			ret += aux::to_hex(ih);
		}

		if (info.info_hashes().has_v2())
		{
			if (info.info_hashes().has_v1()) ret += '&';
			sha256_hash const& ih = info.info_hashes().v2;
			ret += "xt=urn:btmh:1220";
			ret += aux::to_hex(ih);
		}

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
		, void*)
	{
		add_torrent_params params;
		error_code ec;
		parse_magnet_uri(uri, params, ec);
		params.storage_mode = storage_mode;
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
		bool has_ih[2] = { false, false };
		while (!sv.empty())
		{
			string_view name;
			std::tie(name, sv) = aux::split_string(sv, '=');
			string_view value;
			std::tie(value, sv) = aux::split_string(sv, '&');

			// parameter names are allowed to have a .<number>-suffix.
			// the number has no meaning, just strip it
			// if the characters after the period are not digits, don't strip
			// anything
			auto const [stripped_name, number] = aux::split_string(name, '.');
			if (std::all_of(number.begin(), number.end(), [](char const c) { return aux::is_digit(c); } ))
				name = stripped_name;

			if (aux::string_equal_no_case(name, "dn"_sv)) // display name
			{
				error_code e;
				display_name = unescape_string(value, e);
			}
			else if (aux::string_equal_no_case(name, "tr"_sv)) // tracker
			{
				// since we're about to assign tiers to the trackers, make sure the two
				// vectors are aligned
				if (p.tracker_tiers.size() != p.trackers.size())
					p.tracker_tiers.resize(p.trackers.size(), 0);
				error_code e;
				std::string tracker = unescape_string(value, e);
				if (!e && !tracker.empty())
				{
					p.trackers.push_back(std::move(tracker));
					p.tracker_tiers.push_back(tier++);
				}
			}
			else if (aux::string_equal_no_case(name, "ws"_sv)) // web seed
			{
				error_code e;
				std::string webseed = unescape_string(value, e);
				if (!e) p.url_seeds.push_back(std::move(webseed));
			}
			else if (aux::string_equal_no_case(name, "xt"_sv))
			{
				std::string unescaped_btih;
				if (value.find('%') != string_view::npos)
				{
					unescaped_btih = unescape_string(value, ec);
					if (ec) return;
					value = unescaped_btih;
				}

				if (value.substr(0, 9) == "urn:btih:")
				{
					value = value.substr(9);

					sha1_hash info_hash;
					if (value.size() == 40) aux::from_hex(value, info_hash.data());
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
					p.info_hashes.v1 = info_hash;
					has_ih[0] = true;
				}
				else if (value.substr(0, 9) == "urn:btmh:")
				{
					value = value.substr(9);

					// hash must be sha256
					if (value.substr(0, 4) != "1220")
					{
						ec = errors::invalid_info_hash;
						return;
					}

					value = value.substr(4);

					if (value.size() != 64)
					{
						ec = errors::invalid_info_hash;
						return;
					}
					aux::from_hex(value, p.info_hashes.v2.data());
					has_ih[1] = true;
				}
			}
			else if (aux::string_equal_no_case(name, "so"_sv)) // select-only (files)
			{
				// accept only digits, '-' and ','
				if (std::any_of(value.begin(), value.end(), [](char c)
					{ return !aux::is_digit(c) && c != '-' && c != ','; }))
					continue;

				do
				{
					string_view token;
					std::tie(token, value) = aux::split_string(value, ',');

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

						idx1 = std::atoi(std::string(token.substr(0, divider)).c_str());
						if (idx1 < 0 || idx1 > max_index) // invalid index
							continue;
						idx2 = std::atoi(std::string(token.substr(divider + 1)).c_str());
						if (idx2 < 0 || idx2 > max_index) // invalid index
							continue;

						if (idx1 > idx2) // wrong range limits
							continue;
					}
					else // it's an index
					{
						idx1 = std::atoi(std::string(token).c_str());
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
			else if (aux::string_equal_no_case(name, "x.pe"_sv))
			{
				error_code e;
				tcp::endpoint endp = aux::parse_endpoint(value, e);
				if (!e) p.peers.push_back(std::move(endp));
			}
#ifndef TORRENT_DISABLE_DHT
			else if (aux::string_equal_no_case(name, "dht"_sv))
			{
				auto const divider = value.find_last_of(':');
				if (divider != std::string::npos)
				{
					int const port = std::atoi(std::string(value.substr(divider + 1)).c_str());
					if (port > 0 && port < int(std::numeric_limits<std::uint16_t>::max()))
						p.dht_nodes.emplace_back(value.substr(0, divider), port);
				}
			}
#endif
		}

		if (!has_ih[0] && !has_ih[1])
		{
			ec = errors::missing_info_hash_in_uri;
			return;
		}

#if TORRENT_ABI_VERSION < 3
		p.info_hash = p.info_hashes.get_best();
#endif
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
