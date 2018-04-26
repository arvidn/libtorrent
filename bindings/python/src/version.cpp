// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/version.hpp>

using namespace boost::python;
using lt::version;

void bind_version()
{
    scope().attr("__version__") = version();

#if TORRENT_ABI_VERSION == 1
    scope().attr("version") = LIBTORRENT_VERSION;
    scope().attr("version_major") = LIBTORRENT_VERSION_MAJOR;
    scope().attr("version_minor") = LIBTORRENT_VERSION_MINOR;
#endif
}

