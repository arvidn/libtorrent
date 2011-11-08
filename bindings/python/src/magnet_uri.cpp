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

extern void dict_to_add_torrent_params(dict params
    , add_torrent_params& p, std::vector<char>& rd);

namespace {
    
    torrent_handle _add_magnet_uri(session& s, std::string uri, dict params)
    {
        add_torrent_params p;

        std::vector<char> resume_buf;
        dict_to_add_torrent_params(params, p, resume_buf);

#ifndef BOOST_NO_EXCEPTIONS
        return add_magnet_uri(s, uri, p);
#else
        error_code ec;
        return add_magnet_uri(s, uri, p, ec);
#endif
    }
}

void bind_magnet_uri()
{
    def("add_magnet_uri", &_add_magnet_uri);
}
