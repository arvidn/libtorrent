// Copyright Andrew Resch 2008. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt

#include "boost_python.hpp"
#include "bytes.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include "gil.hpp"
#include "bytes.hpp"

using namespace boost::python;
using namespace lt;

extern void dict_to_add_torrent_params(dict params, add_torrent_params& p);

namespace {

#if TORRENT_ABI_VERSION == 1
	torrent_handle _add_magnet_uri(lt::session& s, std::string uri, dict params)
	{
        python_deprecated("add_magnet_uri() is deprecated");
		add_torrent_params p;

		dict_to_add_torrent_params(params, p);

		allow_threading_guard guard;

		p.url = uri;

#ifndef BOOST_NO_EXCEPTIONS
		return s.add_torrent(p);
#else
		error_code ec;
		return s.add_torrent(p, ec);
#endif
	}
#endif

	dict parse_magnet_uri_dict(std::string const& uri)
	{
		error_code ec;
		add_torrent_params p = parse_magnet_uri(uri, ec);

		if (ec) throw system_error(ec);

		dict ret;

		if (p.ti) ret["ti"] = p.ti;
		list tracker_list;
		for (std::vector<std::string>::const_iterator i = p.trackers.begin()
			, end(p.trackers.end()); i != end; ++i)
			tracker_list.append(*i);
		ret["trackers"] = tracker_list;

		list nodes_list;
		for (auto const& i : p.dht_nodes)
			nodes_list.append(boost::python::make_tuple(i.first, i.second));
		ret["dht_nodes"] =  nodes_list;
		if (p.info_hashes.has_v2())
			ret["info_hashes"] = bytes(p.info_hashes.v2.to_string());
		else
			ret["info_hashes"] = bytes(p.info_hashes.v1.to_string());
#if TORRENT_ABI_VERSION < 3
		ret["info_hash"] = bytes(p.info_hashes.get_best().to_string());
#endif
		ret["name"] = p.name;
		ret["save_path"] = p.save_path;
		ret["storage_mode"] = p.storage_mode;
#if TORRENT_ABI_VERSION == 1
		ret["url"] = p.url;
#endif
		ret["flags"] = p.flags;
		return ret;
	}

	add_torrent_params parse_magnet_uri_wrap(std::string const& uri)
	{
		error_code ec;
		add_torrent_params p = parse_magnet_uri(uri, ec);
		if (ec) throw system_error(ec);
		return p;
	}

	std::string (*make_magnet_uri0)(torrent_handle const&) = make_magnet_uri;
	std::string (*make_magnet_uri1)(torrent_info const&) = make_magnet_uri;
	std::string (*make_magnet_uri2)(add_torrent_params const&) = make_magnet_uri;
}

void bind_magnet_uri()
{
#if TORRENT_ABI_VERSION == 1
	def("add_magnet_uri", &_add_magnet_uri);
#endif
	def("make_magnet_uri", make_magnet_uri0);
	def("make_magnet_uri", make_magnet_uri1);
	def("make_magnet_uri", make_magnet_uri2);
	def("parse_magnet_uri", parse_magnet_uri_wrap);
	def("parse_magnet_uri_dict", parse_magnet_uri_dict);
}
