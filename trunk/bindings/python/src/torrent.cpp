// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/torrent.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

void bind_torrent()
{
    class_<torrent, boost::noncopyable>("torrent", no_init);
}

