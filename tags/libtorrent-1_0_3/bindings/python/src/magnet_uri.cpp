// Copyright Andrew Resch 2008. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt

#include <boost/python.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

extern void dict_to_add_torrent_params(dict params, add_torrent_params& p);

namespace {

#ifndef TORRENT_NO_DEPRECATE
    torrent_handle _add_magnet_uri(session& s, std::string uri, dict params)
    {
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

	dict parse_magnet_uri_wrap(std::string const& uri)
	{
		add_torrent_params p;
		error_code ec;
		parse_magnet_uri(uri, p, ec);

		if (ec) throw libtorrent_exception(ec);

		dict ret;

		ret["ti"] = p.ti;
		list tracker_list;
		for (std::vector<std::string>::const_iterator i = p.trackers.begin()
			, end(p.trackers.end()); i != end; ++i)
			tracker_list.append(*i);
		ret["trackers"] = tracker_list;

		list nodes_list;
		for (std::vector<std::pair<std::string, int> >::const_iterator i = p.dht_nodes.begin()
			, end(p.dht_nodes.end()); i != end; ++i)
			tracker_list.append(boost::python::make_tuple(i->first, i->second));
		ret["dht_nodes"] =  nodes_list;
		ret["info_hash"] = p.info_hash;
		ret["name"] = p.name;
		ret["save_path"] = p.save_path;
		ret["storage_mode"] = p.storage_mode;
		ret["url"] = p.url;
		ret["uuid"] = p.uuid;
		ret["source_feed_url"] = p.source_feed_url;
		ret["flags"] = p.flags;
		return ret;
	}

	std::string (*make_magnet_uri0)(torrent_handle const&) = make_magnet_uri;
	std::string (*make_magnet_uri1)(torrent_info const&) = make_magnet_uri;
}

void bind_magnet_uri()
{
#ifndef TORRENT_NO_DEPRECATE
    def("add_magnet_uri", &_add_magnet_uri);
#endif
    def("make_magnet_uri", make_magnet_uri0);
    def("make_magnet_uri", make_magnet_uri1);
    def("parse_magnet_uri", parse_magnet_uri_wrap);
}

