// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

#include "boost_python.hpp"
#include "bytes.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/bdecode.hpp"

using namespace boost::python;

// defined in torrent_info.cpp
lt::load_torrent_limits dict_to_limits(dict limits);

namespace {

    lt::add_torrent_params load_torrent_file1(std::string filename, dict cfg)
{
    return lt::load_torrent_file(filename, dict_to_limits(cfg));
}

lt::add_torrent_params load_torrent_buffer0(bytes b)
{
    return lt::load_torrent_buffer(b.arr);
}

lt::add_torrent_params load_torrent_buffer1(bytes b, dict cfg)
{
    return lt::load_torrent_buffer(b.arr, dict_to_limits(cfg));
}


lt::add_torrent_params load_torrent_parsed1(lt::bdecode_node const& n, dict cfg)
{
    return lt::load_torrent_parsed(n, dict_to_limits(cfg));
}

}

void bind_load_torrent()
{
    lt::add_torrent_params (*load_torrent_file0)(std::string const&) = &lt::load_torrent_file;
    lt::add_torrent_params (*load_torrent_parsed0)(lt::bdecode_node const&) = &lt::load_torrent_parsed;

    def("load_torrent_file", load_torrent_file0);
    def("load_torrent_file", load_torrent_file1);
    def("load_torrent_buffer", load_torrent_buffer0);
    def("load_torrent_buffer", load_torrent_buffer1);
    def("load_torrent_parsed", load_torrent_parsed0);
    def("load_torrent_parsed", load_torrent_parsed1);
}
