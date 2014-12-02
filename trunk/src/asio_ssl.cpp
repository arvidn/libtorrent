// builds all boost.asio SSL source as a separate compilation unit
#include <boost/version.hpp>
#include <climits>

#ifndef BOOST_ASIO_SOURCE
#define BOOST_ASIO_SOURCE
#endif

#include "libtorrent/config.hpp"

#define TORRENT_HAS_ASIO_DECL x ## BOOST_ASIO_DECL

// only define BOOST_ASIO_DECL if it hasn't already been defined
// or if it has been defined to an empty string
#if TORRENT_HAS_ASIO_DECL == x
#define BOOST_ASIO_DECL BOOST_SYMBOL_EXPORT
#endif

#if BOOST_VERSION >= 104610
#include <boost/asio/ssl/impl/src.hpp>
#endif

