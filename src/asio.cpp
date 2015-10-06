// builds all boost.asio source as a separate compilation unit
#include <boost/version.hpp>

#ifndef BOOST_ASIO_SOURCE
#define BOOST_ASIO_SOURCE
#endif

#include "libtorrent/config.hpp"

#ifdef TORRENT_BUILDING_SHARED

#define TORRENT_HAS_ASIO_DECL x ## BOOST_ASIO_DECL

// only define BOOST_ASIO_DECL if it hasn't already been defined
// or if it has been defined to an empty string
#if TORRENT_HAS_ASIO_DECL == x
#define BOOST_ASIO_DECL BOOST_SYMBOL_EXPORT
#endif

#endif // TORRENT_BUILDING_SHARED

#if BOOST_VERSION >= 104500
#include <boost/asio/impl/src.hpp>
#elif BOOST_VERSION >= 104400
#include <boost/asio/impl/src.cpp>
#endif

